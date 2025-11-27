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

ScriptWindow::ScriptWindow(ScriptAPI *api, QWidget *parent)
    : QMainWindow(parent), m_api(api)
{
    setWindowTitle(tr("Script Console"));
    resize(600, 500); // 默认大小

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *layout = new QVBoxLayout(central);

    // --- 顶部按钮栏 ---
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);

    // 1. 打开按钮
    QPushButton *openBtn = new QPushButton(tr("Load Script..."), this);
    openBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    openBtn->setToolTip(tr("Open a Python script file"));
    connect(openBtn, &QPushButton::clicked, this, &ScriptWindow::onOpenClicked);

    // 2. 保存按钮
    QPushButton *saveBtn = new QPushButton(tr("Save Script..."), this);
    saveBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    saveBtn->setToolTip(tr("Save current script to file"));
    connect(saveBtn, &QPushButton::clicked, this, &ScriptWindow::onSaveClicked);

    // 3. 运行按钮
    QPushButton *runBtn = new QPushButton(tr("Run (Ctrl+Enter)"), this);
    runBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    runBtn->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Return)); // 快捷键 Ctrl+回车
    connect(runBtn, &QPushButton::clicked, this, &ScriptWindow::onRunClicked);

    // 添加到布局
    btnLayout->addWidget(openBtn);
    btnLayout->addWidget(saveBtn);
    btnLayout->addStretch(); // 中间弹簧，让运行按钮靠右或让按钮左对齐
    btnLayout->addWidget(runBtn);

    // --- 编辑器区域 ---
    
    // 4. 代码编辑器 (上方)
    m_editor = new QTextEdit(this);
    // 设置等宽字体，看起来更像代码
    QFont font("Consolas", 10);
    font.setStyleHint(QFont::Monospace);
    m_editor->setFont(font);

    // 设置默认示例文本，展示新功能
    m_editor->setText(
        "# Python Script Example\n"
        "import inspector\n"
        "print('Hello from Script Console!')\n"
        "\n"
        "# 1. Load File\n"
        "# api.load_file('C:/data/test.csv')\n"
        "\n"
        "# 2. Find and use Signal ID\n"
        "# sig_id = api.find_id('Speed')\n"
        "# if sig_id:\n"
        "#     data = api.get_data(sig_id)\n"
        "#     print(f'Got {len(data)} points for Speed')\n"
        "\n"
        "# 3. Manipulate Plot\n"
        "# api.set_x_range(0, 50)\n"
        "# api.set_y_range(-10, 10)\n"
        "# api.autoscale()\n"
        "\n"
        "# 4. Export\n"
        "# api.export_plot('plot_1.png')\n"
        "# api.export_view('full_layout.png')\n");

    // 5. 日志输出 (下方)
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

    // 组装主布局
    layout->addLayout(btnLayout); // 添加按钮栏
    layout->addWidget(splitter);  // 添加编辑器和日志
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

void ScriptWindow::onOpenClicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, 
        tr("Open Python Script"), 
        "", 
        tr("Python Scripts (*.py);;All Files (*)"));

    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file for reading"));
        return;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    m_editor->setPlainText(content);
    
    appendLog(tr("Loaded script: %1").arg(fileName));
}

void ScriptWindow::onSaveClicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, 
        tr("Save Python Script"), 
        "", 
        tr("Python Scripts (*.py);;All Files (*)"));

    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file for writing"));
        return;
    }

    QTextStream out(&file);
    out << m_editor->toPlainText();
    file.close();

    appendLog(tr("Saved script to: %1").arg(fileName));
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