cmake_minimum_required(VERSION 3.10)
project(DataInspector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# --- 1. 第三方库配置 ---
# ZLIB
add_library(zlib STATIC IMPORTED)
set_target_properties(zlib PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/zlib131/lib/libzlibstatic.a")

# HDF5
add_library(hdf5 STATIC IMPORTED)
set_target_properties(hdf5 PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/hdf5-1.14.6/lib/libhdf5.a")
target_link_libraries(hdf5 INTERFACE zlib)

# MATIO
add_library(matio STATIC IMPORTED)
set_target_properties(matio PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/matio-1.5.28/lib/libmatio.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_libs/matio-1.5.28/include"
)
target_link_libraries(matio INTERFACE hdf5)

# QCustomPlot
add_definitions(-DQCUSTOMPLOT_USE_OPENGL) # 启用 OpenGL
add_library(qcustomplot "${CMAKE_SOURCE_DIR}/third_libs/qcustomplot-2.1.1/qcustomplot.cpp")
set_target_properties(qcustomplot PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_libs/qcustomplot-2.1.1")

# QuaZip
add_library(bzip STATIC IMPORTED)
set_target_properties(bzip PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/quazip-1.5/lib/libbzip2.a")

add_library(quazip STATIC IMPORTED)
set_target_properties(quazip PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/quazip-1.5/lib/libquazip1-qt5.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_libs/quazip-1.5/include/QuaZip-Qt5-1.5"
)
target_link_libraries(quazip INTERFACE bzip)

# Qt5 & OpenGL
find_package(Qt5 REQUIRED COMPONENTS Core Xml Gui Widgets PrintSupport OpenGL)
find_package(OpenGL REQUIRED)
target_link_libraries(qcustomplot Qt5::Core Qt5::PrintSupport Qt5::OpenGL ${OPENGL_LIBRARIES})

# --- 2. 源文件列表 ---
set(SOURCES
    main.cpp
    mainwindow.cpp
    datamanager.cpp
    cursormanager.cpp
    replaymanager.cpp
    signaltreedelegate.cpp
    signalpropertiesdialog.cpp
)

# --- 3. 目标构建 ---
add_executable(DataInspector ${SOURCES} resources.qrc DataInspector.rc)

target_compile_definitions(DataInspector PRIVATE QUAZIP_STATIC)
target_include_directories(DataInspector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(DataInspector
    Qt5::Core Qt5::Xml Qt5::Gui Qt5::Widgets Qt5::OpenGL
    matio qcustomplot quazip ${OPENGL_LIBRARIES}
)
