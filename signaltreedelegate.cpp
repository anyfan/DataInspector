#include "signaltreedelegate.h"
#include <QPainter>
#include <QPen>
#include <QVariant>
#include <QApplication>

// --- 修改：更新了 PenDataRole 的值 ---
// 在 mainwindow.h 中定义
const int UniqueIdRole = Qt::UserRole + 1;
const int IsFileItemRole = Qt::UserRole + 2;
const int PenDataRole = Qt::UserRole + 3;
// --- ---------------------------- ---

SignalTreeDelegate::SignalTreeDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void SignalTreeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
    // 1. 先调用基类 paint() 来绘制标准内容 (复选框、文本、图标等)
    // 我们会修改矩形，为右侧的预览腾出空间
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // --- 新增：如果是文件条目，则不绘制预览 ---
    bool isFileItem = index.data(IsFileItemRole).toBool();
    // --- -------------------------------- ---

    // 预留右侧 40 像素用于绘制预览
    const int previewWidth = 40;
    const int margin = 5;

    // 保存原始矩形，用于绘制预览线
    QRect fullRect = opt.rect;

    // 告诉基类 paint() 不要绘制在我们的预览区域
    if (!isFileItem) // <-- 只对信号条目执行此操作
        opt.rect.setWidth(opt.rect.width() - previewWidth - margin);

    // 获取 QStyle 对象
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();

    // 手动绘制，而不是调用 QStyledItemDelegate::paint(painter, opt, index);
    // 因为基类 paint 仍然会填充整个背景
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

    // 绘制复选框 (如果存在)
    if (opt.features & QStyleOptionViewItem::HasCheckIndicator)
    {
        QRect checkRect = style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &opt, opt.widget);
        QStyleOptionButton checkOpt;
        checkOpt.rect = checkRect;
        checkOpt.state = opt.state | QStyle::State_Enabled;
        if (index.data(Qt::CheckStateRole).toInt() == Qt::Checked)
        {
            checkOpt.state |= QStyle::State_On;
        }
        else if (index.data(Qt::CheckStateRole).toInt() == Qt::PartiallyChecked)
        {
            checkOpt.state |= QStyle::State_NoChange;
        }
        else
        {
            checkOpt.state |= QStyle::State_Off;
        }
        style->drawControl(QStyle::CE_CheckBox, &checkOpt, painter, opt.widget);

        // 调整文本矩形，使其不与复选框重叠
        opt.rect.setLeft(checkRect.right() + margin);
    }

    // 绘制文本
    QString text = opt.text;
    QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
    textRect.setWidth(opt.rect.width() - textRect.left()); // 限制文本宽度
    style->drawItemText(painter, textRect, opt.displayAlignment, opt.palette, opt.state & QStyle::State_Enabled, text);

    // 2. 绘制我们的自定义预览线
    // --- 修改：只为信号条目绘制 ---
    if (!isFileItem)
    {
        // 从模型的 UserRole + 1 中获取 QPen
        QVariant penData = index.data(PenDataRole);
        if (penData.canConvert<QPen>())
        {
            QPen linePen = penData.value<QPen>();
            linePen.setWidth(2); // 在预览中加粗线条

            painter->save();
            painter->setPen(linePen);

            // 计算预览线的矩形区域
            // 它位于条目的最右侧
            QRect previewRect(
                fullRect.right() - previewWidth, // X
                fullRect.top(),                  // Y
                previewWidth,                    // Width
                fullRect.height()                // Height
            );

            // 在矩形中间绘制一条水平线
            int y = previewRect.center().y();
            painter->drawLine(previewRect.left() + margin, y, previewRect.right() - margin, y);

            painter->restore();
        }
    }
    // --- ----------------------- ---
}