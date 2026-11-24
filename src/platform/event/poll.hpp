// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

namespace pwm::platform::event {

[[nodiscard]] auto wait_readable(handle_t fd, i32 timeout_ms) noexcept -> result<bool>;

}