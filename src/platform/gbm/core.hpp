// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/macros.hpp>
#include <base/types.hpp>

#include <gbm.h>

#include <string_view>

namespace pwm::platform::gbm {

struct buffer {
    gbm_bo* val;

    PWM_ALWAYS_INLINE bool valid() const noexcept {
        return val != nullptr;
    }

    PWM_ALWAYS_INLINE static buffer from(gbm_bo* obj) noexcept {
        return {obj};
    }

    PWM_ALWAYS_INLINE void destroy() const noexcept {
        gbm_bo_destroy(val);
    }

    PWM_ALWAYS_INLINE pwm::handle_t fd() const noexcept {
        return gbm_bo_get_fd(val);
    }
};

struct context {
    gbm_device* val;

    PWM_ALWAYS_INLINE bool valid() const noexcept {
        return val != nullptr;
    }

    PWM_ALWAYS_INLINE static context from(pwm::handle_t handle) noexcept {
        return {gbm_create_device(handle)};
    }

    PWM_ALWAYS_INLINE buffer allocate(pwm::u32 width, pwm::u32 height, pwm::u32 format,
                                      pwm::u32 flags) {
        return buffer::from(gbm_bo_create(val, width, height, format, flags));
    }
    PWM_ALWAYS_INLINE pwm::handle_t fd() const noexcept {
        return gbm_device_get_fd(val);
    }
    PWM_ALWAYS_INLINE std::string_view backend_name() const noexcept {
        return gbm_device_get_backend_name(val);
    }

    PWM_ALWAYS_INLINE void destroy() const noexcept {
        gbm_device_destroy(val);
    }
};

}
