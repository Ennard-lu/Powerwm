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

add_library(powerwm_core STATIC
    src/base/fatal.cpp
    src/platform/drm/device.cpp
    src/platform/drm/lookup.cpp
)

target_compile_options(powerwm_core PUBLIC -freflection)
target_include_directories(powerwm_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${POWERWM_GENERATED_DIR}
)
target_link_libraries(powerwm_core PUBLIC Libdrm::Libdrm)

add_executable(powerwm src/app/main.cpp)
target_link_libraries(powerwm PRIVATE powerwm_core)

if(POWERWM_BUILD_BRINGUP_TOOL)
    add_executable(powerwm_drm_bringup tools/drm_bringup.cpp)
    target_link_libraries(powerwm_drm_bringup PRIVATE powerwm_core)
endif()

if(BUILD_TESTING)
    add_executable(powerwm_tests tests/unit/test_smoke.cpp)
    target_link_libraries(powerwm_tests PRIVATE powerwm_core)
    add_test(NAME unit.smoke COMMAND powerwm_tests)
endif()