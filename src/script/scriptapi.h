#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <QObject>

class MainWindow;
class ScriptWindow;

class ScriptAPI
{
public:
    ScriptAPI(MainWindow *mainWin);
    void setScriptWindow(ScriptWindow *win);

    void log(std::string msg);

    // --- 文件操作 ---
    bool load_file(std::string path, bool overwrite = false);
    bool remove_file(std::string filename);

    // --- 信号操作 ---
    std::string find_id(std::string name);
    std::vector<double> get_data(std::string id);
    std::vector<double> get_time_data(std::string id);
    // 获取所有信号ID
    std::vector<std::string> get_all_signal_ids();

    // --- 视图与布局操作 ---
    void import_view(std::string path);
    bool export_plot(std::string path);
    bool export_view(std::string path);

    // 设置网格布局
    void set_layout(int rows, int cols);
    // 获取子图数量
    int get_view_count();
    // 获取当前激活视图索引
    int get_active_view_index();
    // 设置激活视图
    void set_active_view(int index);

    // --- 视图内容操作 ---
    // 增加可选参数 view_index，默认 -1 表示当前视图
    void set_x_range(double min, double max, int view_index = -1);
    void set_y_range(double min, double max, int view_index = -1);

    // 获取范围
    std::tuple<double, double> get_x_range(int view_index = -1);
    std::tuple<double, double> get_y_range(int view_index = -1);

    void autoscale(int view_index = -1);
    void fit_view_y_all();

    // 获取某视图下的所有信号ID
    std::vector<std::string> get_view_signals(int view_index = -1);
    // 添加信号到视图
    bool add_signal(std::string id, int view_index = -1);
    // 从视图移除信号
    bool remove_signal(std::string id, int view_index = -1);

    // 根据 ID 获取信号名称
    std::string get_signal_name(std::string id);

private:
    MainWindow *m_mainWin;
    ScriptWindow *m_scriptWin;

    // 辅助：获取目标 Plot 指针
    class QCustomPlot *getTargetPlot(int view_index);
};
