#include "plotmanager.h"
#include "FlowLegend.h"
#include "types.h"
#include <QGridLayout>
#include <QDebug>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QTimer>
#include <QMenu>

PlotManager::PlotManager(QWidget *parentContainer, QObject *parent)
    : QObject(parent),
      m_container(parentContainer),
      m_activePlot(nullptr),
      m_yAxisGroup(nullptr),
      m_openGL(false),
      m_antialiasing(true),
      m_legendMode(0),
      m_isMaximized(false),
      m_savedActivePlotIndex(-1)
{
    // 确保容器有布局
    if (!m_container->layout())
    {
        QGridLayout *grid = new QGridLayout(m_container);
        grid->setSpacing(0);
        grid->setContentsMargins(0, 0, 0, 0);
    }
}

PlotManager::~PlotManager()
{
    clearLayout();
}

void PlotManager::clearLayout()
{
    if (m_yAxisGroup)
    {
        delete m_yAxisGroup;
        m_yAxisGroup = nullptr;
    }

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_container->layout());
    if (grid)
    {
        QLayoutItem *item;
        while ((item = grid->takeAt(0)) != nullptr)
        {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }

    m_plots.clear();
    m_activePlot = nullptr;
}

void PlotManager::setupLayout(int rows, int cols)
{
    QList<QRect> geometries;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            geometries.append(QRect(c, r, 1, 1));
    setupLayout(geometries);
}

void PlotManager::setupLayout(const QList<QRect> &geometries)
{
    // 暂存旧的 X 轴范围以保持同步
    QCPRange sharedXRange;
    bool hasSharedXRange = false;
    if (!m_plots.isEmpty() && m_plots.first()->graphCount() > 0)
    {
        sharedXRange = m_plots.first()->xAxis->range();
        hasSharedXRange = true;
    }

    clearLayout();

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_container->layout());

    for (int i = 0; i < geometries.size(); ++i)
    {
        const QRect &geo = geometries[i];

        QFrame *plotFrame = new QFrame(m_container);
        plotFrame->setFrameShape(QFrame::NoFrame);
        plotFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");

        QVBoxLayout *frameLayout = new QVBoxLayout(plotFrame);
        frameLayout->setContentsMargins(0, 0, 0, 0);

        QCustomPlot *plot = new QCustomPlot(plotFrame);
        if (i == 0)
            m_yAxisGroup = new QCPMarginGroup(plot); // 创建对齐组

        frameLayout->addWidget(plot);
        grid->addWidget(plotFrame, geo.y(), geo.x(), geo.height(), geo.width());

        m_plots.append(plot);

        // 应用通用设置
        setupPlotInteractions(plot);
        plot->axisRect()->setMarginGroup(QCP::msLeft, m_yAxisGroup);

        if (hasSharedXRange)
            plot->xAxis->setRange(sharedXRange);
    }

    if (!m_plots.isEmpty())
    {
        onPlotClicked(); // 默认激活第一个
    }

    emit layoutChanged();
    emit plotUpdated();
}

void PlotManager::setupPlotInteractions(QCustomPlot *plot)
{
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables | QCP::iSelectLegend);
    plot->setAutoAddPlottableToLegend(false);

    configurePlotLegend(plot);

    plot->setOpenGl(m_openGL);
    plot->setNotAntialiasedElements(m_antialiasing ? QCP::aeNone : QCP::aePlottables);

    // 字体设置
    QFont axisFont = plot->font();
    axisFont.setPointSize(7);
    plot->xAxis->setTickLabelFont(axisFont);
    plot->xAxis->setLabelFont(axisFont);
    plot->yAxis->setTickLabelFont(axisFont);
    plot->yAxis->setLabelFont(axisFont);

    plot->yAxis->setNumberFormat("g");
    plot->yAxis->setNumberPrecision(4);

    // 信号连接
    connect(plot, &QCustomPlot::mousePress, this, &PlotManager::onPlotClicked);
    connect(plot, &QCustomPlot::selectionChangedByUser, this, &PlotManager::onPlotSelectionChanged);
    connect(plot->xAxis, static_cast<void (QCPAxis::*)(const QCPRange &)>(&QCPAxis::rangeChanged),
            this, &PlotManager::onXAxisRangeChanged);

    // 图例交互
    if (plot->legend)
        connect(plot, &QCustomPlot::legendClick, this, &PlotManager::onLegendClick);

    // 右键菜单
    plot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(plot, &QCustomPlot::customContextMenuRequested, this, &PlotManager::onCustomContextMenu);

    // 拖放支持
    plot->setAcceptDrops(true);
    plot->installEventFilter(this);
}

void PlotManager::configurePlotLegend(QCustomPlot *plot)
{
    if (m_legendMode == -1)
    {
        plot->legend->setVisible(false);
        return;
    }

    bool outside = (m_legendMode == 0);
    bool isFlow = (dynamic_cast<FlowLegend *>(plot->legend) != nullptr);

    // 1. 确保 Legend 对象类型正确
    if ((outside && !isFlow) || (!outside && isFlow) || !plot->legend)
    {
        if (plot->legend)
        {
            if (plot->legend->layout())
                plot->legend->layout()->take(plot->legend);
            delete plot->legend;
        }
        plot->legend = outside ? new FlowLegend() : new QCPLegend();
        // 连接点击事件
        connect(plot, &QCustomPlot::legendClick, this, &PlotManager::onLegendClick);
    }

    // 2. 样式设置
    QFont font = plot->font();
    font.setPointSize(7);
    plot->legend->setFont(font);
    plot->legend->setIconSize(10, 10);
    plot->legend->setIconTextPadding(3);
    plot->legend->setBorderPen(Qt::NoPen);
    plot->legend->setBrush(Qt::NoBrush);
    plot->legend->setMargins(QMargins(2, 2, 2, 2));

    // 允许选择图例项
    plot->legend->setSelectableParts(QCPLegend::spItems);
    plot->legend->setSelectedBrush(Qt::NoBrush);
    plot->legend->setSelectedBorderPen(Qt::NoPen);
    plot->legend->setSelectedTextColor(plot->legend->textColor());
    plot->legend->setSelectedFont(plot->legend->font());

    QCPLayoutGrid *mainLayout = plot->plotLayout();

    // 先从现有布局中移除，确保状态干净
    if (plot->legend->layout())
        plot->legend->layout()->take(plot->legend);

    if (outside)
    {
        // 只有当有曲线时才添加图例行
        if (plot->graphCount() > 0)
        {
            // 如果第一行不是图例且不是 AxisRect (防止重复插入)，插入新行
            if (mainLayout->rowCount() < 1 || (mainLayout->hasElement(0, 0) && mainLayout->element(0, 0) == plot->axisRect()))
            {
                mainLayout->insertRow(0);
            }

            mainLayout->addElement(0, 0, plot->legend);
            mainLayout->setRowSpacing(0);
            mainLayout->setRowStretchFactor(0, 0.001); // 让图例行尽可能小，只占用必要空间

            plot->legend->setVisible(true);
        }
        else
        {
            // 如果没有曲线，清理布局以移除空行
            mainLayout->simplify();
            plot->legend->setVisible(false);
        }
    }
    else
    {
        mainLayout->simplify();

        QCPLayoutInset *insetLayout = plot->axisRect()->insetLayout();
        Qt::Alignment align = Qt::AlignTop | (m_legendMode == 1 ? Qt::AlignLeft : Qt::AlignRight);
        insetLayout->addElement(plot->legend, align);
        plot->legend->setVisible(true);
    }

    plot->legend->clearItems();
    for (int i = 0; i < plot->graphCount(); ++i)
        plot->graph(i)->addToLegend(plot->legend);
}

void PlotManager::onPlotClicked(QMouseEvent *event)
{
    QCustomPlot *clickedPlot = qobject_cast<QCustomPlot *>(sender());
    if (!clickedPlot && !m_plots.isEmpty())
        clickedPlot = m_plots.first();

    if (!clickedPlot)
        return;

    activatePlot(clickedPlot);

    if (event)
    {
        if (!clickedPlot->plottableAt(event->pos(), false))
        {
            clickedPlot->deselectAll();
            clickedPlot->replot();
        }
    }
}

void PlotManager::addSignal(const QString &uniqueId, const SignalLocation &loc, QCustomPlot *targetPlot, bool replot, bool autoScale, bool updateLegend)
{
    if (!targetPlot)
        targetPlot = m_activePlot;
    if (!targetPlot)
        return;

    int plotIndex = m_plots.indexOf(targetPlot);
    if (plotIndex == -1)
        return;

    // 防止重复添加
    if (m_plotSignalMap[plotIndex].contains(uniqueId))
    {
        if (getGraph(targetPlot, uniqueId))
            return;
    }

    // 传递 autoScale 给 setupGraphInstance
    setupGraphInstance(targetPlot, uniqueId, loc, autoScale);
    m_plotSignalMap[plotIndex].insert(uniqueId);

    if (updateLegend)
    {
        configurePlotLegend(targetPlot);
    }

    if (replot)
    {
        emit plotUpdated();
        targetPlot->replot();
    }
}

void PlotManager::removeSignal(const QString &uniqueId, QCustomPlot *targetPlot)
{
    if (!targetPlot)
        targetPlot = m_activePlot;
    if (!targetPlot)
        return;

    int plotIndex = m_plots.indexOf(targetPlot);
    if (plotIndex == -1)
        return;

    if (m_plotSignalMap[plotIndex].contains(uniqueId))
    {
        QCPGraph *graph = getGraph(targetPlot, uniqueId);
        if (graph)
        {
            targetPlot->removeGraph(graph);
            m_plotSignalMap[plotIndex].remove(uniqueId);

            configurePlotLegend(targetPlot);
            emit plotUpdated();

            targetPlot->replot();
        }
    }
}

void PlotManager::setupGraphInstance(QCustomPlot *plot, const QString &uniqueID, const SignalLocation &loc, bool autoScale)
{
    if (!loc.table)
        return;

    QCPGraph *graph = plot->addGraph();
    graph->setName(loc.name);
    graph->setData(loc.table->timeData, loc.table->valueData[loc.signalIndex]);
    graph->setPen(loc.pen);
    graph->setProperty("id", uniqueID);

    if (autoScale)
    {
        int totalGraphCount = 0;
        for (QCustomPlot *p : m_plots)
            totalGraphCount += p->graphCount();

        if (totalGraphCount == 1)
        {
            performFitView(true, true, FitAllPlots);
        }
        else
        {
            QList<QCustomPlot *> target;
            target << plot;
            performFitView(target, false, true);
        }
    }
}

void PlotManager::updateLegends()
{
    for (QCustomPlot *plot : m_plots)
    {
        configurePlotLegend(plot);
    }
}

QCPGraph *PlotManager::getGraph(QCustomPlot *plot, const QString &uniqueID) const
{
    for (int i = 0; i < plot->graphCount(); ++i)
    {
        if (plot->graph(i)->property("id").toString() == uniqueID)
            return plot->graph(i);
    }
    return nullptr;
}

void PlotManager::clearAllPlots()
{
    for (QCustomPlot *plot : m_plots)
    {
        plot->clearGraphs();
        configurePlotLegend(plot);
        plot->replot();
    }
    m_plotSignalMap.clear();
    emit plotUpdated();
}

void PlotManager::removeFileSignals(const QString &filenamePrefix)
{
    QString prefix = filenamePrefix + "/";
    for (int i = 0; i < m_plots.size(); ++i)
    {
        QCustomPlot *plot = m_plots[i];
        QList<QCPGraph *> toDelete;
        for (int j = 0; j < plot->graphCount(); ++j)
        {
            QCPGraph *g = plot->graph(j);
            if (g->property("id").toString().startsWith(prefix))
                toDelete.append(g);
        }

        if (!toDelete.isEmpty())
        {
            for (QCPGraph *g : toDelete)
            {
                QString id = g->property("id").toString();
                plot->removeGraph(g);
                m_plotSignalMap[i].remove(id);
            }
            configurePlotLegend(plot);
            plot->replot();
        }
    }
    emit plotUpdated();
}

void PlotManager::setOpenGL(bool enabled)
{
    m_openGL = enabled;
    for (auto p : m_plots)
    {
        p->setOpenGl(enabled);
        p->replot();
    }
}

void PlotManager::setAntialiasing(bool enabled)
{
    m_antialiasing = enabled;
    for (auto p : m_plots)
    {
        p->setNotAntialiasedElements(enabled ? QCP::aeNone : QCP::aePlottables);
        p->replot();
    }
}

void PlotManager::setLegendPosition(int mode)
{
    m_legendMode = mode;
    for (auto p : m_plots)
        configurePlotLegend(p);
    for (auto p : m_plots)
        p->replot();
}

void PlotManager::performFitView(bool fitX, bool fitY, FitTarget target)
{
    QList<QCustomPlot *> targets;
    if (target == FitActivePlot && m_activePlot)
        targets << m_activePlot;
    else if (target == FitAllPlots)
        targets = m_plots;

    performFitView(targets, fitX, fitY);
}

void PlotManager::performFitView(const QList<QCustomPlot *> &targets, bool fitX, bool fitY)
{
    if (targets.isEmpty())
        return;

    QCPRange globalX;
    bool hasX = false;

    // 1. 计算全局X (基于所有 m_plots，保持全局时间轴一致)
    if (fitX)
    {
        for (auto p : m_plots)
        {
            for (int i = 0; i < p->graphCount(); ++i)
            {
                bool found = false;
                QCPRange r = p->graph(i)->data()->keyRange(found);
                if (found)
                {
                    if (!hasX)
                    {
                        globalX = r;
                        hasX = true;
                    }
                    else
                        globalX.expand(r);
                }
            }
        }
        if (hasX)
        {
            double m = globalX.size() * 0.02;
            globalX.lower -= m;
            globalX.upper += m;
        }
        else
        {
            globalX = QCPRange(0, 10);
        }
    }

    // 2. 应用
    for (auto p : targets)
    {
        if (fitX)
        {
            QSignalBlocker b(p->xAxis);
            p->xAxis->setRange(globalX);
        }

        if (fitY)
        {
            QCPRange searchX = fitX ? globalX : p->xAxis->range();
            QCPRange foundY;
            bool hasY = false;
            for (int i = 0; i < p->graphCount(); ++i)
            {
                bool found = false;
                // 注意：这里使用了 sdBoth，确保搜索整个可见范围
                QCPRange r = p->graph(i)->getValueRange(found, QCP::sdBoth, searchX);
                if (found)
                {
                    if (!hasY)
                    {
                        foundY = r;
                        hasY = true;
                    }
                    else
                        foundY.expand(r);
                }
            }
            if (hasY)
            {
                double sz = foundY.size();
                if (sz == 0)
                    sz = 1.0;
                double m = sz * 0.05;
                if (m == 0.05 && foundY.center() == 0)
                    m = 0.5;
                foundY.lower -= m;
                foundY.upper += m;
                p->yAxis->setRange(foundY);
            }
            else
            {
                p->yAxis->setRange(0, 1);
            }
        }
        p->replot();
    }

    // 同步 X 轴 (如果进行了 X 轴适应)
    if (fitX && !m_plots.isEmpty())
        onXAxisRangeChanged(m_plots.first()->xAxis->range());

    emit viewChanged();
}

void PlotManager::onXAxisRangeChanged(const QCPRange &newRange)
{
    QObject *senderAxis = sender();
    for (QCustomPlot *plot : m_plots)
    {
        if (plot->xAxis != senderAxis)
        {
            QSignalBlocker blocker(plot->xAxis);
            plot->xAxis->setRange(newRange);
            plot->replot();
        }
    }
    emit viewChanged();
}

void PlotManager::onPlotSelectionChanged()
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot)
        return;

    // 处理图例和曲线的同步选择逻辑
    QList<QCPGraph *> selectedGraphs = plot->selectedGraphs();
    QList<QCPAbstractLegendItem *> selectedLegendItems = plot->legend->selectedItems();

    if (!selectedGraphs.isEmpty() && selectedLegendItems.isEmpty())
    {
        for (QCPGraph *graph : selectedGraphs)
            if (QCPPlottableLegendItem *item = plot->legend->itemWithPlottable(graph))
                item->setSelected(true);
    }
    else if (!selectedLegendItems.isEmpty() && selectedGraphs.isEmpty())
    {
        for (QCPAbstractLegendItem *item : selectedLegendItems)
        {
            if (QCPPlottableLegendItem *pli = qobject_cast<QCPPlottableLegendItem *>(item))
                if (QCPGraph *graph = qobject_cast<QCPGraph *>(pli->plottable()))
                    graph->setSelection(QCPDataSelection(graph->data()->dataRange()));
        }
    }

    // 发出信号通知 SignalBrowser
    if (!plot->selectedGraphs().isEmpty())
    {
        emit signalSelectionChanged(plot->selectedGraphs().first()->property("id").toString());
    }
    else
    {
        emit signalSelectionChanged(QString());
    }
}

// 处理拖放事件
bool PlotManager::eventFilter(QObject *watched, QEvent *event)
{
    QCustomPlot *targetPlot = qobject_cast<QCustomPlot *>(watched);
    if (!targetPlot || !m_plots.contains(targetPlot))
        return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::DragEnter)
    {
        QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent *>(event);
        if (dragEvent->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist"))
            dragEvent->acceptProposedAction();
        return true;
    }
    else if (event->type() == QEvent::Drop)
    {
        QDropEvent *dropEvent = static_cast<QDropEvent *>(event);
        QByteArray encoded = dropEvent->mimeData()->data("application/x-qabstractitemmodeldatalist");
        QDataStream stream(&encoded, QIODevice::ReadOnly);

        while (!stream.atEnd())
        {
            int r, c;
            QMap<int, QVariant> data;
            stream >> r >> c >> data;

            // 依赖 TreeItemRoles
            if (data.contains(UniqueIdRole) && data.value(IsSignalItemRole).toBool())
            {
                QString uniqueID = data.value(UniqueIdRole).toString();
                // 激活目标图表
                if (m_activePlot != targetPlot)
                {
                    activatePlot(targetPlot);
                }
                // 请求数据添加
                emit signalDropRequested(uniqueID, targetPlot);
            }
        }
        dropEvent->acceptProposedAction();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void PlotManager::onCustomContextMenu(const QPoint &pos)
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot)
        return;

    bool skipLegendCheck = false;
    if (m_legendMode == 0 && plot->axisRect())
    {
        if (plot->axisRect()->outerRect().contains(pos))
        {
            skipLegendCheck = true;
        }
    }

    // 1. 优先检查是否点击了图例项
    if (!skipLegendCheck && plot->legend && plot->legend->visible())
    {
        if (plot->legend->selectTest(pos, false) < plot->selectionTolerance())
        {
            // 进一步检查具体点到了哪个图例项
            for (int i = 0; i < plot->legend->itemCount(); ++i)
            {
                QCPAbstractLegendItem *item = plot->legend->item(i);
                if (item->selectTest(pos, false) < plot->selectionTolerance())
                {
                    // 找到了被点击的图例项
                    if (QCPPlottableLegendItem *pli = qobject_cast<QCPPlottableLegendItem *>(item))
                    {
                        if (pli->plottable())
                        {
                            QMenu menu;
                            QString name = pli->plottable()->name();
                            QAction *delAction = menu.addAction(tr("Delete '%1'").arg(name));

                            QString uniqueId = pli->plottable()->property("id").toString();
                            connect(delAction, &QAction::triggered, [this, uniqueId]()
                                    { QTimer::singleShot(0, this, [this, uniqueId]()
                                                         { emit removeSignalRequested(uniqueId); }); });

                            menu.exec(plot->mapToGlobal(pos));
                            return; // 找到并处理了图例项，直接返回
                        }
                    }
                }
            }
        }
    }

    // 2. 检查是否点击了曲线 (Plottable)
    QCPAbstractPlottable *plottable = plot->plottableAt(pos, false);
    QCPGraph *graph = qobject_cast<QCPGraph *>(plottable);

    if (graph)
    {
        QMenu menu;
        QAction *del = menu.addAction(tr("Delete '%1'").arg(graph->name()));
        connect(del, &QAction::triggered, [this, graph]()
                { emit removeSignalRequested(graph->property("id").toString()); });
        menu.exec(plot->mapToGlobal(pos));
    }
    else
    {
        // 3. 点击空白处：子图菜单
        QMenu menu;
        QAction *del = menu.addAction(tr("Clear Subplot"));
        connect(del, &QAction::triggered, [this, plot]()
                {
             int idx = m_plots.indexOf(plot);
             if(idx >=0) emit removeSubplotRequested(idx); });

        menu.addSeparator();

        QAction *exportAction = menu.addAction(tr("Export Plot..."));
        connect(exportAction, &QAction::triggered, [this, plot]()
                { exportPlot(plot); });

        menu.exec(plot->mapToGlobal(pos));
    }
}
void PlotManager::onLegendClick(QCPLegend *legend, QCPAbstractLegendItem *item, QMouseEvent *event)
{
    Q_UNUSED(legend);

    if (event->button() == Qt::LeftButton)
    {
        if (QCPPlottableLegendItem *pli = qobject_cast<QCPPlottableLegendItem *>(item))
        {
            if (pli->plottable())
            {
                pli->plottable()->setVisible(!pli->plottable()->visible());
                pli->plottable()->parentPlot()->replot();
            }
        }
    }
}

QSet<QString> PlotManager::getPlotSignalIDs(int plotIndex) const
{
    return m_plotSignalMap.value(plotIndex);
}

QSet<QString> PlotManager::getActivePlotSignalIDs() const
{
    int idx = m_plots.indexOf(m_activePlot);
    if (idx == -1)
        return QSet<QString>();
    return m_plotSignalMap.value(idx);
}

QList<QRect> PlotManager::captureLayoutGeometries() const
{
    QList<QRect> geometries;
    QGridLayout *grid = qobject_cast<QGridLayout *>(m_container->layout());
    if (!grid)
        return geometries;

    for (int i = 0; i < grid->count(); ++i)
    {
        int row, col, rowSpan, colSpan;
        grid->getItemPosition(i, &row, &col, &rowSpan, &colSpan);
        geometries.append(QRect(col, row, colSpan, rowSpan));
    }
    return geometries;
}

void PlotManager::toggleMaximizeActive()
{
    if (m_isMaximized)
    {
        m_plotSignalMap = m_savedPlotSignalMap;
        setupLayout(m_savedGeometries);
        m_isMaximized = false;

        if (m_savedActivePlotIndex >= 0 && m_savedActivePlotIndex < m_plots.size())
        {
            activatePlot(m_plots[m_savedActivePlotIndex]);
        }
    }
    else
    {
        if (!m_activePlot)
            return;
        int idx = m_plots.indexOf(m_activePlot);

        m_savedPlotSignalMap = m_plotSignalMap;
        m_savedGeometries = captureLayoutGeometries();
        m_savedActivePlotIndex = idx;

        QSet<QString> activeSignals = m_plotSignalMap.value(idx);
        m_plotSignalMap.clear();
        m_plotSignalMap.insert(0, activeSignals);

        setupLayout(1, 1);
        m_isMaximized = true;
    }
    emit layoutChanged();
    emit plotUpdated();
}

void PlotManager::exportAllViews(const QString &path)
{
    if (!m_container)
        return;

    QString filters = tr("PNG Image (*.png);;JPG Image (*.jpg);;BMP Image (*.bmp)");

    // 定义默认的基础导出目录
    QString defaultBaseDir = QCoreApplication::applicationDirPath() + "/export_file";
    QString exportPath = path;

    if (exportPath.isEmpty())
    {
        // 1. 交互模式 (UI 触发)
        QDir dir(defaultBaseDir);
        if (!dir.exists())
        {
            dir.mkpath(".");
        }

        exportPath = QFileDialog::getSaveFileName(nullptr, tr("Export All Views"), defaultBaseDir, filters);
    }
    else
    {
        // 2. API 模式 (脚本触发)
        QFileInfo info(exportPath);
        if (info.isRelative())
        {
            // 如果是相对路径 (例如 "view.png")，则拼接到默认目录下
            exportPath = defaultBaseDir + "/" + exportPath;
        }

        // 确保目标目录存在 (防止 API 指定了不存在的子目录)
        QFileInfo finalInfo(exportPath);
        QDir finalDir = finalInfo.absoluteDir();
        if (!finalDir.exists())
        {
            finalDir.mkpath(".");
        }
    }

    if (exportPath.isEmpty())
        return;

    if (!exportPath.contains('.'))
    {
        exportPath += ".png";
    }

    QPixmap pixmap = m_container->grab();
    bool success = pixmap.save(exportPath, nullptr, 100);

    if (!success)
    {
        QMessageBox::warning(nullptr, tr("Export Failed"), tr("Failed to save all views to %1").arg(exportPath));
    }
}

void PlotManager::exportActivePlot(const QString &path)
{
    exportPlot(m_activePlot, path);
}

void PlotManager::exportPlot(QCustomPlot *plot, const QString &path)
{
    if (!plot)
        return;

    QString filters = tr("PNG Image (*.png);;JPG Image (*.jpg);;BMP Image (*.bmp);;PDF Document (*.pdf)");

    // 定义默认的基础导出目录
    QString defaultBaseDir = QCoreApplication::applicationDirPath() + "/export_file";
    QString exportPath = path;

    if (exportPath.isEmpty())
    {
        // 1. 交互模式
        QDir dir(defaultBaseDir);
        if (!dir.exists())
        {
            dir.mkpath(".");
        }

        // 使用 nullptr 作为父对象，或者是 m_container，避免模态框问题
        exportPath = QFileDialog::getSaveFileName(m_container, tr("Export Plot"), defaultBaseDir, filters);
    }
    else
    {
        // 2. API 模式
        QFileInfo info(exportPath);
        if (info.isRelative())
        {
            // 如果是相对路径，拼接到默认 export_file 目录
            exportPath = defaultBaseDir + "/" + exportPath;
        }

        // 确保目录存在
        QFileInfo finalInfo(exportPath);
        QDir finalDir = finalInfo.absoluteDir();
        if (!finalDir.exists())
        {
            finalDir.mkpath(".");
        }
    }

    if (exportPath.isEmpty())
        return;

    double scale = 3.0; // 3 倍分辨率
    int quality = 100;  // JPG 质量 (0-100)

    bool success = false;
    if (exportPath.endsWith(".png", Qt::CaseInsensitive))
    {
        success = plot->savePng(exportPath, 0, 0, scale, -1);
    }
    else if (exportPath.endsWith(".jpg", Qt::CaseInsensitive) || exportPath.endsWith(".jpeg", Qt::CaseInsensitive))
    {
        success = plot->saveJpg(exportPath, 0, 0, scale, quality);
    }
    else if (exportPath.endsWith(".bmp", Qt::CaseInsensitive))
    {
        success = plot->saveBmp(exportPath, 0, 0, scale);
    }
    else if (exportPath.endsWith(".pdf", Qt::CaseInsensitive))
    {
        success = plot->savePdf(exportPath);
    }
    else
    {
        exportPath += ".png";
        success = plot->savePng(exportPath, 0, 0, scale, -1);
    }

    if (!success)
    {
        QMessageBox::warning(m_container, tr("Export Failed"), tr("Failed to save image to %1").arg(exportPath));
    }
}

void PlotManager::activatePlot(QCustomPlot *plot)
{
    if (!plot)
        return;

    // 1. 取消旧子图的激活状态
    if (m_activePlot && m_activePlot != plot)
    {
        m_activePlot->deselectAll();
        m_activePlot->replot();
        if (QWidget *w = m_activePlot->parentWidget())
            w->setStyleSheet("QFrame { border: 2px solid transparent; }");
    }

    // 2. 设置新子图为激活状态
    m_activePlot = plot;
    if (QWidget *w = m_activePlot->parentWidget())
        w->setStyleSheet("QFrame { border: 2px solid #0078d4; }");

    // 3. 发出信号
    emit activePlotChanged(m_activePlot);
}

int PlotManager::getActivePlotIndex() const
{
    if (!m_activePlot)
        return -1;
    return m_plots.indexOf(m_activePlot);
}

void PlotManager::setActivePlotIndex(int index)
{
    if (index >= 0 && index < m_plots.size())
    {
        activatePlot(m_plots[index]);
    }
}