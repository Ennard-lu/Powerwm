# Contributing to powerwm

powerwm is a long-running project. Please read this page before starting.

## Before you start

1. Read the [README](README.md) and [roadmap](docs/roadmap.md); confirm the change belongs
   to the current phase.
2. Search existing issues. If none exists, open one with the problem, motivation, and a
   proposed direction.
3. Architectural changes (layering, threading, protocol scope, dependencies) require an ADR
   in `docs/decisions/`.
4. Small fixes may go straight to a pull request, but describe how you tested them.

## Development environment

The recommended environment is the reproducible container:

```bash
docker build -t powerwm-dev -f containers/dev.Dockerfile .
docker run --rm -it -v "$PWD:/workspace" -w /workspace powerwm-dev bash
```

Native build:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Requirements:

- Linux with GCC 16+ (C++26 reflection and coroutines)
- CMake 3.25+
- Ninja
- libdrm, libinput, libudev, libseat, Vulkan loader and drivers
- No `libwayland-*` dependency in powerwm itself
- The entire project builds with `-fno-exceptions`

## Commit style

Use Conventional Commits:

```text
feat(scene): add damage tracking for transformed nodes
fix(input): release pointer grab on client disconnect
docs(roadmap): split Phase 3 protocol scope
ci: cache ccache between builds
```

- One logical change per commit.
- Explain **why**, not only what changed.
- Use `BREAKING CHANGE:` for incompatible changes.

## Pull request checklist

- [ ] Scope matches the issue and roadmap.
- [ ] `clang-format` passes.
- [ ] `bash tools/check_policy.sh` passes.
- [ ] `python3 tools/check_comments.py` passes.
- [ ] Debug and Release builds pass.
- [ ] New or changed behavior has tests; bug fixes have regression tests.
- [ ] Client-facing error paths cannot crash the compositor.
- [ ] Hot paths add no allocations, formatting, or locks.
- [ ] Public APIs are documented, `[[nodiscard]]`, and state failure conditions.
- [ ] Architectural changes include an ADR.
- [ ] Performance changes include hardware, scenario, and measurements.
- [ ] Documentation and examples are updated.

## Review focus

1. **Correctness**: lifetime, error paths, concurrency boundaries, protocol semantics.
2. **Layering**: dependency direction and abstraction placement.
3. **Testability**: deterministic verification in a headless environment.
4. **Performance**: hot-path allocations, copies, unbounded growth.
5. **Maintainability**: naming, structure, and tests.

## Code of conduct

- Discuss ideas and evidence, not people.
- Newcomer questions are welcome; read the docs and search issues first.
- Do not submit private code, secrets, or unlicensed assets.
- Performance and compatibility claims must be reproducible.

## License

powerwm is licensed under the Apache License 2.0. See [LICENSE](LICENSE).

Contributions are accepted under Apache-2.0 without a CLA.

- Source files use `SPDX-License-Identifier: Apache-2.0`.
- Third-party code must be license-compatible and retain its original notices.
- Do not submit code of unknown origin.
