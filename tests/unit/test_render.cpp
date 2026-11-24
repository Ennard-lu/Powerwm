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
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <new>
#include <print>
#include <span>
#include <string>
#include <vector>

namespace {

std::atomic<pwm::usize> live_allocations{0};

}

auto operator new(pwm::usize size) -> void* {
    void* memory = std::malloc(size);
    if (memory == nullptr) {
        std::abort();
    }
    live_allocations.fetch_add(1, std::memory_order_relaxed);
    return memory;
}

auto operator delete(void* memory) noexcept -> void {
    if (memory != nullptr) {
        live_allocations.fetch_sub(1, std::memory_order_relaxed);
        std::free(memory);
    }
}

auto operator new[](pwm::usize size) -> void* {
    return ::operator new(size);
}

auto operator delete[](void* memory) noexcept -> void {
    ::operator delete(memory);
}

auto operator delete(void* memory, pwm::usize) noexcept -> void {
    ::operator delete(memory);
}

auto operator delete[](void* memory, pwm::usize) noexcept -> void {
    ::operator delete(memory);
}

namespace {

namespace vk = pwm::vulkanpp;
namespace scene = pwm::scene;
namespace render = pwm::render;

constexpr pwm::u32 width_px = 320;
constexpr pwm::u32 height_px = 180;

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

auto expect_near(const vk::color& value, const vk::color& expected, float tolerance = 0.02F)
    -> bool {
    return std::fabs(value.red - expected.red) <= tolerance &&
           std::fabs(value.green - expected.green) <= tolerance &&
           std::fabs(value.blue - expected.blue) <= tolerance;
}

auto check_pixel(const vk::offscreen_target& target, pwm::u32 x, pwm::u32 y,
                 const vk::color& expected, const char* label) -> bool {
    const auto actual = target.read_pixel(x, y);
    if (!actual) {
        std::println(stderr, "read_pixel failed for {}", label);
        return false;
    }
    if (!expect_near(*actual, expected)) {
        std::println(stderr, "{} at ({}, {}): got ({:.3f}, {:.3f}, {:.3f})", label, x, y,
                     actual->red, actual->green, actual->blue);
        return false;
    }
    return true;
}

auto make_solid_texture(const vk::device& device, pwm::u32 size, vk::color value)
    -> pwm::result<vk::texture> {
    std::vector<std::byte> pixels(static_cast<pwm::usize>(size) * size * 4);
    for (pwm::usize index = 0; index < static_cast<pwm::usize>(size) * size; ++index) {
        pixels[index * 4] = static_cast<std::byte>(value.red * 255.0F);
        pixels[index * 4 + 1] = static_cast<std::byte>(value.green * 255.0F);
        pixels[index * 4 + 2] = static_cast<std::byte>(value.blue * 255.0F);
        pixels[index * 4 + 3] = static_cast<std::byte>(value.alpha * 255.0F);
    }
    return vk::create_texture(device, size, size, pixels);
}

struct fixture {
    pwm::raii<vk::instance> instance;
    pwm::raii<vk::device> device;
    pwm::raii<vk::offscreen_target> target;
    pwm::raii<vk::texture> texture;
    pwm::raii<render::scene_renderer> renderer;
};

auto create_fixture(fixture& context) -> pwm::result<void> {
    const vk::application_info application{
        .name = "powerwm_render_test",
        .version = 1,
        .engine_name = "powerwm",
        .engine_version = 1,
        .api_version = 0,
    };
    auto instance_result = vk::instance::create(application);
    if (!instance_result) {
        return std::unexpected(instance_result.error());
    }
    context.instance = pwm::raii{std::move(*instance_result)};

    auto device_result = vk::device::create(context.instance.value());
    if (!device_result) {
        return std::unexpected(device_result.error());
    }
    context.device = pwm::raii{std::move(*device_result)};

    auto target_result = vk::create_offscreen_target(context.device.value(), width_px, height_px);
    if (!target_result) {
        return std::unexpected(target_result.error());
    }
    context.target = pwm::raii{std::move(*target_result)};

    auto texture_result = make_solid_texture(
        context.device.value(), 8, {.red = 0.20F, .green = 0.35F, .blue = 0.85F, .alpha = 1.0F});
    if (!texture_result) {
        return std::unexpected(texture_result.error());
    }
    context.texture = pwm::raii{std::move(*texture_result)};

    auto vertex = load_spirv(std::string{POWERWM_SHADER_DIR} + "/scene.vert.spv");
    if (!vertex) {
        return std::unexpected(vertex.error());
    }
    auto fragment = load_spirv(std::string{POWERWM_SHADER_DIR} + "/scene.frag.spv");
    if (!fragment) {
        return std::unexpected(fragment.error());
    }

    auto renderer_result = render::scene_renderer::create(
        context.device.value(), render::target_format::rgba8, *vertex, *fragment);
    if (!renderer_result) {
        return std::unexpected(renderer_result.error());
    }
    context.renderer = pwm::raii{std::move(*renderer_result)};
    return {};
}

struct golden_nodes {
    scene::node background;
    scene::node rounded;
    scene::node textured;
    scene::node shadowed;
    scene::node transformed;
    scene::node clipper;
    scene::node clipped;
};

auto build_golden_scene(golden_nodes& nodes, scene::scene& graph) -> void {
    auto& root = graph.root();

    nodes.background = scene::node{1};
    nodes.background.set_bounds({0, 0, width_px, height_px});
    nodes.background.set_style(
        {.fill = {.red = 0.05F, .green = 0.06F, .blue = 0.08F, .alpha = 1.0F}});
    root.add_child(nodes.background);

    nodes.rounded = scene::node{2};
    nodes.rounded.set_bounds({10, 10, 64, 64});
    nodes.rounded.set_style({.fill = {.red = 0.90F, .green = 0.10F, .blue = 0.12F, .alpha = 1.0F},
                             .corner_radius = 12.0F});
    root.add_child(nodes.rounded);

    nodes.textured = scene::node{3};
    nodes.textured.set_bounds({90, 10, 64, 64});
    nodes.textured.set_style(
        {.fill = {.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F}, .texture = 7});
    root.add_child(nodes.textured);

    nodes.shadowed = scene::node{4};
    nodes.shadowed.set_bounds({170, 10, 64, 64});
    nodes.shadowed.set_style({.fill = {.red = 0.95F, .green = 0.95F, .blue = 0.95F, .alpha = 1.0F},
                              .shadow = {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.8F},
                              .corner_radius = 6.0F,
                              .shadow_radius = 10.0F,
                              .shadow_offset_y = 4.0F});
    root.add_child(nodes.shadowed);

    nodes.transformed = scene::node{8};
    nodes.transformed.set_bounds({240, 80, 40, 40});
    nodes.transformed.set_style(
        {.fill = {.red = 0.95F, .green = 0.15F, .blue = 0.85F, .alpha = 1.0F},
         .rotation = 0.785398F,
         .scale = 0.7F});
    root.add_child(nodes.transformed);

    nodes.clipper = scene::node{5};
    nodes.clipper.set_bounds({0, 100, 100, 60});
    nodes.clipper.set_style({.fill = {.red = 0.10F, .green = 0.70F, .blue = 0.20F, .alpha = 1.0F},
                             .clip_children = true});
    root.add_child(nodes.clipper);

    nodes.clipped = scene::node{6};
    nodes.clipped.set_bounds({80, 100, 60, 40});
    nodes.clipped.set_style({.fill = {.red = 0.90F, .green = 0.85F, .blue = 0.10F, .alpha = 1.0F}});
    nodes.clipper.add_child(nodes.clipped);
}

auto check_golden_pixels(const vk::offscreen_target& target, const vk::color& clear) -> bool {
    bool ok = true;
    ok &= check_pixel(target, 2, 2, clear, "background");
    ok &= check_pixel(target, 40, 40, {.red = 0.90F, .green = 0.10F, .blue = 0.12F, .alpha = 1.0F},
                      "rounded fill");
    ok &= check_pixel(target, 10, 10, clear, "rounded corner");
    ok &= check_pixel(target, 120, 40, {.red = 0.20F, .green = 0.35F, .blue = 0.85F, .alpha = 1.0F},
                      "texture");
    ok &= check_pixel(target, 200, 40, {.red = 0.95F, .green = 0.95F, .blue = 0.95F, .alpha = 1.0F},
                      "shadowed fill");
    ok &=
        check_pixel(target, 260, 100, {.red = 0.95F, .green = 0.15F, .blue = 0.85F, .alpha = 1.0F},
                    "rotated center");
    ok &= check_pixel(target, 242, 82, clear, "rotated corner");
    ok &= check_pixel(target, 90, 120, {.red = 0.90F, .green = 0.85F, .blue = 0.10F, .alpha = 1.0F},
                      "clipped child");
    ok &= check_pixel(target, 110, 120, clear, "clip boundary");
    return ok;
}

auto run_golden_test() -> int {
    fixture context;
    auto fixture_result = create_fixture(context);
    if (!fixture_result) {
        std::println(stderr, "render fixture unavailable: {}", fixture_result.error().message);
        return 77;
    }

    scene::scene graph;
    golden_nodes nodes;
    build_golden_scene(nodes, graph);

    const std::array<render::texture_binding, 1> textures{{
        {.id = 7, .texture = &context.texture.value()},
    }};
    const vk::color clear{.red = 0.05F, .green = 0.06F, .blue = 0.08F, .alpha = 1.0F};

    if (auto rendered = context.renderer->render(context.target->native_image(), width_px,
                                                 height_px, graph.root(), textures, {}, clear);
        !rendered) {
        std::println(stderr, "render failed: {}", rendered.error().message);
        return EXIT_FAILURE;
    }
    const pwm::u32 full_draw_calls = context.renderer->statistics().draw_calls;
    if (auto downloaded = context.target->download(); !downloaded) {
        std::println(stderr, "download failed: {}", downloaded.error().message);
        return EXIT_FAILURE;
    }
    bool ok = check_golden_pixels(context.target.value(), clear);

    auto transaction = graph.begin();
    transaction.move(nodes.rounded, {10, 80, 64, 64});
    graph.add_damage(transaction.commit());

    if (auto rendered =
            context.renderer->render(context.target->native_image(), width_px, height_px,
                                     graph.root(), textures, graph.collect_damage(), clear);
        !rendered) {
        std::println(stderr, "partial render failed: {}", rendered.error().message);
        return EXIT_FAILURE;
    }
    const pwm::u32 partial_draw_calls = context.renderer->statistics().draw_calls;
    if (auto downloaded = context.target->download(); !downloaded) {
        std::println(stderr, "partial download failed: {}", downloaded.error().message);
        return EXIT_FAILURE;
    }

    if (partial_draw_calls >= full_draw_calls) {
        std::println(stderr, "partial redraw did not reduce draw calls: {} >= {}",
                     partial_draw_calls, full_draw_calls);
        ok = false;
    }
    ok &= check_pixel(context.target.value(), 40, 40, clear, "damaged old position");
    ok &= check_pixel(context.target.value(), 40, 90,
                      {.red = 0.90F, .green = 0.10F, .blue = 0.12F, .alpha = 1.0F},
                      "damaged new position");
    if (!ok) {
        return EXIT_FAILURE;
    }
    std::println("golden render test passed");
    return EXIT_SUCCESS;
}

auto run_allocation_test() -> int {
    fixture context;
    auto fixture_result = create_fixture(context);
    if (!fixture_result) {
        std::println(stderr, "render fixture unavailable: {}", fixture_result.error().message);
        return 77;
    }

    scene::scene graph;
    scene::node background{1};
    background.set_bounds({0, 0, width_px, height_px});
    graph.root().add_child(background);

    std::array<scene::node, 32> nodes;
    for (pwm::usize index = 0; index < nodes.size(); ++index) {
        auto& item = nodes[index];
        item = scene::node{static_cast<scene::node_id>(index + 2)};
        item.set_bounds({static_cast<pwm::i32>((index % 8) * 36),
                         static_cast<pwm::i32>((index / 8) * 36), 32, 32});
        item.set_texture(7);
        graph.root().add_child(item);
    }

    const std::array<render::texture_binding, 1> textures{{
        {.id = 7, .texture = &context.texture.value()},
    }};
    const vk::color clear{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
    const auto render_once = [&]() -> bool {
        auto rendered = context.renderer->render(context.target->native_image(), width_px,
                                                 height_px, graph.root(), textures, {}, clear);
        return static_cast<bool>(rendered);
    };

    if (!render_once()) {
        std::println(stderr, "warmup render failed");
        return EXIT_FAILURE;
    }

    const pwm::usize before = live_allocations.load(std::memory_order_relaxed);
    for (pwm::u32 frame = 0; frame < 60; ++frame) {
        if (!render_once()) {
            std::println(stderr, "allocation test render failed");
            return EXIT_FAILURE;
        }
    }
    const pwm::usize after = live_allocations.load(std::memory_order_relaxed);
    if (after != before) {
        std::println(stderr, "steady-state render allocated {} objects", after - before);
        return EXIT_FAILURE;
    }
    std::println("steady-state render allocation test passed");
    return EXIT_SUCCESS;
}

auto run_budget_test() -> int {
    fixture context;
    auto fixture_result = create_fixture(context);
    if (!fixture_result) {
        std::println(stderr, "render fixture unavailable: {}", fixture_result.error().message);
        return 77;
    }

    scene::scene graph;
    auto& root = graph.root();
    scene::node background{1};
    background.set_bounds({0, 0, width_px, height_px});
    root.add_child(background);

    std::array<scene::node, 100> nodes;
    for (pwm::usize index = 0; index < nodes.size(); ++index) {
        auto& item = nodes[index];
        const pwm::i32 column = static_cast<pwm::i32>(index % 10);
        const pwm::i32 row = static_cast<pwm::i32>(index / 10);
        item.set_bounds({column * 30, row * 16, 26, 14});
        item.set_texture(7);
        root.add_child(item);
    }

    const std::array<render::texture_binding, 1> textures{{
        {.id = 7, .texture = &context.texture.value()},
    }};
    const vk::color clear{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};

    constexpr pwm::u32 iterations = 12;
    pwm::f64 total_ms = 0.0;
    for (pwm::u32 frame = 0; frame < iterations; ++frame) {
        for (pwm::usize index = 0; index < nodes.size(); ++index) {
            const pwm::i32 offset = static_cast<pwm::i32>((frame + index) % 8);
            const pwm::i32 column = static_cast<pwm::i32>(index % 10);
            const pwm::i32 row = static_cast<pwm::i32>(index / 10);
            nodes[index].set_bounds({column * 30 + offset, row * 16, 26, 14});
        }
        if (auto rendered = context.renderer->render(context.target->native_image(), width_px,
                                                     height_px, root, textures, {}, clear);
            !rendered) {
            std::println(stderr, "budget render failed: {}", rendered.error().message);
            return EXIT_FAILURE;
        }
        total_ms += context.renderer->statistics().cpu_ms;
    }

    const pwm::f64 average_ms = total_ms / static_cast<pwm::f64>(iterations);
    std::println("100 surfaces average frame submission {:.3f} ms", average_ms);
    if (average_ms > 40.0) {
        std::println(stderr, "frame submission budget exceeded");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

}

int main() {
    const int golden = run_golden_test();
    if (golden != EXIT_SUCCESS) {
        return golden;
    }
    const int allocation = run_allocation_test();
    if (allocation != EXIT_SUCCESS) {
        return allocation;
    }
    return run_budget_test();
}
