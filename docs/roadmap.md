# powerwm roadmap

> Version: 2026-11 (Phase 3)
> Principle: **every phase must be demonstrable, testable, and reversible on its own.**

powerwm is a long build. It is split into eight phases: the first three establish display,
rendering, and protocol foundations; the next three make a usable desktop; the last two
turn it into a product that can be trusted daily.

```text
P0 Foundation ─► P1 Ignition ─► P2 Canvas ─► P3 Protocol ─► P4 Order ─► P5 Skin ─► P6 Ecosystem ─► P7 Polish
```

## 1. Status (2026-11)

| Area | Status |
| --- | --- |
| Repository | Apache-2.0, English docs, CI green |
| Build | CMake + GCC 16, `-fno-exceptions`, presets `dev`/`release`/`asan` |
| Display | DRM/KMS atomic modeset, page flips, VT pause/resume, hotplug restart |
| Input | libinput context and probe; not yet routed into the compositor |
| Session | libseat wrapper, device opening, enable/disable callbacks |
| Rendering | Vulkan scene renderer: textures, transforms, clipping, rounded corners, shadows, blur, damage |
| KMS bridge | Vulkan scene frames presented through DMA-BUF and KMS under the event loop, seat, and VT |
| Protocol | Not implemented |
| Tests | CTest smoke, event loop, headless output, geometry, scene damage, golden render, frame budget |

## 2. Phase overview

| Phase | Codename | Estimate | Core question | Demo |
| --- | --- | --- | --- | --- |
| P0 | Foundation | 2–3 weeks | Can we build and verify sustainably? | One-command build + green CI |
| P1 | Ignition | 4–6 weeks | Can we own display, input, and session? | TTY page-flip animation, VT switch |
| P2 | Canvas | 8–12 weeks | Can we compose efficiently and correctly? | Vulkan frames presented through KMS |
| P3 | Protocol | 14–20 weeks | Can we implement Wayland ourselves? | Real clients, clipboard, input |
| P4 | Order | 6–8 weeks | Can we manage windows well? | Tiling, workspaces, keybindings, IPC |
| P5 | Skin | 8–10 weeks | Can we deliver a coherent desktop? | Panel, launcher, notifications, lock screen |
| P6 | Ecosystem | 8–12 weeks | Can we fit the Linux ecosystem? | XWayland, portals, screencopy, IME |
| P7 | Polish | Ongoing | Can we use it every day? | 7-day dogfood, performance, packages |

Estimates are planning aids, not commitments. Drivers and protocol compatibility dominate
actual progress.

## 3. Phase 0 — Foundation

**Goal:** turn prototype code into a long-lived engineering project.

### Deliverables

- [x] Project naming, vision, roadmap, architecture, coding style, testing strategy, CI/CD.
- [x] GitHub Actions: format, policy, builds, sanitizers, release skeleton.
- [x] CMake presets and reproducible container.
- [x] License, SPDX headers, English docs.
- [x] Namespace `pwm`, fixed-width integer aliases, trailing return types.
- [x] `-fno-exceptions`; `pwm::result<T>` and error codes.
- [x] C API wrapping policy, dependency-ownership rule, valid-object contract.
- [x] Base/platform split; `powerwm_core`, application, tools, and tests.
- [x] CTest smoke, event-loop, and headless tests.
- [x] Build metadata: git commit, build time, compiler, flags, features.
- [ ] Full directory layout: `event`, `render`, `wayland`, `wm`, `shell`, `config`.
- [ ] Reflection isolated behind `base/meta.hpp`.
- [ ] Logging subsystem and assertion macros.
- [ ] Branch protection and required checks.

### Exit criteria

1. Fresh clone builds and tests with one preset command.
2. CI green on Linux x86_64 with no format exemptions.
3. `powerwm --version` and `--build-info` work without touching DRM.
4. At least 20 base unit tests.
5. ADR-0001 through ADR-0013 are accepted or superseded.

## 4. Phase 1 — Ignition

**Goal:** stable DRM/KMS output, input discovery, session handling, and hotplug foundations.

### Deliverables

- [x] DRM device enumeration (`/dev/dri/card*`) and primary output selection.
- [x] Atomic modesetting with connector/CRTC/plane properties.
- [x] Page-flip completion and poll-based event handling.
- [x] Double-buffered animation without tearing.
- [x] libseat session wrapper, device opening, enable/disable callbacks.
- [x] VT disable pauses commits; enable reacquires master and resumes.
- [x] libinput + libudev context and input probe.
- [x] udev DRM hotplug monitor with safe runtime restart.
- [x] Multi-fd poll event loop, eventfd and timerfd.
- [x] Headless output skeleton and test.
- [x] Read-only output diagnostics (`powerwm_probe`).
- [ ] Input routing: focus surfaces, grabs, shortcut interception.
- [ ] Full input-device enumeration and diagnostics.
- [ ] Nested backend.

### Validation

TTY3 hardware tests passed:

- 180 frames and 1200 frames of page-flip animation;
- 10 VT switches with seat disable/enable and resume;
- Vulkan DMA-BUF presentation through KMS.

Still pending: 1-hour soak, 50 VT switches, physical hotplug 10 times, idle CPU and
resource-leak checks. See [phase-1-validation.md](phase-1-validation.md).

## 5. Phase 2 — Canvas

**Goal:** a Vulkan renderer and scene graph for correct, measurable composition.

### Deliverables

- [x] `vulkanpp`: instance, physical/logical device, queue.
- [x] command pool, command buffer, fence, queue submit.
- [x] image, readback buffer, offscreen clear/readback.
- [x] DMA-BUF export and KMS framebuffer import.
- [x] Vulkan image presented directly through KMS page flip.
- [x] Vulkan → KMS path integrated with the event loop and seat/VT.
- [x] Render pass, graphics pipeline, shader modules, fullscreen triangle.
- [x] Shader triangle rendered into DMA-BUF and presented through KMS.
- [x] Textures, transforms, clipping, rounded corners, shadows, blur.
- [x] Scene node tree, transactions, and damage accumulation (foundation).
- [x] Scene graph rendering integration and surface nodes.
- [x] Geometry/region damage merging and unit tests.
- [x] Damage tracking at surface/scene/output levels in the renderer.
- [x] DMA-BUF modifier negotiation and format fallback.
- [x] Golden-image tests and frame budgets.

### Validation

- `powerwm_render_benchmark`: 100 animated 256×256 surfaces at 1920×1080 average
  3.08 ms per frame submission, maximum 5.66 ms, against a 16.67 ms budget.
- TTY3 hardware: `powerwm_vulkan_compositor` presented 900 frames across 10 VT
  switches with seat disable/enable and full redraw recovery.
- `unit.render`: deterministic golden pixels, partial-damage redraw, and zero
  steady-state heap allocations across 60 renderer frames.

### Exit criteria

1. 100 animated 256×256 surfaces at 1080p@60 on integrated graphics.
2. Local updates scale with damaged area.
3. Zero steady-state heap allocations per frame.
4. No torn or half-updated frames.
5. Headless rendering produces deterministic golden images.

## 6. Phase 3 — Protocol

**Goal:** implement the Wayland wire protocol and a coroutine protocol engine without
libwayland.

> Highest-risk phase. Split into 3A/3B/3C, 14–20 weeks total.

### 3A — Wire and coroutine runtime (4–6 weeks)

- [x] message headers and argument encoding;
- [x] XML parser and C++ descriptor generator (no `wayland-scanner`);
- [x] generated core and xdg-shell bindings with typed codecs;
- [x] C++26 reflection request dispatch;
- [x] basic object table mapping ids to generated interfaces and reflected handlers;
- [x] Unix socket transport, incremental message buffering, and partial writes;
- [x] `SCM_RIGHTS` fd transport in the socket wrapper;
- [x] basic `wl_display` sync/delete_id and `wl_registry` global/registry handling;
- [x] `wl_compositor`, `wl_region`, `wl_surface`, `wl_shm`, `wl_shm_pool`, `wl_buffer`
  server objects with surface state and commit/frame callbacks;
- [x] basic `xdg_wm_base`, `xdg_positioner`, `xdg_surface`, `xdg_toplevel`, and
  `xdg_popup` state with configure/ack_configure;
- [x] shm buffer mmap and surface commit pixel callback;
- [ ] Vulkan texture upload and surface scene rendering;
- [x] `wl_output` geometry/mode/scale/name/description events and `wl_seat`
  pointer/keyboard/touch object creation;
- [x] keyboard keymap, enter, key, and modifier event delivery;
- [x] pointer enter, leave, motion, button, axis, and frame delivery;
- [x] touch down, up, motion, and frame delivery;
- [x] protocol surfaces uploaded to Vulkan textures and presented through KMS;
- [x] `wl_subcompositor` and `wl_data_device_manager` object skeletons;
- [x] `zxdg_output_manager_v1`/`zxdg_output_v1` logical geometry and output metadata;
- [x] GTK4 Wayland client connects and commits a shm window;
- [ ] complete clipboard/DnD data transfer;
- [ ] client IDs, interface versions, destruction lifetime, per-client state;
- [ ] event loop coroutine scheduler, cancellation, timers;
- [x] in-tree socket handshake and raw-wire client verification;
- [x] in-tree socket handshake, shm surface commit, and KMS presentation;
- [ ] broader real-client matrix (GTK, Qt, foot) and wire fuzzing.

Exit: handshake with real libwayland clients; fd stress test; protocol errors do not
crash; 24-hour fuzz; no `libwayland-*` in `ldd`.

### 3B — Core compositor protocols (6–8 weeks)

- `wl_compositor`, `wl_surface`, `wl_region`, `wl_shm`;
- `xdg_wm_base`, `xdg_surface`, `xdg_toplevel`, `xdg_popup`;
- `wl_seat`, keyboard, pointer, touch; `wl_output`;
- `wl_data_device_manager`, `linux-dmabuf-v1`, buffer release;
- surface state machine: pending/current, commit, frame callbacks.

Exit: foot, weston-terminal, GTK4, Qt6 work; clipboard works; multi-output coordinates.

### 3C — Desktop protocol polish (4–6 weeks)

- viewporter, fractional scale, presentation time;
- xdg-decoration, xdg-activation, layer-shell;
- primary selection, pointer constraints, relative pointer;
- keyboard shortcuts inhibit, idle inhibit;
- protocol diagnostics and broader fuzzing.

Exit: layer-shell panels/lock screens; HiDPI correct; 12+ objects fuzzed; disconnects are safe.

### Protocol priority

| Priority | Protocol | Purpose |
| --- | --- | --- |
| P0 | wl_compositor, wl_surface, wl_region, wl_shm | surfaces and shared memory |
| P0 | wl_seat, wl_keyboard, wl_pointer, wl_touch | input |
| P0 | xdg_wm_base, xdg_surface, xdg_toplevel, xdg_popup | desktop windows |
| P0 | wl_output, zxdg_output_manager_v1 | output information |
| P0 | wl_data_device_manager | clipboard and DnD |
| P1 | linux-dmabuf-v1, feedback | zero-copy buffers |
| P1 | viewporter, fractional scale | scaling and HiDPI |
| P1 | presentation time | presentation feedback |
| P1 | xdg_decoration, xdg_activation | decoration and activation |
| P1 | layer shell | panels, backgrounds, lock screens |
| P1 | primary selection | middle-click selection |
| P2 | pointer constraints, relative pointer | games and remote desktop |
| P2 | keyboard shortcuts inhibit | VMs and games |
| P2 | idle inhibit / idle notify | power management |
| P2 | text-input-v3, input-method | input methods |
| P2 | session lock | lock screen |
| P2 | screencopy / image copy capture | screenshots and recording |
| P2 | foreign toplevel management | taskbars and switchers |
| P3 | gamma control, output management | color and output config |

## 7. Phase 4 — Order

**Goal:** window-management policy that makes the desktop usable.

- tiling layouts: master-stack, dwindle, spiral; pluggable policies;
- floating layer for dialogs and fixed-size windows;
- per-output workspaces, cross-output moves, persistence;
- focus model: click-to-focus, focus-follows-mouse, focus stack, urgency;
- window rules by app_id/title/role;
- keybinding modes, chords, repeats, hot reload;
- server-side decoration and client decoration negotiation;
- Unix-socket JSON IPC for state and actions;
- layout animations driven by the compositor clock;
- multi-output: primary, mirror, extend, migration after hotplug.

Exit: keyboard-only daily operations; 120 Hz layout animations; rules and keybindings reload
atomically; IPC is scriptable; 8-hour mixed load without crashes.

## 8. Phase 5 — Skin

**Goal:** a coherent, themeable desktop shell.

- panel: workspaces, titles, status, tray later;
- launcher: desktop entries, fuzzy search, keyboard-first;
- notification daemon with history, DND, actions;
- OSD for volume, brightness, layout changes;
- lock screen with session-lock and PAM, blurred background;
- static/dynamic wallpapers per output;
- theme tokens: color, radius, shadow, typography, spacing;
- animation system: easing, springs, one timeline, degradation;
- screenshot/recording UI.

Exit: complete desktop after first login; one design-token system; refresh-rate-independent
animation; lock screen cannot be bypassed; basic accessibility.

## 9. Phase 6 — Ecosystem

**Goal:** compatibility with the real Linux desktop.

- XWayland integration;
- `xdg-desktop-portal-powerwm` for screenshots, sharing, file picker;
- screencopy and PipeWire output;
- text-input-v3 + input-method-v2 with fcitx5/ibus;
- pointer constraints, relative pointer, keyboard lock, VRR experiments;
- gamma, basic ICC, night mode; experimental HDR;
- idle detection, DPMS, lid/power key policy via logind;
- multi-GPU and improved hotplug;
- packaging: Arch AUR, Debian, Fedora COPR.

Exit: common application matrix works; screen sharing works through portal + PipeWire;
CJK input methods work; XWayland scaling correct; installable on three distributions.

## 10. Phase 7 — Polish

**Goal:** from “works” to “trusted daily driver” and 1.0.

- stability: 7-day dogfood, crash reporting, recovery;
- performance: CI benchmarks with budgets, flame graphs;
- security: input validation, fuzzing, least privilege, seccomp evaluation;
- accessibility: screen reader protocol, keyboard navigation, contrast, scaling;
- docs: user manual, config reference, migration guide;
- release engineering: reproducible builds, signatures, checksums, rollback;
- code quality: dead code, API stabilization, dependency audit.

1.0 exit: 7 days of daily use with no P0/P1 defects; all performance budgets met; supported
GPU/distribution matrix tested; compatibility policy defined; security audit complete;
docs cover install, config, keybindings, IPC, and troubleshooting.

## 11. Version milestones

| Version | Phase | Content |
| --- | --- | --- |
| `v0.0.x` | P0 | Engineering foundation, no feature guarantees |
| `v0.1` | P1 | Stable DRM/KMS output, input, VT switching |
| `v0.2` | P2 | Vulkan rendering, scene graph, damage tracking |
| `v0.3` | P3 | In-tree protocol engine, real clients |
| `v0.4` | P4 | Tiling, workspaces, IPC |
| `v0.5` | P5 | Panel, launcher, notifications, lock screen, theme |
| `v0.6`–`v0.9` | P6 | XWayland, portals, IME, color, packaging |
| `v1.0` | P7 | Stable, daily-usable, installable, supported matrix |

`0.x` makes no ABI or config compatibility promise, but every breaking change must be in
`CHANGELOG.md`.

## 12. Explicit non-goals before 1.0

- No X11 window-manager mode.
- No wlroots and no wlroots plugin ABI.
- No libwayland dependency or compatibility layer.
- No EGL/GLES rendering path; software rasterization is for tests only.
- No Windows, macOS, or mobile support.
- No claim of supporting every Wayland protocol.
- No stable plugin ABI or embedded scripting runtime.
- HDR, VRR, and multi-GPU are not 1.0 blockers.

## 13. Risk register

| Risk | Probability | Impact | Mitigation |
| --- | --- | --- | --- |
| C++26 reflection/module toolchain instability | Medium | High | Isolate reflection; non-reflection fallback; pin GCC 16 |
| In-tree Wayland wire incompatibility | High | High | Real clients as black-box spec; wire tests + fuzzing |
| Protocol XML generator defects | Medium | High | Compare generated output with upstream XML |
| Coroutine scheduling/cancellation bugs | Medium | High | Invariant tests, cancellation stress, thread assertions |
| Protocol scope explosion | High | Medium | Prioritize real clients; phase whitelists |
| Vulkan external memory/modifier gaps | High | High | Capability detection, linear fallback, lavapipe tests |
| GPU driver variance | High | High | Capability probing, fallbacks, hardware matrix |
| No-exception third-party incompatibility | Medium | Medium | Validate dependencies; isolate wrappers |
| Input/focus state bugs | Medium | High | Property tests, invariants, real-client regression |
| Silent performance regressions | Medium | Medium | CI budgets, always-on frame statistics |
| Maintainer fatigue | Medium | High | Small increments, docs, automation, dogfood |
| Security vulnerabilities | Low | High | Fuzzing, validation, least privilege, audit |
| Packaging fragmentation | Medium | Medium | Support 2–3 distributions first; container builds |

## 14. Review cadence

- **Weekly:** update blockers; CI failures stay at zero.
- **Phase end:** demo against exit criteria; do not start the next phase if unmet.
- **Quarterly:** reorder priorities, add/supersede ADRs, review performance budgets.
- **Every major version:** retrospective on estimates, abstractions, and next risks.

> The roadmap is not a contract; it is a tool for reducing decision fatigue. Record deviations.
