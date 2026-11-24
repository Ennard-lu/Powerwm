// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <coroutine>
#include <exception>
#include <utility>

namespace pwm {

template <typename T>
class task {
  public:
    struct promise_type {
        T value{};

        [[nodiscard]] auto get_return_object() noexcept -> task {
            return task{handle_type::from_promise(*this)};
        }

        [[nodiscard]] auto initial_suspend() const noexcept -> std::suspend_always {
            return {};
        }

        [[nodiscard]] auto final_suspend() const noexcept -> std::suspend_always {
            return {};
        }

        auto return_value(T result) noexcept -> void {
            value = std::move(result);
        }

        [[noreturn]] auto unhandled_exception() const noexcept -> void {
            std::terminate();
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    task() noexcept = default;

    task(const task&) = delete;
    auto operator=(const task&) -> task& = delete;

    task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {
    }

    auto operator=(task&& other) noexcept -> task& {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~task() noexcept {
        if (handle_) {
            handle_.destroy();
        }
    }

    [[nodiscard]] auto valid() const noexcept -> bool {
        return static_cast<bool>(handle_);
    }

    [[nodiscard]] auto done() const noexcept -> bool {
        return !handle_ || handle_.done();
    }

    auto resume() const noexcept -> void {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    [[nodiscard]] auto value() const noexcept -> const T& {
        return handle_.promise().value;
    }

  private:
    explicit task(handle_type handle) noexcept : handle_(handle) {
    }

    handle_type handle_{};
};

template <>
class task<void> {
  public:
    struct promise_type {
        [[nodiscard]] auto get_return_object() noexcept -> task {
            return task{handle_type::from_promise(*this)};
        }

        [[nodiscard]] auto initial_suspend() const noexcept -> std::suspend_always {
            return {};
        }

        [[nodiscard]] auto final_suspend() const noexcept -> std::suspend_always {
            return {};
        }

        auto return_void() const noexcept -> void {
        }

        [[noreturn]] auto unhandled_exception() const noexcept -> void {
            std::terminate();
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    task() noexcept = default;

    task(const task&) = delete;
    auto operator=(const task&) -> task& = delete;

    task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {
    }

    auto operator=(task&& other) noexcept -> task& {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~task() noexcept {
        if (handle_) {
            handle_.destroy();
        }
    }

    [[nodiscard]] auto valid() const noexcept -> bool {
        return static_cast<bool>(handle_);
    }

    [[nodiscard]] auto done() const noexcept -> bool {
        return !handle_ || handle_.done();
    }

    auto resume() const noexcept -> void {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

  private:
    explicit task(handle_type handle) noexcept : handle_(handle) {
    }

    handle_type handle_{};
};

}
