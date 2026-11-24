# ADR-0007: Vulkan + internal vulkanpp

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [architecture.md](../architecture.md), [ADR-0003](0003-renderer-backend.md), [ADR-0008](0008-no-exceptions.md), [ADR-0009](0009-c-api-wrapping.md)

## Context

ADR-0003 chose EGL/GLES3 first. powerwm now chooses Vulkan directly, with an internal
`vulkanpp` RAII layer and no EGL/GLES rendering path.

`vulkanpp` means the in-tree module at `src/render/vulkanpp/` wrapping Vulkan C API. An
external library with the same name would require a new ADR.

## Decision

1. Vulkan is the only hardware rendering backend.
2. All Vulkan C API goes through `vulkanpp`:
   - no `Vk*` in business code;
   - creation returns `result<raii<T>>`;
   - destruction is `noexcept`, assumes validity, called once.
3. Presentation:
   - render to exportable `VkImage`;
   - export DMA-BUF via `VK_KHR_external_memory_fd` and `VK_EXT_external_memory_dma_buf`;
   - import the DMA-BUF as a KMS framebuffer;
   - commit with atomic page flip.
4. Client buffers import as Vulkan images; incompatible formats request redraw or fallback.
5. Headless tests use lavapipe/llvmpipe.
6. No EGL/GLES/GBM rendering path; GBM is only a temporary bring-up tool.

## Consequences

### Positive

- Explicit synchronization and memory control.
- One RAII/error model for GPU resources.
- Zero-copy DMA-BUF path to KMS.
- Validation layers catch resource errors.

### Negative

- More boilerplate and slower bring-up than GLES.
- Driver differences in external memory, modifiers, and formats.
- Higher debugging complexity.
- Need both headless and real-GPU validation.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| EGL/GLES3 | broad, fast bring-up | implicit state, less control | project chose Vulkan |
| Vulkan-Hpp | official C++ bindings | thin, different error model | not enough |
| wgpu/Dawn | cross-platform | heavy runtime | not native |
| Software | deterministic | too slow | tests only |

## Revisit conditions

- Vulkan cannot meet budgets or stability on target hardware.
- External memory/modifier support is impractical across drivers.
- `vulkanpp` cost exceeds its benefit.
