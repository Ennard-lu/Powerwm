# powerwm documentation

This directory contains the long-term design, engineering rules, validation notes, and
decision records for powerwm. Code changes; decisions and constraints must remain traceable.

## Reading order

| Order | Document | Audience | What you will learn |
| --- | --- | --- | --- |
| 0 | [README.zh-CN.md](README.zh-CN.md) | Chinese readers | Chinese project overview (the only Chinese document) |
| 1 | [roadmap.md](roadmap.md) | Everyone | Where the project is going and how each phase is judged |
| 2 | [architecture.md](architecture.md) | Contributors | Layers, threading, frame lifecycle, directory plan |
| 3 | [coding-style.md](coding-style.md) | Contributors | How to write consistent, maintainable C++26 |
| 4 | [testing.md](testing.md) | Contributors | How correctness and performance are proven |
| 5 | [ci-cd.md](ci-cd.md) | Maintainers | How pipelines, gates, and releases work |
| 6 | [phase-1-validation.md](phase-1-validation.md) | Hardware testers | Phase 1 acceptance steps and current results |
| 7 | [protocol-scanner.md](protocol-scanner.md) | Protocol contributors | XML scanner, generated bindings, reflected dispatch |
| 8 | [decisions/](decisions/) | Everyone | Why key choices were made and what they cost |

## Conventions

- English is the primary language for code, comments, commits, and documentation.
- `README.zh-CN.md` is the only Chinese document and is kept for accessibility.
- Architecture-level changes require an ADR in `decisions/`.
- Performance claims must state hardware, scenario, and measurement method.
- Outdated documents are either updated or marked **Deprecated** with a replacement link.
- Diagrams use Mermaid so GitHub and GitLab render them directly.

## Directory

```text
docs/
├── README.md                 # This index
├── README.zh-CN.md           # The only Chinese document
├── roadmap.md                # Phases, milestones, risks
├── architecture.md           # System design
├── coding-style.md           # C++26 rules
├── testing.md                # Test and performance strategy
├── ci-cd.md                  # Pipelines and releases
├── phase-1-validation.md     # Hardware acceptance record
├── protocol-scanner.md       # XML scanner, bindings, reflected dispatch
└── decisions/
    ├── README.md             # ADR index and template
    ├── 0001-native-compositor.md
    ├── ...
    ├── 0013-c-api-assumes-valid.md
    └── 0014-protocol-scanner-reflection.md
```

## Maintenance

1. Decide first, implement second: write an ADR for cross-module choices.
2. ADRs are never deleted. Mark them `Superseded by ADR-XXXX` instead.
3. Review the roadmap quarterly; explain reordering in the PR.
4. Prefer executable rules: formatting, policy scripts, and CI over verbal conventions.
