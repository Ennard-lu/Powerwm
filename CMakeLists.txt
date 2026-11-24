# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 26)

project(powerwm VERSION 0.0.1 LANGUAGES CXX)

add_compile_options(-fno-exceptions)

include(CTest)
option(POWERWM_BUILD_BRINGUP_TOOL "Build the DRM bring-up tool" ON)
include(cmake/PowerWMBuildInfo.cmake)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)
find_package(Libdrm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Libinput REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(Libudev REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(Libseat REQUIRED IMPORTED_TARGET libseat)

add_library(powerwm_core STATIC
    src/base/fatal.cpp
    src/platform/drm/device.cpp
    src/platform/event/event_fd.cpp
    src/platform/event/event_loop.cpp
    src/platform/event/poll.cpp
    src/platform/event/timer_fd.cpp
    src/platform/drm/lookup.cpp
)

target_compile_options(powerwm_core PUBLIC -freflection)
target_include_directories(powerwm_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${POWERWM_GENERATED_DIR}
)
target_link_libraries(powerwm_core PUBLIC Libdrm::Libdrm)

add_library(powerwm_input STATIC
    src/platform/input/context.cpp
)
target_link_libraries(powerwm_input PUBLIC
    powerwm_core
    PkgConfig::Libinput
    PkgConfig::Libudev
)

add_library(powerwm_seat STATIC
    src/platform/seat/seat.cpp
)
target_link_libraries(powerwm_seat PUBLIC
    powerwm_core
    PkgConfig::Libseat
)

add_executable(powerwm src/app/main.cpp)
target_link_libraries(powerwm PRIVATE powerwm_core)

if(POWERWM_BUILD_BRINGUP_TOOL)
    add_executable(powerwm_drm_bringup tools/drm_bringup.cpp)
    target_link_libraries(powerwm_drm_bringup PRIVATE powerwm_seat powerwm_input)

    add_executable(powerwm_input_probe tools/input_probe.cpp)
    target_link_libraries(powerwm_input_probe PRIVATE powerwm_input)

    add_executable(powerwm_seat_probe tools/seat_probe.cpp)
    target_link_libraries(powerwm_seat_probe PRIVATE powerwm_seat)
endif()

if(BUILD_TESTING)
    add_executable(powerwm_tests tests/unit/test_smoke.cpp)
    target_link_libraries(powerwm_tests PRIVATE powerwm_core)
    add_test(NAME unit.smoke COMMAND powerwm_tests)

    add_executable(powerwm_event_tests tests/unit/test_event_loop.cpp)
    target_link_libraries(powerwm_event_tests PRIVATE powerwm_core)
    add_test(NAME unit.event_loop COMMAND powerwm_event_tests)
endif()