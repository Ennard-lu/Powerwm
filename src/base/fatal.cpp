// SPDX-License-Identifier: Apache-2.0

#include <base/fatal.hpp>

#include <cstdlib>
#include <print>

namespace pwm {

void terminate_with_message(std::string_view message) noexcept {
    std::println(stderr, "fatal: {}", message);
    std::abort();
}

}