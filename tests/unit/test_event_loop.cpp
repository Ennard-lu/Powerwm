// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <platform/event/event_fd.hpp>
#include <platform/event/event_loop.hpp>
#include <platform/event/timer_fd.hpp>

#include <cstdlib>
#include <utility>

namespace {

namespace event = pwm::platform::event;

struct dispatch_state {
    int first_count = 0;
    int second_count = 0;
};

auto count_first(void* context, event::poll_event) -> void {
    static_cast<dispatch_state*>(context)->first_count++;
}

auto count_second(void* context, event::poll_event) -> void {
    static_cast<dispatch_state*>(context)->second_count++;
}

auto create_event_fd(pwm::u32 value) -> pwm::raii<event::event_fd> {
    auto result = event::event_fd::create(value);
    if (!result) {
        std::exit(EXIT_FAILURE);
    }
    return pwm::raii{std::move(*result)};
}

auto create_timer_fd() -> pwm::raii<event::timer_fd> {
    auto result = event::timer_fd::create();
    if (!result) {
        std::exit(EXIT_FAILURE);
    }
    return pwm::raii{std::move(*result)};
}

auto test_timer_dispatch() -> bool {
    auto timer = create_timer_fd();
    if (!timer->arm_ms(1)) {
        return false;
    }

    dispatch_state state{};
    event::event_loop loop;
    if (!loop.add(timer->fd(), event::poll_event::readable, count_first, &state)) {
        return false;
    }

    auto dispatched = loop.run_once(100);
    if (!dispatched || *dispatched != 1 || state.first_count != 1) {
        return false;
    }
    auto expirations = timer->read_expirations();
    if (!expirations || *expirations == 0) {
        return false;
    }
    return true;
}

auto test_dispatch_and_remove() -> bool {
    auto first = create_event_fd(0);
    auto second = create_event_fd(0);

    dispatch_state state{};
    event::event_loop loop;
    if (!loop.add(first->fd(), event::poll_event::readable, count_first, &state)) {
        return false;
    }
    if (!loop.add(second->fd(), event::poll_event::readable, count_second, &state)) {
        return false;
    }

    if (!first->write(1)) {
        return false;
    }
    auto dispatched = loop.run_once(100);
    if (!dispatched || *dispatched != 1) {
        return false;
    }
    if (state.first_count != 1 || state.second_count != 0) {
        return false;
    }
    if (!first->read()) {
        return false;
    }

    if (!loop.remove(second->fd())) {
        return false;
    }
    if (!second->write(1)) {
        return false;
    }
    dispatched = loop.run_once(0);
    if (!dispatched || *dispatched != 0) {
        return false;
    }
    if (state.second_count != 0) {
        return false;
    }

    return true;
}

}

int main() {
    if (!test_dispatch_and_remove()) {
        return EXIT_FAILURE;
    }
    if (!test_timer_dispatch()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}