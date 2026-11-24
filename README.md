# powerwm

> A modern Wayland compositor and tiling window manager for Linux, built from scratch in C++26.

[![CI](https://github.com/Ennard-lu/Powerwm/actions/workflows/ci.yml/badge.svg)](https://github.com/Ennard-lu/Powerwm/actions/workflows/ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

## Vision

powerwm aims to be a compositor that feels fast, predictable, and coherent. It owns the
entire graphics path instead of wrapping another compositor framework: display, input,
rendering, protocol handling, window policy, and the desktop shell are designed together.

The long-term goal is a daily-driver Wayland desktop with tiling as a first-class model,
Vulkan-native rendering, measurable latency, and a visual language that is consistent
from the first frame.

## What makes powerwm different

- **Native by design** — direct DRM/KMS and input integration, with no compositor framework
  in between.
- **Vulkan-first rendering** — a modern GPU path designed for low latency and explicit
  control over synchronization and memory.
- **A protocol stack built in-tree** — Wayland handling is part of the project, not an
  external black box.
- **Tiling as the default** — workspaces, layouts, focus, and rules are part of the core
  model.
- **Performance with a budget** — frame timing, input latency, and steady-state behavior
  are treated as features, not afterthoughts.
- **A cohesive desktop** — panel, launcher, notifications, lock screen, animation, and
  theming share one visual and interaction model.
- **Built for the long run** — explicit architecture, decision records, and quality gates
  instead of short-lived hacks.

## Current status

**Phase 3 in progress — in-tree Wayland protocol scanner, bindings, and reflection dispatch. Not usable as a daily desktop yet.**

Working today:

- native DRM/KMS output with atomic modesetting and page flips
- double-buffered animation and VT pause/resume
- seat, input, hotplug, and event-loop foundations
- Vulkan scene renderer with textures, transforms, clipping, rounded corners, shadows, and blur
- surface/scene/output damage tracking with partial redraw
- DMA-BUF format/modifier negotiation and KMS page-flip presentation
- event-loop compositor with seat and VT pause/resume
- golden-image, damage, allocation, and frame-budget tests
- headless output and diagnostics for DRM outputs and Vulkan devices
- in-tree XML protocol scanner generating Wayland core and xdg-shell bindings
- wire message reader/writer and reflection-based request dispatch
- Unix socket transport, incremental framing, `SCM_RIGHTS`, and basic `wl_display`/
  `wl_registry` handling
- compositor, region, surface, shm pool, and buffer protocol state with frame callbacks
- xdg-shell role state, configure/acknowledge handshake, and popup events

Still ahead:

- Wayland socket transport, object table, coroutine sessions, and real clients
- tiling layouts, workspaces, and window policy
- the desktop shell, theming, and animation system
- ecosystem compatibility, packaging, and 1.0 hardening

## Roadmap

| Phase | Focus | Status |
| --- | --- | --- |
| 0 | Foundation | complete |
| 1 | Native display, input, seat, hotplug | implemented, hardware validation in progress |
| 2 | Vulkan rendering and scene graph | complete |
| 3 | Wayland protocol stack | **in progress** |
| 4 | Tiling window management | upcoming |
| 5 | Desktop shell and visual system | upcoming |
| 6 | Ecosystem compatibility | upcoming |
| 7 | Stability, performance, packaging, 1.0 | ongoing |

## Documentation

- [Documentation index](docs/README.md)
- [Chinese README](docs/README.zh-CN.md)
- [Roadmap](docs/roadmap.md)
- [Architecture](docs/architecture.md)
- [Design decisions](docs/decisions/)

Build instructions, coding rules, testing strategy, CI details, and hardware validation
live in the documentation above.

## Contributing

Contributions are welcome. Start with the roadmap, open an issue before architectural
changes, and keep pull requests small and testable.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow.

## License

Apache License 2.0. See [LICENSE](LICENSE).

**powerwm** — build the foundation first.
