// SPDX-License-Identifier: Apache-2.0

#include <wayland/wire.hpp>

#include <cstring>

namespace pwm::wayland {

namespace {

auto protocol_error(std::string_view message) noexcept -> error {
    return make_error(error_code::protocol, message);
}

auto write_value(std::vector<std::byte>& data, const void* value, usize size) -> void {
    const auto* bytes = static_cast<const std::byte*>(value);
    data.insert(data.end(), bytes, bytes + size);
}

}

protocol_reader::protocol_reader(std::span<const std::byte> data,
                                 std::span<const handle_t> fds) noexcept
    : data_(data), fds_(fds) {
}

auto protocol_reader::remaining() const noexcept -> usize {
    return position_ <= data_.size() ? data_.size() - position_ : 0;
}

auto protocol_reader::take(usize size) noexcept -> result<std::span<const std::byte>> {
    if (size > remaining()) {
        return std::unexpected(protocol_error("message argument is truncated"));
    }
    const auto result = data_.subspan(position_, size);
    position_ += size;
    return result;
}

auto protocol_reader::align_to_four() noexcept -> bool {
    const usize aligned = (position_ + 3U) & ~static_cast<usize>(3U);
    if (aligned > data_.size()) {
        return false;
    }
    position_ = aligned;
    return true;
}

auto protocol_reader::read_u32() noexcept -> result<u32> {
    auto bytes = take(sizeof(u32));
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    u32 value = 0;
    std::memcpy(&value, bytes->data(), sizeof(value));
    return value;
}

auto protocol_reader::read_i32() noexcept -> result<i32> {
    auto bytes = take(sizeof(i32));
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    i32 value = 0;
    std::memcpy(&value, bytes->data(), sizeof(value));
    return value;
}

auto protocol_reader::read_fixed() noexcept -> result<f64> {
    auto value = read_i32();
    if (!value) {
        return std::unexpected(value.error());
    }
    return static_cast<f64>(*value) / 256.0;
}

auto protocol_reader::read_string() noexcept -> result<std::string_view> {
    auto length = read_u32();
    if (!length) {
        return std::unexpected(length.error());
    }
    if (*length == 0) {
        return std::string_view{};
    }
    auto bytes = take(*length);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (bytes->back() != std::byte{0}) {
        return std::unexpected(protocol_error("string argument is not NUL terminated"));
    }
    const auto text =
        std::span<const char>{reinterpret_cast<const char*>(bytes->data()), bytes->size() - 1};
    return std::string_view{text.data(), text.size()};
}

auto protocol_reader::read_array() noexcept -> result<std::span<const std::byte>> {
    auto length = read_u32();
    if (!length) {
        return std::unexpected(length.error());
    }
    auto bytes = take(*length);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (!align_to_four()) {
        return std::unexpected(protocol_error("array argument padding is truncated"));
    }
    return *bytes;
}

auto protocol_reader::read_object() noexcept -> result<object_id> {
    return read_u32();
}

auto protocol_reader::read_new_id() noexcept -> result<object_id> {
    return read_u32();
}

auto protocol_reader::read_untyped_new_id() noexcept -> result<untyped_new_id> {
    auto id = read_u32();
    if (!id) {
        return std::unexpected(id.error());
    }
    auto interface = read_string();
    if (!interface) {
        return std::unexpected(interface.error());
    }
    return untyped_new_id{*id, *interface};
}

auto protocol_reader::read_fd() noexcept -> result<handle_t> {
    if (fd_position_ >= fds_.size()) {
        return std::unexpected(protocol_error("message is missing a file descriptor"));
    }
    return fds_[fd_position_++];
}

auto protocol_writer::write_raw(std::span<const std::byte> value) noexcept -> result<void> {
    data_.insert(data_.end(), value.begin(), value.end());
    return {};
}

auto protocol_writer::pad_to_four() noexcept -> result<void> {
    while (data_.size() % 4 != 0) {
        data_.push_back(std::byte{0});
    }
    return {};
}

auto protocol_writer::begin_message(object_id object, u16 opcode) noexcept -> result<void> {
    if (message_open_) {
        return std::unexpected(protocol_error("previous message is still open"));
    }
    message_start_ = data_.size();
    message_open_ = true;
    const message_header header{.object = object, .opcode = opcode, .size = 0};
    write_value(data_, &header, sizeof(header));
    return {};
}

auto protocol_writer::end_message() noexcept -> result<void> {
    if (!message_open_) {
        return std::unexpected(protocol_error("no message is open"));
    }
    const usize size = data_.size() - message_start_;
    if (size > 0xFFFFU) {
        return std::unexpected(protocol_error("message exceeds the wire size limit"));
    }
    message_header header{};
    std::memcpy(&header, data_.data() + message_start_, sizeof(header));
    header.size = static_cast<u16>(size);
    std::memcpy(data_.data() + message_start_, &header, sizeof(header));
    message_open_ = false;
    return {};
}

auto protocol_writer::write_u32(u32 value) noexcept -> result<void> {
    write_value(data_, &value, sizeof(value));
    return {};
}

auto protocol_writer::write_i32(i32 value) noexcept -> result<void> {
    write_value(data_, &value, sizeof(value));
    return {};
}

auto protocol_writer::write_fixed(f64 value) noexcept -> result<void> {
    return write_i32(static_cast<i32>(value * 256.0));
}

auto protocol_writer::write_string(std::string_view value) noexcept -> result<void> {
    const auto length = static_cast<u32>(value.size() + 1);
    if (auto written = write_u32(length); !written) {
        return written;
    }
    const auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(value.data()), value.size()};
    if (auto written = write_raw(bytes); !written) {
        return written;
    }
    data_.push_back(std::byte{0});
    return pad_to_four();
}

auto protocol_writer::write_array(std::span<const std::byte> value) noexcept -> result<void> {
    if (auto written = write_u32(static_cast<u32>(value.size())); !written) {
        return written;
    }
    if (auto written = write_raw(value); !written) {
        return written;
    }
    return pad_to_four();
}

auto protocol_writer::write_object(object_id value) noexcept -> result<void> {
    return write_u32(value);
}

auto protocol_writer::write_new_id(object_id value) noexcept -> result<void> {
    return write_u32(value);
}

auto protocol_writer::write_untyped_new_id(object_id value, std::string_view interface) noexcept
    -> result<void> {
    if (auto written = write_u32(value); !written) {
        return written;
    }
    return write_string(interface);
}

auto protocol_writer::write_fd(handle_t value) noexcept -> result<void> {
    fds_.push_back(value);
    return {};
}

auto protocol_writer::bytes() const noexcept -> std::span<const std::byte> {
    return data_;
}

auto protocol_writer::fds() const noexcept -> std::span<const handle_t> {
    return fds_;
}

auto protocol_writer::clear() noexcept -> void {
    data_.clear();
    fds_.clear();
    message_start_ = 0;
    message_open_ = false;
}

auto decode_header(std::span<const std::byte> data) noexcept -> result<message_header> {
    if (data.size() < sizeof(message_header)) {
        return std::unexpected(protocol_error("message header is truncated"));
    }
    message_header header{};
    std::memcpy(&header, data.data(), sizeof(header));
    if (header.size < sizeof(message_header) || header.size > data.size()) {
        return std::unexpected(protocol_error("message size is invalid"));
    }
    return header;
}

}
