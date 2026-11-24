// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/instance.hpp>
#include <render/vulkanpp/triangle.hpp>

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <print>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace vk = pwm::vulkanpp;

auto require_value(pwm::result<void>&& value) -> void {
    if (!value) {
        std::println(stderr, "error: {} (system={})", value.error().message,
                     value.error().system_error);
        std::exit(EXIT_FAILURE);
    }
}

template <typename T>
auto require_value(pwm::result<T>&& value) -> T {
    if (!value) {
        std::println(stderr, "error: {} (system={})", value.error().message,
                     value.error().system_error);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*value);
}

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

auto run_probe() -> int {
    const vk::application_info application{
        .name = "powerwm_vulkan_pipeline_probe",
        .version = 1,
        .engine_name = "powerwm",
        .engine_version = 1,
        .api_version = 0,
    };

    auto instance = pwm::raii{require_value(vk::instance::create(application))};
    auto device = pwm::raii{require_value(vk::device::create(instance.value()))};

    auto vertex_spirv =
        require_value(load_spirv(std::string{POWERWM_SHADER_DIR} + "/triangle.vert.spv"));
    auto fragment_spirv =
        require_value(load_spirv(std::string{POWERWM_SHADER_DIR} + "/triangle.frag.spv"));

    auto target = pwm::raii{require_value(
        vk::create_triangle_target(device.value(), 64, 64, std::span<const std::byte>{vertex_spirv},
                                   std::span<const std::byte>{fragment_spirv}))};
    require_value(target->render());

    const vk::color center = require_value(target->read_pixel(32, 32));
    std::println("triangle center r={:.2f} g={:.2f} b={:.2f}", center.red, center.green,
                 center.blue);
    return EXIT_SUCCESS;
}

}

int main() {
    return run_probe();
}