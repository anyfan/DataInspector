#include "signalbrowser.h"
#include "signaltreedelegate.h"
#include "signalpropertiesdialog.h"
#include "mainwindow.h" // 需要 TreeItemRoles 枚举

#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QSignalBlocker>

SignalBrowser::SignalBrowser(QWidget *parent)
    : QWidget(parent),
      m_searchBox(nullptr),
      m_treeView(nullptr),
      m_model(nullptr),
      m_colorIndex(0)
{
    // 初始化颜色列表
    m_colorList << QColor("#0072bd") << QColor("#d95319") << QColor("#edb120")
                << QColor("#7e2f8e") << QColor("#77ac30") << QColor("#4dbeee")
                << QColor("#a2142f") << QColor("#139fff") << QColor("#ff6929")
                << QColor("#b746ff") << QColor("#64d413") << QColor("#ff13a6")
                << QColor("#fe330a") << QColor("#22b573");

    setupUi();
}

void SignalBrowser::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("搜索信号..."));
    m_searchBox->setClearButtonEnabled(true);
    layout->addWidget(m_searchBox);

    m_treeView = new QTreeView(this);
    m_model = new QStandardItemModel(this);
    m_treeView->setModel(m_model);
    m_treeView->setHeaderHidden(true);
    m_treeView->setItemDelegate(new SignalTreeDelegate(m_treeView));
    m_treeView->setIndentation(10);
    m_treeView->setDragEnabled(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragOnly);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);

    layout->addWidget(m_treeView);

    // 连接信号
    connect(m_searchBox, &QLineEdit::textChanged, this, &SignalBrowser::onSearchTextChanged);
    connect(m_model, &QStandardItemModel::itemChanged, this, &SignalBrowser::onItemChanged);
    connect(m_treeView, &QTreeView::doubleClicked, this, &SignalBrowser::onItemDoubleClicked);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &SignalBrowser::onCustomContextMenu);
}

void SignalBrowser::loadFileData(const FileData &data)
{
    m_treeView->setUpdatesEnabled(false);

    QString filename = QFileInfo(data.filePath).fileName();

    QStandardItem *fileItem = new QStandardItem(filename);
    fileItem->setEditable(false);
    fileItem->setCheckable(false);
    fileItem->setData(filename, FileNameRole);
    fileItem->setData(true, IsFileItemRole);
    fileItem->setData(false, IsSignalItemRole);
    m_model->appendRow(fileItem);

    for (int t_idx = 0; t_idx < data.tables.size(); ++t_idx)
    {
        const SignalTable &table = data.tables.at(t_idx);
        bool skipTableNode = (data.tables.size() == 1 && table.name == QFileInfo(filename).completeBaseName());

        QStandardItem *parentItem = fileItem;
        if (!skipTableNode)
        {
            QStandardItem *tableItem = new QStandardItem(table.name);
            tableItem->setEditable(false);
            tableItem->setCheckable(false);
            tableItem->setData(filename, FileNameRole);
            tableItem->setData(false, IsFileItemRole);
            tableItem->setData(false, IsSignalItemRole);
            fileItem->appendRow(tableItem);
            parentItem = tableItem;
        }

        QString idPrefix = filename + "/" + table.name + "/";

        for (int i = 0; i < table.headers.count(); ++i)
        {
            QString signalName = table.headers[i].trimmed();
            if (signalName.isEmpty())
                signalName = tr("Signal %1").arg(i + 1);

            QStandardItem *item = new QStandardItem(signalName);
            item->setEditable(false);
            item->setCheckable(true);
            item->setCheckState(Qt::Unchecked);

            QString uniqueID = idPrefix + QString::number(i);

            item->setData(uniqueID, UniqueIdRole);
            item->setData(false, IsFileItemRole);
            item->setData(true, IsSignalItemRole);
            item->setData(filename, FileNameRole);

            QColor color = m_colorList.at(m_colorIndex);
            m_colorIndex = (m_colorIndex + 1) % m_colorList.size();

            QPen pen(color, 1);
            item->setData(QVariant::fromValue(pen), PenDataRole);

            parentItem->appendRow(item);
            m_uniqueIdMap.insert(uniqueID, item);
        }
    }

    m_treeView->expandAll();
    m_treeView->setUpdatesEnabled(true);
}

void SignalBrowser::removeFile(const QString &filename)
{
    // 1. 清理 ID 映射
    QMutableHashIterator<QString, QStandardItem *> it(m_uniqueIdMap);
    while (it.hasNext())
    {
        it.next();
        if (it.key().startsWith(filename + "/"))
            it.remove();
    }

    // 2. 移除树节点
    QList<QStandardItem *> items = m_model->findItems(filename);
    for (QStandardItem *item : items)
    {
        if (item->data(IsFileItemRole).toBool() && item->parent() == nullptr)
        {
            m_model->removeRow(item->row());
            break;
        }
    }
}

QPen SignalBrowser::getSignalPen(const QString &uniqueId) const
{
    QStandardItem *item = m_uniqueIdMap.value(uniqueId, nullptr);
    if (item)
        return item->data(PenDataRole).value<QPen>();
    return QPen(Qt::black);
}

void SignalBrowser::setSignalChecked(const QString &uniqueId, bool checked, bool blockSignals)
{
    QStandardItem *item = m_uniqueIdMap.value(uniqueId, nullptr);
    if (!item)
        return;

    if (blockSignals)
    {
        disconnect(m_model, &QStandardItemModel::itemChanged, this, &SignalBrowser::onItemChanged);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        connect(m_model, &QStandardItemModel::itemChanged, this, &SignalBrowser::onItemChanged);
    }
    else
    {
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
}

void SignalBrowser::updateChecksForActivePlot(const QSet<QString> &activeSignalIds)
{
    disconnect(m_model, &QStandardItemModel::itemChanged, this, &SignalBrowser::onItemChanged);

    for (auto it = m_uniqueIdMap.constBegin(); it != m_uniqueIdMap.constEnd(); ++it)
    {
        QStandardItem *item = it.value();
        QString uniqueID = it.key();

        bool shouldBeChecked = activeSignalIds.contains(uniqueID);
        Qt::CheckState newState = shouldBeChecked ? Qt::Checked : Qt::Unchecked;

        if (item->checkState() != newState)
        {
            item->setCheckState(newState);
        }
    }

    connect(m_model, &QStandardItemModel::itemChanged, this, &SignalBrowser::onItemChanged);
}

void SignalBrowser::onItemChanged(QStandardItem *item)
{
    if (!item || !item->data(IsSignalItemRole).toBool())
        return;

    QString uniqueID = item->data(UniqueIdRole).toString();
    bool isChecked = (item->checkState() == Qt::Checked);

    emit signalCheckStateChanged(uniqueID, isChecked);
}

void SignalBrowser::onItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    QStandardItem *item = m_model->itemFromIndex(index);
    if (!item || !item->data(IsSignalItemRole).toBool())
        return;

    // 点击位置检测 (仅预览线区域响应双击)
    QRect itemRect = m_treeView->visualRect(index);
    QPoint localPos = m_treeView->viewport()->mapFromGlobal(QCursor::pos());

    // 假设预览区域宽度为 40 (需与 Delegate 一致)
    const int previewWidth = 40;
    QRect previewRect(itemRect.right() - previewWidth, itemRect.top(), previewWidth, itemRect.height());

    if (localPos.x() < previewRect.left())
        return; // 点击了文本区域，忽略

    QPen currentPen = item->data(PenDataRole).value<QPen>();
    SignalPropertiesDialog dialog(currentPen, this);

    if (dialog.exec() == QDialog::Accepted)
    {
        QPen newPen = dialog.getSelectedPen();
        item->setData(QVariant::fromValue(newPen), PenDataRole);

        QString uniqueID = item->data(UniqueIdRole).toString();
        emit signalPenChanged(uniqueID, newPen);
    }
}

void SignalBrowser::onCustomContextMenu(const QPoint &pos)
{
    QModelIndex index = m_treeView->indexAt(pos);
    if (!index.isValid())
        return;

    QStandardItem *item = m_model->itemFromIndex(index);
    if (item && item->data(IsFileItemRole).toBool())
    {
        QString filename = item->data(FileNameRole).toString();
        QMenu menu(this);
        QAction *delAction = menu.addAction(tr("移除文件 '%1'").arg(filename));
        connect(delAction, &QAction::triggered, [this, filename]()
                { emit fileRemoveRequested(filename); });
        menu.exec(m_treeView->viewport()->mapToGlobal(pos));
    }
}

void SignalBrowser::onSearchTextChanged(const QString &text)
{
    QString query = text.trimmed().toLower();
    QStandardItem *root = m_model->invisibleRootItem();
    for (int i = 0; i < root->rowCount(); ++i)
        filterTree(root->child(i), query);

    if (!query.isEmpty())
        m_treeView->expandAll();
}

bool SignalBrowser::filterTree(QStandardItem *item, const QString &query)
{
    if (!item)
        return false;
    bool selfMatches = item->text().toLower().contains(query);
    bool childrenMatch = false;
    for (int i = 0; i < item->rowCount(); ++i)
    {
        if (filterTree(item->child(i), query))
            childrenMatch = true;
    }
    bool visible = query.isEmpty() || selfMatches || childrenMatch;
    m_treeView->setRowHidden(item->row(), item->parent() ? item->parent()->index() : QModelIndex(), !visible);
    return visible;
}

QStandardItem *SignalBrowser::findItemByName(const QString &signalName) const
{
    return findItemByName_Recursive(m_model->invisibleRootItem(), signalName);
}

QStandardItem *SignalBrowser::findItemByName_Recursive(QStandardItem *parentItem, const QString &name) const
{
    if (!parentItem)
        return nullptr;
    for (int i = 0; i < parentItem->rowCount(); ++i)
    {
        QStandardItem *child = parentItem->child(i);
        if (child->data(IsSignalItemRole).toBool())
        {
            if (child->text() == name)
                return child;
        }
        else
        {
            if (QStandardItem *found = findItemByName_Recursive(child, name))
                return found;
        }
    }
    return nullptr;
}

void SignalBrowser::clear()
{
    m_model->clear();
    m_uniqueIdMap.clear();
    m_colorIndex = 0;
}

// 在文件末尾或适当位置添加实现
QString SignalBrowser::getSignalName(const QString &uniqueId) const
{
    QStandardItem *item = m_uniqueIdMap.value(uniqueId, nullptr);
    if (item)
    {
        return item->text();
    }
    return QString();
}

void SignalBrowser::selectSignal(const QString &uniqueId)
{
    QStandardItem *item = m_uniqueIdMap.value(uniqueId, nullptr);
    if (item)
    {
        QModelIndex index = item->index();

        // 1. 展开父节点以确保可见
        QStandardItem *parent = item->parent();
        while (parent)
        {
            m_treeView->expand(parent->index());
            parent = parent->parent();
        }

        // 2. 选中并滚动
        m_treeView->setCurrentIndex(index);
        m_treeView->scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
    else
    {
        // 如果未找到或传入空ID，清除选择
        m_treeView->clearSelection();
        m_treeView->setCurrentIndex(QModelIndex());
    }
}