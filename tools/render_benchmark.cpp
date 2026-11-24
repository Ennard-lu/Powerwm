// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/instance.hpp>
#include <render/vulkanpp/offscreen.hpp>
#include <render/vulkanpp/scene_renderer.hpp>
#include <render/vulkanpp/texture.hpp>
#include <scene/scene.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <print>
#include <span>
#include <string>
#include <vector>

namespace {

namespace vk = pwm::vulkanpp;
namespace render = pwm::render;
namespace scene = pwm::scene;

constexpr pwm::u32 width_px = 1920;
constexpr pwm::u32 height_px = 1080;
constexpr pwm::usize surface_count = 100;
constexpr pwm::u32 frame_count = 120;

auto load_spirv(const std::string& path) -> pwm::result<std::vector<std::byte>> {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "cannot open SPIR-V file"));
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "empty SPIR-V file"));
    }
    std::vector<std::byte> data(static_cast<pwm::usize>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "cannot read SPIR-V file"));
    }
    return data;
}

auto make_texture(const vk::device& device) -> pwm::result<vk::texture> {
    std::vector<std::byte> pixels(256 * 256 * 4);
    for (pwm::u32 y = 0; y < 256; ++y) {
        for (pwm::u32 x = 0; x < 256; ++x) {
            const bool light = ((x / 32) + (y / 32)) % 2 == 0;
            const pwm::usize offset = (static_cast<pwm::usize>(y) * 256 + x) * 4;
            const pwm::u8 value = light ? 0xCC : 0x66;
            pixels[offset] = static_cast<std::byte>(value);
            pixels[offset + 1] = static_cast<std::byte>(value);
            pixels[offset + 2] = static_cast<std::byte>(value);
            pixels[offset + 3] = std::byte{0xFF};
        }
    }
    return vk::create_texture(device, 256, 256, pixels);
}

struct benchmark_result {
    pwm::f64 average_ms = 0.0;
    pwm::f64 maximum_ms = 0.0;
};

auto build_benchmark_scene(scene::scene& graph, scene::node& background,
                           std::array<scene::node, surface_count>& surfaces) -> void {
    background = scene::node{1};
    background.set_bounds({0, 0, width_px, height_px});
    background.set_style({.fill = {.red = 0.02F, .green = 0.02F, .blue = 0.03F, .alpha = 1.0F}});
    graph.root().add_child(background);

    for (pwm::usize index = 0; index < surfaces.size(); ++index) {
        auto& item = surfaces[index];
        item = scene::node{static_cast<scene::node_id>(index + 2)};
        const pwm::i32 column = static_cast<pwm::i32>(index % 10);
        const pwm::i32 row = static_cast<pwm::i32>(index / 10);
        item.set_bounds({column * 190, row * 106, 256, 256});
        item.set_style({.fill = {.red = 0.20F, .green = 0.45F, .blue = 0.75F, .alpha = 1.0F},
                        .shadow = {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.4F},
                        .texture = 1,
                        .corner_radius = 12.0F,
                        .shadow_radius = 8.0F,
                        .shadow_offset_y = 4.0F});
        graph.root().add_child(item);
    }
}

auto measure_frames(render::scene_renderer& renderer, vk::offscreen_target& target,
                    scene::scene& graph, std::span<const render::texture_binding> bindings,
                    std::array<scene::node, surface_count>& surfaces)
    -> pwm::result<benchmark_result> {
    const vk::color clear{.red = 0.02F, .green = 0.02F, .blue = 0.03F, .alpha = 1.0F};
    benchmark_result result;
    for (pwm::u32 frame = 0; frame < frame_count; ++frame) {
        for (pwm::usize index = 0; index < surfaces.size(); ++index) {
            const pwm::i32 column = static_cast<pwm::i32>(index % 10);
            const pwm::i32 row = static_cast<pwm::i32>(index / 10);
            const pwm::i32 offset = static_cast<pwm::i32>((frame + index) % 16);
            surfaces[index].set_bounds({column * 190 + offset, row * 106, 256, 256});
        }
        auto rendered = renderer.render(target.native_image(), width_px, height_px, graph.root(),
                                        bindings, {}, clear);
        if (!rendered) {
            return std::unexpected(rendered.error());
        }
        const pwm::f64 elapsed = renderer.statistics().cpu_ms;
        result.average_ms += elapsed;
        result.maximum_ms = elapsed > result.maximum_ms ? elapsed : result.maximum_ms;
    }
    result.average_ms /= static_cast<pwm::f64>(frame_count);
    return result;
}

auto run_benchmark() -> int {
    const vk::application_info application{
        .name = "powerwm_render_benchmark",
        .version = 1,
        .engine_name = "powerwm",
        .engine_version = 1,
        .api_version = 0,
    };
    auto instance_result = vk::instance::create(application);
    if (!instance_result) {
        std::println(stderr, "Vulkan instance unavailable: {}", instance_result.error().message);
        return 77;
    }
    auto instance = pwm::raii{std::move(*instance_result)};
    auto device_result = vk::device::create(instance.value());
    if (!device_result) {
        std::println(stderr, "Vulkan device unavailable: {}", device_result.error().message);
        return 77;
    }
    auto device = pwm::raii{std::move(*device_result)};

    auto target_result = vk::create_offscreen_target(device.value(), width_px, height_px);
    if (!target_result) {
        std::println(stderr, "offscreen target unavailable: {}", target_result.error().message);
        return EXIT_FAILURE;
    }
    auto target = pwm::raii{std::move(*target_result)};

    auto texture_result = make_texture(device.value());
    if (!texture_result) {
        std::println(stderr, "texture unavailable: {}", texture_result.error().message);
        return EXIT_FAILURE;
    }
    auto texture = pwm::raii{std::move(*texture_result)};

    auto vertex = load_spirv(std::string{POWERWM_SHADER_DIR} + "/scene.vert.spv");
    auto fragment = load_spirv(std::string{POWERWM_SHADER_DIR} + "/scene.frag.spv");
    if (!vertex || !fragment) {
        std::println(stderr, "shader unavailable");
        return EXIT_FAILURE;
    }
    auto renderer_result = render::scene_renderer::create(
        device.value(), render::target_format::rgba8, *vertex, *fragment);
    if (!renderer_result) {
        std::println(stderr, "renderer unavailable: {}", renderer_result.error().message);
        return EXIT_FAILURE;
    }
    auto renderer = pwm::raii{std::move(*renderer_result)};

    scene::scene graph;
    scene::node background;
    std::array<scene::node, surface_count> surfaces;
    build_benchmark_scene(graph, background, surfaces);

    const std::array<render::texture_binding, 1> bindings{{
        {.id = 1, .texture = &texture.value()},
    }};
    auto measured = measure_frames(renderer.value(), target.value(), graph, bindings, surfaces);
    if (!measured) {
        std::println(stderr, "render failed: {}", measured.error().message);
        return EXIT_FAILURE;
    }

    std::println("{} surfaces {}x{}: average {:.3f} ms, maximum {:.3f} ms, budget {:.2f} ms",
                 surface_count, width_px, height_px, measured->average_ms, measured->maximum_ms,
                 1000.0 / 60.0);
    return measured->average_ms <= 1000.0 / 60.0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}

int main() {
    return run_benchmark();
}
