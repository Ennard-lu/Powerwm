// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/event/poll.hpp>
#include <platform/input/context.hpp>

#include <chrono>
#include <cstdlib>
#include <print>
#include <utility>

namespace {

namespace event = pwm::platform::event;
namespace input = pwm::platform::input;

using namespace std::chrono_literals;

auto require_value(pwm::result<input::context>&& value) -> input::context {
    if (!value) {
        std::println(stderr, "error: {}", value.error().message);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*value);
}

auto print_event(const input::input_event& event) -> void {
    using input::input_event_type;
    switch (event.type) {
    case input_event_type::keyboard_key:
        std::println("key code={} state={}", event.key_code,
                     event.pressed ? "pressed" : "released");
        break;
    case input_event_type::pointer_motion:
        std::println("motion dx={:.2f} dy={:.2f}", event.dx, event.dy);
        break;
    case input_event_type::pointer_button:
        std::println("button code={} state={}", event.button,
                     event.pressed ? "pressed" : "released");
        break;
    case input_event_type::pointer_axis:
        std::println("axis value={:.2f}", event.axis_value);
        break;
    case input_event_type::touch_down:
    case input_event_type::touch_up:
    case input_event_type::touch_motion:
        std::println("touch slot={} x={:.2f} y={:.2f}", event.touch_slot, event.x, event.y);
        break;
    default:
        break;
    }
}

auto open_input_context() -> pwm::raii<input::context> {
    auto context = require_value(input::context::create("seat0"));
    return pwm::raii{std::move(context)};
}

auto run_input_probe() -> int {
    auto context = open_input_context();
    const auto deadline = std::chrono::steady_clock::now() + 5s;

    while (std::chrono::steady_clock::now() < deadline) {
        auto readable = event::wait_readable(context->fd(), 100);
        if (!readable) {
            std::println(stderr, "error: {}", readable.error().message);
            return EXIT_FAILURE;
        }
        if (!*readable) {
            continue;
        }

        auto dispatched = context->dispatch();
        if (!dispatched) {
            std::println(stderr, "error: {}", dispatched.error().message);
            return EXIT_FAILURE;
        }

        while (auto input_event = context->next_event()) {
            print_event(*input_event);
        }
    }

    return EXIT_SUCCESS;
}

}

int main() {
    return run_input_probe();
}