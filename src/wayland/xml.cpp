// SPDX-License-Identifier: Apache-2.0

#include <wayland/xml.hpp>

#include <cctype>
#include <utility>

namespace pwm::wayland {

namespace {

auto is_space(char value) -> bool {
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

auto is_name_char(char value) -> bool {
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_' || value == '-' ||
           value == ':' || value == '.';
}

auto starts_with(std::string_view input, usize position, std::string_view value) -> bool {
    return position + value.size() <= input.size() && input.substr(position, value.size()) == value;
}

auto decode_entities(std::string_view input) -> std::string {
    std::string result;
    result.reserve(input.size());
    for (usize index = 0; index < input.size(); ++index) {
        if (input[index] != '&') {
            result.push_back(input[index]);
            continue;
        }
        const usize semicolon = input.find(';', index);
        if (semicolon == std::string_view::npos) {
            result.push_back(input[index]);
            continue;
        }
        const std::string_view entity = input.substr(index + 1, semicolon - index - 1);
        if (entity == "amp") {
            result.push_back('&');
        } else if (entity == "lt") {
            result.push_back('<');
        } else if (entity == "gt") {
            result.push_back('>');
        } else if (entity == "quot") {
            result.push_back('"');
        } else if (entity == "apos") {
            result.push_back('\'');
        } else if (!entity.empty() && entity.front() == '#') {
            const bool hexadecimal = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string_view digits = entity.substr(hexadecimal ? 2 : 1);
            u32 codepoint = 0;
            for (const char digit : digits) {
                const u32 value =
                    hexadecimal
                        ? static_cast<u32>(
                              std::isxdigit(static_cast<unsigned char>(digit)) != 0
                                  ? (digit <= '9' ? digit - '0' : (digit | 0x20) - 'a' + 10)
                                  : 0)
                        : static_cast<u32>(std::isdigit(static_cast<unsigned char>(digit)) != 0
                                               ? digit - '0'
                                               : 0);
                codepoint = codepoint * (hexadecimal ? 16U : 10U) + value;
            }
            if (codepoint >= 0x80U) {
                result.push_back('?');
            } else {
                result.push_back(static_cast<char>(codepoint));
            }
        } else {
            result.append(input.substr(index, semicolon - index + 1));
        }
        index = semicolon;
    }
    return result;
}

class parser {
  public:
    explicit parser(std::string_view input) noexcept : input_(input) {
    }

    auto parse() -> result<xml_node> {
        skip_misc();
        if (!starts_with(input_, position_, "<")) {
            return std::unexpected(make_error(error_code::protocol, "expected XML root element"));
        }
        xml_node root;
        if (auto parsed = parse_node(root); !parsed) {
            return std::unexpected(parsed.error());
        }
        return root;
    }

  private:
    auto fail_void(std::string_view message) const noexcept -> result<void> {
        return std::unexpected(make_error(error_code::protocol, message));
    }

    auto skip_space() noexcept -> void {
        while (position_ < input_.size() && is_space(input_[position_])) {
            ++position_;
        }
    }

    auto skip_until(std::string_view terminator) noexcept -> bool {
        const usize found = input_.find(terminator, position_);
        if (found == std::string_view::npos) {
            position_ = input_.size();
            return false;
        }
        position_ = found + terminator.size();
        return true;
    }

    auto skip_misc() noexcept -> void {
        while (true) {
            skip_space();
            if (starts_with(input_, position_, "<?")) {
                skip_until("?>");
                continue;
            }
            if (starts_with(input_, position_, "<!--")) {
                skip_until("-->");
                continue;
            }
            if (starts_with(input_, position_, "<!")) {
                skip_until(">");
                continue;
            }
            break;
        }
    }

    auto parse_name() noexcept -> std::string_view {
        const usize start = position_;
        while (position_ < input_.size() && is_name_char(input_[position_])) {
            ++position_;
        }
        return input_.substr(start, position_ - start);
    }

    auto parse_attribute(xml_attribute& attribute) -> result<void> {
        skip_space();
        attribute.name = std::string{parse_name()};
        if (attribute.name.empty()) {
            return fail_void("expected XML attribute name");
        }
        skip_space();
        if (position_ >= input_.size() || input_[position_] != '=') {
            return fail_void("expected '=' after XML attribute");
        }
        ++position_;
        skip_space();
        if (position_ >= input_.size() || (input_[position_] != '"' && input_[position_] != '\'')) {
            return fail_void("expected quoted XML attribute");
        }
        const char quote = input_[position_++];
        const usize start = position_;
        const usize end = input_.find(quote, position_);
        if (end == std::string_view::npos) {
            return fail_void("unterminated XML attribute");
        }
        attribute.value = decode_entities(input_.substr(start, end - start));
        position_ = end + 1;
        return {};
    }

    auto parse_node(xml_node& node) -> result<void> {
        ++position_;
        node.name = std::string{parse_name()};
        if (node.name.empty()) {
            return fail_void("expected XML element name");
        }

        while (true) {
            skip_space();
            if (starts_with(input_, position_, "/>")) {
                position_ += 2;
                return {};
            }
            if (position_ < input_.size() && input_[position_] == '>') {
                ++position_;
                break;
            }
            xml_attribute attribute;
            if (auto parsed = parse_attribute(attribute); !parsed) {
                return std::unexpected(parsed.error());
            }
            node.attributes.push_back(std::move(attribute));
        }

        while (position_ < input_.size()) {
            if (starts_with(input_, position_, "</")) {
                position_ += 2;
                const std::string_view closing = parse_name();
                if (closing != node.name) {
                    return fail_void("mismatched XML closing tag");
                }
                skip_space();
                if (position_ >= input_.size() || input_[position_] != '>') {
                    return fail_void("unterminated XML closing tag");
                }
                ++position_;
                return {};
            }
            if (starts_with(input_, position_, "<!--")) {
                skip_until("-->");
                continue;
            }
            if (starts_with(input_, position_, "<?")) {
                skip_until("?>");
                continue;
            }
            if (starts_with(input_, position_, "<!")) {
                skip_until(">");
                continue;
            }
            if (input_[position_] != '<') {
                const usize start = position_;
                while (position_ < input_.size() && input_[position_] != '<') {
                    ++position_;
                }
                node.text += decode_entities(input_.substr(start, position_ - start));
                continue;
            }
            xml_node child;
            if (auto parsed = parse_node(child); !parsed) {
                return std::unexpected(parsed.error());
            }
            node.children.push_back(std::move(child));
        }
        return fail_void("unterminated XML element");
    }

    std::string_view input_;
    usize position_ = 0;
};

}

auto xml_node::attribute(std::string_view key) const noexcept -> std::string_view {
    for (const auto& entry : attributes) {
        if (entry.name == key) {
            return entry.value;
        }
    }
    return {};
}

auto parse_xml(std::string_view input) noexcept -> result<xml_node> {
    parser reader{input};
    return reader.parse();
}

}
