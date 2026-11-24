// SPDX-License-Identifier: Apache-2.0

#include <platform/wayland/mapped_region.hpp>

#include <unistd.h>

#include <cerrno>
#include <string>
#include <sys/mman.h>
#include <utility>

namespace pwm::platform::wayland {

namespace {

auto mapping_error(std::string_view message) noexcept -> error {
    return make_error(error_code::io, message, std::source_location::current(), errno);
}

}

mapped_region::mapped_region(mapped_region&& other) noexcept
    : address_(other.address_), size_(other.size_), writable_(other.writable_) {
    other.address_ = nullptr;
    other.size_ = 0;
}

auto mapped_region::operator=(mapped_region&& other) noexcept -> mapped_region& {
    if (this != &other) {
        destroy();
        address_ = other.address_;
        size_ = other.size_;
        writable_ = other.writable_;
        other.address_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

mapped_region::~mapped_region() noexcept {
    destroy();
}

auto mapped_region::create(handle_t fd, usize offset, usize size, bool writable) noexcept
    -> result<mapped_region> {
    if (size == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "mapping size must be non-zero"));
    }
    const int protection = PROT_READ | (writable ? PROT_WRITE : 0);
    void* address = ::mmap(nullptr, size, protection, MAP_SHARED, fd, static_cast<off_t>(offset));
    if (address == MAP_FAILED) {
        return std::unexpected(mapping_error("mmap failed"));
    }
    return mapped_region{address, size, writable};
}

auto mapped_region::valid() const noexcept -> bool {
    return address_ != nullptr;
}

auto mapped_region::data() const noexcept -> std::span<const std::byte> {
    if (address_ == nullptr) {
        return {};
    }
    return {static_cast<const std::byte*>(address_), size_};
}

auto mapped_region::writable_data() noexcept -> std::span<std::byte> {
    if (address_ == nullptr || !writable_) {
        return {};
    }
    return {static_cast<std::byte*>(address_), size_};
}

auto mapped_region::size() const noexcept -> usize {
    return size_;
}

auto mapped_region::destroy() const noexcept -> void {
    if (address_ != nullptr) {
        ::munmap(address_, size_);
        address_ = nullptr;
        size_ = 0;
    }
}

auto create_anonymous_file(std::string_view name, usize size) noexcept -> result<file_descriptor> {
    if (size == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "anonymous file size must be non-zero"));
    }
    const std::string file_name{name};
    const handle_t fd = ::memfd_create(file_name.c_str(), MFD_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(mapping_error("memfd_create failed"));
    }
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        const error failure = mapping_error("ftruncate failed");
        ::close(fd);
        return std::unexpected(failure);
    }
    return file_descriptor::adopt(fd);
}

}
