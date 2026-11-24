// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <vector>

namespace pwm::platform::event {

enum class poll_event : u8 {
    readable = 1,
    writable = 2,
    error = 4,
};

using poll_callback = auto (*)(void* context, poll_event events) -> void;

class event_loop {
  public:
    event_loop() noexcept = default;

    [[nodiscard]] auto add(handle_t fd, poll_event events, poll_callback callback,
                           void* context) noexcept -> result<void>;

    [[nodiscard]] auto remove(handle_t fd) noexcept -> result<void>;

    [[nodiscard]] auto run_once(i32 timeout_ms) noexcept -> result<i32>;

    auto destroy() noexcept -> void;

  private:
    struct registration {
        handle_t fd = -1;
        poll_event events = poll_event::readable;
        poll_callback callback = nullptr;
        void* context = nullptr;
    };

    std::vector<registration> registrations_;
};

}