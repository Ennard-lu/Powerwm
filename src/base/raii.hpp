// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/fatal.hpp>
#include <base/result.hpp>
#include <base/types.hpp>

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace pwm {

struct without_valid_check_t {};

constexpr inline without_valid_check_t without_valid_check;

template <typename T, typename... Args>
requires(std::is_nothrow_move_constructible_v<T> && std::is_move_assignable_v<T> &&
         std::is_default_constructible_v<T>)
class raii {
  public:
    using value_t = std::remove_all_extents_t<T>;

    raii() noexcept = default;

    raii(value_t&& value, const Args&... arguments) noexcept
        : owns_(true), value_(std::move(value)), arguments_(arguments...) {
        if (!value_.valid()) {
            terminate_with_message("raii received an invalid handle");
        }
    }

    raii(without_valid_check_t, value_t&& value, const Args&... arguments) noexcept
        : owns_(true), value_(value), arguments_(arguments...) {
    }

    raii(const raii&) = delete;
    auto operator=(const raii&) -> raii& = delete;

    raii(raii&& other) noexcept
        : owns_(other.owns_), value_(std::move(other.value_)),
          arguments_(std::move(other.arguments_)) {
        other.owns_ = false;
    }

    auto operator=(raii&& other) noexcept -> raii& {
        if (this == &other) {
            return *this;
        }
        destroy_owned();
        owns_ = other.owns_;
        value_ = std::move(other.value_);
        arguments_ = std::move(other.arguments_);
        other.owns_ = false;
        return *this;
    }

    ~raii() noexcept {
        destroy_owned();
    }

    [[nodiscard]] auto release() noexcept -> value_t {
        owns_ = false;
        return std::move(value_);
    }

    [[nodiscard]] auto is_owned() const noexcept -> bool {
        return owns_;
    }

    [[nodiscard]] auto value() const noexcept -> const value_t& {
        return value_;
    }

    [[nodiscard]] auto value() noexcept -> value_t& {
        return value_;
    }

    [[nodiscard]] auto operator->() const noexcept -> const value_t* {
        return &value_;
    }

    [[nodiscard]] auto operator->() noexcept -> value_t* {
        return &value_;
    }

  private:
    auto destroy_owned() noexcept -> void {
        if (!owns_ || !value_.valid()) {
            owns_ = false;
            return;
        }
        destroy();
        owns_ = false;
    }

    auto destroy() noexcept -> void {
        [&, this]<usize... N>(std::index_sequence<N...>) {
            value_.destroy(std::get<N>(arguments_)...);
        }(std::make_index_sequence<sizeof...(Args)>());
    }

    bool owns_ = false;
    value_t value_{};
    std::tuple<Args...> arguments_{};
};

template <typename T, typename... Args>
raii(without_valid_check_t, T&&, const Args&...) -> raii<T, Args...>;

template <typename T, typename... Args>
requires(!std::same_as<std::remove_all_extents_t<T>, without_valid_check_t>)
raii(T&&, const Args&...) -> raii<T, Args...>;

}