# Changelog

All notable changes to powerwm are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); release versions follow the
pre-1.0 `0.x.y` convention.

## [Unreleased]

### Added

- Phase 0 engineering foundation: roadmap, architecture, coding style, testing strategy,
  CI/CD, and ADRs.
- CMake presets: `dev`, `release`, `asan`.
- GitHub Actions: format, policy, Debug/Release/ASan+UBSan builds, tests, release skeleton.
- Reproducible development container with GCC 16 and Vulkan.
- CTest smoke tests plus event-loop and headless output tests.
- `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore`.
- `tools/check_policy.sh`: no exceptions, no leeway for libwayland, C API wrapping,
  integer aliases.
- `tools/check_comments.py`: no explanatory comments; English API docs and SPDX only.
- Apache-2.0 `LICENSE` and SPDX headers.
- ADR-0006 through ADR-0013: in-tree protocol stack, Vulkan/vulkanpp, no exceptions,
  C API wrapping, license, namespace/integers, dependent resources, valid-object contract.
- `cmake/PowerWMBuildInfo.cmake`: git commit, branch, dirty state, UTC build time,
  compiler, flags, features.
- `powerwm --build-info` prints build metadata.
- English README; Chinese README moved to `docs/README.zh-CN.md`.
- Phase 1: DRM/KMS page flips, double buffering, libinput, libseat, udev hotplug,
  headless output, event loop, VT pause/resume.
- Phase 2: Vulkan instance/device/queue, command pools, fences, images, readback buffers,
  offscreen clear, DMA-BUF export, KMS framebuffer import, direct KMS presentation,
  render pass, graphics pipeline, shaders, fullscreen-triangle readback, and shader
  triangle presentation through DMA-BUF + KMS.
- Phase 2 foundation: geometry/region damage merging, scene node tree, transactions,
  and unit tests for geometry and scene damage.
- Phase 2 completion: textured scene renderer with rounded corners, shadows, blur,
  rotation/scale transforms, parent clipping, and per-damage scissor redraw.
- Scene style/damage API, surface invalidation, and scene damage collection.
- DMA-BUF format fallback and DRM format modifier negotiation.
- `powerwm_vulkan_compositor`: event-loop compositor with seat/VT handling and
  double-buffered KMS presentation.
- `powerwm_render_benchmark`: 100 animated 256×256 surfaces at 1080p.
- `unit.render`: golden pixels, partial redraw, and steady-state allocation checks.
- Phase 3 foundation: in-tree XML parser, protocol model, vendored `wayland.xml` and
  `xdg-shell.xml`, build-time C++ binding scanner, typed message encode/decode, wire
  header/argument reader/writer, and reflection-based request dispatch.
- `unit.wayland`: XML parsing, wire round-trip, generated descriptors, reflected
  handler dispatch, object-table dispatch, and real socket transport.
- Phase 3 transport: nonblocking Unix sockets, `SCM_RIGHTS` fd passing, incremental
  reads and partial writes, connection framing, and basic `wl_display`/`wl_registry`
  server objects.
- Phase 3 core objects: `wl_compositor`, `wl_region`, `wl_surface`, `wl_shm`,
  `wl_shm_pool`, and `wl_buffer` with damage, commit, and frame callback handling.
- Phase 3 xdg-shell: `xdg_wm_base`, `xdg_positioner`, `xdg_surface`, `xdg_toplevel`,
  and `xdg_popup` state, configure/acknowledge flow, role metadata, and transport test.
- Phase 3 shm: memfd mapping, buffer pixel views, `SCM_RIGHTS` pool transfer, and
  surface commit callback with validated buffer dimensions/stride/format.
- Phase 3 output/seat: `wl_output` geometry/mode/scale/name/description events and
  `wl_seat` pointer/keyboard/touch object creation.
- `powerwm_wayland_server`: event-loop Unix socket server with core globals, registry,
  sync, shm surfaces, xdg-shell, output, and seat objects; verified with a raw wire client.
- `powerwm_wayland_kms_compositor`: protocol clients → shm commit → Vulkan texture →
  scene → DMA-BUF → KMS page flip; verified on TTY3 with an in-tree wire client.
- Keyboard keymap, enter, key, and modifier events; libinput keyboard forwarding in the
  KMS compositor.
- Pointer enter/leave/motion/button/axis/frame events, scene hit testing, and libinput
  pointer forwarding.
- Touch down/up/motion/frame events and libinput touch forwarding.
- Surface-scene bridge: shm commits upload strided BGRA/RGBA textures into scene nodes
  and render through the Vulkan scene renderer.
- `wl_subcompositor`/`wl_subsurface` and `wl_data_device_manager`/data source/device/
  offer object skeletons.
- `zxdg_output_manager_v1`/`zxdg_output_v1` with logical position, logical size, name,
  description, and done events.
- `wp_viewporter`, `wp_fractional_scale_manager_v1`,
  `zxdg_decoration_manager_v1`, and `xdg_activation_v1` protocol objects with scale and
  decoration configure events.
- `wp_presentation`/`wp_presentation_feedback` with feedback deferred to the next KMS
  page flip; GTK4 validation passes with the global advertised.
- Cross-client clipboard: `wl_data_source` mime offers, `wl_data_device.set_selection`,
  compositor-brokered `wl_data_offer` creation, accept targets, and `receive` file
  descriptor forwarding between clients.
- Drag-and-drop: `start_drag` tracking, data-device `data_offer`/`enter`/`motion`/`drop`
  events, offer actions, `dnd_drop_performed`, `dnd_finished`, and cancellation.
- Real GTK4 Wayland client validation: GTK connects, creates surfaces, and commits a
  shm window through the KMS compositor.
- Diagnostics: `powerwm_probe`, `powerwm_vulkan_probe`, `powerwm_vulkan_dmabuf_probe`,
  `powerwm_input_probe`, `powerwm_seat_probe`, `powerwm_vulkan_pipeline_probe`.

### Changed

- C++ sources formatted with the repository `.clang-format`.
- Root namespace migrated from `bc` to `pwm`; `LIBDRMPP_*` macros to `PWM_*`.
- Fixed-width aliases (`pwm::i32`, `u32`, `usize`, ...) are project-wide.
- `-fno-exceptions` enabled; errors use `pwm::result<T>` and error codes.
- Renderer direction: Vulkan + `vulkanpp`; protocol direction: in-tree wire protocol +
  coroutines; no libwayland.
- C API wrappers no longer validate object legality; callers guarantee valid objects.
- Named functions use trailing return types (`auto foo() -> T`).
- Root README simplified to features, goals, and status.
- Documentation translated to English except `docs/README.zh-CN.md`.

### Known limitations

- Not usable as a daily desktop yet.
- Wayland protocol stack, scene graph, tiling policy, and shell are not implemented.
- Output selection uses the first connected connector and first plane; compatibility
  matching is incomplete.
- Hotplug restarts the output runtime rather than preserving client state.
- HDR, VRR, multi-GPU, and full color management are future work.
