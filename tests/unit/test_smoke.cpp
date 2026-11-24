// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>

#include <cstdlib>
#include <utility>

namespace {

struct test_resource {
    bool* destroyed = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return destroyed != nullptr;
    }

    void destroy() const noexcept {
        *destroyed = true;
    }
};

auto test_result_type() -> bool {
    const pwm::result<pwm::i32> success{42};
    const pwm::result<pwm::i32> failure{
        std::unexpect,
        pwm::make_error(pwm::error_code::not_found, "missing value"),
    };

    if (!success || failure) {
        return false;
    }
    if (success.value_or(7) != 42 || failure.value_or(7) != 7) {
        return false;
    }
    return failure.error().code == pwm::error_code::not_found;
}

auto test_raii_destroy() -> bool {
    bool destroyed = false;
    {
        pwm::raii<test_resource> resource{test_resource{&destroyed}};
        if (destroyed) {
            return false;
        }
    }
    return destroyed;
}

auto test_raii_move_transfers_ownership() -> bool {
    bool destroyed = false;
    {
        pwm::raii<test_resource> first{test_resource{&destroyed}};
        pwm::raii<test_resource> second{std::move(first)};
        if (first.is_owned() || !second.is_owned() || destroyed) {
            return false;
        }
    }
    return destroyed;
}

}

int main() {
    if (!test_result_type()) {
        return EXIT_FAILURE;
    }
    if (!test_raii_destroy()) {
        return EXIT_FAILURE;
    }
    if (!test_raii_move_transfers_ownership()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}