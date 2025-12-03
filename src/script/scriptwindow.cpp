#include "scriptwindow.h"
#include "scriptapi.h"
#include <QMessageBox>
#include <QDebug>
#include <QScrollBar>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QHBoxLayout>
#include <QStyle>
#include <QDir>
#include <QCoreApplication>
#include <QMetaObject>

ScriptWorker::ScriptWorker(ScriptAPI *api) : m_api(api) {}

void ScriptWorker::runScript(const QString &code)
{
    if (code.isEmpty())
    {
        emit finished();
        return;
    }

    try
    {
        // 关键：在子线程获取 GIL
        py::gil_scoped_acquire acquire;

        // 获取 Python __main__ 模块的字典
        py::object main = py::module::import("__main__");
        py::object global = main.attr("__dict__");

        py::module::import("inspector");

        // 注入 'api' 对象
        if (m_api)
        {
            global["api"] = m_api;
        }

        // 重定向 stdout/stderr
        py::exec(
            "import sys\n"
            "class CatchOut:\n"
            "    def __init__(self):\n"
            "        pass\n"
            "    def write(self, txt):\n"
            "        if txt:\n"
            "            api.log(txt)\n"
            "    def flush(self):\n"
            "        pass\n"
            "sys.stdout = CatchOut()\n"
            "sys.stderr = CatchOut()\n",
            global);

        // 执行用户代码
        py::exec(code.toStdString(), global);
    }
    catch (py::error_already_set &e)
    {
        // 捕获异常发送回主线程
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
    catch (const std::exception &e)
    {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }

    emit finished();
}

// --- ScriptWindow 实现 ---

ScriptWindow::ScriptWindow(ScriptAPI *api, QWidget *parent)
    : QMainWindow(parent), m_api(api), m_workerThread(nullptr), m_worker(nullptr)
{
    setWindowTitle(tr("Script Console"));
    resize(600, 500);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *layout = new QVBoxLayout(central);

    // --- 顶部按钮栏 ---
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);

    QPushButton *openBtn = new QPushButton(tr("Load Script..."), this);
    openBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(openBtn, &QPushButton::clicked, this, &ScriptWindow::onOpenClicked);

    QPushButton *saveBtn = new QPushButton(tr("Save Script..."), this);
    saveBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(saveBtn, &QPushButton::clicked, this, &ScriptWindow::onSaveClicked);

    QPushButton *clearBtn = new QPushButton(tr("Clear Log"), this);
    clearBtn->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
    connect(clearBtn, &QPushButton::clicked, this, &ScriptWindow::onClearLogClicked);

    m_runBtn = new QPushButton(tr("Run (Ctrl+Enter)"), this);
    m_runBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_runBtn->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Return));
    connect(m_runBtn, &QPushButton::clicked, this, &ScriptWindow::onRunClicked);

    btnLayout->addWidget(openBtn);
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(clearBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_runBtn);

    // --- 编辑器区域 ---
    m_editor = new QTextEdit(this);
    QFont font("Consolas", 10);
    font.setStyleHint(QFont::Monospace);
    m_editor->setFont(font);
    m_editor->setText(
        "# Python Script Example\n"
        "import inspector\n"
        "import time\n"
        "print('Starting long task...')\n"
        "# time.sleep(2) # UI will not freeze now\n"
        "print('Done!')\n"
        "\n"
        "# api.load_file('test.csv')\n");

    m_logOutput = new QTextEdit(this);
    m_logOutput->setReadOnly(true);
    m_logOutput->setStyleSheet("background-color: #f0f0f0; color: #333;");

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_editor);
    splitter->addWidget(m_logOutput);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    layout->addLayout(btnLayout);
    layout->addWidget(splitter);

    // --- 初始化线程 ---
    m_workerThread = new QThread(this);
    m_worker = new ScriptWorker(m_api);
    m_worker->moveToThread(m_workerThread);

    // 连接信号槽
    connect(this, &ScriptWindow::startScriptExecution, m_worker, &ScriptWorker::runScript);
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
    // 线程安全检查：如果当前不是主线程，使用 invokeMethod 转发
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

void ScriptWindow::onOpenClicked()
{
    QString scriptPath = QCoreApplication::applicationDirPath() + "/py_scripts";
    QDir dir(scriptPath);
    if (!dir.exists())
        dir.mkpath(".");

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Python Script"), scriptPath, tr("Python Scripts (*.py);;All Files (*)"));
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&file);
        in.setCodec("UTF-8");
        m_editor->setPlainText(in.readAll());
    }
}

void ScriptWindow::onSaveClicked()
{
    QString scriptPath = QCoreApplication::applicationDirPath() + "/py_scripts";
    QDir dir(scriptPath);
    if (!dir.exists())
        dir.mkpath(".");

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Python Script"), scriptPath, tr("Python Scripts (*.py);;All Files (*)"));
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << m_editor->toPlainText();
    }
}

void ScriptWindow::onRunClicked()
{
    QString code = m_editor->toPlainText();
    if (code.isEmpty())
        return;

    // 禁用运行按钮防止重复点击
    m_runBtn->setEnabled(false);
    m_runBtn->setText(tr("Running..."));

    // 发送信号通知子线程开始
    emit startScriptExecution(code);
}

void ScriptWindow::onScriptFinished()
{
    m_runBtn->setEnabled(true);
    m_runBtn->setText(tr("Run (Ctrl+Enter)"));
}
