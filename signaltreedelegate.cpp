#include "signaltreedelegate.h"
#include "mainwindow.h"

#include <QPainter>
#include <QPen>
#include <QVariant>
#include <QApplication>

SignalTreeDelegate::SignalTreeDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void SignalTreeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
    // 获取是否为信号条目
    bool isSignalItem = index.data(TreeItemRoles::IsSignalItemRole).toBool();

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // 预留右侧 40 像素用于绘制预览
    const int previewWidth = 40;
    const int margin = 2;

    QRect previewRect;

    if (isSignalItem)
    {
        opt.rect.adjust(0, 0, -previewWidth - margin, 0);

        // 计算预览区域的几何位置
        previewRect = option.rect;
        previewRect.setLeft(option.rect.right() - previewWidth);
    }

    QStyledItemDelegate::paint(painter, opt, index);

    // 2. 在预留区域绘制自定义预览线
    if (isSignalItem)
    {
        QVariant penData = index.data(TreeItemRoles::PenDataRole);
        if (penData.canConvert<QPen>())
        {
            QPen linePen = penData.value<QPen>();

            painter->save();
            painter->setPen(linePen);

            // 设置抗锯齿让线条更好看
            painter->setRenderHint(QPainter::Antialiasing);

            // 在矩形中间绘制一条水平线
            int y = previewRect.center().y();
            // 左右各留一点边距
            painter->drawLine(previewRect.left() + margin, y, previewRect.right() - margin, y);

            painter->restore();
        }
    }
}