# ADR-0003: GLES3/EGL first, Vulkan later

- Status: Superseded by [ADR-0007](0007-vulkan-vulkanpp.md)
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [architecture.md](../architecture.md), [testing.md](../testing.md)

> Superseded 2026-11-24: powerwm moved directly to Vulkan + internal `vulkanpp`. This
> document is kept for history.

## Context

A compositor needs broad GPU support, DMA-BUF import, damage rendering, effects, and
testability. Candidates were EGL/GLES3, Vulkan, software, and cross-platform abstractions.

## Decision

- Start with EGL + GLES3 + GBM.
- Keep a thin `renderer` interface with no GL types above it.
- Add Vulkan later if measurements justify it.
- Use a headless backend for CI and golden images.

## Consequences

- Broad driver coverage and faster early iteration.
- GL state-machine and driver differences must be wrapped.
- Less explicit control than Vulkan.
- Two backends may eventually need maintenance.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| Vulkan first | modern, explicit | slower bring-up | deferred |
| Software | deterministic | too slow | tests only |
| wgpu/Dawn | cross-platform | heavy dependency | not native enough |
| Pixman | simple | CPU-bound | fallback only |

## Revisit conditions

- GLES cannot meet frame budgets.
- Vulkan coverage and maintenance cost become favorable.
- HDR/explicit synchronization require Vulkan.

This ADR was revisited and replaced by ADR-0007.
