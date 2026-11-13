# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(riley-shot VERSION 0.1.0 LANGUAGES CXX)

get_filename_component(_rs_source "${CMAKE_SOURCE_DIR}" REALPATH)
get_filename_component(_rs_binary "${CMAKE_BINARY_DIR}" REALPATH)
cmake_path(IS_PREFIX _rs_source "${_rs_binary}" NORMALIZE _rs_in_source)
if(_rs_in_source)
    message(FATAL_ERROR "riley-shot is never built inside its source tree; pass -B with a directory outside it "
                        "(for example /srv/devops/forge/build/riley-shot/<label>)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
# Every program (riley-shot, the tests, the gate's helpers) lands at the top of the build folder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Geometry compares doubles with hand-worked values: no fused multiply-add contraction, no fast maths.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -ffp-contract=off)
endif()

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick Test)
find_package(LayerShellQt 6.8 REQUIRED)
# libxml2 (MIT) validates config.xml against data/config.xsd.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0>=2.14)
qt_standard_project_setup(REQUIRES 6.11)

# ---------------------------------------------------------------------------------------------------------------
# The capture core: no UI. Builder A owns output/region/window/compose (.cpp), builder B grab/config/theme/deliver/
# cli (.cpp); the headers are the M1 contract (tests/run-capture.sh).
add_library(rileyshot_core STATIC
    src/core/output.h   src/core/output.cpp
    src/core/region.h   src/core/region.cpp
    src/core/window.h   src/core/window.cpp
    src/core/compose.h  src/core/compose.cpp
    src/core/grab.h     src/core/grab.cpp
    src/core/config.h   src/core/config.cpp
    src/core/theme.h    src/core/theme.cpp
    src/core/deliver.h  src/core/deliver.cpp
    src/core/cli.h      src/core/cli.cpp
    # M2: the overlay's logic without UI (builder A).
    src/core/selection.h src/core/selection.cpp)
target_include_directories(rileyshot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
target_link_libraries(rileyshot_core PUBLIC Qt6::Core Qt6::Gui PRIVATE PkgConfig::LIBXML2)
# The schema travels inside the binary as :/riley-shot/config.xsd, so an installed riley-shot needs no data files.
qt_add_resources(rileyshot_core "rileyshot_schema"
    PREFIX "/riley-shot"
    BASE data
    FILES data/config.xsd)

# ---------------------------------------------------------------------------------------------------------------
# The capture overlay (M2): QML module RileyShot, compiled into a static library so the installed riley-shot needs no
# QML files on disk. Builder A owns the C++ (capturecontroller, overlaysurface, session), builder B the QML.
# Programs that load the QML link rileyshot_uiplugin and say Q_IMPORT_QML_PLUGIN(RileyShotPlugin).
qt_add_library(rileyshot_ui STATIC)
qt_add_qml_module(rileyshot_ui
    URI RileyShot
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    SOURCES
        src/app/capturecontroller.h src/app/capturecontroller.cpp
        src/app/overlaysurface.h    src/app/overlaysurface.cpp
        src/app/session.h           src/app/session.cpp
        # M3, the editor. Builder A: the document model, renderer, undo, save and copy (src/editor). Builder B: the
        # window, the canvas item and the QML (src/app/editorwindow.*, src/app/documentcanvas.*, src/qml/Editor*.qml,
        # ColourPicker.qml, TextEntry.qml), and the wiring in session.cpp, main.cpp and cli.cpp. The headers are the
        # M3 contract (tests/run-editor.sh). A builder adds a helper file only to the list of its own files.
        src/editor/annotation.h
        src/editor/document.h       src/editor/document.cpp
        src/editor/render.h         src/editor/render.cpp
        src/app/editorwindow.h      src/app/editorwindow.cpp
        src/app/documentcanvas.h    src/app/documentcanvas.cpp
    QML_FILES
        src/qml/Overlay.qml
        src/qml/CaptureBar.qml
        src/qml/SelectionFrame.qml
        src/qml/BarButton.qml
        src/qml/Editor.qml
        src/qml/EditorToolbar.qml
        src/qml/ColourPicker.qml
        src/qml/TextEntry.qml)
target_include_directories(rileyshot_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/app" "${CMAKE_CURRENT_SOURCE_DIR}/src/editor")
target_link_libraries(rileyshot_ui PUBLIC rileyshot_core Qt6::Gui Qt6::Qml Qt6::Quick LayerShellQt::Interface)

# The program.
qt_add_executable(riley-shot src/app/main.cpp)
target_link_libraries(riley-shot PRIVATE rileyshot_core rileyshot_ui rileyshot_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick
    LayerShellQt::Interface)
install(TARGETS riley-shot RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------------------------------------------
# Tests (the gate writer's). Each is a Qt Test program the gate runs by name.
enable_testing()
function(rileyshot_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE rileyshot_core Qt6::Test Qt6::Gui)
    target_compile_definitions(${name} PRIVATE
        RILEYSHOT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        RILEYSHOT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

rileyshot_test(tst_geometry)
rileyshot_test(tst_grab)
rileyshot_test(tst_config)
rileyshot_test(tst_theme)
rileyshot_test(tst_deliver)
rileyshot_test(tst_cli)

# M2 (tests/run-overlay.sh).
rileyshot_test(tst_selection)
rileyshot_test(tst_controller)
target_link_libraries(tst_controller PRIVATE rileyshot_ui)
rileyshot_test(tst_overlay_qml)
target_link_libraries(tst_overlay_qml PRIVATE rileyshot_ui rileyshot_uiplugin Qt6::Quick)
set_tests_properties(tst_overlay_qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

# M3 (tests/run-editor.sh), offscreen.
rileyshot_test(tst_document)
target_link_libraries(tst_document PRIVATE rileyshot_ui)
rileyshot_test(tst_editor_qml)
target_link_libraries(tst_editor_qml PRIVATE rileyshot_ui rileyshot_uiplugin Qt6::Quick)
set_tests_properties(tst_editor_qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
rileyshot_test(tst_editor_cli)

# The live part's helpers: the test window that draws a known pattern on DP-2, and its checker.
add_executable(rileyshot-testpattern tests/live/pattern.cpp tests/live/pattern.h)
target_link_libraries(rileyshot-testpattern PRIVATE Qt6::Gui LayerShellQt::Interface)
add_executable(rileyshot-checkpattern tests/live/checkpattern.cpp tests/live/pattern.h)
target_link_libraries(rileyshot-checkpattern PRIVATE Qt6::Gui)
# M2: compares a grab of DP-2 under the overlay with the grab before it, outside the capture bar.
add_executable(rileyshot-comparegrab tests/live/comparegrab.cpp)
target_link_libraries(rileyshot-comparegrab PRIVATE Qt6::Gui)
