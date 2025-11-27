#ifndef SCRIPTAPI_H
#define SCRIPTAPI_H

#include <string>
#include <vector>
#include <QObject>

// 前向声明，避免包含 mainwindow.h 导致的循环引用
class MainWindow;
class ScriptWindow;

class ScriptAPI
{
public:
    // 构造函数声明
    ScriptAPI(MainWindow *mainWin);

    // 设置脚本窗口指针
    void setScriptWindow(ScriptWindow *win);

    // --- 供 Python 调用的方法声明 ---

    // 打印日志
    void log(std::string msg);

    // 加载数据文件 (.csv, .mat)
    bool load_file(std::string path);

    // 导入视图布局 (.mldatx)
    void import_view(std::string path);

    // 查找信号ID (通过名称)
    std::string find_id(std::string name);

    // 设置当前活动子图的 X 轴范围
    void set_x_range(double min, double max);

    // 设置当前活动子图的 Y 轴范围
    void set_y_range(double min, double max);

    // 自适应当前视图 (X 和 Y)
    void autoscale();

    // 全局自适应 Y 轴 (所有子图)
    void fit_view_y_all();

    // 获取信号数据
    std::vector<double> get_data(std::string id);

    // 获取信号的时间数据
    std::vector<double> get_time_data(std::string id);

    // 导出当前激活的子图为图片
    bool export_plot(std::string path);

    // 导出整个视图布局为图片
    bool export_view(std::string path);

private:
    MainWindow *m_mainWin;
    ScriptWindow *m_scriptWin;
};

#endif // SCRIPTAPI_H
