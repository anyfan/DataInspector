#ifndef SIGNALTREEDELEGATE_H
#define SIGNALTREEDELEGATE_H

#include <QStyledItemDelegate>

/**
 * @brief 自定义委托，用于在 QTreeView 的条目右侧绘制信号预览线
 * * 遵循 readme.md 中 2.1 节关于属性编辑器的精神，
 * 但作为第一步在条目中直接提供只读预览。
 */
class SignalTreeDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SignalTreeDelegate(QObject *parent = nullptr);

    // 重新实现 paint 来绘制自定义预览
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

#endif // SIGNALTREEDELEGATE_H