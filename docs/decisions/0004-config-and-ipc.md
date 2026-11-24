# ADR-0004: TOML config + JSON Lines IPC

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [architecture.md](../architecture.md), [coding-style.md](../coding-style.md)

## Context

powerwm needs human-readable configuration, declarative keybindings/rules/layouts/themes,
runtime control, and atomic hot reload without embedding a heavy runtime.

## Decision

### Config

- TOML is the only user config format.
- Use a pinned mature parser inside `config/`.
- Layer: defaults → system → user → runtime overrides.
- Hot reload is atomic: parse and validate before swapping; failure keeps the old config.
- No embedded scripting before 1.0; complex logic uses IPC plus external scripts.

### IPC

- Unix domain socket, default `$XDG_RUNTIME_DIR/powerwm-$WAYLAND_DISPLAY.sock`, mode 0600.
- JSON Lines: one request/response/event per line.
- Requests contain `id`, `command`, `args`; responses contain `id`, `ok`, `result`/`error`.
- Whitelisted actions only; no arbitrary code execution.
- Event subscriptions for status bars, tools, and tests.

Example:

```json
{"id":1,"command":"get_outputs"}
{"id":1,"ok":true,"result":[{"name":"DP-1","width":2560,"height":1440,"scale":1.25}]}
{"id":2,"command":"focus_workspace","args":{"name":"2"}}
{"event":"workspace_changed","data":{"name":"2","output":"DP-1"}}
```

## Consequences

### Positive

- TOML is simple, reviewable, and easy to diagnose.
- Config is data and can be tested/reflected.
- JSON Lines is scriptable from any language.
- Unix socket permissions are simple; no embedded runtime attack surface.

### Negative

- TOML cannot express complex logic.
- JSON Lines needs a versioning/schema strategy.
- D-Bus ecosystem integration still needs separate work.
- Atomic reload requires careful cross-module state.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| YAML | expressive | syntax traps, poor errors | not sole format |
| Lua | programmable | embedded runtime/security cost | no scripting before 1.0 |
| INI | simple | weak nesting/arrays | insufficient |
| D-Bus | standard | verbose, heavy | too much for core control |
| gRPC | typed | heavy, overkill | wrong scale |

## Revisit conditions

- TOML cannot express real user configs and external scripts are unacceptable.
- IPC needs cross-user/system control.
- D-Bus becomes mandatory for deep portal integration; add it rather than replace JSON IPC.
