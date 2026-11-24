# powerwm C++ coding style

> Scope: all C++ under `src/`, `include/`, `tests/`, `tools/`.
> The goal is long-term maintainability, predictable performance, and auditable lifetimes.

Enforcement:

- `clang-format` blocks CI.
- `tools/check_comments.py` enforces comment rules.
- `tools/check_policy.sh` enforces no exceptions, no libwayland, C API wrapping, and
  integer aliases.
- Debug, Release, and ASan+UBSan builds must pass.

## 1. Language baseline

| Item | Rule |
| --- | --- |
| Standard | C++26 |
| Primary compiler | GCC 16+ |
| Secondary compiler | Clang when it supports the used C++26 features |
| Build | CMake ≥ 3.25 + Ninja |
| Platform | Linux (x86_64 first, aarch64 later) |

Encouraged:

- `std::expected`, `std::optional`, `std::span`, `std::string_view`, ranges, chrono;
- concepts and `requires` instead of SFINAE;
- `constexpr` / `consteval` / `if constexpr`;
- `std::print` / `std::format` for tools, logging backends, and tests;
- reflection only through `base/meta.hpp`, never required for core logic;
- designated initializers for config and descriptors.

Use carefully:

| Feature | Rule |
| --- | --- |
| Exceptions | Forbidden; `-fno-exceptions`; errors use `result<T>` |
| RTTI / `dynamic_cast` | Not in hot paths; explain if used |
| Macros | Compile-time switches, assertions, logging only |
| Modules | Evaluate in Phase 2; start with `base`; no half-module units |
| Coroutines | Async I/O and protocol sessions only; never hide blocking |
| Vulkan | Only through `vulkanpp`; no `Vk*` in business code |

Forbidden:

- `using namespace` in headers or global scope;
- owning raw pointers;
- `printf` / `std::cout` as runtime logging;
- non-`inline` global definitions in headers;
- undefined-behavior-dependent optimizations;
- explanatory comments in code.

## 2. Naming

Everything is `lower_snake_case` unless noted.

| Entity | Rule | Example |
| --- | --- | --- |
| Namespace | lower_snake | `pwm`, `pwm::platform::drm` |
| Type | lower_snake | `device`, `frame_context`, `renderer` |
| Function | lower_snake | `commit_atomic`, `begin_frame` |
| Variable/parameter | lower_snake | `output_count`, `damage_region` |
| Private member | lower_snake + `_` | `fd_handle_`, `pending_state_` |
| Public struct field | lower_snake | `width_px`, `format` |
| Constant | lower_snake | `max_outputs`, `default_refresh_hz` |
| Enum value | lower_snake | `error_code::not_found` |
| Template parameter | `T`, `Ts...`, or semantic | `template <typename T>` |
| Macro | `PWM_UPPER_SNAKE` | `PWM_ASSERT`, `PWM_LOG` |
| File | lower_snake | `page_flip.cpp` |

Additional rules:

- Names must be self-explanatory; functions start with a verb, types are nouns.
- Forbidden names: `from`, `process`, `handle`, `data`, `tmp`, `util`, `helper`, `misc`.
- Booleans use `is_`, `has_`, `can_`.
- Getters do not use `get_`: `width()`, `outputs()`.
- Factories use `create_*` / `open_*`; dependent resources are created by their host.
- Put units in names: `timeout_ms`, `width_px`, `scale_factor`.

## 3. Files and headers

Include order:

```cpp
#include "pwm/render/renderer.hpp"

#include <pwm/base/result.hpp>
#include <pwm/scene/node.hpp>

#include <drm/drm.h>

#include <expected>
#include <span>
#include <string_view>
```

- Use `#pragma once`.
- Headers must be self-contained.
- Forward-declare where it reduces coupling.
- No `using namespace`, anonymous namespaces, or non-`inline` definitions in headers.
- Keep public API minimal; hidden visibility by default.
- One main responsibility per `.cpp`; split before ~800 lines.

## 4. Formatting

Controlled by `.clang-format`:

- 4 spaces, no tabs;
- 100 columns;
- pointer/reference left: `T*`, `const T&`;
- attached braces;
- no short function bodies on one line;
- no namespace closing comments;
- run `clang-format -i` before committing.

Do not fight formatting in a PR. Change the config in a dedicated commit.

## 5. Types and API design

- Prefer value semantics; pass large objects by `const&` or `span`.
- RAII for every resource: fd, mmap, DRM object, Wayland resource, Vulkan object.
- Rule of zero/five; explicitly handle copy/move.
- `explicit` constructors unless implicit conversion is intended.
- `[[nodiscard]]` on factories, queries, and fallible functions.
- `noexcept` on destructors, moves, swaps, and trivial queries.
- `final` on concrete implementations.
- Return `result<T>` for recoverable errors; `optional<T>` for absence.
- Do not use `bool` as the only failure signal.

### Fixed-width integers

Use project aliases, not standard fixed-width names:

| Alias | Purpose |
| --- | --- |
| `pwm::i8/i16/i32/i64` | signed fields, handles, counters |
| `pwm::u8/u16/u32/u64` | flags, IDs, sizes |
| `pwm::f32/f64` | scale, animation, geometry |
| `pwm::usize/isize` | container sizes and indices |

Only `base/types.hpp`, C ABI wrappers, and low-level Vulkan/DRM boundaries may use the
underlying standard types.

### Function declaration style

All named functions use trailing return types:

```cpp
auto create_device(const char* path) -> result<device>;
auto destroy() const noexcept -> void;
auto is_valid() const noexcept -> bool;
```

- `void` is written `-> void`.
- Static members: `static auto create(...) -> result<T>`.
- Exceptions: `main`, constructors, destructors, conversion operators.
- No leading return types in new code.

## 6. Error handling

```cpp
namespace pwm {
enum class error_code : u16 {
    invalid_argument, not_found, permission_denied, io, protocol,
    unsupported, resource_exhausted, cancelled, internal,
};

struct error {
    error_code code;
    std::string_view message;
    std::source_location where{};
    i32 system_error = 0;
};

template <typename T = void>
using result = std::expected<T, error>;
}
```

Rules:

1. No exceptions anywhere.
2. Recoverable errors return `result<T>`.
3. Missing values use `optional<T>`.
4. Broken invariants use `PWM_ASSERT` and terminate.
5. Client errors become protocol errors; the compositor stays alive.
6. Hardware errors attempt recovery, then safe exit.
7. Error paths avoid allocation; use static messages and numeric context.
8. `std::expected::value()` on failure terminates; use `error()` / `value_or` / monadic ops.

## 7. Ownership and lifetime

| Case | Use |
| --- | --- |
| Exclusive resource | `std::unique_ptr<T>` or custom RAII |
| Shared ownership (rare) | `std::shared_ptr<T>` with justification |
| Non-owning reference | `T&`, `T*`, `span`, `string_view` |
| Buffer | `std::vector<T>`, `std::span<T>` |
| C handle | `pwm::raii<T, Args...>` |

Rules:

- Non-owning pointers must not cross async boundaries without a lifetime token.
- Callbacks must be unregisterable.
- Wayland resource destruction is idempotent; late events never access freed objects.
- GPU buffers are not freed while in use.
- No `shared_ptr` cycles.

## 8. C API wrapping contract

Every C API resource must be wrapped before business code sees it.

```cpp
struct file_descriptor {
    pwm::handle_t fd = -1;

    [[nodiscard]] static auto create(const char* path, pwm::i32 flags)
        -> result<raii<file_descriptor>>;

    auto destroy() const noexcept -> void { ::close(fd); }
};
```

Rules:

1. `create` allocates; `destroy` releases.
2. Business code holds `raii<T,...>` or a dedicated RAII type.
3. `destroy` is `noexcept`, assumes the object is valid, and is called exactly once.
4. No null/fd/handle checks inside wrappers. `create` success establishes validity;
   callers guarantee later use.
5. C headers only in `base/`, `platform/`, `render/vulkanpp/`.
6. Convert C constants/enums to project types at the boundary.
7. `create` returns `result<raii<T>>` for recoverable failure.
8. Dependent resources are created by their host:
   `device.create_dumb_buffer()`, `device.create_framebuffer()`. Do not provide
   `dependent::create(owner, ...)`.
9. `valid()` is an optional caller query; wrappers must not rely on it for safety.
10. `raii` does not validate objects; default construction owns nothing.

## 9. Concurrency and coroutines

Default: one logical thread.

- Scene/window/protocol state is main-thread only.
- Workers handle unshared jobs and post results back to the loop.
- No locks in hot paths; document lock order if unavoidable.
- `std::jthread` + `stop_token`; no detach.
- Cross-thread communication uses atomics or queues, never shared containers.
- Monotonic time only.

Coroutines:

- Use `pwm::event::task<T>`; no raw `coroutine_handle`.
- Lazy start; parent/loop owns the task; no detach.
- Errors travel in `result<T>`; no exceptions.
- No references to stack objects across suspension points.
- Every suspension registers with the event loop and supports cancellation.
- No blocking I/O or sleeps inside coroutines.
- Resume only on the owning event-loop thread.

## 10. Performance

Initial 1080p@60 budget:

| Item | Budget |
| --- | --- |
| Input event | < 0.2 ms |
| Scene + layout | < 1.0 ms |
| CPU render submission | < 3.0 ms |
| GPU render | < 8.0 ms |
| Frame P99 | < 16.7 ms |
| Steady-state heap allocations | 0 per frame |
| Idle wakeups | 0 per second |

Rules:

- Hot paths: no formatting, allocation, file I/O, locks, or exceptions.
- Reserve containers; reuse buffers; use pools/arenas.
- Prefer contiguous data and indices; use SoA only when measured.
- Avoid `std::function` in hot paths; use templates or `function_ref`.
- `[[likely]]`/`[[unlikely]]` only with data.
- Profile before optimizing; include hardware, scenario, and method.

## 11. Logging and assertions

```cpp
PWM_LOG_DEBUG("drm", "connector {} mode {}x{}@{}", connector_id, width, height, refresh_hz);
```

- Runtime logging goes through `base::log`, not iostream/printf.
- Levels: trace, debug, info, warn, error.
- Categories: drm, input, scene, wayland, wm, config.
- Hot paths are silent by default; enable with environment variables.
- Never log secrets, clipboard contents, or key sequences.
- `PWM_ASSERT` is for invariants only.

## 12. Function size and nesting

- Business functions target ≤ 60 lines, hard limit 80.
- Nesting depth ≤ 5 (`if`, loops, switch, lambdas).
- `main()` composes and manages lifetime only.
- Prefer early returns, named helpers, ranges algorithms.
- Lambdas over 20 lines must become named functions.
- Complex conditions become named boolean functions.
- `clang-tidy` thresholds: lines 80, statements 60, branches 20, nesting 5.

## 13. Comments

**Explanatory comments are forbidden.**

This includes restating code, design rationale, TODOs, FIXMEs, author tags, namespace
closing markers, commented-out code, and non-English comments.

Allowed in code:

1. `SPDX-License-Identifier` metadata.
2. English structured API documentation: `///`, `//!`, `/** ... */`, `/*! ... */`.

API comments describe the contract only:

- parameter ranges;
- ownership and lifetime;
- failure conditions;
- thread/coroutine constraints;
- preconditions and invariants.

```cpp
/// Commits one frame to the output.
///
/// The caller must keep frame resources alive until the page flip completes.
/// Only one commit may be in flight per output.
///
/// @return An error when the driver rejects the commit.
[[nodiscard]] auto commit(const frame& frame) -> result<void>;
```

Rules:

- API comments are English.
- Comments do not replace naming; rename first.
- `.cpp` files contain no comments except SPDX.
- Config, scripts, Dockerfiles, and YAML contain no explanatory comments.
- `docs/` and `README.zh-CN.md` may use Chinese; they are not code.
- `tools/check_comments.py` enforces this.

## 14. Testing

- Every behavior change needs a test; every bug fix needs a regression test.
- Pure logic needs unit tests; protocol/input need headless integration tests.
- Tests are deterministic: inject clocks, fixed seeds, no sleeps.
- Test names: `test_<unit>_<behavior>`.
- See [testing.md](testing.md).

## 15. Commits and PRs

Conventional Commits:

```text
<type>(<scope>): <summary>

<body: why, trade-offs, impact>

<footer: BREAKING CHANGE / Refs>
```

Types: feat, fix, perf, refactor, docs, test, build, ci, chore, revert.  
Scopes: drm, input, scene, wayland, wm, ci, docs, etc.

PR rules:

- One logical change, preferably under 400 lines.
- Describe problem, approach, alternatives, tests, risks.
- Important architecture changes include an ADR.
- Performance claims include data.
- No generated files or build directories.
- CI green and at least one approval.

Review checklist:

- [ ] Layering and dependency direction correct?
- [ ] Error paths tested? Client errors cannot crash the compositor?
- [ ] Lifetimes clear? No dangling references or double destroy?
- [ ] Hot path free of new allocations, formatting, locks?
- [ ] Names, units, and ownership self-explanatory?
- [ ] Public APIs documented and `[[nodiscard]]`?
- [ ] Config/protocol changes compatible or documented?

## 16. Warnings and static analysis

Target warning set after cleanup:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
-Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
-Wnull-dereference -Wdouble-promotion -Wformat=2
```

- No unexplained `#pragma GCC diagnostic ignored`.
- `clang-tidy`: bugprone, performance, modernize (except reflection-conflicting checks).
- Reflection-heavy files are exempt until Clang parses C++26 reflection.

## 17. Compatibility

- `0.x` promises no ABI/config compatibility, but breaking changes go in `CHANGELOG.md`.
- Deprecations use `[[deprecated("use X")]]` for at least one minor cycle.
- Config key deprecations warn and name the replacement.
- Protocol versions are announced only when implemented.

**Summary:** let compilers, CI, and types remember the rules; spend human attention on design.
