#pragma once
#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QVariantList>
#include <QVector>
#include <memory>

#include "loadedtable.h"
#include "exporttable.h"
#include "render/plotseriesstore.h"
#include "signalmodel.h"
#include "sessiondocument.h"
#include <optional>

class DataExportWorker;
class DataLoadWorker;
class PlotItem;
class TrajectoryItem;
class QThread;

// GUI-thread session controller: owns the signal model, the shared series
// store, the loader/exporter worker threads and coordinates every subplot.
// Implementation is split by responsibility:
//   appcontroller.cpp          lifetime, status and progress plumbing
//   appcontroller_loading.cpp  import queue, file removal, Excel/MAT export
//   appcontroller_plots.cpp    subplot bindings, legend actions, fitting

// Qt 6.8 QML registration instantiates a QQmlElement<T> wrapper, even for
// uncreatable types, so this QObject type must not be final.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList presetColors READ presetColors CONSTANT)
    Q_PROPERTY(SignalModel *signalModel READ signalModel CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool restoringSession READ restoringSession NOTIFY restoringSessionChanged)
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY sessionPathChanged)
    Q_PROPERTY(bool sessionModified READ sessionModified NOTIFY sessionModifiedChanged)
    Q_PROPERTY(int loadingProgress READ loadingProgress NOTIFY loadingProgressChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(bool imageExporting READ imageExporting NOTIFY exportingChanged)
    Q_PROPERTY(int exportProgress READ exportProgress NOTIFY exportProgressChanged)
    Q_PROPERTY(bool canUndoView READ canUndoView NOTIFY viewHistoryChanged)
    Q_PROPERTY(bool matExportSupported READ matExportSupported CONSTANT)
    Q_PROPERTY(int loadedFileCount READ loadedFileCount NOTIFY currentFileChanged)
    Q_PROPERTY(int signalCount READ signalCount NOTIFY currentFileChanged)
    Q_PROPERTY(int plotRows READ plotRows NOTIFY layoutChanged)
    Q_PROPERTY(int plotColumns READ plotColumns NOTIFY layoutChanged)
    Q_PROPERTY(int activePlotIndex READ activePlotIndex NOTIFY activePlotChanged)
    Q_PROPERTY(int plotStateRevision READ plotStateRevision NOTIFY plotBindingsChanged)
    // >= 0 while a single subplot is shown maximized; -1 while tiled.
    Q_PROPERTY(int soloPlotIndex READ soloPlotIndex WRITE setSoloPlot NOTIFY soloPlotChanged)
public:
    enum ExportScope { AllLoadedData = 0, PlottedSignals = 1 };
    Q_ENUM(ExportScope)
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;
    static const QVector<QColor> &signalPalette();
    QVariantList presetColors() const;

    // Properties
    SignalModel *signalModel() const { return m_signals; }
    QString status() const { return m_status; }
    QString currentFile() const { return m_currentFile; }
    bool loading() const { return m_loading; }
    bool restoringSession() const { return m_restoringSession; }
    QString sessionPath() const { return m_sessionPath; }
    bool sessionModified() const { return m_sessionModified; }
    void markSessionModified();
    int loadingProgress() const { return m_loadingProgress; }
    bool exporting() const { return m_exporting; }
    bool imageExporting() const { return m_imageExporting; }
    int exportProgress() const { return m_exportProgress; }
    bool matExportSupported() const;
    int loadedFileCount() const { return m_loadedFileNames.size(); }
    int signalCount() const { return m_signals->sourceCount(); }
    int plotRows() const { return m_plotRows; }
    int plotColumns() const { return m_plotColumns; }
    int activePlotIndex() const { return m_signals->activePlot(); }
    int plotStateRevision() const { return m_plotStateRevision; }
    int soloPlotIndex() const { return m_soloPlotIndex; }

    Q_INVOKABLE bool saveSession(const QVariant &filePath);
    Q_INVOKABLE bool restoreSession(const QVariant &filePath);
    Q_INVOKABLE QVariantList missingSessionFiles(const QVariant &filePath) const;
    Q_INVOKABLE bool restoreSessionWithFiles(const QVariant &filePath, const QVariantMap &replacements);

    // Import / export
    Q_INVOKABLE bool loadCsv(const QString &filePath);
    Q_INVOKABLE int loadFiles(const QVariant &filePaths);
    // Opens files passed on the command line (Explorer drop onto the executable,
    // "Open with", file association). A session argument wins over data files.
    Q_INVOKABLE int openStartupFiles(const QStringList &arguments);
    Q_INVOKABLE bool removeFile(const QString &fileName);
    Q_INVOKABLE bool exportXlsx(const QVariant &filePath, int scope,
                                bool zipCompressionEnabled = false);
    // Writes pN / pN_title variables in the same layout the MAT loader reads.
    Q_INVOKABLE bool exportMat(const QVariant &filePath, int scope);
    Q_INVOKABLE bool exportPlotImage(QObject *item, const QVariant &filePath, int scale = 2);
    Q_INVOKABLE void cancelExport();

    // Signal selection and styling
    Q_INVOKABLE void selectSignal(int row);
    Q_INVOKABLE void addSignalToPlot(int plotIndex, int row);
    Q_INVOKABLE void toggleSignal(int row);
    Q_INVOKABLE void filterSignals(const QString &text);
    Q_INVOKABLE bool plotSignalEnabled(int plotIndex, int row) const;
    Q_INVOKABLE QVariantList plotSignalRows(int plotIndex) const;
    Q_INVOKABLE QColor signalColor(int row) const { return m_signals->signalColor(row); }
    Q_INVOKABLE double signalWidth(int row) const { return m_signals->signalWidth(row); }
    Q_INVOKABLE int signalStyle(int row) const { return static_cast<int>(m_signals->signalStyle(row)); }
    Q_INVOKABLE void setSignalPen(int row, const QColor &color,
                                  double width, int style);
    Q_INVOKABLE QString signalName(int row) const;
    // Changes the display name used by the tree, legends and exports.
    Q_INVOKABLE bool renameSignal(int row, const QString &name);

    Q_INVOKABLE QString originalSignalName(int row) const { return m_signals->originalNameAt(row); }
    Q_INVOKABLE bool resetSignalName(int row);
    Q_INVOKABLE QVariant timeOffsetForScope(int scope, int row, const QString &group) const;
    Q_INVOKABLE bool setTimeOffset(int scope, int row, const QString &group, double seconds);
    Q_INVOKABLE double signalTimeOffset(int row) const;
    // scope: 0 signal, 1 table group, 2 file.
    Q_INVOKABLE bool addTimeOffset(int scope, int row, const QString &group, double seconds);

    // Spatial trajectories keep their axes/camera independent of the shared time axis.
    Q_INVOKABLE bool addTrajectory(int index);
    Q_INVOKABLE bool removeTrajectory(int index, int track);
    Q_INVOKABLE bool selectTrajectory(int index, int track);
    Q_INVOKABLE bool styleTrajectory(int index, const QString &name, const QColor &color, double width, bool visible);
    Q_INVOKABLE bool configureAttitude(int index, int mode, int a, int b, int c, int d,
        bool radians, int order, bool scalarLast, bool navigationToBody);
    Q_INVOKABLE QVariantMap trajectoryState(int index) const;
    Q_INVOKABLE QVariantList trajectorySignalOptions(int index) const;
    Q_INVOKABLE bool configureTrajectory(int index, bool enabled, int x, int y, int z, bool geographic = false);
    Q_INVOKABLE void enterTrajectoryMode(int index);
    Q_INVOKABLE bool bindTrajectoryAxis(int index, int axis, int row);
    Q_INVOKABLE void attachTrajectory(QObject *item, int index);
    Q_INVOKABLE void detachTrajectory(QObject *item, int index);

    // Subplots
    Q_INVOKABLE void attachPlot(QObject *plot, int index = 0);
    Q_INVOKABLE void detachPlot(QObject *plot, int index = 0);
    Q_INVOKABLE void setLayout(int rows, int columns);
    Q_INVOKABLE void setActivePlot(int index);
    // Restricts X fitting to one maximized subplot; pass -1 to fit the union again.
    Q_INVOKABLE void setSoloPlot(int index);
    Q_INVOKABLE void fitAllPlots();
    Q_INVOKABLE void setCursorMode(int mode);
    bool canUndoView() const { return !m_viewHistory.isEmpty(); }
    Q_INVOKABLE void beginViewChange();
    Q_INVOKABLE void endViewChange();
    Q_INVOKABLE void undoView();
    Q_INVOKABLE void fitPlots(bool fitX, bool fitY, bool allPlots = false);
    Q_INVOKABLE void revealLegendSignal(int plotIndex, int row);
    Q_INVOKABLE void moveLegendSignal(int fromPlot, int toPlot, int row);
    Q_INVOKABLE void removeLegendSignal(int plotIndex, int row);
    Q_INVOKABLE void clearPlotSignals(int plotIndex);
    Q_INVOKABLE void clearAllPlotSignals();
    Q_INVOKABLE void fitPlotY(int plotIndex);

    // Drops every loaded file and signal. Use clearAllPlotSignals() to keep data.
    Q_INVOKABLE void clear();
signals:
    void viewHistoryChanged();
    void revealSignalRequested(int row);
    void restoringSessionChanged();
    void sessionPathChanged();
    void sessionModifiedChanged();
    void sessionError(const QString &message);
    void sessionRestored(int cursorMode);
    void sessionRestoreFinished(bool success, const QString &message);
    void statusChanged();
    void currentFileChanged();
    void loadingChanged();
    void loadingProgressChanged();
    void exportingChanged();
    void exportProgressChanged();
    void imageExportFinished(bool success, const QString &message);
    void layoutChanged();
    void plotBindingsChanged();
    void activePlotChanged();
    void soloPlotChanged();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void recordViewChange();
    void clearViewHistory();
    struct ViewRange {
        QPointer<PlotItem> plot;
        double minimum, maximum;
        bool normalized;
    };
    struct ViewState {
        double xMinimum, xMaximum;
        QVector<ViewRange> ranges;
        QHash<int, TrajectoryCamera> cameras;
    };
    QVector<ViewState> m_viewHistory;
    int m_viewChangeDepth = 0;
    bool m_viewChangeRecorded = false;
    bool m_restoringView = false;
    bool sessionInteractionBlocked() const { return m_restoringSession && !m_applyingSession; }
    void finishSessionRestore();
    void completeSessionRestore(bool success, const QString &message);
    SessionPlot capturePlotView(PlotItem *plot) const;
    void applyPlotView(PlotItem *plot, const SessionPlot &view);
    void cachePlotView(PlotItem *plot);
    void onLoadFinished(const QString &path,
                        const QVector<LoadedTable> &tables,
                        int skipped, const QString &error);
    void onLoadProgress(const QString &path, int percentage);
    void startNextLoad();
    void appendLoadedTables(const QString &path, const QVector<LoadedTable> &tables);
    void setLoadingProgress(int progress);
    void setExportProgress(int progress);
    // Collects the export tables for a scope; false when nothing applies.
    bool collectExportTables(int scope, QVector<DataExportTable> *tables);
    void beginExport(const QString &kind);
    void setStatus(const QString &status);
    void updateCurrentFileLabel();
    void notifyPlotBindingsChanged();
    void syncSignalSelection(bool refresh = false);
    bool setTrajectorySignal(int index, int row, bool selected);
    void refreshPlot(int index, bool fitY = true);
    void refreshTrajectory(int index);
    void syncTrajectoryCursors();
    void remapTrajectoryAxes(const QVector<int> &removed);
    bool unbindPlotSignals(int plotIndex);
    PlotItem *plotAt(int index) const;
    QSet<PlotSeriesId> timeOffsetRows(int scope, int row, const QString &group) const;
    bool applyTimeOffset(int scope, int row, const QString &group, double seconds, bool absolute);
    // Fit scope helpers: which subplots a fit request applies to, and the
    // signal rows whose time bounds define the fitted X range.
    bool fitScopeIncludes(int plotIndex, bool allPlots) const;
    QVector<PlotSeriesId> fitSourceRows(bool allPlots) const;
    void applySharedXRange(double xMinimum, double xMaximum, PlotItem *except = nullptr);
    static void syncCursorsFrom(PlotItem *source, PlotItem *target);

    QString m_sessionPath, m_pendingSessionPath;
    bool m_sessionModified = false;
    bool m_sessionRelocated = false;
    bool m_imageExporting = false, m_imageExportCancelled = false;
    std::optional<SessionDocument> m_pendingSession;
    QStringList m_sessionSourcePaths;
    QVector<QVector<LoadedTable>> m_stagedSessionTables;
    bool m_restoringSession = false, m_applyingSession = false;
    QHash<int, SessionPlot> m_plotViews;
    SessionCursor m_sessionCursor;
    bool m_haveCursorState = false;
    SignalModel *m_signals;
    QVector<QPointer<PlotItem>> m_plots;
    QVector<QPointer<TrajectoryItem>> m_trajectoryPlots;
    QHash<int, SessionTrajectory> m_trajectories;
    int m_plotRows = 1;
    int m_plotColumns = 1;
    QString m_status = QStringLiteral("打开 CSV、TXT、Excel 或 MAT 文件开始查看");
    QString m_currentFile;
    QStringList m_loadedFileNames;
    QSet<QString> m_loadedPaths;
    QHash<QString, QString> m_sourcePathsByGroup; // unique UI file key -> canonical path
    QSet<QString> m_pendingPaths;
    QQueue<QString> m_loadQueue;
    QString m_activeLoadPath;
    std::shared_ptr<PlotSeriesStore> m_seriesStore;
    QVector<QColor> m_signalColors;
    int m_nextColorIndex = 0;
    QThread *m_loadThread = nullptr;
    DataLoadWorker *m_loader = nullptr;
    QThread *m_exportThread = nullptr;
    DataExportWorker *m_exporter = nullptr;
    bool m_loading = false;
    int m_loadingProgress = 0;
    bool m_exporting = false;
    int m_exportProgress = 0;
    QString m_exportKind;
    int m_batchTotal = 0;
    int m_batchCompleted = 0;
    int m_batchErrors = 0;
    int m_batchSignals = 0;
    qint64 m_batchRows = 0;
    int m_batchSkipped = 0;
    QString m_batchFirstError;
    bool m_syncingRanges = false;
    bool m_initialSignalFitDone = false;
    bool m_syncingCursors = false;
    double m_sharedXMinimum = 0.0;
    double m_sharedXMaximum = 1.0;
    int m_plotStateRevision = 0;
    int m_soloPlotIndex = -1;
};
