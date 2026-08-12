# Archived C++ ZV implementation

This directory preserves the former C++ implementation of ZV, including its CMake build, `zv-client`, Python bindings, Python proxy, and C API. It is not built by the root CI and is not shipped in current Rust releases.

The last C++ release was `v0.1.1`. This archived tree also includes later unmerged C++ development present when Rust became the active implementation.

To build it manually:

```sh
cmake -S attic/zv-cpp -B attic/zv-cpp/build -G Ninja
cmake --build attic/zv-cpp/build
```

Its original instructions and packaging metadata remain in this directory. The Rust implementation at the repository root is the supported project.
