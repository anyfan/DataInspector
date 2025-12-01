#pragma once

#include "qcustomplot.h"

// 自定义流式布局图例类
class FlowLegend : public QCPLegend
{
public:
    explicit FlowLegend() : QCPLegend()
    {
        setColumnSpacing(10);
        setRowSpacing(5);
    }

    virtual QSize minimumOuterSizeHint() const override
    {
        if (itemCount() == 0)
            return QSize(0, 0);

        int currentWidth = mOuterRect.width();
        if (currentWidth <= 0)
            currentWidth = 100;

        // 只计算高度，不应用布局 (apply=false)
        int requiredHeight = calculateLayout(currentWidth, false);

        return QSize(0, requiredHeight);
    }

    virtual void updateLayout() override
    {
        // 计算并应用布局 (apply=true)
        calculateLayout(mOuterRect.width(), true);
    }

private:
    // [核心逻辑] 统一的布局计算函数
    // apply: 如果为 true，则实际设置元素位置；如果为 false，仅计算所需高度
    int calculateLayout(int availWidth, bool apply) const
    {
        double x = mMargins.left();
        double y = mMargins.top();
        double currentLineHeight = 0;

        QList<QCPLayoutElement *> items;
        for (int i = 0; i < itemCount(); ++i)
        {
            if (QCPAbstractLegendItem *el = item(i))
            {
                if (el->realVisibility() && !el->minimumOuterSizeHint().isEmpty())
                    items.append(el);
            }
        }

        if (items.isEmpty())
            return mMargins.top() + mMargins.bottom();

        for (QCPLayoutElement *el : items)
        {
            QSize sz = el->minimumOuterSizeHint();

            // 换行判断
            if (x + sz.width() + mMargins.right() > availWidth && x > mMargins.left())
            {
                x = mMargins.left();
                y += currentLineHeight + mRowSpacing;
                currentLineHeight = 0;
            }

            // 应用位置
            if (apply)
            {
                el->setOuterRect(QRect(mOuterRect.left() + x, mOuterRect.top() + y, sz.width(), sz.height()));
            }

            x += sz.width() + mColumnSpacing;

            if (sz.height() > currentLineHeight)
                currentLineHeight = sz.height();
        }

        return y + currentLineHeight + mMargins.bottom();
    }
};
