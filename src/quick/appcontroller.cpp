// AppController: session lifetime, worker threads, status/progress plumbing.
// Loading/export live in appcontroller_loading.cpp; subplot coordination in
// appcontroller_plots.cpp.
#include "appcontroller.h"
#include "plotitem.h"
#include "dataloadworker.h"
#include "dataexportworker.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QQuickWindow>
#include <QThread>

const QVector<QColor> &AppController::signalPalette()
{
    // Keep the legacy SignalBrowser / SignalPropertiesDialog palette and order.
    static const QVector<QColor> colors{
        QColor("#0072bd"), QColor("#d95319"), QColor("#edb120"),
        QColor("#7e2f8e"), QColor("#77ac30"), QColor("#4dbeee"),
        QColor("#a2142f"), QColor("#139fff"), QColor("#ff6929"),
        QColor("#b746ff"), QColor("#64d413"), QColor("#ff13a6"),
        QColor("#fe330a"), QColor("#22b573")};
    return colors;
}

QVariantList AppController::presetColors() const
{
    QVariantList colors;
    for (const auto &color : signalPalette()) colors.append(color);
    return colors;
}

AppController::AppController(QObject *parent)
    : QObject(parent), m_signals(new SignalModel(this)),
      m_seriesStore(std::make_shared<PlotSeriesStore>())
{
    connect(m_signals, &QAbstractItemModel::dataChanged, this,
        [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
            if (roles.isEmpty() || roles.contains(SignalModel::NameRole) || roles.contains(SignalModel::ColorRole)
                || roles.contains(SignalModel::WidthRole) || roles.contains(SignalModel::LineStyleRole)
                || roles.contains(SignalModel::CheckedRole)) markSessionModified();
        });
    connect(this, &AppController::currentFileChanged, this, &AppController::markSessionModified);
    connect(this, &AppController::layoutChanged, this, &AppController::markSessionModified);
    connect(this, &AppController::activePlotChanged, this, &AppController::markSessionModified);
    connect(this, &AppController::soloPlotChanged, this, &AppController::markSessionModified);
    connect(this, &AppController::plotBindingsChanged, this, &AppController::markSessionModified);
    QCoreApplication::instance()->installEventFilter(this);
    m_signals->setPlotCount(1);
    qRegisterMetaType<LoadedTable>();
    qRegisterMetaType<QVector<LoadedTable>>();
    m_loadThread = new QThread(this);
    auto *worker = new DataLoadWorker;
    m_loader = worker; worker->moveToThread(m_loadThread);
    connect(worker, &DataLoadWorker::progress, this, &AppController::onLoadProgress,
            Qt::QueuedConnection);
    connect(worker, &DataLoadWorker::finished, this, &AppController::onLoadFinished, Qt::QueuedConnection);
    connect(m_loadThread, &QThread::finished, worker, &QObject::deleteLater);
    m_loadThread->start();

    m_exportThread = new QThread(this);
    auto *exporter = new DataExportWorker;
    m_exporter = exporter;
    exporter->moveToThread(m_exportThread);
    connect(exporter, &DataExportWorker::progress, this,
            [this](int percentage) { setExportProgress(percentage); },
            Qt::QueuedConnection);
    connect(exporter, &DataExportWorker::finished, this,
            [this](const QString &path, const QString &error, bool cancelled) {
                const bool imageExport = m_imageExporting;
                m_imageExporting = false;
                m_exporting = false;
                emit exportingChanged();
                if (cancelled) {
                    setStatus(QStringLiteral("已取消导出"));
                } else if (!error.isEmpty()) {
                    setStatus(QStringLiteral("导出失败：%1").arg(error));
                } else {
                    setExportProgress(100);
                    setStatus(QStringLiteral("已导出 %1：%2")
                                  .arg(m_exportKind, QFileInfo(path).fileName()));
                }
                if (imageExport) {
                    emit imageExportFinished(!cancelled && error.isEmpty(), status());
                }
            }, Qt::QueuedConnection);
    connect(m_exportThread, &QThread::finished, exporter,
            &QObject::deleteLater);
    m_exportThread->start();
}

AppController::~AppController()
{
    cancelObjectEvaluation();
    if (m_loadThread) { m_loadThread->requestInterruption(); m_loadThread->quit(); m_loadThread->wait(); }
    if (m_exporter) m_exporter->requestCancel();
    if (m_exportThread) { m_exportThread->quit(); m_exportThread->wait(); }
}

void AppController::markSessionModified()
{
    if (m_restoringSession || m_applyingSession || m_sessionModified
        || (loadedFileCount() == 0 && m_sessionPath.isEmpty() && m_objects.isEmpty())) return;
    m_sessionModified = true;
    emit sessionModifiedChanged();
}

bool AppController::eventFilter(QObject *watched, QEvent *event)
{
    // Clear before dispatch, so a legend click can select its entry afterwards.
    // Observing the window also covers axes, other subplots, and toolbar controls
    // without intercepting their normal pan, cursor, or button interactions.
    if (qobject_cast<QQuickWindow *>(watched)
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick
            || event->type() == QEvent::TouchBegin)) {
        for (const auto &plot : m_plots)
            if (plot) plot->setHighlightedSeries(-1);
    }
    return QObject::eventFilter(watched, event);
}

void AppController::clear()
{
    if (m_loading || m_exporting) {
        setStatus(m_loading ? QStringLiteral("文件正在加载，完成后再清空")
                            : QStringLiteral("数据正在导出，完成后再清空"));
        return;
    }
    clearViewHistory();
    cancelObjectEvaluation(); m_objects.clear(); m_objectErrors.clear(); m_objectEvaluationStates.clear(); emit objectsChanged();
    m_seriesStore->clear();
    for (auto &state : m_trajectories) {
        auto tracks = state.entries();
        for (auto &track : tracks) { track.axes.fill(-1); track.attitude.sources.fill(-1); }
        state.tracks = tracks; state.signalIds.clear();
    }
    for (int i = 0; i < m_trajectoryPlots.size(); ++i) refreshTrajectory(i);
    m_initialSignalFitDone = false;
    m_signalColors.clear();
    m_nextColorIndex = 0;
    m_signals->setNames({});
    m_loadedPaths.clear();
    m_sourcePathsByGroup.clear();
    m_loadedFileNames.clear();
    updateCurrentFileLabel();
    for (const QPointer<PlotItem> &plot : std::as_const(m_plots))
        if (plot) plot->setVisibleSeries({});
    notifyPlotBindingsChanged();
    setLoadingProgress(0);
    setStatus(QStringLiteral("已清空"));
}

void AppController::setStatus(const QString &status)
{
    if (m_status == status) return;
    m_status = status;
    emit statusChanged();
}

void AppController::setLoadingProgress(int progress)
{
    const int normalized = qBound(0, progress, 100);
    if (m_loading && normalized < m_loadingProgress) return;
    if (m_loadingProgress == normalized) return;
    m_loadingProgress = normalized;
    emit loadingProgressChanged();
}

void AppController::setExportProgress(int progress)
{
    const int normalized = qBound(0, progress, 100);
    if (m_exporting && normalized < m_exportProgress) return;
    if (m_exportProgress == normalized) return;
    m_exportProgress = normalized;
    emit exportProgressChanged();
}

void AppController::updateCurrentFileLabel()
{
    m_currentFile = m_loadedFileNames.isEmpty()
        ? QString()
        : m_loadedFileNames.size() == 1
            ? m_loadedFileNames.first()
            : QStringLiteral("已加载 %1 个文件").arg(m_loadedFileNames.size());
    emit currentFileChanged();
}

void AppController::notifyPlotBindingsChanged()
{
    syncSignalSelection();
    ++m_plotStateRevision;
    emit plotBindingsChanged();
}
