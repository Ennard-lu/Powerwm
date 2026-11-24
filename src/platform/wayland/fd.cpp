// SPDX-License-Identifier: Apache-2.0

#include <platform/wayland/fd.hpp>

#include <unistd.h>

namespace pwm::platform::wayland {

file_descriptor::file_descriptor(file_descriptor&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

file_descriptor::~file_descriptor() noexcept {
    destroy();
}

auto file_descriptor::operator=(file_descriptor&& other) noexcept -> file_descriptor& {
    if (this != &other) {
        destroy();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

auto file_descriptor::adopt(handle_t fd) noexcept -> file_descriptor {
    return file_descriptor{fd};
}

auto file_descriptor::valid() const noexcept -> bool {
    return fd_ >= 0;
}

auto file_descriptor::fd() const noexcept -> handle_t {
    return fd_;
}

auto file_descriptor::destroy() const noexcept -> void {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}
