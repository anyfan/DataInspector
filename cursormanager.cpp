#include "cursormanager.h"
#include "qcustomplot.h"
#include <QMouseEvent>
#include <QDebug>

CursorManager::CursorManager(QMap<QCustomPlot *, QMap<QString, QCPGraph *>> *plotGraphMap,
                             QList<QCustomPlot *> *plotWidgets,
                             QCustomPlot **lastMousePlotPtr,
                             QObject *parent)
    : QObject(parent),
      m_plotGraphMap(plotGraphMap),
      m_plotWidgets(plotWidgets),
      m_lastMousePlotPtr(lastMousePlotPtr)
{
    m_cursorMode = CursorManager::NoCursor;
    m_cursorKey1 = 0;
    m_cursorKey2 = 0;
    m_isDraggingCursor1 = false;
    m_isDraggingCursor2 = false;
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

    setMode(newMode); // <-- 调用新的 setMode 槽
}

/**
 * @brief [新增] 以编程方式设置游标模式
 */
void CursorManager::setMode(CursorManager::CursorMode mode)
{
    if (m_cursorMode == mode)
        return; // 模式未改变

    m_cursorMode = mode;

    // --- 修正：在启用/禁用游标时，始终保持平移开启 ---
    for (QCustomPlot *plot : *m_plotWidgets)
    {
        plot->setInteraction(QCP::iRangeDrag, true);
    }
    // --- ----------------------------------------- ---

    // 重建游标 UI
    setupCursors();
    updateAllCursors(); // <-- 使用 updateAllCursors
}

/**
 * @brief [新增] 使用内部键值强制更新所有游标
 */
void CursorManager::updateAllCursors()
{
    updateCursors(m_cursorKey1, 1);
    updateCursors(m_cursorKey2, 2);
}

/**
 * @brief 响应 Plot 上的鼠标按下
 */
void CursorManager::onPlotMousePress(QMouseEvent *event)
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot)
        return;

    // (MainWindow 的 onPlotMousePress 中的非游标逻辑被保留在 MainWindow 中)
    // ...

    if (m_cursorMode == CursorManager::NoCursor)
        return; // 游标未激活

    // 检查是否点击了游标 1
    if (m_cursorMode == CursorManager::SingleCursor || m_cursorMode == CursorManager::DoubleCursor)
    {
        int plotIndex = m_plotWidgets->indexOf(plot);
        if (plotIndex != -1 && plotIndex < m_cursorLines1.size())
        {
            // 检查与线的距离
            double dist1 = m_cursorLines1.at(plotIndex)->selectTest(event->pos(), false);
            if (dist1 >= 0 && dist1 < plot->selectionTolerance())
            {
                m_isDraggingCursor1 = true;
                // --- 修正：暂时禁用平移 ---
                plot->setInteraction(QCP::iRangeDrag, false);
                event->accept(); // 接受事件，阻止 QCustomPlot 的 iRangeDrag
                return;          // 优先拖动游标 1
            }
        }
    }

    // 检查是否点击了游标 2
    if (m_cursorMode == CursorManager::DoubleCursor)
    {
        int plotIndex = m_plotWidgets->indexOf(plot);
        if (plotIndex != -1 && plotIndex < m_cursorLines2.size())
        {
            double dist2 = m_cursorLines2.at(plotIndex)->selectTest(event->pos(), false);
            if (dist2 >= 0 && dist2 < plot->selectionTolerance())
            {
                m_isDraggingCursor2 = true;
                // --- 修正：暂时禁用平移 ---
                plot->setInteraction(QCP::iRangeDrag, false);
                event->accept(); // 接受事件，阻止 QCustomPlot 的 iRangeDrag
                return;
            }
        }
    }
}

/**
 * @brief 响应 Plot 上的鼠标移动
 */
void CursorManager::onPlotMouseMove(QMouseEvent *event)
{
    // (*m_lastMousePlotPtr) 是对 MainWindow::m_lastMousePlot 的解引用
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (plot)
        (*m_lastMousePlotPtr) = plot; // 记录最后交互的 plot
    else if ((*m_lastMousePlotPtr))
        plot = (*m_lastMousePlotPtr); // 后备
    else if (!m_plotWidgets->isEmpty())
        plot = m_plotWidgets->first(); // 最终后备
    else
        return; // 没有 plot

    // 1. 从 plot 获取平滑的 x 坐标 (key)
    double smoothKey = plot->xAxis->pixelToCoord(event->pos().x());
    double snappedKey = smoothKey; // 默认使用平滑键

    // --- 拖拽逻辑 (在此处实现吸附) ---
    if (m_isDraggingCursor1 || m_isDraggingCursor2)
    {
        // 仅在拖动时执行吸附
        double closestKey = smoothKey;
        double minDistance = -1.0;

        // 2. 查找此图(plot)上的所有图表(graph)
        const auto &graphsOnPlot = m_plotGraphMap->value(plot);
        if (!graphsOnPlot.isEmpty())
        {
            // 3. 遍历此图上的所有图表，找到最近的数据点键
            for (QCPGraph *graph : graphsOnPlot)
            {
                if (graph && !graph->data()->isEmpty())
                {
                    // 使用 QCPDataContainer 的 findBegin 进行高效的二分查找
                    auto it = graph->data()->findBegin(smoothKey); // 找到第一个 >= smoothKey 的点

                    // 检查找到的点
                    if (it != graph->data()->constEnd())
                    {
                        double distAt = qAbs(it->key - smoothKey);
                        if (minDistance < 0 || distAt < minDistance)
                        {
                            minDistance = distAt;
                            closestKey = it->key;
                        }
                    }

                    // 检查找到的点的前一个点
                    if (it != graph->data()->constBegin())
                    {
                        double distBefore = qAbs((it - 1)->key - smoothKey);
                        if (minDistance < 0 || distBefore < minDistance)
                        {
                            minDistance = distBefore;
                            closestKey = (it - 1)->key;
                        }
                    }
                }
            } // 结束 for (graphs)

            if (minDistance >= 0) // 如果找到了一个最近的键
            {
                snappedKey = closestKey; // 4. 使用吸附后的键
            }
        }
        // else: 如果此图上没有图表, 将使用 smoothKey
    }

    // --- 5. 使用最终的键 (snappedKey) 更新游标 ---
    if (m_isDraggingCursor1)
    {
        updateCursors(snappedKey, 1);
        event->accept();
    }
    else if (m_isDraggingCursor2)
    {
        updateCursors(snappedKey, 2);
        event->accept();
    }
    // --- 悬停逻辑 (保持不变，不吸附) ---
    else if (m_cursorMode != CursorManager::NoCursor)
    {
        int plotIndex = m_plotWidgets->indexOf(plot);
        if (plotIndex == -1)
        {
            plot->setCursor(Qt::ArrowCursor);
            return;
        }

        bool nearCursor = false;
        if (plotIndex < m_cursorLines1.size())
        {
            // 悬停检查是基于像素位置的，所以我们不需要使用键
            double dist1 = m_cursorLines1.at(plotIndex)->selectTest(event->pos(), false);
            if (dist1 >= 0 && dist1 < plot->selectionTolerance())
                nearCursor = true;
        }

        if (!nearCursor && m_cursorMode == CursorManager::DoubleCursor && plotIndex < m_cursorLines2.size())
        {
            double dist2 = m_cursorLines2.at(plotIndex)->selectTest(event->pos(), false);
            if (dist2 >= 0 && dist2 < plot->selectionTolerance())
                nearCursor = true;
        }

        if (nearCursor)
            plot->setCursor(Qt::SizeHorCursor);
        else
            plot->setCursor(Qt::ArrowCursor);
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

    // --- 修正：重新启用平移 ---
    if (m_isDraggingCursor1 || m_isDraggingCursor2)
    {
        // 必须找到正确的 plot 指针，sender() 可能不可靠
        QCustomPlot *plot = (*m_lastMousePlotPtr);
        if (plot && m_cursorMode != CursorManager::NoCursor)
        {
            // 拖动游标后，重新启用平移
            plot->setInteraction(QCP::iRangeDrag, true);
        }
    }
    // --- ----------------------- ---

    m_isDraggingCursor1 = false;
    m_isDraggingCursor2 = false;
}

/**
 * @brief 销毁所有游标项
 */
void CursorManager::clearCursors()
{
    // 辅助lambda函数，用于安全地移除 item
    auto safeRemoveItem = [](QCPAbstractItem *item)
    {
        if (item && item->parentPlot())
        {
            item->parentPlot()->removeItem(item);
        }
        else if (item)
        {
            qWarning() << "clearCursors: Item has no parent plot, deleting directly.";
            delete item;
        }
    };

    for (QCPItemText *item : m_cursorYLabels1.values())
        safeRemoveItem(item);
    m_cursorYLabels1.clear();
    for (QCPItemText *item : m_cursorYLabels2.values())
        safeRemoveItem(item);
    m_cursorYLabels2.clear();

    for (QCPItemTracer *item : m_graphTracers1.values())
        safeRemoveItem(item);
    m_graphTracers1.clear();
    for (QCPItemTracer *item : m_graphTracers2.values())
        safeRemoveItem(item);
    m_graphTracers2.clear();

    for (QCPItemLine *item : m_cursorLines1)
        safeRemoveItem(item);
    m_cursorLines1.clear();
    for (QCPItemLine *item : m_cursorLines2)
        safeRemoveItem(item);
    m_cursorLines2.clear();
    for (QCPItemText *item : m_cursorXLabels1)
        safeRemoveItem(item);
    m_cursorXLabels1.clear();
    for (QCPItemText *item : m_cursorXLabels2)
        safeRemoveItem(item);
    m_cursorXLabels2.clear();
}

/**
 * @brief 根据 m_cursorMode 设置游标
 */
void CursorManager::setupCursors()
{
    clearCursors();

    if (m_cursorMode == CursorManager::NoCursor)
    {
        // 隐藏所有图表的游标信息
        for (QCustomPlot *plot : *m_plotWidgets)
        {
            plot->replot();
        }
        return;
    }

    QPen pen1(Qt::red, 0, Qt::DashLine);
    QPen pen2(Qt::blue, 0, Qt::DashLine);

    QColor xLabelBgColor(255, 255, 255, 200);
    QBrush xLabelBrush(xLabelBgColor);
    QPen xLabelPen(Qt::black);

    QFont cursorFont;
    if (!m_plotWidgets->isEmpty())
        cursorFont = m_plotWidgets->first()->font();
    else
        cursorFont = QFont(); // Use default

    cursorFont.setPointSize(7);

    // 1. 为每个 Plot 创建垂直线 和 X轴标签
    for (QCustomPlot *plot : *m_plotWidgets)
    {
        // --- 创建游标 1 ---
        QCPItemLine *line1 = new QCPItemLine(plot);
        line1->setPen(pen1);
        line1->setSelectable(true);
        line1->start->setType(QCPItemPosition::ptAbsolute);
        line1->end->setType(QCPItemPosition::ptAbsolute);
        line1->setClipToAxisRect(true);
        m_cursorLines1.append(line1);

        QCPItemText *xLabel1 = new QCPItemText(plot);
        xLabel1->setLayer("overlay");
        xLabel1->setClipToAxisRect(false);
        xLabel1->setPadding(QMargins(5, 2, 5, 2));
        xLabel1->setBrush(xLabelBrush);
        xLabel1->setPen(xLabelPen);
        xLabel1->setFont(cursorFont);
        xLabel1->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter);
        xLabel1->position->setParentAnchor(line1->start);
        xLabel1->position->setCoords(0, 5);
        m_cursorXLabels1.append(xLabel1);

        if (m_cursorMode == CursorManager::DoubleCursor)
        {
            // --- 创建游标 2 ---
            QCPItemLine *line2 = new QCPItemLine(plot);
            line2->setPen(pen2);
            line2->setSelectable(true);
            line2->start->setType(QCPItemPosition::ptAbsolute);
            line2->end->setType(QCPItemPosition::ptAbsolute);
            line2->setClipToAxisRect(true);
            m_cursorLines2.append(line2);

            QCPItemText *xLabel2 = new QCPItemText(plot);
            xLabel2->setLayer("overlay");
            xLabel2->setClipToAxisRect(false);
            xLabel2->setPadding(QMargins(5, 2, 5, 2));
            xLabel2->setBrush(xLabelBrush);
            xLabel2->setPen(xLabelPen);
            xLabel2->setFont(cursorFont);
            xLabel2->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter);
            xLabel2->position->setParentAnchor(line2->start);
            xLabel2->position->setCoords(0, 5);
            m_cursorXLabels2.append(xLabel2);
        }
    }

    // 2. 为每个 Graph 创建跟踪器 和 Y轴标签
    for (auto it = m_plotGraphMap->begin(); it != m_plotGraphMap->end(); ++it)
    {
        QCustomPlot *plot = it.key();
        for (QCPGraph *graph : it.value())
        {
            // --- 游标 1 Y标签 ---
            QCPItemTracer *tracer1 = new QCPItemTracer(plot);
            tracer1->setGraph(graph);
            tracer1->setInterpolating(false);
            tracer1->setVisible(true);
            tracer1->setStyle(QCPItemTracer::tsCircle);
            tracer1->setSize(3);
            tracer1->setPen(graph->pen());
            tracer1->setBrush(QBrush(graph->pen().color()));
            m_graphTracers1.insert(graph, tracer1);

            QCPItemText *yLabel1 = new QCPItemText(plot);
            yLabel1->setLayer("overlay");
            yLabel1->setClipToAxisRect(false);
            yLabel1->setPadding(QMargins(5, 2, 5, 2));
            yLabel1->setBrush(QBrush(QColor(255, 255, 255, 180)));
            yLabel1->setPen(QPen(graph->pen().color()));
            yLabel1->setColor(graph->pen().color());
            yLabel1->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            yLabel1->position->setParentAnchor(tracer1->position);
            yLabel1->position->setCoords(5, 0);
            m_cursorYLabels1.insert(tracer1, yLabel1);

            if (m_cursorMode == CursorManager::DoubleCursor)
            {
                // --- 游标 2 Y标签 ---
                QCPItemTracer *tracer2 = new QCPItemTracer(plot);
                tracer2->setGraph(graph);
                tracer2->setInterpolating(false);
                tracer2->setVisible(true);
                tracer2->setStyle(QCPItemTracer::tsCircle);
                tracer2->setSize(3);
                tracer2->setPen(graph->pen());
                tracer2->setBrush(QBrush(graph->pen().color()));
                m_graphTracers2.insert(graph, tracer2);

                QCPItemText *yLabel2 = new QCPItemText(plot);
                yLabel2->setLayer("overlay");
                yLabel2->setClipToAxisRect(false);
                yLabel2->setPadding(QMargins(5, 2, 5, 2));
                yLabel2->setBrush(QBrush(QColor(255, 255, 255, 180)));
                yLabel2->setPen(QPen(graph->pen().color()));
                yLabel2->setColor(graph->pen().color());
                yLabel2->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                yLabel2->position->setParentAnchor(tracer2->position);
                yLabel2->position->setCoords(5, 0);
                m_cursorYLabels2.insert(tracer2, yLabel2);
            }
        }
    }

    // 确保所有子图都重绘
    for (QCustomPlot *plot : *m_plotWidgets)
    {
        plot->replot();
    }
}

/**
 * @brief [槽] 核心同步逻辑：更新所有游标
 */
void CursorManager::updateCursors(double key, int cursorIndex)
{
    if (m_cursorMode == CursorManager::NoCursor)
        return;

    // (此函数依赖 MainWindow::getGlobalTimeRange()，
    //  在更深度的重构中，这个也应该被移走，但现在
    //  我们假定 key 已经是钳制过的)

    // 1. 存储 key
    QList<QCPItemLine *> *lines;
    QMap<QCPGraph *, QCPItemTracer *> *tracers;
    QList<QCPItemText *> *xLabels;
    QMap<QCPItemTracer *, QCPItemText *> *yLabels;
    double *keyToUpdate;

    if (cursorIndex == 1)
    {
        keyToUpdate = &m_cursorKey1;
        lines = &m_cursorLines1;
        tracers = &m_graphTracers1;
        xLabels = &m_cursorXLabels1;
        yLabels = &m_cursorYLabels1;
    }
    else if (cursorIndex == 2 && m_cursorMode == CursorManager::DoubleCursor)
    {
        keyToUpdate = &m_cursorKey2;
        lines = &m_cursorLines2;
        tracers = &m_graphTracers2;
        xLabels = &m_cursorXLabels2;
        yLabels = &m_cursorYLabels2;
    }
    else
    {
        return; // 无效
    }

    // 2. 检查 key 是否真的改变了
    if (qFuzzyCompare(*keyToUpdate, key))
    {
        // return; // 避免不必要的重绘
    }
    *keyToUpdate = key;

    // 3. 遍历所有 plot，更新它们的游标
    for (int i = 0; i < m_plotWidgets->size(); ++i)
    {
        QCustomPlot *plot = m_plotWidgets->at(i);
        if (i >= lines->size())
            continue; // 安全检查

        // A. 更新垂直线 (使用绝对像素坐标)
        double xPixel = plot->xAxis->coordToPixel(key);
        QCPItemLine *line = lines->at(i);
        line->start->setCoords(xPixel, plot->axisRect()->bottom());
        line->end->setCoords(xPixel, plot->axisRect()->top());

        // B. 更新 X 轴文本标签
        QCPItemText *xLabel = xLabels->at(i);
        xLabel->setText(QString::number(key, 'f', 4));

        // C. 遍历此 plot 上的所有 graph，更新 Y 轴标签
        if (m_plotGraphMap->contains(plot))
        {
            for (QCPGraph *graph : m_plotGraphMap->value(plot))
            {
                if (tracers->contains(graph))
                {
                    QCPItemTracer *tracer = tracers->value(graph);
                    tracer->setGraphKey(key);
                    tracer->updatePosition();

                    QCPItemText *yLabel = yLabels->value(tracer, nullptr);
                    if (yLabel)
                    {
                        double value = tracer->position->value();
                        yLabel->setText(QString::number(value, 'f', 3));
                        yLabel->setVisible(true);
                    }
                }
            }
        }
        plot->replot();
    }

    // 4. 发出信号，通知 MainWindow (和其他人) key 已更新
    emit cursorKeyChanged(key, cursorIndex);
}