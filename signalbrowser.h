#ifndef SIGNALBROWSER_H
#define SIGNALBROWSER_H

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHash>
#include <QSet>
#include "datamanager.h" // 需要 FileData 定义

class SignalBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit SignalBrowser(QWidget *parent = nullptr);

    /**
     * @brief 加载文件数据到树中
     */
    void loadFileData(const FileData &data);

    /**
     * @brief 从树中移除文件
     */
    void removeFile(const QString &filename);

    /**
     * @brief 获取指定信号的画笔样式 (颜色、线宽等)
     */
    QPen getSignalPen(const QString &uniqueId) const;

    /**
     * @brief 设置指定信号的勾选状态
     * @param uniqueId 信号ID
     * @param checked 是否勾选
     * @param blockSignals 是否阻止发出信号 (防止循环调用)
     */
    void setSignalChecked(const QString &uniqueId, bool checked, bool blockSignals = true);

    /**
     * @brief 批量更新信号勾选状态 (通常用于切换子图时)
     * @param activeSignalIds 当前活动子图包含的所有信号ID集合
     */
    void updateChecksForActivePlot(const QSet<QString> &activeSignalIds);

    /**
     * @brief 通过名称查找信号 Item (用于导入视图)
     */
    QStandardItem *findItemByName(const QString &signalName) const;

    QString getSignalName(const QString &uniqueId) const;

    /**
     * @brief 在树中选中指定ID的信号并滚动到该位置
     */
    void selectSignal(const QString &uniqueId);

    /**
     * @brief 清空所有内容
     */
    void clear();

    /**
     * @brief 设置新加载信号的默认线宽
     */
    void setDefaultPenWidth(int width);

    /**
     * @brief 获取当前的默认线宽
     */
    int defaultPenWidth() const;

signals:
    /**
     * @brief 当用户在树中勾选或取消勾选信号时发出
     */
    void signalCheckStateChanged(const QString &uniqueId, bool checked);

    /**
     * @brief 当用户双击修改了信号样式时发出
     */
    void signalPenChanged(const QString &uniqueId, const QPen &newPen);

    /**
     * @brief 当用户请求删除文件时发出 (右键菜单)
     */
    void fileRemoveRequested(const QString &filename);

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemChanged(QStandardItem *item);
    void onItemDoubleClicked(const QModelIndex &index);
    void onCustomContextMenu(const QPoint &pos);

private:
    void setupUi();
    bool filterTree(QStandardItem *item, const QString &query);
    QStandardItem *findItemByName_Recursive(QStandardItem *parentItem, const QString &name) const;

    QLineEdit *m_searchBox;
    QTreeView *m_treeView;
    QStandardItemModel *m_model;

    // 快速查找表: ID -> Item
    QHash<QString, QStandardItem *> m_uniqueIdMap;

    // 颜色管理
    QVector<QColor> m_colorList;
    int m_colorIndex;

    // 默认线宽设置
    int m_defaultPenWidth;
};

#endif // SIGNALBROWSER_H