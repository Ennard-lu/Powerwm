// SPDX-License-Identifier: Apache-2.0

#include <base/result.hpp>
#include <wayland/protocol.hpp>
#include <wayland/xml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace wayland = pwm::wayland;

auto is_cpp_keyword(std::string_view value) -> bool {
    constexpr std::array keywords{
        "alignas",
        "alignof",
        "and",
        "asm",
        "auto",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "class",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "continue",
        "co_await",
        "co_return",
        "co_yield",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "nullptr",
        "operator",
        "or",
        "private",
        "protected",
        "public",
        "register",
        "reinterpret_cast",
        "requires",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
        "while",
        "xor",
    };
    return std::ranges::find(keywords, value) != keywords.end();
}

auto sanitize(std::string_view value) -> std::string {
    std::string result{value};
    std::ranges::replace(result, '-', '_');
    std::ranges::replace(result, '.', '_');
    if (is_cpp_keyword(result)) {
        result.push_back('_');
    }
    if (!result.empty() && std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), '_');
    }
    return result;
}

auto argument_type(const wayland::protocol_argument& argument) -> std::string {
    if (argument.type == "int") {
        return "i32";
    }
    if (argument.type == "uint") {
        return "u32";
    }
    if (argument.type == "fixed") {
        return "f64";
    }
    if (argument.type == "string") {
        return "std::string_view";
    }
    if (argument.type == "array") {
        return "std::span<const std::byte>";
    }
    if (argument.type == "object") {
        return "object_id";
    }
    if (argument.type == "new_id") {
        return "object_id";
    }
    if (argument.type == "fd") {
        return "handle_t";
    }
    return "u32";
}

auto decode_method(const wayland::protocol_argument& argument) -> std::string {
    if (argument.type == "int") {
        return "read_i32";
    }
    if (argument.type == "uint") {
        return "read_u32";
    }
    if (argument.type == "fixed") {
        return "read_fixed";
    }
    if (argument.type == "string") {
        return "read_string";
    }
    if (argument.type == "array") {
        return "read_array";
    }
    if (argument.type == "object") {
        return "read_object";
    }
    if (argument.type == "new_id") {
        return "read_new_id";
    }
    if (argument.type == "fd") {
        return "read_fd";
    }
    return "read_u32";
}

auto encode_method(const wayland::protocol_argument& argument) -> std::string {
    if (argument.type == "int") {
        return "write_i32";
    }
    if (argument.type == "uint") {
        return "write_u32";
    }
    if (argument.type == "fixed") {
        return "write_fixed";
    }
    if (argument.type == "string") {
        return "write_string";
    }
    if (argument.type == "array") {
        return "write_array";
    }
    if (argument.type == "object") {
        return "write_object";
    }
    if (argument.type == "new_id") {
        return "write_new_id";
    }
    if (argument.type == "fd") {
        return "write_fd";
    }
    return "write_u32";
}

struct wire_field {
    std::string type;
    std::string decode;
    std::string encode;
};

auto wire_fields(const wayland::protocol_message& message) -> std::vector<wire_field> {
    std::vector<wire_field> fields;
    for (const auto& argument : message.arguments) {
        if (argument.type == "new_id" && argument.interface.empty()) {
            fields.push_back({"std::string_view", "read_string", "write_string"});
            fields.push_back({"u32", "read_u32", "write_u32"});
            fields.push_back({"object_id", "read_new_id", "write_new_id"});
            continue;
        }
        fields.push_back(
            {argument_type(argument), decode_method(argument), encode_method(argument)});
    }
    return fields;
}

auto message_struct_name(std::string_view kind, std::string_view name) -> std::string {
    return std::string{kind} + "_" + sanitize(name);
}

auto arguments_tuple(const wayland::protocol_message& message) -> std::string {
    const auto fields = wire_fields(message);
    if (fields.empty()) {
        return "std::tuple<>";
    }
    std::string result = "std::tuple<";
    for (pwm::usize index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        result += fields[index].type;
    }
    result += ">";
    return result;
}

auto write_names(std::ofstream& output, std::string_view array_name,
                 const std::vector<wayland::protocol_message>& messages, std::string_view prefix)
    -> void {
    output << "    static constexpr std::array<std::string_view, " << messages.size() << "> "
           << array_name << "{";
    for (pwm::usize index = 0; index < messages.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << '"' << prefix << messages[index].name << '"';
    }
    output << "};\n";
}

auto write_message_struct(std::ofstream& output, std::string_view kind,
                          const wayland::protocol_message& message, pwm::u32 opcode) -> void {
    const std::string name = message_struct_name(kind, message.name);
    output << "    struct " << name << " {\n";
    output << "        static constexpr u16 opcode = " << opcode << ";\n";
    output << "        static constexpr std::string_view name = \"" << message.name << "\";\n";
    output << "        static constexpr bool is_destructor = "
           << (message.destructor ? "true" : "false") << ";\n";
    output << "        using argument_types = " << arguments_tuple(message) << ";\n";
    output << "        argument_types arguments{};\n";
    output << "        static auto decode(protocol_reader& reader) -> result<" << name << ">;\n";
    output << "        static auto encode(protocol_writer& writer, object_id object, const " << name
           << "& message) -> result<void>;\n";
    output << "    };\n";
}

auto write_interface_header(std::ofstream& output, const wayland::protocol_interface& interface)
    -> void {
    const std::string name = sanitize(interface.name);
    output << "struct " << name << " {\n";
    output << "    static constexpr std::string_view name = \"" << interface.name << "\";\n";
    output << "    static constexpr u32 version = " << interface.version << ";\n";
    output << "    static constexpr u32 request_count = " << interface.requests.size() << ";\n";
    output << "    static constexpr u32 event_count = " << interface.events.size() << ";\n";
    write_names(output, "request_names", interface.requests, "");
    write_names(output, "request_handler_names", interface.requests, "request_");
    write_names(output, "event_names", interface.events, "");

    for (pwm::u32 index = 0; index < interface.requests.size(); ++index) {
        write_message_struct(output, "request", interface.requests[index], index);
    }
    for (pwm::u32 index = 0; index < interface.events.size(); ++index) {
        write_message_struct(output, "event", interface.events[index], index);
    }

    output << "    using requests = std::tuple<";
    for (pwm::usize index = 0; index < interface.requests.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << message_struct_name("request", interface.requests[index].name);
    }
    output << ">;\n";
    output << "    using events = std::tuple<";
    for (pwm::usize index = 0; index < interface.events.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << message_struct_name("event", interface.events[index].name);
    }
    output << ">;\n";
    output << "};\n\n";
}

auto write_enum_header(std::ofstream& output, const wayland::protocol_interface& interface,
                       const wayland::protocol_enum& enumeration) -> void {
    output << "enum class " << sanitize(interface.name) << "_" << sanitize(enumeration.name)
           << " : u32 {\n";
    for (const auto& entry : enumeration.entries) {
        output << "    " << sanitize(entry.name) << " = " << entry.value << ",\n";
    }
    output << "};\n\n";
}

auto write_header(std::ofstream& output, const wayland::protocol& protocol) -> void {
    output << "// SPDX-License-Identifier: Apache-2.0\n\n";
    output << "#pragma once\n\n";
    output << "#include <array>\n";
    output << "#include <span>\n";
    output << "#include <string_view>\n";
    output << "#include <tuple>\n\n";
    output << "#include <base/result.hpp>\n";
    output << "#include <base/types.hpp>\n";
    output << "#include <wayland/wire.hpp>\n\n";
    output << "namespace pwm::wayland::generated {\n\n";
    output << "inline constexpr std::string_view " << sanitize(protocol.name)
           << "_protocol_name = \"" << protocol.name << "\";\n\n";
    for (const auto& interface : protocol.interfaces) {
        write_interface_header(output, interface);
        for (const auto& enumeration : interface.enums) {
            write_enum_header(output, interface, enumeration);
        }
    }
    output << "}\n";
}

auto write_decode(std::ofstream& output, const wayland::protocol_interface& interface,
                  const wayland::protocol_message& message, std::string_view kind) -> void {
    const std::string interface_name = sanitize(interface.name);
    const std::string message_name = message_struct_name(kind, message.name);
    output << "auto " << interface_name << "::" << message_name
           << "::decode(protocol_reader& reader) -> result<" << message_name << "> {\n";
    output << "    " << message_name << " message{};\n";
    const auto fields = wire_fields(message);
    for (pwm::usize index = 0; index < fields.size(); ++index) {
        output << "    auto argument" << index << " = reader." << fields[index].decode << "();\n";
        output << "    if (!argument" << index << ") {\n";
        output << "        return std::unexpected(argument" << index << ".error());\n";
        output << "    }\n";
        output << "    std::get<" << index << ">(message.arguments) = *argument" << index << ";\n";
    }
    output << "    return message;\n";
    output << "}\n\n";
}

auto write_encode(std::ofstream& output, const wayland::protocol_interface& interface,
                  const wayland::protocol_message& message, std::string_view kind) -> void {
    const std::string interface_name = sanitize(interface.name);
    const std::string message_name = message_struct_name(kind, message.name);
    output << "auto " << interface_name << "::" << message_name
           << "::encode(protocol_writer& writer, object_id object, const " << message_name
           << "& message) -> result<void> {\n";
    output << "    if (auto begun = writer.begin_message(object, opcode); !begun) {\n";
    output << "        return std::unexpected(begun.error());\n";
    output << "    }\n";
    const auto fields = wire_fields(message);
    for (pwm::usize index = 0; index < fields.size(); ++index) {
        output << "    if (auto written = writer." << fields[index].encode << "(std::get<" << index
               << ">(message.arguments)); !written) {\n";
        output << "        return std::unexpected(written.error());\n";
        output << "    }\n";
    }
    output << "    return writer.end_message();\n";
    output << "}\n\n";
}

auto write_source(std::ofstream& output, const wayland::protocol& protocol) -> void {
    output << "// SPDX-License-Identifier: Apache-2.0\n\n";
    output << "#include \"" << sanitize(protocol.name) << "_bindings.hpp\"\n\n";
    output << "namespace pwm::wayland::generated {\n\n";
    for (const auto& interface : protocol.interfaces) {
        for (const auto& message : interface.requests) {
            write_decode(output, interface, message, "request");
            write_encode(output, interface, message, "request");
        }
        for (const auto& message : interface.events) {
            write_decode(output, interface, message, "event");
            write_encode(output, interface, message, "event");
        }
    }
    output << "}\n";
}

auto read_file(const char* path) -> pwm::result<std::string> {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "cannot open protocol XML"));
    }
    const std::streamsize size = input.tellg();
    if (size <= 0) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "protocol XML is empty"));
    }
    std::string data(static_cast<pwm::usize>(size), '\0');
    input.seekg(0);
    input.read(data.data(), size);
    if (!input) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "cannot read protocol XML"));
    }
    return data;
}

auto generate(const char* xml_path, const std::string& header_path, const std::string& source_path)
    -> int {
    auto xml_text = read_file(xml_path);
    if (!xml_text) {
        std::println(stderr, "error: {}", xml_text.error().message);
        return EXIT_FAILURE;
    }
    auto tree = wayland::parse_xml(*xml_text);
    if (!tree) {
        std::println(stderr, "xml error: {}", tree.error().message);
        return EXIT_FAILURE;
    }
    auto protocol = wayland::build_protocol(*tree);
    if (!protocol) {
        std::println(stderr, "protocol error: {}", protocol.error().message);
        return EXIT_FAILURE;
    }

    std::ofstream header{header_path};
    if (!header) {
        std::println(stderr, "error: cannot create generated header");
        return EXIT_FAILURE;
    }
    write_header(header, *protocol);

    std::ofstream source{source_path};
    if (!source) {
        std::println(stderr, "error: cannot create generated source");
        return EXIT_FAILURE;
    }
    write_source(source, *protocol);
    std::println("generated {} interfaces from {}", protocol->interfaces.size(), xml_path);
    return EXIT_SUCCESS;
}

}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::println(stderr,
                     "usage: powerwm_protocol_scanner <input.xml> <output.hpp> <output.cpp>");
        return EXIT_FAILURE;
    }
    return generate(argv[1], argv[2], argv[3]);
}
