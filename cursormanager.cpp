#include "cursormanager.h"
#include "qcustomplot.h"
#include <QMouseEvent>
#include <QDebug>
#include <algorithm>
#include <QFontMetrics>

CursorManager::CursorManager(QList<QCustomPlot *> *plotWidgets,
                             QObject *parent)
    : QObject(parent),
      m_plotWidgets(plotWidgets),
      m_currentActivePlot(nullptr)
{
    m_cursorMode = CursorManager::NoCursor;
    // 初始化两个游标
    m_cursors.resize(2);
}

CursorManager::~CursorManager()
{
    // clearCursors 会删除所有 QCPItems
    // 我们不需要手动删除，因为 QCustomPlot 会管理它们
}

CursorManager::CursorMode CursorManager::getMode() const
{
    return m_cursorMode;
}

void CursorManager::setActivePlot(QCustomPlot *plot)
{
    m_currentActivePlot = plot;
}

/**
 * @brief 响应游标模式切换
 */
void CursorManager::onCursorActionTriggered(QAction *action)
{
    QString text = action->text();
    CursorMode newMode = CursorManager::NoCursor;

    if (text == tr("单游标"))
    {
        newMode = CursorManager::SingleCursor;
    }
    else if (text == tr("双游标"))
    {
        newMode = CursorManager::DoubleCursor;
    }
    else // (text == tr("关闭游标"))
    {
        newMode = CursorManager::NoCursor;
    }

    setMode(newMode);
}

/**
 * @brief 以编程方式设置游标模式
 */
void CursorManager::setMode(CursorManager::CursorMode mode)
{
    if (m_cursorMode == mode)
        return;

    CursorMode oldMode = m_cursorMode;
    m_cursorMode = mode;

    if (mode != CursorManager::NoCursor)
    {
        QCustomPlot *plot = m_currentActivePlot;
        if (!plot && !m_plotWidgets->isEmpty())
            plot = m_plotWidgets->first();

        QCPRange visibleRange(0, 1);
        if (plot)
            visibleRange = plot->xAxis->range();

        double margin = visibleRange.size() * 0.05;
        if (margin <= 0)
            margin = 0.1;

        // 初始化位置逻辑
        if (oldMode == CursorManager::NoCursor)
        {
            m_cursors[0].key = visibleRange.lower + margin;
            if (mode == CursorManager::DoubleCursor)
            {
                m_cursors[1].key = visibleRange.upper - margin;
            }
        }
        else if (oldMode == CursorManager::SingleCursor && mode == CursorManager::DoubleCursor)
        {
            // 保持 C1 不变，计算 C2
            double newKey2 = visibleRange.upper - margin;
            if (newKey2 <= m_cursors[0].key + margin)
            {
                newKey2 = m_cursors[0].key + (visibleRange.size() * 0.1);
                if (newKey2 >= visibleRange.upper)
                    newKey2 = (m_cursors[0].key + visibleRange.upper) / 2.0;
            }
            m_cursors[1].key = newKey2;
        }

        // 吸附
        m_cursors[0].key = snapKeyToData(m_cursors[0].key);
        if (mode == CursorManager::DoubleCursor)
        {
            m_cursors[1].key = snapKeyToData(m_cursors[1].key);
        }
    }

    for (QCustomPlot *plot : *m_plotWidgets)
    {
        plot->setInteraction(QCP::iRangeDrag, true);
    }

    setupCursors();
    updateAllCursors();
}
/**
 * @brief 使用内部键值强制更新所有游标
 */
void CursorManager::updateAllCursors()
{
    if (m_cursorMode == CursorManager::NoCursor)
        return;

    updateCursors(m_cursors[0].key, 1);
    if (m_cursorMode == CursorManager::DoubleCursor)
        updateCursors(m_cursors[1].key, 2);
}

/**
 * @brief 响应 Plot 上的鼠标按下
 */
void CursorManager::onPlotMousePress(QMouseEvent *event)
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot || m_cursorMode == CursorManager::NoCursor)
        return;

    int plotIndex = m_plotWidgets->indexOf(plot);
    if (plotIndex == -1)
        return;

    // 遍历所有活动游标检查点击
    int maxCursorIdx = (m_cursorMode == CursorManager::DoubleCursor) ? 1 : 0;

    for (int i = 0; i <= maxCursorIdx; ++i)
    {
        if (plotIndex < m_cursors[i].lines.size())
        {
            double dist = m_cursors[i].lines.at(plotIndex)->selectTest(event->pos(), false);
            if (dist >= 0 && dist < plot->selectionTolerance())
            {
                m_cursors[i].isDragging = true;
                plot->setInteraction(QCP::iRangeDrag, false);
                event->accept();
                return; // 优先响应第一个捕获的游标
            }
        }
    }
}

/**
 * @brief 响应 Plot 上的鼠标移动
 */
void CursorManager::onPlotMouseMove(QMouseEvent *event)
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (plot)
        m_currentActivePlot = plot;
    else if (m_currentActivePlot)
        plot = m_currentActivePlot;
    else if (!m_plotWidgets->isEmpty())
        plot = m_plotWidgets->first();
    else
        return;

    // 1. 处理拖拽
    int draggingIndex = -1;
    if (m_cursors[0].isDragging)
        draggingIndex = 0;
    else if (m_cursors[1].isDragging)
        draggingIndex = 1;

    if (draggingIndex != -1)
    {
        double smoothKey = plot->xAxis->pixelToCoord(event->pos().x());
        double snappedKey = smoothKey;

        // 简单的最近邻查找逻辑 (复用原逻辑，但简化变量)
        double minDistance = -1.0;

        for (int i = 0; i < plot->graphCount(); ++i)
        {
            QCPGraph *graph = plot->graph(i);
            if (graph && !graph->data()->isEmpty())
            {
                auto it = graph->data()->findBegin(smoothKey);
                if (it != graph->data()->constEnd())
                {
                    double dist = qAbs(it->key - smoothKey);
                    if (minDistance < 0 || dist < minDistance)
                    {
                        minDistance = dist;
                        snappedKey = it->key;
                    }
                }
                if (it != graph->data()->constBegin())
                {
                    double dist = qAbs((it - 1)->key - smoothKey);
                    if (minDistance < 0 || dist < minDistance)
                    {
                        minDistance = dist;
                        snappedKey = (it - 1)->key;
                    }
                }
            }
        }

        // 更新对应游标
        updateCursors(snappedKey, draggingIndex + 1);
        event->accept();
    }
    // 2. 处理悬停效果
    else if (m_cursorMode != CursorManager::NoCursor)
    {
        int plotIndex = m_plotWidgets->indexOf(plot);
        if (plotIndex != -1)
        {
            bool nearCursor = false;
            int maxCursorIdx = (m_cursorMode == CursorManager::DoubleCursor) ? 1 : 0;
            for (int i = 0; i <= maxCursorIdx; ++i)
            {
                if (plotIndex < m_cursors[i].lines.size())
                {
                    double dist = m_cursors[i].lines.at(plotIndex)->selectTest(event->pos(), false);
                    if (dist >= 0 && dist < plot->selectionTolerance())
                        nearCursor = true;
                }
            }
            plot->setCursor(nearCursor ? Qt::SizeHorCursor : Qt::ArrowCursor);
        }
    }
    else
    {
        plot->setCursor(Qt::ArrowCursor);
    }
}

/**
 * @brief 响应 Plot 上的鼠标释放
 */
void CursorManager::onPlotMouseRelease(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (m_cursors[0].isDragging || m_cursors[1].isDragging)
    {
        if (m_currentActivePlot && m_cursorMode != CursorManager::NoCursor)
            m_currentActivePlot->setInteraction(QCP::iRangeDrag, true);
    }
    m_cursors[0].isDragging = false;
    m_cursors[1].isDragging = false;
}

/**
 * @brief 销毁所有游标项
 */
void CursorManager::clearCursors()
{
    auto safeRemoveItem = [](QCPAbstractItem *item)
    {
        if (item && item->parentPlot())
            item->parentPlot()->removeItem(item);
        else if (item)
            delete item;
    };

    // 遍历所有游标数据进行清理
    for (int i = 0; i < m_cursors.size(); ++i)
    {
        for (auto item : m_cursors[i].yLabels.values())
            safeRemoveItem(item);
        m_cursors[i].yLabels.clear();

        for (auto item : m_cursors[i].graphTracers.values())
            safeRemoveItem(item);
        m_cursors[i].graphTracers.clear();

        for (auto item : m_cursors[i].lines)
            safeRemoveItem(item);
        m_cursors[i].lines.clear();

        for (auto item : m_cursors[i].xLabels)
            safeRemoveItem(item);
        m_cursors[i].xLabels.clear();
    }
}

/**
 * @brief 根据 m_cursorMode 设置游标
 */
void CursorManager::setupCursors()
{
    clearCursors();

    if (m_cursorMode == CursorManager::NoCursor)
    {
        for (QCustomPlot *plot : *m_plotWidgets)
            plot->replot();
        return;
    }

    // 定义样式配置
    struct StyleConfig
    {
        QColor color;
        Qt::PenStyle style;
    };
    StyleConfig configs[] = {
        {Qt::red, Qt::DashLine}, // Cursor 1
        {Qt::blue, Qt::DashLine} // Cursor 2
    };

    QColor xLabelBgColor(255, 255, 255, 200);
    QBrush xLabelBrush(xLabelBgColor);
    QPen xLabelPen(Qt::black);
    QFont cursorFont = (!m_plotWidgets->isEmpty()) ? m_plotWidgets->first()->font() : QFont();
    cursorFont.setPointSize(7);

    int activeCursors = (m_cursorMode == DoubleCursor) ? 2 : 1;

    // 遍历每个子图
    for (QCustomPlot *plot : *m_plotWidgets)
    {
        // 遍历每个逻辑游标 (1 或 2)
        for (int i = 0; i < activeCursors; ++i)
        {
            CursorData &cursor = m_cursors[i];
            QPen linePen(configs[i].color, 0, configs[i].style);

            // 1. 创建 Line
            QCPItemLine *line = new QCPItemLine(plot);
            line->setPen(linePen);
            line->setSelectable(true);
            line->start->setType(QCPItemPosition::ptAbsolute);
            line->end->setType(QCPItemPosition::ptAbsolute);
            line->setClipToAxisRect(true);
            cursor.lines.append(line);

            // 2. 创建 X Label
            QCPItemText *xLabel = new QCPItemText(plot);
            xLabel->setLayer("overlay");
            xLabel->setClipToAxisRect(false);
            xLabel->setPadding(QMargins(5, 2, 5, 2));
            xLabel->setBrush(xLabelBrush);
            xLabel->setPen(xLabelPen);
            xLabel->setFont(cursorFont);
            xLabel->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter);
            xLabel->position->setParentAnchor(line->start);
            xLabel->position->setCoords(0, 5);
            cursor.xLabels.append(xLabel);

            // 3. 创建 Tracers & Y Labels
            for (int g = 0; g < plot->graphCount(); ++g)
            {
                QCPGraph *graph = plot->graph(g);
                if (!graph)
                    continue;

                QCPItemTracer *tracer = new QCPItemTracer(plot);
                tracer->setGraph(graph);
                tracer->setInterpolating(false);
                tracer->setVisible(true);
                tracer->setStyle(QCPItemTracer::tsCircle);
                tracer->setSize(3);
                tracer->setPen(graph->pen());
                tracer->setBrush(QBrush(graph->pen().color()));
                cursor.graphTracers.insert(graph, tracer);

                QCPItemText *yLabel = new QCPItemText(plot);
                yLabel->setLayer("overlay");
                yLabel->setClipToAxisRect(false);
                yLabel->setPadding(QMargins(5, 2, 5, 2));
                yLabel->setBrush(QBrush(QColor(255, 255, 255, 180)));
                yLabel->setPen(QPen(graph->pen().color()));
                yLabel->setColor(graph->pen().color());
                yLabel->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                yLabel->position->setParentAnchor(tracer->position);
                yLabel->position->setCoords(5, 0);
                yLabel->setFont(cursorFont);
                cursor.yLabels.insert(tracer, yLabel);
            }
        }
    }

    for (QCustomPlot *plot : *m_plotWidgets)
        plot->replot();
}

/**
 * @brief [辅助] 解析并堆叠重叠的Y轴游标标签
 * * 这个函数在 updateCursors 中被调用
 * @param labelsOnPlot 此子图上此游标的所有 Y 轴标签
 */
void CursorManager::resolveLabelOverlaps(QList<QCPItemText *> &labelsOnPlot)
{
    if (labelsOnPlot.size() < 2)
    {
        // 如果只有一个或没有标签，重置其偏移量 (以防万一)
        if (!labelsOnPlot.isEmpty())
            labelsOnPlot.first()->position->setCoords(5, 0);
        return;
    }

    // 1. 按理想的 Y 像素位置 (从上到下) 对标签进行排序
    std::sort(labelsOnPlot.begin(), labelsOnPlot.end(), [](QCPItemText *a, QCPItemText *b)
              {

        QCPItemPosition *posA = static_cast<QCPItemPosition*>(a->position->parentAnchor()); // Tracer A
        QCPItemPosition *posB = static_cast<QCPItemPosition*>(b->position->parentAnchor()); // Tracer B
  
        
        if (posA && posB)
        {
            // Y 像素坐标 0 在顶部
            return posA->pixelPosition().y() < posB->pixelPosition().y();
        }
        return false; });

    // 2. 迭代，检查重叠并应用垂直偏移
    // (我们假设所有标签字体和内边距都相同)
    QFontMetrics fm(labelsOnPlot.first()->font());
    const int labelHeight = fm.height() + labelsOnPlot.first()->padding().top() + labelsOnPlot.first()->padding().bottom();
    const int verticalGap = 2;      // 标签之间的垂直间隙
    const int horizontalOffset = 5; // 默认水平偏移

    // 'lastBottomY' 存储上一个标签放置后的*屏幕* Y 像素坐标 (底部边缘)
    double lastBottomY = -1e9; // 初始化为一个非常小的值

    for (QCPItemText *label : labelsOnPlot)
    {

        QCPItemPosition *anchor = static_cast<QCPItemPosition *>(label->position->parentAnchor()); // Tracer

        if (!anchor)
            continue;

        // 标签锚点 (tracer) 的理想 Y 像素位置 (垂直居中)
        double idealY = anchor->pixelPosition().y();

        // 该标签的理想*顶部*边缘
        // (标签的 Y 坐标是其中心，所以顶部是 中心 - 半高)
        double idealTopY = idealY - (labelHeight / 2.0);

        // 检查是否与上一个标签重叠
        if (idealTopY < lastBottomY + verticalGap)
        {
            //  重叠：向下推
            // 新的顶部边缘应位于上一个标签的底部 + 间隙
            double newTopY = lastBottomY + verticalGap;

            // 计算新的中心点
            double newCenterY = newTopY + (labelHeight / 2.0);

            // Y 偏移量是 *新中心* 和 *理想中心* 之间的差值
            double yOffset = newCenterY - idealY;

            label->position->setCoords(horizontalOffset, yOffset);

            // 更新下一个循环要检查的 "最后一个底部"
            lastBottomY = newTopY + labelHeight;
        }
        else
        {
            //  不重叠：使用理想位置
            label->position->setCoords(horizontalOffset, 0); // 0 垂直偏移

            // 更新 "最后一个底部"
            lastBottomY = idealTopY + labelHeight;
        }
    }
}

/**
 * @brief [槽] 核心同步逻辑：更新所有游标
 */
void CursorManager::updateCursors(double key, int cursorIndex)
{
    // 验证索引 (1-based)
    if (cursorIndex < 1 || cursorIndex > m_cursors.size())
        return;

    // 获取对应的 CursorData (0-based)
    CursorData &cursor = m_cursors[cursorIndex - 1];

    if (qFuzzyCompare(cursor.key, key))
    {
        if (m_cursorMode == CursorManager::NoCursor)
            return;
    }

    cursor.key = key;
    emit cursorKeyChanged(key, cursorIndex);

    if (m_cursorMode == CursorManager::NoCursor)
        return;
    if (cursorIndex == 2 && m_cursorMode != CursorManager::DoubleCursor)
        return;

    // 遍历所有 Plot 并更新视觉元素
    for (int i = 0; i < m_plotWidgets->size(); ++i)
    {
        QCustomPlot *plot = m_plotWidgets->at(i);
        if (i >= cursor.lines.size())
            continue;

        QList<QCPItemText *> labelsOnThisPlot;

        // A. Update Line
        double xPixel = plot->xAxis->coordToPixel(key);
        QCPItemLine *line = cursor.lines.at(i);
        line->start->setCoords(xPixel, plot->axisRect()->bottom());
        line->end->setCoords(xPixel, plot->axisRect()->top());

        // B. Update X Label
        cursor.xLabels.at(i)->setText(QString::number(key, 'f', 4));

        // C. Update Tracers and Y Labels
        for (int j = 0; j < plot->graphCount(); ++j)
        {
            QCPGraph *graph = plot->graph(j);
            if (cursor.graphTracers.contains(graph))
            {
                QCPItemTracer *tracer = cursor.graphTracers.value(graph);
                tracer->setGraphKey(key);
                tracer->updatePosition();

                QCPItemText *yLabel = cursor.yLabels.value(tracer, nullptr);
                if (yLabel)
                {
                    double value = tracer->position->value();
                    yLabel->setText(QString::number(value, 'f', 3));
                    yLabel->setVisible(true);
                    labelsOnThisPlot.append(yLabel);
                }
            }
        }
        resolveLabelOverlaps(labelsOnThisPlot);
        plot->replot();
    }
}

/**
 * @brief [辅助] 将一个 key (X坐标) 吸附到活动子图上的最近数据点
 * @param key 要吸附的原始 key
 * @return 吸附后的 key
 */
double CursorManager::snapKeyToData(double key) const
{
    QCustomPlot *plot = m_currentActivePlot;
    if (!plot && !m_plotWidgets->isEmpty())
        plot = m_plotWidgets->first();
    if (!plot)
        return key;

    // 修改：直接检查 graphCount
    if (plot->graphCount() == 0)
        return key;

    double closestKey = key;
    double minDistance = std::numeric_limits<double>::max();
    bool foundAny = false;

    // 修改：直接遍历 graph
    for (int i = 0; i < plot->graphCount(); ++i)
    {
        QCPGraph *graph = plot->graph(i);
        if (!graph || !graph->visible() || graph->data()->isEmpty())
            continue;

        auto it = graph->data()->findBegin(key);

        if (it != graph->data()->constEnd())
        {
            double dist = qAbs(it->key - key);
            if (dist < minDistance)
            {
                minDistance = dist;
                closestKey = it->key;
                foundAny = true;
            }
        }
        if (it != graph->data()->constBegin())
        {
            auto prevIt = it - 1;
            double dist = qAbs(prevIt->key - key);
            if (dist < minDistance)
            {
                minDistance = dist;
                closestKey = prevIt->key;
                foundAny = true;
            }
        }
    }

    return foundAny ? closestKey : key;
}