// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <wayland/wire.hpp>

#include <array>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace pwm::wayland {

consteval auto find_message_handler(std::meta::info implementation, std::string_view name)
    -> std::meta::info {
    for (auto member :
         std::meta::members_of(implementation, std::meta::access_context::unchecked())) {
        if (std::meta::is_function(member) && std::meta::has_identifier(member) &&
            std::meta::identifier_of(member) == name) {
            return member;
        }
    }
    return {};
}

template <typename Implementation, typename Message, std::meta::info Handler>
auto invoke_message_handler(Implementation& implementation, Message& message) -> result<void> {
    return std::apply(
        [&](auto&&... arguments) -> result<void> {
            using return_type = decltype((implementation.*&[:Handler:])(
                std::forward<decltype(arguments)>(arguments)...));
            if constexpr (std::is_void_v<return_type>) {
                (implementation.*&[:Handler:])(std::forward<decltype(arguments)>(arguments)...);
                return {};
            } else {
                return (implementation.*
                        &[:Handler:])(std::forward<decltype(arguments)>(arguments)...);
            }
        },
        std::move(message.arguments));
}

template <typename Implementation, typename Interface, usize Index>
auto dispatch_request_at(Implementation& implementation, protocol_reader& reader) -> result<void> {
    using message_type = std::tuple_element_t<Index, typename Interface::requests>;
    auto message = message_type::decode(reader);
    if (!message) {
        return std::unexpected(message.error());
    }

    constexpr auto handler =
        find_message_handler(^^Implementation, Interface::request_handler_names[Index]);
    if constexpr (handler == std::meta::info{}) {
        return std::unexpected(make_error(error_code::unsupported, "no reflected request handler"));
    } else {
        return invoke_message_handler<Implementation, message_type, handler>(implementation,
                                                                             *message);
    }
}

template <typename Implementation, typename Interface, usize... Index>
constexpr auto make_request_table(std::index_sequence<Index...>)
    -> std::array<result<void> (*)(Implementation&, protocol_reader&), sizeof...(Index)> {
    return {&dispatch_request_at<Implementation, Interface, Index>...};
}

template <typename Implementation, typename Interface>
inline constexpr auto request_table = make_request_table<Implementation, Interface>(
    std::make_index_sequence<Interface::request_count>{});

template <typename Implementation, typename Interface>
auto dispatch_request(Implementation& implementation, u16 opcode, protocol_reader& reader)
    -> result<void> {
    if (opcode >= Interface::request_count) {
        return std::unexpected(make_error(error_code::protocol, "request opcode is out of range"));
    }
    return request_table<Implementation, Interface>[opcode](implementation, reader);
}

}
