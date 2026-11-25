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
find_package(Vulkan REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(POWERWM_SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(POWERWM_SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${POWERWM_SHADER_BINARY_DIR}")

set(POWERWM_SHADER_OUTPUTS "")
foreach(SHADER triangle.vert triangle.frag scene.vert scene.frag)
    set(SHADER_INPUT "${POWERWM_SHADER_SOURCE_DIR}/${SHADER}")
    set(SHADER_OUTPUT "${POWERWM_SHADER_BINARY_DIR}/${SHADER}.spv")
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT}"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.3
                -o "${SHADER_OUTPUT}" "${SHADER_INPUT}"
        DEPENDS "${SHADER_INPUT}"
        COMMENT "Compiling ${SHADER}"
        VERBATIM
    )
    list(APPEND POWERWM_SHADER_OUTPUTS "${SHADER_OUTPUT}")
endforeach()
add_custom_target(powerwm_shaders DEPENDS ${POWERWM_SHADER_OUTPUTS})
pkg_check_modules(Libinput REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(Libudev REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(Libseat REQUIRED IMPORTED_TARGET libseat)

add_library(powerwm_core STATIC
    src/base/fatal.cpp
    src/base/geometry.cpp
    src/scene/scene.cpp
    src/platform/drm/device.cpp
    src/platform/event/event_fd.cpp
    src/platform/event/event_loop.cpp
    src/platform/event/poll.cpp
    src/platform/event/timer_fd.cpp
    src/platform/drm/lookup.cpp
    src/platform/headless/output.cpp
)

target_compile_options(powerwm_core PUBLIC -freflection)
target_include_directories(powerwm_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${POWERWM_GENERATED_DIR}
)
target_link_libraries(powerwm_core PUBLIC Libdrm::Libdrm)

add_library(powerwm_protocol_scan STATIC
    src/wayland/xml.cpp
    src/wayland/protocol.cpp
)
target_link_libraries(powerwm_protocol_scan PUBLIC powerwm_core)

add_executable(powerwm_protocol_scanner tools/protocol_scanner.cpp)
target_link_libraries(powerwm_protocol_scanner PRIVATE powerwm_protocol_scan)

set(POWERWM_GENERATED_PROTOCOL_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${POWERWM_GENERATED_PROTOCOL_DIR}")
set(POWERWM_PROTOCOL_OUTPUTS "")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/wayland_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/wayland_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/wayland.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/wayland_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/wayland_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/wayland.xml"
    COMMENT "Generating Wayland core bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/wayland_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/wayland_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_shell_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_shell_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/xdg-shell.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_shell_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_shell_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/xdg-shell.xml"
    COMMENT "Generating xdg-shell bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_shell_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_shell_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_output_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_output_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/xdg-output-unstable-v1.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_output_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_output_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/xdg-output-unstable-v1.xml"
    COMMENT "Generating xdg-output bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_output_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_output_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/viewporter_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/viewporter_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/viewporter.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/viewporter_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/viewporter_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/viewporter.xml"
    COMMENT "Generating viewporter bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/viewporter_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/viewporter_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/fractional_scale_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/fractional_scale_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/fractional-scale-v1.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/fractional_scale_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/fractional_scale_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/fractional-scale-v1.xml"
    COMMENT "Generating fractional-scale bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/fractional_scale_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/fractional_scale_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_decoration_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_decoration_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/xdg-decoration-unstable-v1.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_decoration_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_decoration_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/xdg-decoration-unstable-v1.xml"
    COMMENT "Generating xdg-decoration bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_decoration_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_decoration_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/presentation_time_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/presentation_time_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/presentation-time.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/presentation_time_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/presentation_time_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/presentation-time.xml"
    COMMENT "Generating presentation-time bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/presentation_time_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/presentation_time_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_activation_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_activation_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/xdg-activation-v1.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_activation_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_activation_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/xdg-activation-v1.xml"
    COMMENT "Generating xdg-activation bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_activation_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_activation_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/linux_dmabuf_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/linux_dmabuf_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/linux-dmabuf-v1.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/linux_dmabuf_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/linux_dmabuf_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/linux-dmabuf-v1.xml"
    COMMENT "Generating linux-dmabuf bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/linux_dmabuf_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/linux_dmabuf_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/layer_shell_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/layer_shell_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/layer_shell_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/layer_shell_bindings.cpp"
    DEPENDS powerwm_protocol_scanner "${CMAKE_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml"
    COMMENT "Generating layer-shell bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/layer_shell_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/layer_shell_bindings.cpp")

add_custom_command(
    OUTPUT
        "${POWERWM_GENERATED_PROTOCOL_DIR}/primary_selection_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/primary_selection_bindings.cpp"
    COMMAND $<TARGET_FILE:powerwm_protocol_scanner>
        "${CMAKE_SOURCE_DIR}/protocols/primary-selection-unstable-v1.xml"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/primary_selection_bindings.hpp"
        "${POWERWM_GENERATED_PROTOCOL_DIR}/primary_selection_bindings.cpp"
    DEPENDS powerwm_protocol_scanner
        "${CMAKE_SOURCE_DIR}/protocols/primary-selection-unstable-v1.xml"
    COMMENT "Generating primary-selection bindings"
    VERBATIM
)
list(APPEND POWERWM_PROTOCOL_OUTPUTS
    "${POWERWM_GENERATED_PROTOCOL_DIR}/primary_selection_bindings.hpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/primary_selection_bindings.cpp")

add_custom_target(powerwm_protocols DEPENDS ${POWERWM_PROTOCOL_OUTPUTS})

add_library(powerwm_wayland STATIC
    src/platform/wayland/fd.cpp
    src/platform/wayland/mapped_region.cpp
    src/platform/wayland/socket.cpp
    src/wayland/connection.cpp
    src/wayland/core.cpp
    src/wayland/display.cpp
    src/wayland/object_table.cpp
    src/wayland/surface_scene.cpp
    src/wayland/wire.cpp
    src/wayland/xdg_shell.cpp
    "${POWERWM_GENERATED_PROTOCOL_DIR}/wayland_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_shell_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_output_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/viewporter_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/fractional_scale_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_decoration_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/presentation_time_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/xdg_activation_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/linux_dmabuf_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/layer_shell_bindings.cpp"
    "${POWERWM_GENERATED_PROTOCOL_DIR}/primary_selection_bindings.cpp"
)
target_include_directories(powerwm_wayland PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    "${POWERWM_GENERATED_PROTOCOL_DIR}"
)
target_link_libraries(powerwm_wayland PUBLIC powerwm_core powerwm_protocol_scan powerwm_vulkanpp)
add_dependencies(powerwm_wayland powerwm_protocols)

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

add_library(powerwm_vulkanpp STATIC
    src/render/vulkanpp/instance.cpp
    src/render/vulkanpp/device.cpp
    src/render/vulkanpp/fence.cpp
    src/render/vulkanpp/command_pool.cpp
    src/render/vulkanpp/image.cpp
    src/render/vulkanpp/dma_buf_image.cpp
    src/render/vulkanpp/offscreen.cpp
    src/render/vulkanpp/triangle.cpp
    src/render/vulkanpp/triangle_pass.cpp
    src/render/vulkanpp/texture.cpp
    src/render/vulkanpp/scene_renderer.cpp
)
target_link_libraries(powerwm_vulkanpp PUBLIC
    powerwm_core
    Vulkan::Vulkan
)

add_library(powerwm_udev STATIC
    src/platform/udev/monitor.cpp
)
target_link_libraries(powerwm_udev PUBLIC
    powerwm_core
    PkgConfig::Libudev
)

add_executable(powerwm src/app/main.cpp)
target_link_libraries(powerwm PRIVATE powerwm_core)

if(POWERWM_BUILD_BRINGUP_TOOL)
    add_executable(powerwm_drm_bringup tools/drm_bringup.cpp)
    target_link_libraries(powerwm_drm_bringup PRIVATE powerwm_seat powerwm_input powerwm_udev)

    add_executable(powerwm_input_probe tools/input_probe.cpp)
    target_link_libraries(powerwm_input_probe PRIVATE powerwm_input)

    add_executable(powerwm_seat_probe tools/seat_probe.cpp)
    target_link_libraries(powerwm_seat_probe PRIVATE powerwm_seat)

    add_executable(powerwm_probe tools/probe.cpp)
    target_link_libraries(powerwm_probe PRIVATE powerwm_core)

    add_executable(powerwm_vulkan_probe tools/vulkan_probe.cpp)
    target_link_libraries(powerwm_vulkan_probe PRIVATE powerwm_vulkanpp)

    add_executable(powerwm_vulkan_dmabuf_probe tools/vulkan_dmabuf_probe.cpp)
    target_link_libraries(powerwm_vulkan_dmabuf_probe PRIVATE powerwm_vulkanpp)

    add_executable(powerwm_vulkan_kms_demo tools/vulkan_kms_demo.cpp)
    target_link_libraries(powerwm_vulkan_kms_demo PRIVATE powerwm_vulkanpp)
    target_compile_definitions(powerwm_vulkan_kms_demo PRIVATE
        POWERWM_SHADER_DIR="${POWERWM_SHADER_BINARY_DIR}"
    )
    add_dependencies(powerwm_vulkan_kms_demo powerwm_shaders)

    add_executable(powerwm_render_benchmark tools/render_benchmark.cpp)
    target_link_libraries(powerwm_render_benchmark PRIVATE powerwm_vulkanpp)
    target_compile_definitions(powerwm_render_benchmark PRIVATE
        POWERWM_SHADER_DIR="${POWERWM_SHADER_BINARY_DIR}"
    )
    add_dependencies(powerwm_render_benchmark powerwm_shaders)

    add_executable(powerwm_wayland_kms_compositor tools/wayland_kms_compositor.cpp)
    target_link_libraries(powerwm_wayland_kms_compositor PRIVATE powerwm_wayland powerwm_seat powerwm_input)
    target_compile_definitions(powerwm_wayland_kms_compositor PRIVATE
        POWERWM_SHADER_DIR="${POWERWM_SHADER_BINARY_DIR}"
    )
    add_dependencies(powerwm_wayland_kms_compositor powerwm_shaders)

    add_executable(powerwm_wayland_server tools/wayland_server.cpp)
    target_link_libraries(powerwm_wayland_server PRIVATE powerwm_wayland)

    add_executable(powerwm_vulkan_compositor tools/vulkan_compositor.cpp)
    target_link_libraries(powerwm_vulkan_compositor PRIVATE powerwm_vulkanpp powerwm_seat)
    target_compile_definitions(powerwm_vulkan_compositor PRIVATE
        POWERWM_SHADER_DIR="${POWERWM_SHADER_BINARY_DIR}"
    )
    add_dependencies(powerwm_vulkan_compositor powerwm_shaders)

    add_executable(powerwm_vulkan_pipeline_probe tools/vulkan_pipeline_probe.cpp)
    target_link_libraries(powerwm_vulkan_pipeline_probe PRIVATE powerwm_vulkanpp)
    target_compile_definitions(powerwm_vulkan_pipeline_probe PRIVATE
        POWERWM_SHADER_DIR="${POWERWM_SHADER_BINARY_DIR}"
    )
    add_dependencies(powerwm_vulkan_pipeline_probe powerwm_shaders)
endif()

if(BUILD_TESTING)
    add_executable(powerwm_tests tests/unit/test_smoke.cpp)
    target_link_libraries(powerwm_tests PRIVATE powerwm_core)
    add_test(NAME unit.smoke COMMAND powerwm_tests)

    add_executable(powerwm_event_tests tests/unit/test_event_loop.cpp)
    target_link_libraries(powerwm_event_tests PRIVATE powerwm_core)
    add_test(NAME unit.event_loop COMMAND powerwm_event_tests)

    add_executable(powerwm_headless_tests tests/unit/test_headless.cpp)
    target_link_libraries(powerwm_headless_tests PRIVATE powerwm_core)
    add_test(NAME unit.headless COMMAND powerwm_headless_tests)

    add_executable(powerwm_geometry_tests tests/unit/test_geometry.cpp)
    target_link_libraries(powerwm_geometry_tests PRIVATE powerwm_core)
    add_test(NAME unit.geometry COMMAND powerwm_geometry_tests)

    add_executable(powerwm_scene_tests tests/unit/test_scene.cpp)
    target_link_libraries(powerwm_scene_tests PRIVATE powerwm_core)
    add_test(NAME unit.scene COMMAND powerwm_scene_tests)

    add_executable(powerwm_render_tests tests/unit/test_render.cpp)
    target_link_libraries(powerwm_render_tests PRIVATE powerwm_vulkanpp powerwm_wayland)
    target_compile_definitions(powerwm_render_tests PRIVATE
        POWERWM_SHADER_DIR="${POWERWM_SHADER_BINARY_DIR}"
    )
    add_dependencies(powerwm_render_tests powerwm_shaders)
    add_test(NAME unit.render COMMAND powerwm_render_tests)
    set_tests_properties(unit.render PROPERTIES
        SKIP_RETURN_CODE 77
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:strict_string_checks=1"
    )

    add_executable(powerwm_wayland_tests tests/unit/test_wayland.cpp)
    target_link_libraries(powerwm_wayland_tests PRIVATE powerwm_wayland)
    add_test(NAME unit.wayland COMMAND powerwm_wayland_tests)
endif()
