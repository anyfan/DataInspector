cmake_minimum_required(VERSION 3.21)
project(DataInspector VERSION 0.1 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(ENABLE_MAT "Enable MAT file loading (requires LLVM-MinGW compatible matio/HDF5)" ON)
option(ENABLE_GPU_TESTS "Run native-window GPU raster regression (requires graphics session)" OFF)
set(DATAINSPECTOR_BIN_DIR "bin" CACHE STRING "Application output subdirectory")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2)
qt_standard_project_setup(REQUIRES 6.8)
# Make generated module metadata discoverable by qmllint and QML language servers.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
set(QT_QML_GENERATE_QMLLS_INI ON)
# Qt 6.8 tooling settings collapse QStringList values to strings. Generate one
# import root for the active build, including custom build-directory names.
configure_file(qml/qmllint.ini.in "${CMAKE_SOURCE_DIR}/qml/.qmllint.ini" @ONLY)

# ---------------------------------------------------------------------------
# Shared source lists
# ---------------------------------------------------------------------------
set(DI_QUICK_DIR ${CMAKE_SOURCE_DIR}/src/quick)

set(DI_RENDER_SOURCES
    ${DI_QUICK_DIR}/render/trajectorybuilder.cpp
    ${DI_QUICK_DIR}/render/trajectorybuilder.h
    ${DI_QUICK_DIR}/render/plotaxisutils.cpp
    ${DI_QUICK_DIR}/render/plotaxisutils.h
    ${DI_QUICK_DIR}/render/plotrangeindex.cpp
    ${DI_QUICK_DIR}/render/plotrangeindex.h
    ${DI_QUICK_DIR}/render/plotseriesstore.cpp
    ${DI_QUICK_DIR}/render/plotseriesstore.h
    ${DI_QUICK_DIR}/render/plotlodbuilder.cpp
    ${DI_QUICK_DIR}/render/plotlodbuilder.h
    ${DI_QUICK_DIR}/render/plotlodscheduler.cpp
    ${DI_QUICK_DIR}/render/plotlodscheduler.h
    ${DI_QUICK_DIR}/render/plotgeometrybuilder.cpp
    ${DI_QUICK_DIR}/render/plotgeometrybuilder.h
)

set(DI_PLOTITEM_SOURCES
    ${DI_QUICK_DIR}/trajectoryitem.cpp
    ${DI_QUICK_DIR}/trajectoryitem.h
    ${DI_QUICK_DIR}/plotitem.cpp
    ${DI_QUICK_DIR}/plotitem.h
    ${DI_QUICK_DIR}/plotitem_cursor.cpp
    ${DI_QUICK_DIR}/plotitem_interaction.cpp
    ${DI_QUICK_DIR}/plotitem_render.cpp
)

set(DI_SIGNAL_SOURCES
    ${DI_QUICK_DIR}/signalmetadata.cpp
    ${DI_QUICK_DIR}/signalmetadata.h
    ${DI_QUICK_DIR}/signalmodel.cpp
    ${DI_QUICK_DIR}/signalmodel.h
)

set(DI_XLSX_SOURCES
    ${DI_QUICK_DIR}/xlsxreader.cpp
    ${DI_QUICK_DIR}/xlsxreader.h
    ${DI_QUICK_DIR}/xlsxwriter.cpp
    ${DI_QUICK_DIR}/xlsxwriter.h
    ${DI_QUICK_DIR}/exporttable.h
    ${DI_QUICK_DIR}/exportvalidation.cpp
    ${DI_QUICK_DIR}/loadedtable.h
)

set(DI_MAT_SOURCES
    ${DI_QUICK_DIR}/mat5streamwriter.cpp
    ${DI_QUICK_DIR}/mat5streamwriter.h
    ${DI_QUICK_DIR}/matwriter.cpp
    ${DI_QUICK_DIR}/matwriter.h
)

set(DI_CONTROLLER_SOURCES
    ${DI_QUICK_DIR}/qmltypes.h
    ${DI_QUICK_DIR}/sessiondocument.cpp
    ${DI_QUICK_DIR}/sessiondocument.h
    ${DI_QUICK_DIR}/appcontroller_session.cpp
    ${DI_QUICK_DIR}/appcontroller.cpp
    ${DI_QUICK_DIR}/appcontroller_loading.cpp
    ${DI_QUICK_DIR}/appcontroller_image.cpp
    ${DI_QUICK_DIR}/appcontroller_plots.cpp
    ${DI_QUICK_DIR}/appcontroller_trajectory.cpp
    ${DI_QUICK_DIR}/appcontroller.h
    ${DI_QUICK_DIR}/startupfiles.cpp
    ${DI_QUICK_DIR}/startupfiles.h
    ${DI_QUICK_DIR}/dataloadworker.cpp
    ${DI_QUICK_DIR}/dataloadworker.h
    ${DI_QUICK_DIR}/dataexportworker.cpp
    ${DI_QUICK_DIR}/dataexportworker.h
    ${DI_XLSX_SOURCES}
    ${DI_MAT_SOURCES}
    ${DI_PLOTITEM_SOURCES}
    ${DI_SIGNAL_SOURCES}
)

set(DI_ICON_FILES
    assets/icon/zoom-in.svg
    assets/icon/zoom-out.svg
    assets/icon/zoom-x.svg
    assets/icon/zoom-y.svg
    assets/icon/fullscreen.svg
    assets/icon/fullscreen-exit.svg
    assets/icon/arrows-angle-expand.svg
    assets/icon/arrows-angle-contract.svg
    assets/icon/fit-view.svg
    assets/icon/settings.svg
    assets/icon/layout-single.svg
    assets/icon/layout-rows.svg
    assets/icon/layout-columns.svg
    assets/icon/grid.svg
    assets/icon/cursor_1.svg
    assets/icon/cursor_2.svg
    assets/icon/clear.svg
    assets/icon/download.svg
    assets/icon/arrows_left_right.svg
    assets/icon/arrows_up_down.svg
    assets/icon/DataInspector.svg
)

set(DI_QML_FILES
    qml/Main.qml
    qml/QuickPlot.qml
    qml/TrajectoryPlot.qml
    qml/PlotLegend.qml
    qml/PlotAxisArea.qml
    qml/Splash.qml
)

# Windows resource script carrying the application icon shown by Explorer,
# the taskbar and the window frame.
set(DI_WINDOWS_RESOURCES)
if(WIN32)
    set(DI_WINDOWS_RESOURCES ${CMAKE_SOURCE_DIR}/assets/DataInspector.rc)
    # The .rc pulls in the manifest next to it; reconfigure/rebuild when either
    # the manifest or the icon changes.
    set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/DataInspector.rc
        PROPERTIES OBJECT_DEPENDS
            "${CMAKE_SOURCE_DIR}/assets/DataInspector.manifest;${CMAKE_SOURCE_DIR}/assets/icon/DataInspector.ico")
    # GNU windres shells out to gcc -E to preprocess the .rc file. When the
    # toolchain is LLVM-MinGW clang, a GNU windres picked up from an unrelated
    # MinGW install on PATH fails with a bare "preprocessing failed." as soon as
    # its sibling gcc is not also on PATH. The presets pin llvm-windres; warn
    # loudly when a build is configured without that pin.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
       AND NOT CMAKE_RC_COMPILER MATCHES "llvm-(windres|rc)")
        message(WARNING
            "RC compiler '${CMAKE_RC_COMPILER}' does not belong to the LLVM-MinGW "
            "toolchain used for C++. If the icon resource fails with "
            "'preprocessing failed.', configure with "
            "-DCMAKE_RC_COMPILER=<llvm-mingw>/bin/llvm-windres.exe "
            "or use the qt6-clang-* presets.")
    endif()
endif()

# Adds the qrc:/icons resource set to a target.
function(di_add_icon_resources target)
    qt_add_resources(${target} ${target}_icons
        PREFIX "/icons"
        BASE assets/icon
        FILES ${DI_ICON_FILES}
    )
endfunction()

# ---------------------------------------------------------------------------
# Optional MAT (matio + HDF5 + zlib, prebuilt for LLVM-MinGW)
# ---------------------------------------------------------------------------
if(ENABLE_MAT)
    add_compile_definitions(ENABLE_MAT)
    add_library(zlib_qt6 STATIC IMPORTED)
    set_target_properties(zlib_qt6 PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/zlib131/lib/libzlibstatic.a")
    add_library(hdf5_qt6 STATIC IMPORTED)
    set_target_properties(hdf5_qt6 PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/hdf5-1.14.6/lib/libhdf5.a")
    target_link_libraries(hdf5_qt6 INTERFACE zlib_qt6)
    add_library(matio_qt6 STATIC IMPORTED)
    set_target_properties(matio_qt6 PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_libs/matio-1.5.28/lib/libmatio.a")
    target_link_libraries(matio_qt6 INTERFACE hdf5_qt6)

    # Interface target that carries includes plus the transitive system
    # libraries HDF5's static Windows archive does not record itself.
    add_library(di_mat INTERFACE)
    target_include_directories(di_mat INTERFACE
        third_libs/matio-1.5.28/include
        third_libs/hdf5-1.14.6/include
        third_libs/zlib131/include
    )
    target_link_libraries(di_mat INTERFACE matio_qt6 m ws2_32 wsock32 shlwapi advapi32)
endif()

# ---------------------------------------------------------------------------
# Render core (no QObject GUI dependencies; shared by app and tests)
# ---------------------------------------------------------------------------
add_library(plot_render_core STATIC ${DI_RENDER_SOURCES})
target_include_directories(plot_render_core PUBLIC src/quick)
target_link_libraries(plot_render_core PUBLIC Qt6::Core Qt6::Gui)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(DI_APP_CORE_SOURCES ${DI_CONTROLLER_SOURCES})
list(REMOVE_ITEM DI_APP_CORE_SOURCES ${DI_QUICK_DIR}/qmltypes.h ${DI_PLOTITEM_SOURCES})
add_library(di_plot_items STATIC ${DI_PLOTITEM_SOURCES})
target_include_directories(di_plot_items PUBLIC src/quick)
target_link_libraries(di_plot_items PUBLIC Qt6::Quick plot_render_core)
qt_extract_metatypes(di_plot_items)
add_library(di_app_core STATIC ${DI_APP_CORE_SOURCES})
target_include_directories(di_app_core PUBLIC src/quick)
target_link_libraries(di_app_core PUBLIC Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Qml Qt6::Quick di_plot_items)
qt_extract_metatypes(di_app_core)
if(ENABLE_MAT)
    target_link_libraries(di_app_core PUBLIC di_mat)
endif()

qt_add_executable(DataInspector
    src/quick/main.cpp
    src/quick/qmltypes.h
    ${DI_WINDOWS_RESOURCES}
)
target_link_libraries(DataInspector PRIVATE di_app_core)

foreach(qml_file IN LISTS DI_QML_FILES)
    get_filename_component(qml_alias ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_alias})
endforeach()
qt_add_qml_module(DataInspector
    URI DataInspector
    VERSION 1.0
    DEPENDENCIES QtQuick QtQml.Models QtQuick.Shapes
    IMPORT_PATH "${QT_QML_OUTPUT_DIRECTORY}"
    QML_FILES ${DI_QML_FILES}
)
di_add_icon_resources(DataInspector)

target_include_directories(DataInspector PRIVATE src/quick)
target_link_libraries(DataInspector PRIVATE
    Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Qml Qt6::Quick
    Qt6::QuickControls2 Qt6::QuickDialogs2 di_app_core
)
if(ENABLE_MAT)
    target_link_libraries(DataInspector PRIVATE di_mat)
endif()

if(MSVC)
    target_compile_options(DataInspector PRIVATE /W4)
    target_compile_options(di_app_core PRIVATE /W4)
    target_compile_options(di_plot_items PRIVATE /W4)
else()
    target_compile_options(DataInspector PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(di_app_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(di_plot_items PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(DataInspector PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${DATAINSPECTOR_BIN_DIR}"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(performance_benchmark tools/performance_benchmark.cpp)
    target_link_libraries(performance_benchmark PRIVATE di_app_core)

    # Registers a CTest that can find the Qt DLLs; extra args are passed to the binary.
    function(di_add_test target)
        add_test(NAME ${target}
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
                $<TARGET_FILE:${target}> ${ARGN}
        )
    endfunction()

    add_executable(dataloadworker_test
        tests/dataloadworker_test.cpp
        ${DI_QUICK_DIR}/dataloadworker.cpp
        ${DI_QUICK_DIR}/dataloadworker.h
        ${DI_QUICK_DIR}/signalmetadata.cpp
        ${DI_QUICK_DIR}/xlsxreader.cpp
    )
    target_include_directories(dataloadworker_test PRIVATE src/quick)
    target_link_libraries(dataloadworker_test PRIVATE
        Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Test plot_render_core
    )
    if(ENABLE_MAT)
        target_link_libraries(dataloadworker_test PRIVATE di_mat)
    endif()
    di_add_test(dataloadworker_test)

    add_executable(rendercore_test tests/rendercore_test.cpp)
    target_link_libraries(rendercore_test PRIVATE plot_render_core Qt6::Test)
    di_add_test(rendercore_test)

    add_executable(plotitem_lod_test tests/plotitem_lod_test.cpp)
    target_include_directories(plotitem_lod_test PRIVATE src/quick)
    target_link_libraries(plotitem_lod_test PRIVATE di_plot_items)
    di_add_test(plotitem_lod_test -platform offscreen)

    add_executable(signalselection_test tests/signalselection_test.cpp ${DI_SIGNAL_SOURCES})
    target_include_directories(signalselection_test PRIVATE src/quick)
    target_link_libraries(signalselection_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    di_add_test(signalselection_test)

    add_executable(appcontroller_test tests/appcontroller_test.cpp)
    target_include_directories(appcontroller_test PRIVATE src/quick)
    target_link_libraries(appcontroller_test PRIVATE
        Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Quick Qt6::Test di_app_core
    )
    di_add_icon_resources(appcontroller_test)
    # The QML tests load qml/*.qml from the source tree; sibling components
    # resolve through the module qmldir, which prefers the qrc copy.
    qt_add_resources(appcontroller_test appcontroller_test_qml
        PREFIX "/qt/qml/DataInspector"
        BASE qml
        FILES ${DI_QML_FILES}
    )
    if(ENABLE_MAT)
        target_link_libraries(appcontroller_test PRIVATE di_mat)
    endif()
    di_add_test(appcontroller_test -platform offscreen -o -,txt)

    add_executable(session_test tests/session_test.cpp)
    target_include_directories(session_test PRIVATE src/quick)
    target_link_libraries(session_test PRIVATE
        Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Quick Qt6::Test di_app_core)
    di_add_icon_resources(session_test)
    qt_add_resources(session_test session_test_qml
        PREFIX "/qt/qml/DataInspector" BASE qml FILES ${DI_QML_FILES})
    if(ENABLE_MAT)
        target_link_libraries(session_test PRIVATE di_mat)
    endif()
    di_add_test(session_test -platform offscreen)

    add_executable(matwriter_test tests/matwriter_test.cpp
        ${DI_MAT_SOURCES} ${DI_QUICK_DIR}/exportvalidation.cpp)
    target_include_directories(matwriter_test PRIVATE src/quick)
    target_link_libraries(matwriter_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test plot_render_core)
    if(ENABLE_MAT)
        target_link_libraries(matwriter_test PRIVATE di_mat)
    endif()
    di_add_test(matwriter_test)

    add_executable(xlsxwriter_test tests/xlsxwriter_test.cpp ${DI_XLSX_SOURCES})
    target_include_directories(xlsxwriter_test PRIVATE src/quick)
    target_link_libraries(xlsxwriter_test PRIVATE
        Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Test plot_render_core
    )
    di_add_test(xlsxwriter_test)

    add_executable(startupfiles_test tests/startupfiles_test.cpp)
    target_include_directories(startupfiles_test PRIVATE src/quick)
    target_link_libraries(startupfiles_test PRIVATE
        Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Quick Qt6::Test di_app_core
    )
    if(ENABLE_MAT)
        target_link_libraries(startupfiles_test PRIVATE di_mat)
    endif()
    di_add_test(startupfiles_test -platform offscreen)

    add_executable(startupdrop_test tests/startupdrop_test.cpp)
    target_include_directories(startupdrop_test PRIVATE src/quick)
    target_link_libraries(startupdrop_test PRIVATE
        Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Quick Qt6::Test di_app_core
    )
    di_add_icon_resources(startupdrop_test)
    qt_add_resources(startupdrop_test startupdrop_test_qml
        PREFIX "/qt/qml/DataInspector" BASE qml FILES ${DI_QML_FILES})
    if(ENABLE_MAT)
        target_link_libraries(startupdrop_test PRIVATE di_mat)
    endif()
    di_add_test(startupdrop_test -platform offscreen)

    add_executable(trajectory_test tests/trajectory_test.cpp)
    target_include_directories(trajectory_test PRIVATE src/quick)
    target_link_libraries(trajectory_test PRIVATE Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Quick Qt6::Test di_app_core)
    di_add_icon_resources(trajectory_test)
    # Exercise the same qmlcachegen bindings as the application, as well as source QML.
    qt_add_qml_module(trajectory_test
        URI DataInspectorTrajectoryTest
        VERSION 1.0
        OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/trajectory_test_qml"
        IMPORT_PATH "${QT_QML_OUTPUT_DIRECTORY}"
        QML_FILES ${DI_QML_FILES})
    if(ENABLE_MAT)
        target_link_libraries(trajectory_test PRIVATE di_mat)
    endif()
    di_add_test(trajectory_test -platform offscreen -o -,txt)

    add_executable(trajectory_raster_test tests/trajectory_raster_test.cpp
    )
    target_link_libraries(trajectory_raster_test PRIVATE di_plot_items)
    add_executable(plotitem_raster_test tests/plotitem_raster_test.cpp)
    target_link_libraries(plotitem_raster_test PRIVATE di_plot_items)
    add_executable(plotitem_blend_test tests/plotitem_blend_test.cpp)
    target_link_libraries(plotitem_blend_test PRIVATE di_plot_items)
    foreach(di_controller_test appcontroller_test session_test startupfiles_test startupdrop_test trajectory_test)
        target_sources(${di_controller_test} PRIVATE ${DI_QUICK_DIR}/qmltypes.h)
    endforeach()
    if(ENABLE_GPU_TESTS)
        di_add_test(trajectory_raster_test)
        set_tests_properties(trajectory_raster_test PROPERTIES LABELS gpu SKIP_RETURN_CODE 77)
        add_test(NAME plotitem_raster_test COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            "QT_FORCE_STDERR_LOGGING=1" $<TARGET_FILE:plotitem_raster_test>)
        set_tests_properties(plotitem_raster_test PROPERTIES LABELS gpu SKIP_RETURN_CODE 77)
        add_test(NAME plotitem_dense_raster_test COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            "QT_FORCE_STDERR_LOGGING=1" $<TARGET_FILE:plotitem_raster_test> --dense)
        set_tests_properties(plotitem_dense_raster_test PROPERTIES LABELS gpu SKIP_RETURN_CODE 77)
        add_test(NAME plotitem_compressed_raster_test COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            "QT_FORCE_STDERR_LOGGING=1" $<TARGET_FILE:plotitem_raster_test> --dense --compressed)
        set_tests_properties(plotitem_compressed_raster_test PROPERTIES LABELS gpu SKIP_RETURN_CODE 77)
        add_test(NAME plotitem_blend_test COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            "QT_FORCE_STDERR_LOGGING=1" $<TARGET_FILE:plotitem_blend_test>)
        set_tests_properties(plotitem_blend_test PROPERTIES LABELS gpu SKIP_RETURN_CODE 77)
    endif()
endif()
