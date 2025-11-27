#include "scriptwindow.h"
#include "scriptapi.h"
#include <QMessageBox>
#include <QDebug>
#include <QScrollBar>

ScriptWindow::ScriptWindow(ScriptAPI *api, QWidget *parent)
    : QMainWindow(parent), m_api(api)
{
    setWindowTitle(tr("Script Console"));
    resize(600, 500); // 默认大小

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *layout = new QVBoxLayout(central);

    // 1. 代码编辑器 (上方)
    m_editor = new QTextEdit(this);
    // 设置等宽字体，看起来更像代码
    QFont font("Consolas", 10);
    font.setStyleHint(QFont::Monospace);
    m_editor->setFont(font);

    // 设置默认示例文本
    m_editor->setText(
        "# Python Script Example\n"
        "import inspector\n"
        "print('Hello from Script Console!')\n"
        "# api.set_x_range(0, 10)\n"
        "# api.autoscale()\n");

    // 2. 运行按钮
    QPushButton *runBtn = new QPushButton(tr("Run Script (Ctrl+Enter)"), this);
    runBtn->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Return)); // 快捷键 Ctrl+回车
    connect(runBtn, &QPushButton::clicked, this, &ScriptWindow::onRunClicked);

    // 3. 日志输出 (下方)
    m_logOutput = new QTextEdit(this);
    m_logOutput->setReadOnly(true);
    m_logOutput->setStyleSheet("background-color: #f0f0f0; color: #333;");
    m_logOutput->setPlaceholderText("Output log will appear here...");

    // 使用分割器布局
    QSplitter *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_editor);
    splitter->addWidget(m_logOutput);
    splitter->setStretchFactor(0, 3); // 编辑器占更大比例
    splitter->setStretchFactor(1, 1);

    layout->addWidget(runBtn);
    layout->addWidget(splitter);
}

ScriptWindow::~ScriptWindow()
{
}

void ScriptWindow::appendLog(const QString &msg)
{
    m_logOutput->append(msg);
    // 滚动到底部
    m_logOutput->verticalScrollBar()->setValue(m_logOutput->verticalScrollBar()->maximum());
}

void ScriptWindow::onRunClicked()
{
    QString code = m_editor->toPlainText();
    if (code.isEmpty())
        return;

    try
    {
        // 获取 Python __main__ 模块的字典
        py::object main = py::module::import("__main__");
        py::object global = main.attr("__dict__");

        py::module::import("inspector");

        // 注入 'api' 对象，让脚本可以直接使用
        if (m_api)
        {
            global["api"] = m_api;
        }

        // 重定向 stdout/stderr 到我们的日志窗口，我们在 Python 中定义一个名为 CatchOut 的类，调用 api.log()
        py::exec(
            "import sys\n"
            "class CatchOut:\n"
            "    def __init__(self):\n"
            "        pass\n"
            "    def write(self, txt):\n"
            "        if txt.strip():\n" // 忽略空行
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
        // 捕获 Python 异常并显示
        m_logOutput->append(QString("<font color='red'>Error: %1</font>").arg(e.what()));
    }
}