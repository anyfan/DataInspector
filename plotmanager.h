#ifndef PLOTMANAGER_H
#define PLOTMANAGER_H

#include <QWidget>
#include <QList>
#include <QSet>
#include <QMap>
#include "qcustomplot.h"
#include "datamanager.h" // for SignalLocation

class CursorManager;
class QGridLayout;

class PlotManager : public QObject
{
    Q_OBJECT

public:
    explicit PlotManager(QWidget *parentContainer, QObject *parent = nullptr);
    ~PlotManager();

    // --- 布局管理 ---
    void setupLayout(int rows, int cols);
    void setupLayout(const QList<QRect> &geometries);
    QList<QRect> captureLayoutGeometries() const;
    void clearLayout(); // 清除所有图表

    QList<QCustomPlot *> &getPlots() { return m_plots; }
    QCustomPlot *getActivePlot() const { return m_activePlot; }
    QWidget *getContainer() const { return m_container; }

    QCPGraph *getGraph(QCustomPlot *plot, const QString &uniqueID) const;

    // --- 信号操作 ---
    void addSignal(const QString &uniqueId, const SignalLocation &loc, QCustomPlot *targetPlot = nullptr, bool replot = true);
    void removeSignal(const QString &uniqueId, QCustomPlot *targetPlot = nullptr);
    void clearAllPlots();
    void removeFileSignals(const QString &filenamePrefix);

    // 查询某图表包含哪些信号ID
    QSet<QString> getPlotSignalIDs(int plotIndex) const;
    QSet<QString> getActivePlotSignalIDs() const;

    // --- 视图操作 ---
    enum FitTarget
    {
        FitActivePlot,
        FitAllPlots
    };
    void performFitView(bool fitX, bool fitY, FitTarget target);
    void setOpenGL(bool enabled);
    void setAntialiasing(bool enabled);
    void setLegendPosition(int mode); // 0: OutsideTop, 1: InsideTL, 2: InsideTR, -1: None

    // --- 导出 ---
    void exportActivePlot(const QString &path);
    void exportPlot(QCustomPlot *plot, const QString &path = QString());
    void exportAllViews(const QString &path);

    // 最大化/还原
    void toggleMaximizeActive();
    bool isMaximized() const { return m_isMaximized; }

signals:
    void activePlotChanged(QCustomPlot *plot);
    void plotUpdated();

    // 视图范围变化
    void viewChanged();

    // 当布局发生结构性变化（图表被重建）时发出
    void layoutChanged();

    // 当用户把信号拖入图表时发出，请求 MainWindow 提供数据
    void signalDropRequested(const QString &uniqueId, QCustomPlot *targetPlot);
    // 当图表上的信号被选中时
    void signalSelectionChanged(const QString &selectedId);
    // 请求删除子图
    void removeSubplotRequested(int index);
    // 请求删除信号
    void removeSignalRequested(const QString &uniqueId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onPlotClicked();
    void onPlotSelectionChanged();
    void onXAxisRangeChanged(const QCPRange &newRange);
    void onLegendClick(QCPLegend *legend, QCPAbstractLegendItem *item, QMouseEvent *event);
    void onCustomContextMenu(const QPoint &pos);

private:
    void createPlot(int index, const QRect &geometry);
    void setupPlotInteractions(QCustomPlot *plot);
    void configurePlotLegend(QCustomPlot *plot);
    void setupGraphInstance(QCustomPlot *plot, const QString &uniqueID, const SignalLocation &loc);
    void activatePlot(QCustomPlot *plot);

    QWidget *m_container;
    QList<QCustomPlot *> m_plots;
    QCustomPlot *m_activePlot;
    QCPMarginGroup *m_yAxisGroup;

    // 存储每个图表上的信号ID集合: PlotIndex -> Set<UniqueID>
    QMap<int, QSet<QString>> m_plotSignalMap;

    // 状态缓存
    bool m_openGL;
    bool m_antialiasing;
    int m_legendMode;

    // 最大化相关
    bool m_isMaximized;
    QMap<int, QSet<QString>> m_savedPlotSignalMap;
    QList<QRect> m_savedGeometries;
    int m_savedActivePlotIndex;
};

#endif // PLOTMANAGER_H