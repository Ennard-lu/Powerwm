# ADR-0006: In-tree Wayland wire protocol + coroutine engine

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [architecture.md](../architecture.md), [roadmap.md](../roadmap.md), [ADR-0002](0002-cxx26-toolchain.md)

## Context

libwayland-server provides the wire format, object model, event loop, fd passing, and
scanner. It is a C callback API with global state, which conflicts with powerwm's no-exception,
RAII, and coroutine model.

## Decision

1. Do not link or include any `libwayland-*`.
2. Implement wire format: header, argument encoding, versions, errors, SCM_RIGHTS, partial I/O.
3. Implement object table, client IDs, interfaces, destruction, protocol errors.
4. Parse wayland-protocols XML and generate C++ descriptors (no `wayland-scanner`).
5. Implement `pwm::event_loop` and `pwm::event::task<T>`:
   - lazy start, error-code propagation, cancellation;
   - one `client_session` coroutine per connection.
6. Test with an in-tree wire client plus real external clients (foot, GTK, Qt, wayland-info).

## Consequences

### Positive

- Full control over protocol state, lifetime, errors, and scheduling.
- Linear coroutine code instead of callback chains.
- One error model for protocol errors, cancellation, and I/O.
- No libwayland ABI/version constraints.

### Negative

- We implement and maintain a protocol library.
- Wire compatibility is a high risk; every detail matters.
- Need an XML generator, test client, and fuzz infrastructure.
- Debug tooling must be built or borrowed externally.
- The protocol layer becomes the largest attack surface.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| libwayland-server | mature, compatible | C callbacks, global state | conflicts with rules |
| wlroots | higher-level | still libwayland, constraining | not in-tree |
| Smithay | modern, safe | Rust | wrong stack |
| libwayland + C++ wrapper | pragmatic | lifecycle limits | not coroutine-native |

## Revisit conditions

- The in-tree stack cannot cover daily clients at reasonable cost.
- Key protocols (IME, screen sharing) remain unreliable.
- The project prioritizes delivery speed over protocol ownership.
