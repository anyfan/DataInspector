#ifndef SCRIPTWINDOW_H
#define SCRIPTWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QSplitter>
#include <pybind11/embed.h>

// 前向声明
class ScriptAPI;

namespace py = pybind11;

class ScriptWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit ScriptWindow(ScriptAPI *api, QWidget *parent = nullptr);
    ~ScriptWindow();

public slots:
    // 执行脚本的槽函数
    void onRunClicked();
    // 供 Python 调用的日志打印函数
    void appendLog(const QString &msg);

    // [新增] 打开脚本文件
    void onOpenClicked();
    // [新增] 保存脚本文件
    void onSaveClicked();

private:
    ScriptAPI *m_api;       // API 接口对象
    QTextEdit *m_editor;    // 代码编辑器
    QTextEdit *m_logOutput; // 运行日志输出
};

#endif // SCRIPTWINDOW_H