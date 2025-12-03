#include "scriptapi.h"
#include "script_internal.h"
#include "mainwindow.h"
#include "plotmanager.h"
#include "signalbrowser.h"
#include "qcustomplot.h"

#include <QEventLoop>

// --- 私有辅助 ---
QCustomPlot *ScriptAPI::getTargetPlot(int view_index)
{
    if (!m_mainWin || !m_mainWin->getPlotManager())
        return nullptr;
    if (view_index < 0)
        return m_mainWin->getPlotManager()->getActivePlot();
    auto &plots = m_mainWin->getPlotManager()->getPlots();
    if (view_index >= 0 && view_index < plots.size())
        return plots[view_index];
    return nullptr;
}

// --- 视图与布局操作 ---

void ScriptAPI::import_view(std::string path)
{
    if (!m_mainWin)
        return;
    QString qPath = QString::fromStdString(path);

    QEventLoop loop;
    QObject::connect(m_mainWin, &MainWindow::viewImportFinished, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->importView(qPath); });

    loop.exec();
}

bool ScriptAPI::export_plot(std::string path)
{
    if (!m_mainWin)
        return false;
    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->exportActivePlot(QString::fromStdString(path)); });
    return true;
}

bool ScriptAPI::export_view(std::string path)
{
    if (!m_mainWin)
        return false;
    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->exportAllViews(QString::fromStdString(path)); });
    return true;
}

void ScriptAPI::set_layout(int rows, int cols)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::layoutChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->setupLayout(rows, cols); });
    loop.exec();
}

int ScriptAPI::get_view_count()
{
    if (!m_mainWin)
        return 0;
    return runOnMain(m_mainWin, [&]()
                     {
        if (m_mainWin->getPlotManager())
            return m_mainWin->getPlotManager()->getPlotCount();
        return 0; });
}

int ScriptAPI::get_active_view_index()
{
    if (!m_mainWin)
        return -1;
    return runOnMain(m_mainWin, [&]()
                     {
        if (m_mainWin->getPlotManager())
            return m_mainWin->getPlotManager()->getActivePlotIndex();
        return -1; });
}

void ScriptAPI::set_active_view(int index)
{
    if (!m_mainWin)
        return;
    runOnMainVoid(m_mainWin, [&]()
                  {
        if (m_mainWin->getPlotManager())
            m_mainWin->getPlotManager()->setActivePlotIndex(index); });
}

// --- 视图内容控制 ---

void ScriptAPI::set_x_range(double min, double max, int view_index)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot) {
            plot->xAxis->setRange(min, max);
            plot->replot();
        } });
    loop.exec();
}

void ScriptAPI::set_y_range(double min, double max, int view_index)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot) {
            plot->yAxis->setRange(min, max);
            plot->replot();
        } });
    loop.exec();
}

std::tuple<double, double> ScriptAPI::get_x_range(int view_index)
{
    if (!m_mainWin)
        return std::make_tuple(0.0, 1.0);
    return runOnMain(m_mainWin, [&]()
                     {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot)
            return std::make_tuple(plot->xAxis->range().lower, plot->xAxis->range().upper);
        return std::make_tuple(0.0, 1.0); });
}

std::tuple<double, double> ScriptAPI::get_y_range(int view_index)
{
    if (!m_mainWin)
        return std::make_tuple(0.0, 1.0);
    return runOnMain(m_mainWin, [&]()
                     {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot)
            return std::make_tuple(plot->yAxis->range().lower, plot->yAxis->range().upper);
        return std::make_tuple(0.0, 1.0); });
}

void ScriptAPI::autoscale(int view_index)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  {
        if (view_index < 0)
            m_mainWin->getPlotManager()->performFitView(true, true, PlotManager::FitActivePlot);
        else {
            QCustomPlot *plot = getTargetPlot(view_index);
            if (plot) {
                QList<QCustomPlot *> targets;
                targets << plot;
                m_mainWin->getPlotManager()->performFitView(targets, true, true);
            }
        } });
    loop.exec();
}

void ScriptAPI::fit_view_y_all()
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->performFitView(false, true, PlotManager::FitAllPlots); });
    loop.exec();
}

void ScriptAPI::fit_view_all()
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->performFitView(true, true, PlotManager::FitAllPlots); });
    loop.exec();
}

std::vector<std::string> ScriptAPI::get_view_signals(int view_index)
{
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        std::vector<std::string> result;
        if (!m_mainWin->getPlotManager()) return result;
        int idx = view_index < 0 ? m_mainWin->getPlotManager()->getActivePlotIndex() : view_index;
        QSet<QString> ids = m_mainWin->getPlotManager()->getPlotSignalIDs(idx);
        for (const QString &s : ids)
            result.push_back(s.toStdString());
        return result; });
}

bool ScriptAPI::add_signal(std::string id, int view_index)
{
    if (!m_mainWin)
        return false;
    return runOnMain(m_mainWin, [&]()
                     {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (!plot) return false;
        QString qid = QString::fromStdString(id);
        SignalLocation loc = m_mainWin->getSignalDataFromID(qid);
        if (loc.table)
        {
            m_mainWin->getPlotManager()->addSignal(qid, loc, plot);
            if (plot == m_mainWin->getPlotManager()->getActivePlot())
                m_mainWin->m_signalBrowser->setSignalChecked(qid, true, true);
            return true;
        }
        return false; });
}

bool ScriptAPI::remove_signal(std::string id, int view_index)
{
    if (!m_mainWin)
        return false;
    return runOnMain(m_mainWin, [&]()
                     {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (!plot) return false;
        QString qid = QString::fromStdString(id);
        m_mainWin->getPlotManager()->removeSignal(qid, plot);
        if (plot == m_mainWin->getPlotManager()->getActivePlot())
            m_mainWin->m_signalBrowser->setSignalChecked(qid, false, true);
        return true; });
}

bool ScriptAPI::export_view_json(std::string path)
{
    if (!m_mainWin)
        return false;
    return runOnMain(m_mainWin, [&]()
                     { return m_mainWin->exportViewToJson(QString::fromStdString(path)); });
}

bool ScriptAPI::import_view_json(std::string path)
{
    if (!m_mainWin)
        return false;
    return runOnMain(m_mainWin, [&]()
                     { return m_mainWin->importViewFromJson(QString::fromStdString(path)); });
}