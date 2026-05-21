# Proprietary-Compatible Builds

`osci_scripting` contains optional script parsing and script-backed effect support.

This is an engineering policy document, not legal advice. The root policy is `../../../docs/proprietary-compatible-builds.md`.

## Build Mode

Set `OSCI_PROPRIETARY_BUILD=1` when compiling a proprietary consumer or verifying that this module avoids incompatible dependencies.

Scripting should remain an optional module because it adds a runtime dependency, an API surface for user code, and a sandboxing/security boundary. A proprietary product can include it only when the dependency, notices, and product security model are intentionally accepted.

## Dependencies

LuaJIT is permissively licensed, but consumers must provide compatible LuaJIT headers/libraries and keep the required LuaJIT notice with any distributed binaries or source.

If scripting is disabled or omitted by a product, no code outside this module should require LuaJIT headers.
