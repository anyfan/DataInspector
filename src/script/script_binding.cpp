#include "scriptapi.h"
#include "scriptwindow.h"
#include "mainwindow.h"
#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

ScriptAPI::ScriptAPI(MainWindow *mainWin) : m_mainWin(mainWin), m_scriptWin(nullptr) {}

void ScriptAPI::setScriptWindow(ScriptWindow *win) { m_scriptWin = win; }

void ScriptAPI::log(std::string msg)
{
    if (m_scriptWin)
        m_scriptWin->appendLog(QString::fromStdString(msg));
}

PYBIND11_EMBEDDED_MODULE(inspector, m)
{
    py::class_<ScriptAPI>(m, "API")
        .def(py::init<MainWindow *>())
        .def("log", &ScriptAPI::log)
        // Data
        .def("load_file", &ScriptAPI::load_file, py::arg("path"), py::arg("overwrite") = false)
        .def("remove_file", &ScriptAPI::remove_file)
        .def("find_id", &ScriptAPI::find_id)
        .def("get_data", &ScriptAPI::get_data)
        .def("get_time_data", &ScriptAPI::get_time_data)
        .def("get_all_signal_ids", &ScriptAPI::get_all_signal_ids)
        .def("get_signal_name", &ScriptAPI::get_signal_name, py::arg("id"))
        .def("load_parsed_data", &ScriptAPI::load_parsed_data,
             "直接加载解析后的数据字典 (无需保存文件)",
             py::arg("filename"), py::arg("data_dict"))
        // UI & Views
        .def("import_view", &ScriptAPI::import_view)
        .def("export_plot", &ScriptAPI::export_plot)
        .def("export_view", &ScriptAPI::export_view)
        .def("export_view_json", &ScriptAPI::export_view_json)
        .def("import_view_json", &ScriptAPI::import_view_json)
        .def("set_layout", &ScriptAPI::set_layout, py::arg("rows"), py::arg("cols"))
        .def("get_view_count", &ScriptAPI::get_view_count)
        .def("get_active_view_index", &ScriptAPI::get_active_view_index)
        .def("set_active_view", &ScriptAPI::set_active_view, py::arg("index"))
        .def("get_view_signals", &ScriptAPI::get_view_signals, py::arg("view_index") = -1)
        .def("add_signal", &ScriptAPI::add_signal, py::arg("id"), py::arg("view_index") = -1)
        .def("remove_signal", &ScriptAPI::remove_signal, py::arg("id"), py::arg("view_index") = -1)
        .def("set_x_range", &ScriptAPI::set_x_range, py::arg("min"), py::arg("max"), py::arg("view_index") = -1)
        .def("set_y_range", &ScriptAPI::set_y_range, py::arg("min"), py::arg("max"), py::arg("view_index") = -1)
        .def("get_x_range", &ScriptAPI::get_x_range, py::arg("view_index") = -1)
        .def("get_y_range", &ScriptAPI::get_y_range, py::arg("view_index") = -1)
        .def("autoscale", &ScriptAPI::autoscale, py::arg("view_index") = -1)
        .def("fit_view_y_all", &ScriptAPI::fit_view_y_all)
        .def("fit_view_all", &ScriptAPI::fit_view_all)
        // Algo
        .def("parse_flight_data_fast", &ScriptAPI::parse_flight_data_fast,
             "C++ Accelerated parsing: returns (packets_list, stats_dict)",
             py::arg("path"), py::arg("protocol"));
}