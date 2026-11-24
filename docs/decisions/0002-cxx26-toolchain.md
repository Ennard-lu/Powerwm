# ADR-0002: C++26 + GCC 16 + CMake/Ninja

- Status: Accepted (exception policy superseded by [ADR-0008](0008-no-exceptions.md))
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [coding-style.md](../coding-style.md), [ci-cd.md](../ci-cd.md)

> Update 2026-11-24: “exceptions not used as control flow” became a full
> `-fno-exceptions` policy. Language, compiler, and build-system choices remain.

## Context

The project needs low-level systems programming, protocol/property mapping, maintainability,
and performance. The prototype already uses C++26 reflection with GCC 16.

## Decision

1. C++26; primary compiler GCC 16+.
2. CMake ≥3.25 + Ninja + presets.
3. Recoverable errors use `std::expected<T, error>`; no exception control flow (now no
   exceptions at all).
4. All reflection is isolated in `base/meta.hpp`; core logic has a non-reflection fallback.
5. Start with headers; evaluate C++26 modules in Phase 2, beginning with `base`.
6. clang-format is mandatory; clang-tidy when it can parse C++26.

## Consequences

### Positive

- Reflection removes repetitive protocol/property/config mapping.
- `expected`, spans, concepts, and ranges improve safety and clarity.
- Presets keep local, CI, and container builds identical.
- Native interop with Linux C libraries.

### Negative

- GCC 16 is new; contributors need the container or manual install.
- Clang tooling may lag behind C++26.
- Reflection may change; isolation is required.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| C++23 | mature tooling | no reflection; more boilerplate | gives up key feature |
| Rust | memory safety | skill/code mismatch | wrong stack |
| Zig | clean, comptime | weaker Wayland ecosystem | maturity |
| C | portable | manual lifetime cost | long-term cost |

## Revisit conditions

- GCC 16 is unavailable in target distributions for a long period.
- Reflection changes make migration costlier than its benefit.
- Module migration proves more expensive than headers.
