// SPDX-License-Identifier: Apache-2.0

#include <wayland/connection.hpp>

#include <array>
#include <cstring>
#include <utility>

namespace pwm::wayland {

namespace {

constexpr usize receive_buffer_size = 65536;
constexpr usize receive_fd_capacity = 64;

}

connection::connection(platform::wayland::stream stream) noexcept : stream_(std::move(stream)) {
}

connection::~connection() noexcept {
    disconnect();
}

auto connection::valid() const noexcept -> bool {
    return stream_.valid();
}

auto connection::fd() const noexcept -> handle_t {
    return stream_.fd();
}

auto connection::objects() noexcept -> object_table& {
    return objects_;
}

auto connection::objects() const noexcept -> const object_table& {
    return objects_;
}

auto connection::receive_available() noexcept -> result<void> {
    std::array<std::byte, receive_buffer_size> data{};
    std::array<handle_t, receive_fd_capacity> fds{};
    while (!closed_) {
        auto received = stream_.receive(data, fds);
        if (!received) {
            return std::unexpected(received.error());
        }
        if (received->closed) {
            closed_ = true;
            return {};
        }
        if (received->would_block && received->bytes == 0 && received->fds == 0) {
            return {};
        }
        input_.insert(input_.end(), data.begin(), data.begin() + received->bytes);
        input_fds_.insert(input_fds_.end(), fds.begin(), fds.begin() + received->fds);
        if (received->would_block) {
            return {};
        }
        if (received->bytes == 0 && received->fds == 0) {
            return {};
        }
    }
    return {};
}

auto connection::dispatch_available() noexcept -> result<usize> {
    usize dispatched = 0;
    while (input_.size() >= sizeof(message_header)) {
        message_header header{};
        std::memcpy(&header, input_.data(), sizeof(header));
        if (header.size < sizeof(message_header)) {
            return std::unexpected(make_error(error_code::protocol, "message size is too small"));
        }
        if (header.size > input_.size()) {
            break;
        }

        const auto payload = std::span<const std::byte>{input_}.subspan(
            sizeof(message_header), header.size - sizeof(message_header));
        protocol_reader reader{payload, input_fds_};
        auto handled = objects_.dispatch(header.object, header.opcode, reader);
        if (!handled) {
            return std::unexpected(handled.error());
        }

        const usize consumed = reader.consumed_fds();
        input_fds_.erase(input_fds_.begin(),
                         input_fds_.begin() + static_cast<std::ptrdiff_t>(consumed));
        input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(header.size));
        ++dispatched;
    }
    return dispatched;
}

auto connection::queue_message(const protocol_writer& writer) noexcept -> result<void> {
    const auto bytes = writer.bytes();
    const auto fds = writer.fds();
    output_.insert(output_.end(), bytes.begin(), bytes.end());
    output_fds_.insert(output_fds_.end(), fds.begin(), fds.end());
    return {};
}

auto connection::flush() noexcept -> result<void> {
    while (!output_.empty()) {
        const auto bytes = std::span<const std::byte>{output_}.subspan(output_offset_);
        const auto fds =
            output_fds_sent_ ? std::span<const handle_t>{} : std::span<const handle_t>{output_fds_};
        auto sent = stream_.send(bytes, fds);
        if (!sent) {
            return std::unexpected(sent.error());
        }
        if (sent->would_block) {
            return {};
        }
        if (sent->bytes == 0) {
            return {};
        }
        output_offset_ += sent->bytes;
        if (!output_fds_.empty()) {
            output_fds_sent_ = true;
            output_fds_.clear();
        }
        if (output_offset_ >= output_.size()) {
            output_.clear();
            output_fds_.clear();
            output_offset_ = 0;
            output_fds_sent_ = false;
        }
    }
    return {};
}

auto connection::closed() const noexcept -> bool {
    return closed_;
}

auto connection::disconnect() noexcept -> void {
    stream_.destroy();
    closed_ = true;
}

}
