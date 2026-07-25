# JPEG loading benchmark, macOS arm64, 2026-07-26

## Summary

Rust zv currently loads every image through `image::open(path)?.into_rgba8()` and then copies the tightly packed RGBA bytes into `ImageSRGBA`'s aligned storage (`rust/crates/zv-viewer/src/image_io.rs:7`, `rust/crates/zv-viewer/src/color_image.rs:368`). C++ zv special-cases `.jpg`/`.jpeg`, tries TurboJPEG first, and falls back to stb_image (`libzv/Image_stb.cpp:36`).

On this macOS arm64 machine, TurboJPEG is the clear best JPEG path for Rust zv. A preallocated Rust TurboJPEG decode path measured about 1.7x faster than the current Rust viewer path on the detailed 4K JPEG and about 1.6x faster on the smooth 4K JPEG. ImageIO/CoreGraphics did not beat TurboJPEG.

## Environment

- Machine: MacBook Pro, Apple M1 Pro, 10 cores, 32 GB RAM.
- OS: macOS 26.5.2, Darwin 25.5.0, arm64.
- Rust: `rustc 1.95.0 (59807616e 2026-04-14)`, host `aarch64-apple-darwin`.
- Cargo build mode: `cargo build --release`, default release profile from the temporary benchmark crate.
- C++ build mode: `clang++ -O3 -std=c++17`.
- Iterations: 5 warmup iterations, then 40 measured iterations per decoder and input.
- Measurement scope: file read plus JPEG decode plus RGBA output allocation. Rows named `plus_viewer_copy` also include the current viewer copy into `ImageSRGBA`-style 256-byte-aligned storage. For 3840px RGBA rows, the aligned row size equals the tight row size: `3840 * 4 = 15360`.

## Inputs

Generated under repo-local `tmp/jpeg-bench/inputs` from deterministic scripts:

| Input | Dimensions | File size | Notes |
| --- | ---: | ---: | --- |
| `high_detail_3840x2160_q90.jpg` | 3840x2160 | 1,514,712 bytes | Center-cropped/resized from `tests/books_4k.jpg`, with deterministic high-frequency grid detail, JPEG quality 90, 4:2:0 |
| `smooth_3840x2160_q90.jpg` | 3840x2160 | 393,550 bytes | Deterministic RGB gradient with simple ellipses, JPEG quality 90, 4:2:0 |

## Results

Times are milliseconds. Median and p90 are the main columns to compare.

| Decoder/path | High-detail median | High-detail p90 | Smooth median | Smooth p90 |
| --- | ---: | ---: | ---: | ---: |
| Rust current `image::open` decode only | 31.601 | 31.959 | 15.225 | 15.585 |
| Rust current `image::open` + viewer copy | 31.960 | 32.796 | 16.445 | 17.021 |
| Rust `image` from pre-read memory | 31.459 | 32.015 | 15.349 | 15.797 |
| Rust `zune-jpeg` to RGBA | 30.292 | 31.018 | 14.218 | 14.623 |
| Rust `turbojpeg` high-level RGBA | 19.148 | 19.471 | 10.148 | 10.433 |
| Rust `turbojpeg` preallocated RGBA | 18.868 | 18.960 | 10.132 | 10.522 |
| C++ TurboJPEG default flags | 19.721 | 20.572 | 10.645 | 11.026 |
| C++ TurboJPEG `FASTDCT | FASTUPSAMPLE` | 17.363 | 18.504 | 8.895 | 9.098 |
| macOS ImageIO/CoreGraphics RGBA | 33.874 | 35.643 | 19.210 | 20.573 |

## Findings

- Pre-reading the file before handing it to `image` does not materially improve performance. The current `image::open` path is decode-bound, not meaningfully filesystem-bound for these inputs.
- `zune-jpeg` is slightly faster than `image` on these generated images, but still well behind TurboJPEG.
- Rust `turbojpeg` matches the C++ TurboJPEG baseline when using default flags. The preallocated Rust path is slightly faster on the detailed input because it decodes directly into final row-aligned storage and avoids a post-decode copy.
- macOS ImageIO/CoreGraphics is slower than the current Rust `image`/`zune` paths for the detailed input and much slower than TurboJPEG for both inputs, so it is not worth adding as a fast path.
- `FASTDCT | FASTUPSAMPLE` improves TurboJPEG by about 8% on the detailed image and about 16% on the smooth image, but it changes output pixels. The checksum difference in the benchmark confirms this is not bit-equivalent to the default decode.

## Recommendation

Implement a JPEG-specific Rust fast path using TurboJPEG, guarded by extension and/or JPEG signature checks, with fallback to the existing `image` crate path for portability and non-JPEG formats.

Use a preallocated-output shape: read the JPEG into memory, read the header, allocate `ImageSRGBA` storage, and decompress directly into `ImageSRGBA`'s row stride as RGBA. This avoids the current tightly packed `image::RgbaImage` allocation followed by a second allocation/copy into viewer storage.

Make default TurboJPEG decoding use accurate/default flags first. Add a configurable fast JPEG mode for `FASTDCT | FASTUPSAMPLE` rather than enabling it unconditionally, because it is visibly a quality/performance tradeoff and produces different pixels. The likely UI/config shape is a preference such as "fast JPEG decode" defaulting off initially, or defaulting on only after an explicit project decision that pixel-exact JPEG decode is not required for normal viewing.

## Cross-platform impact

- macOS: `turbojpeg` crate 1.5.1 can build libjpeg-turbo via CMake and produced arm64 NEON-enabled static output in this benchmark. This adds CMake/Ninja-style native build requirements if no system lib is used.
- Linux: the same crate supports `pkg-config` and CMake-backed builds. Prefer system `libturbojpeg` via `pkg-config` where available for distro builds, with CMake build fallback for developer builds.
- Windows: the crate supports CMake/MSVC builds through `turbojpeg-sys`; CI should validate this before making the dependency mandatory. Keep the current `image` path as fallback so a TurboJPEG build issue does not make the Rust viewer Windows-unbuildable.
- Dependency policy: the C++ tree already vendors libjpeg-turbo, so using TurboJPEG in Rust aligns with the existing project direction. The cleanest long-term option is to decide whether Rust should use the `turbojpeg` crate's bundled build, system `pkg-config`, or the repository's vendored `deps/libjpeg-turbo`; the implementation should isolate this behind `image_io` so the rest of the viewer remains cross-platform.
