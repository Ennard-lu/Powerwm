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
};

[[nodiscard]] inline auto
make_error(error_code code, std::string_view message,
           std::source_location where = std::source_location::current()) noexcept -> error {
    return {code, message, where};
}

template <typename T = void>
using result = std::expected<T, error>;

}
