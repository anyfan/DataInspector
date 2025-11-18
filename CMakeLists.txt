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
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/zlib131/lib/libzlibstatic.a"
)

# 添加对HDF5的支持
add_library(hdf5 STATIC IMPORTED)
set_target_properties(hdf5 PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/hdf5-1.14.6/lib/libhdf5.a"
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
    Xml
    Gui
    Widgets
    PrintSupport
    OpenGL
)

# 查找 OpenGL
find_package(OpenGL REQUIRED)

# 添加预处理器定义以启用QCustomPlot的OpenGL支持
add_definitions(-DQCUSTOMPLOT_USE_OPENGL)

add_library(qcustomplot ${CMAKE_SOURCE_DIR}/third_libs/qcustomplot-2.1.1/qcustomplot.cpp)
set_target_properties(qcustomplot PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_libs/qcustomplot-2.1.1"
)
target_link_libraries(qcustomplot Qt5::Core Qt5::PrintSupport Qt5::OpenGL ${OPENGL_LIBRARIES})


add_library(bzip STATIC IMPORTED)
set_target_properties(bzip PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/quazip-1.5/lib/libbzip2.a"
)

# 修改QuaZip库配置，添加缺失的依赖项
add_library(quazip STATIC IMPORTED)
set_target_properties(quazip PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/quazip-1.5/lib/libquazip1-qt5.a"
)
set_target_properties(quazip PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_libs/quazip-1.5/include/QuaZip-Qt5-1.5"
)
target_link_libraries(quazip INTERFACE bzip)

# 添加可执行文件
add_executable(DataInspector
    main.cpp
    mainwindow.cpp
    datamanager.cpp
    signaltreedelegate.cpp
    signalpropertiesdialog.cpp
    cursormanager.cpp
    replaymanager.cpp
    resources.qrc
    DataInspector.rc
)

target_compile_definitions(DataInspector PRIVATE QUAZIP_STATIC)

# 链接 Qt5 库
target_link_libraries(DataInspector
    Qt5::Core
    Qt5::Xml
    Qt5::Gui
    Qt5::Widgets
    Qt5::OpenGL
    matio
    qcustomplot
    quazip
    ${OPENGL_LIBRARIES}
)

# 包含当前
target_include_directories(DataInspector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
