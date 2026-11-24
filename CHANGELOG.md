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
