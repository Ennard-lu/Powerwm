# ADR-0012: Dependent resources are created by their owner

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [ADR-0009](0009-c-api-wrapping.md), [coding-style.md](../coding-style.md)

## Context

Early DRM wrappers created dependent resources like:

```cpp
dumb_buffer::create(device, format);
framebuffer::create(device, buffer);
```

This put creation on the dependent type even though the resource belongs to the device.
Callers could pass the wrong device, outlive it, or duplicate device ownership.

## Decision

The host creates dependent resources:

```cpp
device.create_dumb_buffer(format);
device.create_framebuffer(buffer);
device.create_property_blob(data);
```

Rules:

1. Dependent constructors are private.
2. The host is a friend and the only creator.
3. Resources store the host handle; `destroy()` and `map()` need no extra device argument.
4. No `dependent::create(owner, ...)` or `dependent::from(owner, ...)`.
5. The host must outlive the resource; this is stated in English API comments.
6. Host-independent resources may keep `create()`.

## Consequences

### Positive

- Clear ownership and lifetime.
- Impossible to pass the wrong device at the call site.
- Constructors cannot be bypassed.
- Easier GPU/DRM handle auditing.

### Negative

- Resources store the host handle.
- Using a resource after host destruction is still a caller error.
- Slight coupling via `friend`.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| `dependent::create(owner, ...)` | familiar | ownership mismatch | being removed |
| Builder | extensible | boilerplate | overkill |
| Global factory | centralized | global state | conflicts with RAII/testing |
| Store `device&` | type-safe | lifetime audit | handle is simpler |

## Revisit conditions

- Resources must migrate between devices.
- Async destruction invalidates the host-outlives-resource rule.
- A centralized handle table becomes necessary.
