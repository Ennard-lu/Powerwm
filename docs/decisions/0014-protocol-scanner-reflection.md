# ADR-0014: XML protocol scanner with reflected request dispatch

- Status: Accepted
- Date: 2026-11-24
- Owners: powerwm maintainers
- Related: [ADR-0006](0006-own-wayland-protocol.md), [architecture.md](../architecture.md), [roadmap.md](../roadmap.md)

## Context

Wayland protocol descriptions are XML. `wayland-scanner` emits C structs and callback
tables tied to libwayland. powerwm needs generated descriptors, typed message codecs,
and a dispatch path that does not use C callback tables or hand-written opcode switches.

C++26 reflection lets an implementation class expose named member functions; the
generated interface descriptor provides the protocol message names and opcodes.

## Decision

1. Vendor protocol XML under `protocols/`.
2. Parse XML with an in-tree parser (`wayland::parse_xml`) into a protocol model.
3. `powerwm_protocol_scanner` generates C++ interface descriptors and per-message
   `decode` / `encode` functions at build time. No generated files are checked in.
4. Generated descriptors provide interface name, version, request/event names,
   opcodes, argument tuples, and `request_<name>` handler names.
5. `wayland::dispatch_request<Implementation, Interface>` uses reflection to find the
   member function whose identifier matches `request_<name>` and invokes it with the
   decoded argument tuple.
6. Missing handlers produce an `unsupported` error; malformed arguments produce a
   protocol error.
7. The wire layer owns message headers, argument encoding, alignment, and fd lists.
8. `wayland::object_table` maps object ids to generated interface versions and a
   type-erased reflected dispatcher.
9. Unix socket ownership and `SCM_RIGHTS` live in `platform/wayland`; `wayland::connection`
   owns incremental input/output buffers and framing. Coroutine sessions remain separate.

## Consequences

### Positive

- Protocol additions require XML only; no scanner templates or hand-written switches.
- Handler signatures are ordinary C++ functions, not C callbacks.
- Opcode and argument typing stay generated and testable.
- Reflection is confined to the dispatch boundary; protocol data remains plain structs.

### Negative

- Generated code depends on GCC 16 reflection syntax and must track the compiler.
- Missing handlers are runtime errors unless a later validation pass rejects them.
- Generated message structs expose `std::string_view` and `std::span` views into the
  current message buffer; callers must copy data that outlives dispatch.

### Risks and mitigation

- Reflection syntax changes: isolate dispatch in `wayland/dispatch.hpp`, keep a
  descriptor-only fallback path possible.
- Wire incompatibility: round-trip tests, real clients later, and fuzzing.
- XML dialect drift: scanner tests cover attributes, enums, `new_id`, arrays, and fds.

## Alternatives

| Option | Pros | Cons | Why not |
| --- | --- | --- | --- |
| `wayland-scanner` output | proven | libwayland types, C callbacks | violates ADR-0006 |
| Hand-written opcode switches | trivial toolchain | every protocol edit is manual | defeats the scanner |
| Runtime name lookup table | no compile-time reflection | slower, loses typed handlers | reflection is available |
| Code generator emits switches | no reflection | generator complexity and string duplication | reflection is cleaner |

## Revisit conditions

- GCC reflection changes or is removed from the supported toolchain.
- Generated dispatch cannot express a required protocol pattern.
- Profiling shows reflection-built tables are a meaningful startup or call cost.
