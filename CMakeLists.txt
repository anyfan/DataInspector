cmake_minimum_required(VERSION 3.10)
project(DataInspector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 添加对ZLIB的支持
add_library(zlib STATIC IMPORTED)
set_target_properties(zlib PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/zlib131/libzlibstatic.a"
)

# 添加对HDF5的支持
add_library(hdf5 STATIC IMPORTED)
set_target_properties(hdf5 PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/hdf5-1.14.6/lib/libhdf5.a"
)
set_target_properties(hdf5 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_libs/hdf5-1.14.6/include"
)
target_link_libraries(hdf5 INTERFACE zlib)

# 添加对MATIO的支持
add_library(matio STATIC IMPORTED)
set_target_properties(matio PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/matio-1.5.28/lib/libmatio.a"
)
set_target_properties(matio PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_libs/matio-1.5.28/include"
)

target_link_libraries(matio INTERFACE hdf5)


# 查找 Qt5 
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
    signalpropertiesdialog.cpp
)

# 链接 Qt5 库
target_link_libraries(DataInspector
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::PrintSupport
    Qt5::OpenGL
    matio
)

# 包含当前目录以便找到 qcustomplot.h 等
target_include_directories(DataInspector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})