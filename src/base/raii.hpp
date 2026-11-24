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
requires(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>)
class raii {
  public:
    using value_t = std::remove_all_extents_t<T>;

    raii() noexcept = default;

    raii(value_t&& value, const Args&... arguments) noexcept
        : owns_(true), value_(value), arguments_(arguments...) {
        if (!value_.valid()) {
            terminate_with_message("raii received an invalid handle");
        }
    }

    raii(without_valid_check_t, value_t&& value, const Args&... arguments) noexcept
        : owns_(true), value_(value), arguments_(arguments...) {
    }

    raii(const raii&) = delete;
    raii& operator=(const raii&) = delete;

    raii(raii&& other) noexcept
        : owns_(other.owns_), value_(other.value_), arguments_(std::move(other.arguments_)) {
        other.owns_ = false;
    }

    raii& operator=(raii&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        destroy_owned();
        owns_ = other.owns_;
        value_ = other.value_;
        arguments_ = std::move(other.arguments_);
        other.owns_ = false;
        return *this;
    }

    ~raii() noexcept {
        destroy_owned();
    }

    [[nodiscard]] value_t release() noexcept {
        owns_ = false;
        return value_;
    }

    [[nodiscard]] bool is_owned() const noexcept {
        return owns_;
    }

    [[nodiscard]] const value_t& value() const noexcept {
        return value_;
    }

    [[nodiscard]] value_t& value() noexcept {
        return value_;
    }

    [[nodiscard]] const value_t* operator->() const noexcept {
        return &value_;
    }

    [[nodiscard]] value_t* operator->() noexcept {
        return &value_;
    }

  private:
    void destroy_owned() noexcept {
        if (!owns_ || !value_.valid()) {
            owns_ = false;
            return;
        }
        destroy();
        owns_ = false;
    }

    void destroy() noexcept {
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