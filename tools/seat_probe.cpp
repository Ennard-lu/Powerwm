// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/seat/seat.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <string>
#include <system_error>
#include <utility>

namespace {

namespace seat = pwm::platform::seat;

using namespace std::chrono_literals;

auto require_value(pwm::result<void>&& value) -> void {
    if (!value) {
        std::println(stderr, "error: {}", value.error().message);
        std::exit(EXIT_FAILURE);
    }
}

template <typename T>
auto require_value(pwm::result<T>&& value) -> T {
    if (!value) {
        std::println(stderr, "error: {}", value.error().message);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*value);
}

auto find_first_card_device() -> std::string {
    std::error_code error;
    std::filesystem::directory_iterator iterator{"/dev/dri", error};
    if (error) {
        return {};
    }
    for (const auto& entry : iterator) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("card")) {
            return entry.path().string();
        }
    }
    return {};
}

auto run_seat_probe() -> int {
    auto seat_context = pwm::raii{require_value(seat::session::create())};
    const std::string card = find_first_card_device();
    if (card.empty()) {
        std::println(stderr, "error: no DRM card device found");
        return EXIT_FAILURE;
    }

    auto device = pwm::raii{require_value(seat_context->open_device(card))};
    std::println("seat enabled={} device={} fd={}", seat_context->is_enabled(), card, device->fd());

    bool enabled = seat_context->is_enabled();
    const auto deadline = std::chrono::steady_clock::now() + 5s;

    while (std::chrono::steady_clock::now() < deadline) {
        require_value(seat_context->dispatch(100));
        if (seat_context->is_enabled() != enabled) {
            enabled = seat_context->is_enabled();
            std::println("seat enabled={}", enabled);
        }
    }

    return EXIT_SUCCESS;
}

}

int main() {
    return run_seat_probe();
}