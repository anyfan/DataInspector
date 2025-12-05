#pragma once

#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QLineEdit>
#include <QThread>
#include <pybind11/embed.h>

// 前向声明
class MainWindow;
class ScriptAPI;
class ScriptWindow;

namespace py = pybind11;

// --- 工作线程类 ---
class ScriptWorker : public QObject
{
    Q_OBJECT
public:
    // Worker 现在持有 MainWindow 和 ScriptWindow 的指针，以便在运行时创建 API
    explicit ScriptWorker(MainWindow *mw, ScriptWindow *sw);

public slots:
    // 改为接收文件路径
    void runScriptFile(const QString &filePath);

signals:
    void finished();
    void errorOccurred(const QString &msg);

private:
    MainWindow *m_mainWin;
    ScriptWindow *m_scriptWin;
};

// --- 主窗口类 ---
class ScriptWindow : public QMainWindow
{
    Q_OBJECT
public:
    // 构造函数不再接收 ScriptAPI，而是接收 MainWindow
    explicit ScriptWindow(MainWindow *mainWin, QWidget *parent = nullptr);
    ~ScriptWindow();

public slots:
    void onRunClicked();
    // 线程安全的日志追加
    void appendLog(const QString &msg);

    void onBrowseClicked();
    void onClearLogClicked();

    // 脚本运行结束的处理
    void onScriptFinished();

private:
    MainWindow *m_mainWin;

    // UI 控件
    QLineEdit *m_filePathEdit;
    QTextEdit *m_logOutput;
    QPushButton *m_runBtn;
    QPushButton *m_browseBtn;

    QThread *m_workerThread;
    ScriptWorker *m_worker;

signals:
    // 发送给 Worker 的信号，触发脚本执行
    void startScriptExecution(const QString &filePath);
};