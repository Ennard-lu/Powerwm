// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string_view>

namespace pwm {

[[noreturn]] void terminate_with_message(std::string_view message) noexcept;

}