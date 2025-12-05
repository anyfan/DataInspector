#include "scriptwindow.h"
#include "scriptapi.h"
#include "mainwindow.h"
#include <QMessageBox>
#include <QDebug>
#include <QScrollBar>
#include <QFileDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QDir>
#include <QCoreApplication>
#include <QMetaObject>

ScriptWorker::ScriptWorker(MainWindow *mw, ScriptWindow *sw)
    : m_mainWin(mw), m_scriptWin(sw) {}

void ScriptWorker::runScriptFile(const QString &filePath)
{
    if (filePath.isEmpty())
    {
        emit finished();
        return;
    }

    try
    {
        // 关键：在子线程获取 GIL
        py::gil_scoped_acquire acquire;

        py::module inspector = py::module::import("inspector");

        ScriptAPI localApi(m_mainWin);
        localApi.setScriptWindow(m_scriptWin); // 设置日志输出目标

        inspector.attr("api") = &localApi;

        // 获取 __main__ 字典用于执行环境
        py::object main = py::module::import("__main__");
        py::object global = main.attr("__dict__");

        // 重定向 stdout/stderr
        py::exec(
            "import sys\n"
            "import inspector\n"
            "class CatchOut:\n"
            "    def __init__(self):\n"
            "        pass\n"
            "    def write(self, txt):\n"
            "        if txt and hasattr(inspector, 'api'):\n"
            "            inspector.api.log(txt)\n"
            "    def flush(self):\n"
            "        pass\n"
            "sys.stdout = CatchOut()\n"
            "sys.stderr = CatchOut()\n",
            global);

        // 执行文件
        py::eval_file(filePath.toStdString(), global);

        // --- 清理 ---
        if (py::hasattr(inspector, "api"))
        {
            py::delattr(inspector, "api");
        }
    }
    catch (py::error_already_set &e)
    {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
    catch (const std::exception &e)
    {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }

    emit finished();
}
// --- ScriptWindow 实现 ---

ScriptWindow::ScriptWindow(MainWindow *mainWin, QWidget *parent)
    : QMainWindow(parent), m_mainWin(mainWin), m_workerThread(nullptr), m_worker(nullptr)
{
    setWindowTitle(tr("Script Runner"));
    resize(600, 400);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *layout = new QVBoxLayout(central);

    // --- 1. 文件选择区域 ---
    QHBoxLayout *fileLayout = new QHBoxLayout();

    QLabel *fileLabel = new QLabel(tr("Script File:"), this);
    m_filePathEdit = new QLineEdit(this);
    m_filePathEdit->setPlaceholderText(tr("Select a python script file (.py)..."));
    m_filePathEdit->setReadOnly(true);

    m_browseBtn = new QPushButton(tr("Browse..."), this);
    m_browseBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(m_browseBtn, &QPushButton::clicked, this, &ScriptWindow::onBrowseClicked);

    fileLayout->addWidget(fileLabel);
    fileLayout->addWidget(m_filePathEdit);
    fileLayout->addWidget(m_browseBtn);

    // --- 2. 操作按钮区域 ---
    QHBoxLayout *btnLayout = new QHBoxLayout();

    m_runBtn = new QPushButton(tr("Run Script"), this);
    m_runBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    // 只有选择了文件才允许运行
    m_runBtn->setEnabled(false);
    connect(m_runBtn, &QPushButton::clicked, this, &ScriptWindow::onRunClicked);

    QPushButton *clearBtn = new QPushButton(tr("Clear Log"), this);
    clearBtn->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
    connect(clearBtn, &QPushButton::clicked, this, &ScriptWindow::onClearLogClicked);

    btnLayout->addWidget(m_runBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(clearBtn);

    // --- 3. 日志输出区域 ---
    m_logOutput = new QTextEdit(this);
    m_logOutput->setReadOnly(true);
    m_logOutput->setStyleSheet("background-color: #f0f0f0; color: #333; font-family: Consolas, Monospace;");

    layout->addLayout(fileLayout);
    layout->addLayout(btnLayout);
    layout->addWidget(m_logOutput);

    // --- 初始化线程 ---
    m_workerThread = new QThread(this);
    // 传递 MainWindow 和 this (ScriptWindow) 给 Worker
    m_worker = new ScriptWorker(m_mainWin, this);
    m_worker->moveToThread(m_workerThread);

    // 连接信号槽
    connect(this, &ScriptWindow::startScriptExecution, m_worker, &ScriptWorker::runScriptFile);
    connect(m_worker, &ScriptWorker::finished, this, &ScriptWindow::onScriptFinished);
    connect(m_worker, &ScriptWorker::errorOccurred, this, [this](const QString &msg)
            { appendLog(QString("<font color='red'>Error: %1</font>").arg(msg)); });

    m_workerThread->start();
}

ScriptWindow::~ScriptWindow()
{
    if (m_workerThread)
    {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    if (m_worker)
        delete m_worker;
}

void ScriptWindow::appendLog(const QString &msg)
{
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, "appendLog", Qt::QueuedConnection, Q_ARG(QString, msg));
        return;
    }

    if (msg.trimmed().isEmpty())
        return;

    m_logOutput->append(msg);
    m_logOutput->verticalScrollBar()->setValue(m_logOutput->verticalScrollBar()->maximum());
}

void ScriptWindow::onClearLogClicked()
{
    m_logOutput->clear();
}

void ScriptWindow::onBrowseClicked()
{
    QString scriptPath = QCoreApplication::applicationDirPath() + "/py_scripts";
    QDir dir(scriptPath);
    if (!dir.exists())
        dir.mkpath(".");

    QString fileName = QFileDialog::getOpenFileName(this, tr("Select Python Script"), scriptPath, tr("Python Scripts (*.py);;All Files (*)"));
    if (!fileName.isEmpty())
    {
        m_filePathEdit->setText(fileName);
        m_runBtn->setEnabled(true);
        appendLog(tr("<i>Selected: %1</i>").arg(fileName));
    }
}

void ScriptWindow::onRunClicked()
{
    QString path = m_filePathEdit->text();
    if (path.isEmpty())
        return;

    m_runBtn->setEnabled(false);
    m_runBtn->setText(tr("Running..."));
    m_browseBtn->setEnabled(false);

    appendLog(tr("<b>--- Running script: %1 ---</b>").arg(QFileInfo(path).fileName()));

    // 发送信号通知子线程开始，传递文件路径
    emit startScriptExecution(path);
}

void ScriptWindow::onScriptFinished()
{
    m_runBtn->setEnabled(true);
    m_runBtn->setText(tr("Run Script"));
    m_browseBtn->setEnabled(true);
    appendLog("<i>--- Finished ---</i>");
}