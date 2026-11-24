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

`wayland/object_table.hpp` maps object ids to generated interface names/versions and
the reflected dispatcher:

```cpp
object_table table;
add_object<display_implementation, generated::wl_display>(table, 1, implementation);
table.dispatch(1, opcode, reader);
```

`platform/wayland/socket.hpp` owns nonblocking Unix sockets, incremental receive
buffers, partial sends, and `SCM_RIGHTS` fd transport. `wayland/connection.hpp` frames
messages from the byte stream and routes them through the object table.

`wayland/display.hpp` provides the first server objects:

- `wl_display.sync` → `wl_callback.done` plus `wl_display.delete_id`;
- `wl_display.get_registry` → `wl_registry.global` for registered globals;
- `wl_registry.bind` creates compositor/shm objects through reflection dispatch;
- `wl_compositor`, `wl_region`, `wl_surface`, `wl_shm`, `wl_shm_pool`, and `wl_buffer`
  track protocol state, damage, commit, and frame callbacks;
- `xdg_wm_base`, `xdg_positioner`, `xdg_surface`, `xdg_toplevel`, and `xdg_popup`
  track role state, titles, app ids, geometry, configure serials, and popup events;
- `wl_output` emits geometry, mode, scale, name, description, and done;
- `wl_seat` emits capabilities/name and creates pointer, keyboard, and touch objects;
- shm pools are mmap'd; surface commit invokes a pixel callback with the current buffer
  view, ready for texture upload;
- Vulkan texture upload and scene nodes are implemented in `wayland/surface_scene.hpp`;
  KMS presentation of protocol surfaces is the next integration step.

`powerwm_wayland_server <socket-path> <seconds>` runs the protocol stack on the project
event loop. It advertises compositor, shm, output, seat, and xdg-shell globals, accepts
clients, dispatches requests, and logs committed surface buffers. A raw wire client can
connect to validate `wl_registry`, `wl_display.sync`, and global discovery without
`libwayland`.

`powerwm_wayland_kms_compositor` combines that server with the Vulkan scene renderer and
KMS presentation. Client shm commits become textures, scene nodes, DMA-BUF frames, and
atomic page flips. Keyboard keymap and key/modifier events are forwarded through
`wl_keyboard`; pointer enter/leave/motion/button/axis/frame and touch
down/up/motion/frame events use scene hit testing.

A GTK4 test client now connects through the KMS compositor, creates surfaces, and commits
a shm window. `wl_subcompositor` and `wl_data_device_manager` object skeletons are
available; `zxdg_output_manager_v1` provides logical output geometry and metadata for
GTK-style clients. Viewporter, fractional scaling, and xdg-decoration objects are
available. Full clipboard/DnD transfer is still pending.

Client-id lifetime, complete clipboard/DnD, linux-dmabuf, xdg-output/viewporter,
coroutine sessions, and broader real-client validation are the next Phase 3 steps.
