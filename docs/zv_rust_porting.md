# Rust Porting History

The Rust port is now the active ZV implementation. This file retains the
decision record for the migration rather than describing a future prototype.

- The root Cargo package and binary are both named `zv`.
- Public artifacts use the stripped `release-small` profile.
- Rust implements its own client/server mode through `zv --client` and
  `zv --server`; it is not wire-compatible with the archived C++ protocol.
- The former C++ implementation, Python bindings, C API, and CMake build live
  under `attic/zv-cpp/`.

Current architecture and development details are in [design.md](design.md).
