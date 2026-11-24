// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/macros.hpp>
#include <base/types.hpp>

#include <gbm.h>

#include <string_view>

namespace pwm::platform::gbm {

struct buffer {
    gbm_bo* val;

    [[nodiscard]] PWM_ALWAYS_INLINE auto valid() const noexcept -> bool {
        return val != nullptr;
    }

    [[nodiscard]] PWM_ALWAYS_INLINE static auto wrap(gbm_bo* object) noexcept -> buffer {
        return {object};
    }

    auto destroy() const noexcept -> void {
        gbm_bo_destroy(val);
    }

    [[nodiscard]] PWM_ALWAYS_INLINE auto fd() const noexcept -> pwm::handle_t {
        return gbm_bo_get_fd(val);
    }
};

struct context {
    gbm_device* val;

    [[nodiscard]] PWM_ALWAYS_INLINE auto valid() const noexcept -> bool {
        return val != nullptr;
    }

    [[nodiscard]] PWM_ALWAYS_INLINE static auto create(pwm::handle_t handle) noexcept -> context {
        return {gbm_create_device(handle)};
    }

    [[nodiscard]] PWM_ALWAYS_INLINE auto allocate(pwm::u32 width, pwm::u32 height, pwm::u32 format,
                                                  pwm::u32 flags) -> buffer {
        return buffer::wrap(gbm_bo_create(val, width, height, format, flags));
    }

    [[nodiscard]] PWM_ALWAYS_INLINE auto fd() const noexcept -> pwm::handle_t {
        return gbm_device_get_fd(val);
    }

    [[nodiscard]] PWM_ALWAYS_INLINE auto backend_name() const noexcept -> std::string_view {
        return gbm_device_get_backend_name(val);
    }

    auto destroy() const noexcept -> void {
        gbm_device_destroy(val);
    }
};

}