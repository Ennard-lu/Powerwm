# ADR-0011: `pwm` namespace and fixed-width integers

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [coding-style.md](../coding-style.md), [architecture.md](../architecture.md)

## Context

`powerwm` as a root namespace is noisy. Low-level code needs explicit integer widths for
protocol fields, KMS properties, and GPU formats. The prototype used `bc` and `bc::i32`,
which had no semantic meaning.

## Decision

1. Root namespace: `pwm`.
2. Submodules: `pwm::platform::drm`, `pwm::platform::input`, `pwm::render`,
   `pwm::vulkanpp`, `pwm::scene`, `pwm::wayland`, `pwm::wm`, `pwm::shell`, `pwm::config`,
   `pwm::ipc`.
3. Base aliases in `src/base/types.hpp`:
   - `i8/i16/i32/i64`, `u8/u16/u32/u64`;
   - `f32/f64`, `usize/isize`;
   - `handle_t`, `returned_t`.
4. Business code uses `pwm::i32` etc.; only alias definitions and C ABI boundaries use
   standard fixed-width names.
5. Macro prefix `PWM_`.
6. Include path `include/pwm/`, headers as `#include <pwm/...>`.

## Consequences

- Short, readable namespace.
- Explicit widths at protocol/DRM/Vulkan boundaries.
- Consistent with existing `i32`/`u32` style.
- `pwm` may conflict with a future dependency; full qualification resolves it.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| `powerwm::` | explicit | noisy | rejected |
| keep `bc::` | cheap migration | meaningless | rejected |
| raw `int32_t` | standard | verbose, error-prone | rejected |
| strong `Int32` type | type-safe | boilerplate | overkill |

## Revisit conditions

- `pwm` conflicts irreconcilably with a dependency.
- Aliases cause ABI/format problems that cannot be isolated.
