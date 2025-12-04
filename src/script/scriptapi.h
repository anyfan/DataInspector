#pragma once

#include <string>
#include <vector>
#include <map> // 新增: 用于返回字典结构
#include <tuple>
#include <utility>
#include <QObject>
#include <pybind11/pybind11.h> // 引入 pybind11

class MainWindow;
class ScriptWindow;

namespace py = pybind11;

class ScriptAPI
{
public:
    ScriptAPI(MainWindow *mainWin);
    void setScriptWindow(ScriptWindow *win);

    void log(std::string msg);

    // --- 文件操作 ---
    bool load_file(std::string path, bool overwrite = false);
    bool remove_file(std::string filename);

    // 获取已加载文件信息
    std::vector<std::string> get_loaded_files();
    std::map<std::string, std::vector<std::string>> get_file_info(std::string filename);

    // --- 信号操作 ---
    std::string find_id(std::string name);
    std::vector<double> get_data(std::string id);
    std::vector<double> get_time_data(std::string id);
    std::vector<std::string> get_all_signal_ids();

    // --- 视图与布局操作 ---
    void import_view(std::string path);
    bool export_plot(std::string path);
    bool export_view(std::string path);
    void set_layout(int rows, int cols);
    int get_view_count();
    int get_active_view_index();
    void set_active_view(int index);

    // --- 视图内容操作 ---
    void set_x_range(double min, double max, int view_index = -1);
    void set_y_range(double min, double max, int view_index = -1);
    std::tuple<double, double> get_x_range(int view_index = -1);
    std::tuple<double, double> get_y_range(int view_index = -1);
    void autoscale(int view_index = -1);
    void fit_view_y_all();
    void fit_view_all();
    std::vector<std::string> get_view_signals(int view_index = -1);
    bool add_signal(std::string id, int view_index = -1);
    bool remove_signal(std::string id, int view_index = -1);
    std::string get_signal_name(std::string id);

    bool export_view_json(std::string path);
    bool import_view_json(std::string path);

    py::tuple parse_flight_data_fast(std::string path, std::string protocol);

    bool load_parsed_data(std::string filename, py::dict data_dict);

    void set_cursor_mode(std::string mode);
    void set_cursor_position(double pos, int index = 1);

private:
    MainWindow *m_mainWin;
    ScriptWindow *m_scriptWin;
    class QCustomPlot *getTargetPlot(int view_index);
};
