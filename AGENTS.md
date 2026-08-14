# ZV Rust Development Guide

ZV is a Rust 2024 desktop image viewer built with `eframe`, `egui`, and WGPU. The active package and executable are both named `zv`.

## Commands

```sh
cargo fmt --check
cargo test --locked
cargo clippy --locked --all-targets
cargo build --locked --profile release-small
```

Public release artifacts always use `release-small`; do not publish binaries from the regular `release` profile.

## Tests

Write tests for logic that is tricky and likely to break by accident: geometry
and coordinate math, state machines, clamping and edge cases, anything with a
sign or an axis that can be swapped, and bugs that were actually hit. Prefer a
test that states a property over one that restates the implementation.

Do not keep tests that only exercise a setter, a constant default, a direct
key-to-action mapping, or a case another test already covers. They pass by
construction, so they never catch a regression, and they still have to be read
and updated on every refactor. Delete them instead of carrying them.

## Layout

- `src/`: application and viewer implementation.
- `docs/`: active Rust documentation.
- `debug-scripts/`: checked-in GUI-debug scripts.
- `attic/zv-cpp/`: archived C++ application, Python bindings, C API, and CMake build. Do not add it to root CI.

Use Rust naming and ownership conventions. Keep platform-specific behavior behind `cfg` guards, and preserve the Linux, macOS, and Windows build matrix.

Put temporary GUI screenshots and generated files under repo-local `tmp/`.
