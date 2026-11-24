# Protocol scanner and reflected dispatch

powerwm does not use `wayland-scanner` or any `libwayland-*` code. Protocol XML is
parsed in-tree, C++ bindings are generated at build time, and request handlers are
resolved through C++26 reflection.

## Pipeline

```text
protocols/*.xml
      │
      ▼
powerwm_protocol_scanner
      │  generated <name>_bindings.hpp
      │  generated <name>_bindings.cpp
      ▼
interface descriptors + typed message codecs
      │
      ▼
wayland::dispatch_request<Implementation, Interface>
      │  std::meta::members_of
      ▼
Implementation::request_<message>(...)
```

Generated files live under `build/<preset>/generated/` and are not checked in.

## Scanner

```bash
./build/dev/powerwm_protocol_scanner \
    protocols/wayland.xml \
    build/dev/generated/wayland_bindings.hpp \
    build/dev/generated/wayland_bindings.cpp
```

The scanner currently handles protocol interfaces, requests, events, arguments,
enums, `since`, destructors, `new_id` with and without an interface, strings,
arrays, fixed-point values, objects, and file descriptors.

CMake runs the scanner for `wayland.xml` and `xdg-shell.xml` and compiles the result
into `powerwm_wayland`.

## Generated descriptor

Each interface exposes its names, opcodes, argument tuples, and the handler names
used by reflection:

```cpp
struct wl_display {
    static constexpr std::string_view name = "wl_display";
    static constexpr u32 request_count = 2;
    static constexpr std::array<std::string_view, 2> request_handler_names{
        "request_sync", "request_get_registry"};

    struct request_sync {
        static constexpr u16 opcode = 0;
        using argument_types = std::tuple<object_id>;
        argument_types arguments{};
        static auto decode(protocol_reader& reader) -> result<request_sync>;
    };

    using requests = std::tuple<request_sync, request_get_registry>;
};
```

## Reflected handler

Implement the handler as an ordinary C++ member function. The dispatcher finds it by
identifier at compile time:

```cpp
struct display_implementation {
    auto request_sync(object_id callback) -> result<void> {
        return {};
    }

    auto request_get_registry(object_id registry) -> result<void> {
        return {};
    }
};

protocol_reader reader{payload, fds};
display_implementation implementation;
auto dispatched = wayland::dispatch_request<display_implementation,
                                            generated::wl_display>(
    implementation, opcode, reader);
```

A missing handler returns `error_code::unsupported`. A malformed argument returns
`error_code::protocol`. The dispatcher passes `std::string_view` and
`std::span<const std::byte>` views over the current message buffer; copy them when
they must outlive dispatch.

## Wire layer

`wayland/wire.hpp` provides:

- `message_header` encoding and validation;
- integer, unsigned, fixed-point, string, array, object, `new_id`, and fd arguments;
- four-byte alignment for strings and arrays;
- fd lists passed separately from the message payload.

Socket transport, `SCM_RIGHTS`, object tables, coroutine sessions, and real-client
compatibility are the next Phase 3 steps.
