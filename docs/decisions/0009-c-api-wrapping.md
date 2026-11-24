# ADR-0009: C APIs require create/destroy + RAII

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [coding-style.md](../coding-style.md), [ADR-0008](0008-no-exceptions.md), [ADR-0007](0007-vulkan-vulkanpp.md), [ADR-0012](0012-dependent-resource-creation.md), [ADR-0013](0013-c-api-assumes-valid.md)

## Context

powerwm uses POSIX, DRM/KMS, libinput, libudev, libseat, Vulkan, and future PipeWire/XWayland
C APIs. Their lifetime and error conventions differ; bare calls cause fd leaks, double frees,
and dangling handles.

## Decision

1. Every C API resource is wrapped before business code uses it.
2. Wrapper shape:
   - allocation: `static auto create(...) -> result<raii<T, Args...>>` or `create_*`;
   - destruction: `auto destroy(Args...) const noexcept -> void`;
   - validity: optional caller query only.
3. Business code holds `raii<T,...>` or a dedicated RAII type.
4. `destroy` assumes a valid object, is `noexcept`, and is called exactly once.
5. C headers live only in `base/`, `platform/`, `render/vulkanpp/`.
6. Convert C constants/enums to project types at the boundary.
7. Dependent resources are created by their host (ADR-0012).
8. Wrappers do not check null/fd/handle legality (ADR-0013).
9. Each wrapper tests creation failure, normal destruction, move, and ownership transfer.
10. CI runs `tools/check_policy.sh` for bare C calls and header leakage.

## Consequences

- Lifetimes are centralized and auditable.
- No-exception model is consistent: create returns `result`, destroy never fails.
- Business code is closer to value semantics and testable headlessly.
- More wrapper code, and performance must be measured at the boundary.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| Raw C API | no boilerplate | lifetime bugs | violates project rules |
| `unique_ptr` + deleter | standard | no multi-arg destroy/validity | insufficient |
| Third-party RAII | less code | dependency/style mismatch | current raii suffices |
| Vulkan-Hpp | official | still needs wrapping | can be used internally |

## Revisit conditions

- `raii` cannot express a resource (e.g. async GPU destruction).
- Wrapping becomes a measured bottleneck requiring zero-overhead paths.
