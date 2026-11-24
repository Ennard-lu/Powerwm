# ADR-0013: C API wrappers assume the object is valid

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [ADR-0009](0009-c-api-wrapping.md), [coding-style.md](../coding-style.md)

## Context

Early wrappers checked `nullptr`, `fd >= 0`, and `handle != nullptr` in `destroy`, `wait`,
`dispatch`, and `receive`. `raii` also checked `valid()` during construction.

This treated caller contract violations as recoverable errors, duplicated lifetime logic,
and prevented move-only/partially-owned types.

## Decision

1. `create` / `open_*` detect failure and return `result<T>`.
2. After successful creation, callers guarantee the object remains valid.
3. Wrappers do not check nullability in methods or `destroy`.
4. `raii` does not call `valid()`; it owns once and moves.
5. `valid()` may remain as an explicit caller query, but wrappers never rely on it.
6. A default-constructed RAII wrapper owns nothing and does not destroy.
7. Double destroy, use-after-destroy, and invalid handles are caller bugs with undefined
   behavior.

## Consequences

### Positive

- Thin, readable wrappers with no defensive noise.
- `raii` supports move-only and default-constructed-but-empty types.
- Error source is clear: creation failure in `create`, usage errors at the caller.
- Matches the C API precondition model.

### Negative

- Callers must strictly follow lifetimes and preconditions.
- Invalid-handle crashes move closer to the C API.
- Requires stronger tests and review of call order.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| Validate every method | defensive | noisy, hides contract bugs | conflicts with model |
| Return errors for invalid objects | recoverable | cannot truly recover | false safety |
| `optional` around handles | type-safe | unwrap layers | overengineering |

## Revisit conditions

- Systemic invalid-handle bugs cannot be prevented by ordering/tests.
- Wrappers must operate on an untrusted plugin boundary.
