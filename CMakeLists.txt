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
    src/core/cli.h      src/core/cli.cpp)
target_include_directories(rileyshot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
target_link_libraries(rileyshot_core PUBLIC Qt6::Core Qt6::Gui PRIVATE PkgConfig::LIBXML2)
# The schema travels inside the binary as :/riley-shot/config.xsd, so an installed riley-shot needs no data files.
qt_add_resources(rileyshot_core "rileyshot_schema"
    PREFIX "/riley-shot"
    BASE data
    FILES data/config.xsd)

# ---------------------------------------------------------------------------------------------------------------
# The program. QML (from M2 on) is compiled into it with qt_add_qml_module.
qt_add_executable(riley-shot src/app/main.cpp)
target_link_libraries(riley-shot PRIVATE rileyshot_core Qt6::Gui Qt6::Qml Qt6::Quick LayerShellQt::Interface)
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

# The live part's helpers: the test window that draws a known pattern on DP-2, and its checker.
add_executable(rileyshot-testpattern tests/live/pattern.cpp tests/live/pattern.h)
target_link_libraries(rileyshot-testpattern PRIVATE Qt6::Gui LayerShellQt::Interface)
add_executable(rileyshot-checkpattern tests/live/checkpattern.cpp tests/live/pattern.h)
target_link_libraries(rileyshot-checkpattern PRIVATE Qt6::Gui)
