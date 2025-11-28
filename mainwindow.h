#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QMap>
#include <QSet>
#include <QDomDocument>

#include "datamanager.h"
#include "plotmanager.h"

// Forward Declarations
class QStandardItem;
class QDockWidget;
class QProgressDialog;
class QSpinBox;
class QThread;
class QActionGroup;

class ScriptWindow;
class ScriptAPI;
class ReplayManager;
class CursorManager;
class SignalBrowser;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 供 ScriptAPI 使用
    PlotManager *getPlotManager() const { return m_plotManager; }
    DataManager *getDataManager() const { return m_dataManager; }

    // 供 ScriptAPI 查找数据
    SignalLocation getSignalDataFromID(const QString &uniqueID) const;
    void removeFile(const QString &filename);

    friend class ScriptAPI;

signals:
    void requestLoadCsv(const QString &filePath);
    void requestLoadMat(const QString &filePath);
    void dataProcessingFinished(const QString &filePath);
    void viewImportFinished();
    void plotUpdated(); // Forwarded from PlotManager

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    // --- 菜单动作 ---
    void on_actionLoadFile_triggered();
    void on_actionImportView_triggered();
    void on_actionExportAll_triggered();
    void on_actionClearAllPlots_triggered();

    // 布局与视图
    void onLayoutActionTriggered();
    void on_actionLayoutCustom_triggered();
    void on_actionMaximize_triggered();
    void on_actionFullScreen_triggered();

    // 设置
    void onOpenGLActionToggled(bool checked);
    void onAntialiasingActionToggled(bool checked);
    void onLegendPositionChanged(QAction *action);
    void on_actionSetDefaultPenWidth_triggered();

    // 自适应
    void on_actionFitView_triggered();
    void on_actionFitViewTime_triggered();
    void on_actionFitViewY_triggered();
    void on_actionFitViewYAll_triggered();

    // --- 数据回调 ---
    void onDataLoadFinished(const FileData &data);
    void onDataLoadFailed(const QString &filePath, const QString &errorString);
    void showLoadProgress(int percentage);

    // --- 交互回调 ---
    void onSignalCheckStateChanged(const QString &uniqueId, bool checked);
    void onSignalPenChanged(const QString &uniqueId, const QPen &newPen);
    void onFileRemoveRequested(const QString &filename);

    // PlotManager 回调
    void onActivePlotChanged(QCustomPlot *plot);
    void onPlotManagerUpdated(); // 布局变动后，需刷新游标等
    void onLayoutChanged();
    void onSignalDropRequested(const QString &uniqueId, QCustomPlot *plot);
    void onSignalSelectionChanged(const QString &id);
    void onRemoveSubplotRequested(int index);
    void onRemoveSignalRequested(const QString &id);

    // 游标与工具
    void onReplayActionToggled(bool checked);
    void onCursorMainButtonToggled(bool checked);
    void onCursorMenuActionTriggered(QAction *action);
    void on_actionScriptConsole_triggered();

private:
    void setupDataManagerThread();
    void createActions();
    void createMenus();
    void createToolBars();
    void createDocks();

    void loadFile(const QString &filePath);
    void importView(const QString &filePath);

    // 辅助
    QCPRange getGlobalTimeRange() const;
    double getSmallestTimeStep() const;
    void updateReplayManagerRange();

    // XML 解析
    struct LayoutInfo
    {
        int rows;
        int cols;
        QString layoutType;
    };
    struct SignalInfo
    {
        QString name;
        int id;
        QColor color;
        QList<int> plotIds;
    };
    LayoutInfo parseViewMetaData(const QDomDocument &doc);
    QList<SignalInfo> parseCheckedSignals(const QDomDocument &doc);
    void applyImportedView(const LayoutInfo &layout, const QList<SignalInfo> &signalList);

    // --- 成员变量 ---
    QThread *m_dataThread;
    DataManager *m_dataManager;
    PlotManager *m_plotManager; // 核心管理类
    CursorManager *m_cursorManager;
    ReplayManager *m_replayManager;
    SignalBrowser *m_signalBrowser;

    QWidget *m_plotContainer;
    QDockWidget *m_signalDock;
    QProgressDialog *m_progressDialog;
    QToolBar *m_viewToolBar;

    QDialog *m_customLayoutDialog;
    QSpinBox *m_customRowsSpinBox;
    QSpinBox *m_customColsSpinBox;

    // 数据缓存
    QMap<QString, FileData> m_fileDataMap;

    // Actions
    QAction *m_loadFileAction;
    QAction *m_importViewAction;
    QAction *m_exportAllAction;
    QAction *m_clearAllPlotsAction;
    QAction *m_layout1x1Action;
    QAction *m_layout1x2Action;
    QAction *m_layout2x1Action;
    QAction *m_layout2x2Action;
    QAction *m_layoutSplitBottomAction;
    QAction *m_layoutSplitLeftAction;
    QAction *m_layoutSplitTopAction;
    QAction *m_layoutSplitRightAction;
    QAction *m_layoutCustomAction;

    QAction *m_fitViewAction;
    QAction *m_fitViewTimeAction;
    QAction *m_fitViewYAction;
    QAction *m_fitViewYAllAction;

    QAction *m_openGLAction;
    QAction *m_antialiasingAction;
    QActionGroup *m_legendPosGroup;
    QAction *m_legendPosNoneAction;
    QAction *m_legendPosOutsideTopAction;
    QAction *m_legendPosInsideTLAction;
    QAction *m_legendPosInsideTRAction;
    
    QAction *m_setDefaultPenWidthAction;

    QAction *m_maximizeAction;
    QAction *m_fullScreenAction;

    QToolButton *m_cursorMainBtn;
    QToolButton *m_cursorArrowBtn;
    QAction *m_cursorActionSingle;
    QAction *m_cursorActionDouble;
    QActionGroup *m_cursorMenuGroup;
    int m_currentCursorMode;

    QAction *m_replayAction;
    QAction *m_scriptConsoleAction;

    ScriptAPI *m_scriptAPI;
    ScriptWindow *m_scriptWindow;
};

#endif // MAINWINDOW_H