# Testing and performance strategy

> Goal: every merge is more trustworthy, not merely “it compiles.”

Testing is part of every phase's exit criteria, not a Phase 7 cleanup task.

## 1. Test pyramid

```text
                 ┌──────────────────────┐
                 │ manual dogfood / apps │  few, slow, real hardware
                 ├──────────────────────┤
                 │ integration (headless)│  protocols, scene, input, config
                 ├──────────────────────┤
                 │ golden images         │  rendering correctness
                 ├──────────────────────┤
                 │ unit tests            │  fast, many, deterministic
                 └──────────────────────┘
```

| Level | Scope | Frequency | Target time |
| --- | --- | --- | --- |
| Unit | pure functions, geometry, state machines, parsers | every commit | < 2 s |
| Integration | headless compositor + simulated clients | every PR | < 60 s |
| Golden | pixel comparison | every PR | < 120 s |
| Fuzz | protocol/config/IPC parsing | daily + releases | hours |
| Benchmark | frame time, allocation, startup | every PR | < 5 min |
| Compatibility | real apps/hardware matrix | weekly/release | manual + scripts |

## 2. Tools and layout

```text
tests/
├── unit/
├── integration/
├── golden/
├── fuzz/
├── bench/
└── support/
```

- Framework: Catch2 v3 pinned via FetchContent; must build with
  `CATCH_CONFIG_DISABLE_EXCEPTIONS` (or an assertion backend without exceptions).
- Phase 0 currently runs CTest with a framework-free smoke test plus event-loop and
  headless tests.
- Benchmarks: prefer lightweight steady-clock statistics; use Google Benchmark only if needed.
- Fuzzing: libFuzzer in Clang test builds only.

## 3. Unit tests

Must cover:

- geometry: point/size/rect/region union/intersection/contains/clipping;
- layout: no overlap, no overflow, stable ordering;
- state machines: focus stack, workspaces, key modes, transactions;
- config: valid/invalid TOML, defaults, atomic hot reload;
- protocol parameter validation;
- every `result` failure branch.

Rules:

- Name tests by behavior: `test_region_union_merges_overlap`.
- One concept per test.
- Use table-driven boundary cases.
- No real time, filesystem, or network; inject interfaces.
- Random tests print the seed.
- No sleeps; advance the loop/clock explicitly.

## 4. Integration tests (headless)

The headless backend must:

- not open `/dev/dri/*` and not require a GPU;
- let tests advance time and frames explicitly;
- accept injected input and expose scene/protocol state;
- render to memory for golden tests, or run logic-only frames;
- configure resolution, refresh rate, and scale.

Example scenarios:

1. Two `xdg_toplevel`s, buffers committed, assert scene and focus.
2. `wl_keyboard.key`, assert shortcut and client event order.
3. Partial damage, assert merged output damage.
4. Client disconnect, assert resource release and focus fallback.
5. Simulated VT disable/enable, assert pause and recovery.

### Test clients

- Use an in-tree wire client (no libwayland-client) for protocol tests.
- Use external clients for compatibility: foot, weston-terminal, GTK4, Qt6, wayland-info.
- Run clients in subprocesses to isolate event loops.
- Cover handshake, IDs, versions, invalid messages, fd passing, disconnects.
- Coroutine tests: cancellation, timeout, cross-suspension lifetime, concurrent requests.

## 5. Golden-image tests

1. Build a fixed scene from a description.
2. Render with the headless backend at fixed size, DPI, and time.
3. Compare PNG output with the expected image.
4. Allow a small per-pixel and total-difference threshold.
5. Require an explicit baseline update and attach the diff in the PR.

Rules:

- Fixed clocks, seeds, fonts, and textures.
- Software rendering or tolerant thresholds for driver differences.
- Cover rounded corners, shadows, opacity, scaling, clipping, sRGB, multi-output.
- Do not relax thresholds to hide bugs.

## 6. Fuzzing

Any client byte stream must not crash, overflow, or loop forever.

| Target | Input | Entry point |
| --- | --- | --- |
| Config | arbitrary TOML | `fuzz_config` |
| IPC | arbitrary JSON lines | `fuzz_ipc` |
| Wayland message | arbitrary bytes | `fuzz_wayland_message` |
| Protocol object state | valid/invalid sequences | `fuzz_wayland_object` |
| Coroutine connection | split packets, fd floods, cancel | `fuzz_wayland_connection` |
| EDID/mode | arbitrary blob | `fuzz_edid` |
| Region | arbitrary rectangles | `fuzz_region` |

Requirements:

- 24 hours without crashes before release.
- Every crash becomes a regression corpus entry.
- ASan + UBSan enabled for fuzz builds.
- Deduplicate by stack trace.
- Wire and object state machines are the highest-priority targets.

## 7. Benchmarks and budgets

| Scenario | Description | Metrics |
| --- | --- | --- |
| idle | no clients, no animation | wakeups, power |
| static | 10 static windows | first frame, steady frame time |
| anim | 100 animated surfaces | FPS, P99 frame time, allocations |
| damage | small continuously updated region | GPU work, committed area |
| blur | full-screen blur | GPU time, degradation |
| startup | cold start to first frame | time, peak memory |
| clients | 50 connect/disconnect cycles | leaks, fd count, latency |

Initial budget (1080p@60, recent integrated GPU):

| Metric | Budget | Measurement |
| --- | --- | --- |
| Input-to-present P99 | < 16.7 ms | injected timestamps + presentation feedback |
| CPU frame P99 | < 8 ms | steady_clock around frame processing |
| GPU frame P99 | < 8 ms | Vulkan timestamp queries |
| Steady-state heap allocations | 0 per frame | allocation counter |
| Idle CPU | < 1% of one core | `pidstat` 60 s |
| RSS with 20 clients | < 300 MB | RSS |
| Cold start to first frame | < 500 ms | process timestamps |
| Config hot reload | < 100 ms | injected file change |

- CI compares lightweight benchmarks against a baseline; >10% regressions fail.
- Store results as artifacts; add trend charts in Phase 7.
- Separate performance tests from functional tests; use medians for thresholds.

## 8. Memory and resource tests

- Counting allocator verifies zero steady-state allocations.
- ASan: out-of-bounds, use-after-free, double-free.
- LSan: leaks at exit; suppress only known globals with comments.
- UBSan: undefined behavior.
- TSan: only after threads are introduced; separate CI job.
- File descriptors: count before/after client churn.
- DRM objects: headless cannot cover KMS; hardware smoke in Phase 1.
- Vulkan: validation layers in debug/CI; `vulkanpp` handle counts.
- No-exception builds: all tests compile and run with `-fno-exceptions`.

## 9. CI integration

| Job | Content | Blocking |
| --- | --- | --- |
| format | clang-format | yes |
| policy | exceptions/libwayland/C API/integer/comments | yes |
| build | Debug/Release/`-fno-exceptions` | yes |
| test | unit + integration + golden | yes |
| sanitizers | ASan + UBSan | yes |
| bench | thresholds | yes from Phase 2 |
| fuzz | daily/release 30–60 min | release-blocking |
| hardware | real GPU smoke | release-blocking from Phase 1 |

## 10. Flaky-test policy

- No sleeps; use conditions and loop progress.
- No ordering assumptions; each test initializes independently.
- `[flaky]` is temporary and must link an issue; fix or delete within two weeks.
- Rerunning CI is not a fix; two consecutive failures are real.

## 11. Coverage

- Coverage is a guide, not a target.
- Phase 0 targets:
  - `base`, `config`, `wm/layout`: ≥80% line coverage;
  - protocol glue/platform: mostly integration coverage;
  - hot paths: invariants and benchmarks.
- Reports are CI artifacts.

## 12. Release checklist

- [ ] Unit/integration/golden tests pass.
- [ ] ASan/UBSan clean; no new suppressions.
- [ ] 24-hour fuzz run clean.
- [ ] Performance budgets met; no >10% regression.
- [ ] Real hardware: multi-output, VT switch, hotplug, suspend/resume.
- [ ] App matrix: terminal, browser, editor, chat, video, games.
- [ ] 7-day dogfood and no P0/P1 defects (1.0).
