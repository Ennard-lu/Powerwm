# ADR-0005: License selection (pending)

- Status: Superseded by [ADR-0010](0010-apache-2-0.md)
- Date: 2026-11-24
- Owners: project maintainers
- Related: [README.md](../../README.md), [CONTRIBUTING.md](../../CONTRIBUTING.md)

> Superseded 2026-11-24: Apache-2.0 was chosen. This record keeps the alternatives.

## Context

The license affects distribution adoption, contribution obligations, patent terms, and
compatibility with Linux/Wayland dependencies. The repository originally had no `LICENSE`.

## Candidates

| Option | Pros | Cons | Bias |
| --- | --- | --- | --- |
| MIT | minimal, adoption-friendly | no patent grant | maximize adoption |
| Apache-2.0 | patent grant, clear contribution terms | longer; GPLv2 compatibility care | enterprise-friendly |
| MIT OR Apache-2.0 | downstream choice | dual-license management | Rust-style |
| GPL-3.0-or-later | requires reciprocity | limits some adoption | community protection |
| LGPL-3.0-or-later | library-friendly | compositor derivation questions | library split |

## Consequences

- A clear license removes legal uncertainty.
- SPDX identifiers enable automated auditing.
- Changing a license after release needs all contributors' agreement.

## Questions before deciding

1. Is closed-source commercial use acceptable?
2. Must downstream modifications be published?
3. Is an explicit patent grant required?
4. Should libraries and the compositor use different licenses?

This ADR remained `Proposed` until replaced by ADR-0010.
