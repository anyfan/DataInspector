#pragma once

#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QSplitter>
#include <QThread>
#include <pybind11/embed.h>

// 前向声明
class ScriptAPI;

namespace py = pybind11;

// --- 工作线程类 ---
class ScriptWorker : public QObject
{
    Q_OBJECT
public:
    explicit ScriptWorker(ScriptAPI *api);

public slots:
    // 在子线程中执行脚本
    void runScript(const QString &code);

signals:
    void finished();
    void errorOccurred(const QString &msg);

private:
    ScriptAPI *m_api;
};

// --- 主窗口类 ---
class ScriptWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit ScriptWindow(ScriptAPI *api, QWidget *parent = nullptr);
    ~ScriptWindow();

public slots:
    void onRunClicked();
    // 线程安全的日志追加
    void appendLog(const QString &msg);

    void onOpenClicked();
    void onSaveClicked();
    void onClearLogClicked();

    // 脚本运行结束的处理
    void onScriptFinished();

private:
    ScriptAPI *m_api;
    QTextEdit *m_editor;
    QTextEdit *m_logOutput;
    QPushButton *m_runBtn;

    QThread *m_workerThread;
    ScriptWorker *m_worker;

signals:
    // 发送给 Worker 的信号，触发脚本执行
    void startScriptExecution(const QString &code);
};
