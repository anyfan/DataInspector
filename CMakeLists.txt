cmake_minimum_required(VERSION 3.21)
project(DataInspector VERSION 0.1 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2)
option(ENABLE_MAT "Enable MAT file loading (requires LLVM-MinGW compatible matio/HDF5)" ON)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(DataInspector
    src/quick/main.cpp
    src/quick/appcontroller.cpp
    src/quick/appcontroller.h
    src/quick/plotitem.cpp
    src/quick/plotitem.h
    src/quick/signalmodel.cpp
    src/quick/signalmodel.h
)

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/QuickPlot.qml PROPERTIES QT_RESOURCE_ALIAS QuickPlot.qml)
qt_add_qml_module(DataInspector
    URI DataInspector
    VERSION 1.0
    QML_FILES qml/Main.qml qml/QuickPlot.qml
)

target_include_directories(DataInspector PRIVATE src/quick)
target_include_directories(DataInspector PRIVATE
    third_libs/matio-1.5.28/include
    third_libs/hdf5-1.14.6/include
    third_libs/zlib131/include
)

if(ENABLE_MAT)
    add_compile_definitions(ENABLE_MAT)
    add_library(zlib_qt6 STATIC IMPORTED)
    set_target_properties(zlib_qt6 PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/zlib131/lib/libzlibstatic.a")
    add_library(hdf5_qt6 STATIC IMPORTED)
    set_target_properties(hdf5_qt6 PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/hdf5-1.14.6/lib/libhdf5.a")
    target_link_libraries(hdf5_qt6 INTERFACE zlib_qt6)
    add_library(matio_qt6 STATIC IMPORTED)
    set_target_properties(matio_qt6 PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/matio-1.5.28/lib/libmatio.a")
    target_link_libraries(matio_qt6 INTERFACE hdf5_qt6)
endif()
target_link_libraries(DataInspector PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    Qt6::QuickControls2 Qt6::QuickDialogs2
)
if(ENABLE_MAT)
    # HDF5's static Windows build does not carry transitive system libraries
    # in the archive. Keep them explicit so the same LLVM-MinGW ABI is used
    # regardless of which Qt module happens to pull them in first.
    target_link_libraries(DataInspector PRIVATE
        matio_qt6
        m ws2_32 wsock32 shlwapi advapi32
    )
endif()

if(MSVC)
    target_compile_options(DataInspector PRIVATE /W4)
else()
    target_compile_options(DataInspector PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(DataInspector PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_library(plot_render_core STATIC
        src/quick/render/plotseriesstore.cpp
        src/quick/render/plotseriesstore.h
        src/quick/render/plotlodbuilder.cpp
        src/quick/render/plotlodbuilder.h
        src/quick/render/plotgeometrybuilder.cpp
        src/quick/render/plotgeometrybuilder.h
    )
    target_include_directories(plot_render_core PUBLIC src/quick)
    target_link_libraries(plot_render_core PUBLIC Qt6::Core Qt6::Gui)

    add_executable(rendercore_test tests/rendercore_test.cpp)
    target_link_libraries(rendercore_test PRIVATE plot_render_core Qt6::Test)
    add_test(NAME rendercore_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:rendercore_test>
    )

    add_executable(plotitem_lod_test
        tests/plotitem_lod_test.cpp
        src/quick/plotitem.cpp
        src/quick/plotitem.h
    )
    target_include_directories(plotitem_lod_test PRIVATE src/quick)
    target_link_libraries(plotitem_lod_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick plot_render_core)
    add_test(NAME plotitem_lod_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:plotitem_lod_test> -platform offscreen
    )
endif()
