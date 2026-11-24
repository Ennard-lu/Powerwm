# ADR-0001: Native DRM/KMS compositor, no wlroots

- Status: Accepted (protocol choice superseded by [ADR-0006](0006-own-wayland-protocol.md); renderer choice by [ADR-0007](0007-vulkan-vulkanpp.md))
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [architecture.md](../architecture.md), [roadmap.md](../roadmap.md)

> Update 2026-11-24: libwayland-server and EGL/GLES choices were replaced. The native
> DRM/KMS and “no wlroots” decisions remain.

## Context

A Wayland compositor can be built on wlroots, extended from Mutter/KWin, or constructed
directly on DRM/KMS, libinput, and a protocol implementation. powerwm wants control over
the frame lifecycle, rendering, and input path.

## Decision

- Use DRM/KMS atomic modesetting directly through libdrm.
- Use libinput + libudev + libseat for input and sessions.
- Implement the Wayland protocol in-tree (ADR-0006).
- Use a custom renderer abstraction with Vulkan (ADR-0007).
- Do not link wlroots or provide a wlroots plugin ABI.

## Consequences

### Positive

- Full control over frame scheduling, damage, commits, and synchronization.
- Scene and tiling models can be designed for this compositor.
- Every layer is testable and observable.

### Negative

- Large protocol, input, session, and recovery workload.
- Driver and compatibility issues are ours to solve.
- No reuse of wlroots plugins or tests.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| wlroots | mature, fast | abstraction limits frame/scene control | conflicts with ownership goal |
| Mutter/KWin extension | mature ecosystem | plugin layer, not a core rewrite | not from scratch |
| Smithay (Rust) | modern, memory-safe | language mismatch | C++26 is required |
| X11-only | simpler | declining ecosystem | wrong direction |

## Revisit conditions

- A required capability becomes impractical to build in-tree.
- Protocol maintenance cost exceeds the benefit of ownership.
- Project goal changes from “own the stack” to “ship quickly.”
