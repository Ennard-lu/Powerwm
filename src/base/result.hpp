// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/types.hpp>

#include <expected>
#include <source_location>
#include <string_view>

namespace pwm {

enum class error_code : u16 {
    invalid_argument,
    not_found,
    permission_denied,
    io,
    protocol,
    unsupported,
    resource_exhausted,
    cancelled,
    internal,
};

struct error {
    error_code code;
    std::string_view message;
    std::source_location where{};
    i32 system_error = 0;
};

[[nodiscard]] inline auto make_error(error_code code, std::string_view message,
                                     std::source_location where = std::source_location::current(),
                                     i32 system_error = 0) noexcept -> error {
    return {code, message, where, system_error};
}

template <typename T = void>
using result = std::expected<T, error>;

}
