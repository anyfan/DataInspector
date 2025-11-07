cmake_minimum_required(VERSION 3.10)
project(DataInspector LANGUAGES CXX)

# 需要 C++11 (来自您的 .pro 文件)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动处理 Qt 的 MOC, UI, RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找 Qt5 
# (根据您的 .pro 文件添加了 Core, Gui, Widgets, PrintSupport, OpenGL)
find_package(Qt5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    PrintSupport
    OpenGL
)

# 添加可执行文件
add_executable(DataInspector
    main.cpp
    mainwindow.cpp
    datamanager.cpp
    qcustomplot.cpp 
    signaltreedelegate.cpp
)

# 链接 Qt5 库
target_link_libraries(DataInspector
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::PrintSupport
    Qt5::OpenGL
)

# 包含当前目录以便找到 qcustomplot.h 等
target_include_directories(DataInspector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})