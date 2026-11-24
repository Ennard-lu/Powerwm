# ADR-0008: No exceptions; use error codes

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [coding-style.md](../coding-style.md), [ADR-0002](0002-cxx26-toolchain.md), [ADR-0009](0009-c-api-wrapping.md)

## Context

Errors come from clients, GPU/DRM/input failures, config, cancellation, and resource
exhaustion. Exceptions introduce hidden control flow and complicate C callbacks, coroutines,
and realtime paths.

## Decision

1. The whole project builds with `-fno-exceptions`.
2. `throw`, `try`, and `catch` are forbidden and policy-scanned.
3. Recoverable errors return `pwm::result<T>` (`std::expected<T, error>`).
4. `error` carries a code, static message, source location, and optional errno.
5. Coroutine tasks store `result<T>` and short-circuit on failure.
6. Constructors do not perform fallible business initialization; factories return
   `result<raii<T>>`.
7. Destructors do not fail; they log and clean up.
8. Only broken invariants or unrecoverable startup failures terminate.

## Consequences

### Positive

- Explicit control flow; callers handle errors.
- Safe across C ABI, callbacks, and coroutines.
- Smaller exception tables and more predictable hot paths.
- Error codes are easy to log, count, expose over IPC, and test.

### Negative

- More boilerplate; needs helpers/monadic composition.
- Standard library calls that throw (`std::format` misuse) must be guarded.
- Third-party libraries need no-exception configuration or isolation.
- Constructors cannot report recoverable failure; factories are mandatory.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| Exceptions | concise, standard | hidden control flow | conflicts with rules |
| C-style error codes + out params | simple | easy to ignore | poor ergonomics |
| `std::error_code` | standard | category boilerplate | usable but verbose |
| `tl::expected` | older standard support | extra dependency | C++26 has expected |

## Revisit conditions

- A critical dependency cannot work with `-fno-exceptions` and cannot be isolated.
- Error-code ergonomics become a proven maintenance burden.
