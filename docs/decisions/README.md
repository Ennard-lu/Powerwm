# Architecture decision records (ADRs)

ADRs capture important, hard-to-reverse technical decisions. They answer:

1. What problem did we face?
2. Why this option rather than alternatives?
3. What consequences follow, and when should we revisit it?

## Index

| ID | Title | Status | Date |
| --- | --- | --- | --- |
| [0001](0001-native-compositor.md) | Native DRM/KMS compositor, no wlroots | Accepted (protocol part superseded by 0006) | 2026-11-24 |
| [0002](0002-cxx26-toolchain.md) | C++26 + GCC 16 + CMake/Ninja | Accepted (exceptions part superseded by 0008) | 2026-11-24 |
| [0003](0003-renderer-backend.md) | GLES3/EGL first, Vulkan later | Superseded by 0007 | 2026-11-24 |
| [0004](0004-config-and-ipc.md) | TOML config + JSON Lines IPC | Accepted | 2026-11-24 |
| [0005](0005-license.md) | License selection (pending) | Superseded by 0010 | 2026-11-24 |
| [0006](0006-own-wayland-protocol.md) | In-tree Wayland wire protocol + coroutines | Accepted | 2026-11-24 |
| [0007](0007-vulkan-vulkanpp.md) | Vulkan + internal vulkanpp | Accepted | 2026-11-24 |
| [0008](0008-no-exceptions.md) | No exceptions; error codes | Accepted | 2026-11-24 |
| [0009](0009-c-api-wrapping.md) | C APIs require create/destroy + RAII | Accepted | 2026-11-24 |
| [0010](0010-apache-2-0.md) | Apache License 2.0 | Accepted | 2026-11-24 |
| [0011](0011-namespace-and-integers.md) | `pwm` namespace and fixed-width integers | Accepted | 2026-11-24 |
| [0012](0012-dependent-resource-creation.md) | Dependent resources are created by their host | Accepted | 2026-11-24 |
| [0013](0013-c-api-assumes-valid.md) | C API wrappers assume valid objects | Accepted | 2026-11-24 |
| [0014](0014-protocol-scanner-reflection.md) | XML protocol scanner with reflected request dispatch | Accepted | 2026-11-24 |

## Status definitions

- `Proposed`: under discussion.
- `Accepted`: implemented; code and docs must follow it.
- `Deprecated`: not recommended, not yet superseded.
- `Superseded by ADR-XXXX`: replaced; original kept for history.
- `Rejected`: considered and declined; reason recorded.

## Template

```markdown
# ADR-XXXX: Title

- Status: Proposed | Accepted | Deprecated | Superseded by ADR-XXXX | Rejected
- Date: YYYY-MM-DD
- Decision owners:
- Related:

## Context

What problem and constraints?

## Decision

What was chosen? State boundaries and invariants.

## Consequences

### Positive

### Negative / costs

### Risks and mitigation

## Alternatives

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |

## Revisit conditions

What signals should reopen this decision?
```

## Writing rules

- One decision per ADR.
- Do not rewrite accepted conclusions; add a new ADR and mark the supersession.
- Consequences must include negatives.
- Performance decisions state hardware, scenario, and baseline.
