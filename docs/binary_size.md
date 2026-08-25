# Rust Binary Size Findings

This document captures measured `zv` binary sizes on macOS arm64 while tuning release settings and image format features.

## Build Profiles

- `release`: performance-oriented (`opt-level=3`, `panic=unwind`)
- `release-small`: size-oriented (`opt-level=3`, `panic=abort`, `strip="symbols"`, `lto="thin"`, `codegen-units=1`)

Build commands:

```bash
cargo build --release
cargo build --profile release-small
```

## Measured Sizes

All sizes below are from `target/.../zv` using `ls -lh` and section breakdown via `size -m`.

| Configuration | Binary Size | Delta vs prior |
|---|---:|---:|
| Original release baseline (before tuning) | 17M | - |
| + conservative release tuning (`strip`, `lto`, `codegen-units`) | 13M | -4.0M |
| + `image` limited to `jpeg,png` | 9.3M | -3.7M |
| + `eframe` default feature pruning (`wgpu` only path) | 8.2M | -1.1M |
| + aggressive small profile (`panic=abort`, `opt-level=3`) | 5.5M | -2.7M |

## Requested Format Comparison

Comparison inside `release-small` profile:

- `image` features: `jpeg,png` -> **5.5M** (`5,816,496` bytes)
- `image` features: `jpeg,png,pnm` -> **5.6M** (`5,854,784` bytes)
- `image` features: `bmp,jpeg,png,tga,tiff,webp` -> **5.8M**
- `image` features: `bmp,exr,gif,jpeg,png,tga,tiff,webp` -> **6.3M**
- `image` features: `avif,bmp,exr,gif,jpeg,png,tga,tiff,webp` -> **7.3M**

Net cost for keeping `bmp/tga/tiff/webp` in addition to `jpg/png`: **~0.3M** (~5.5% increase from 5.5M).

## Per-Format Tradeoff Table

Measured by building `release-small` with `jpeg,png` plus exactly one additional format feature:

| Extra format on top of `jpeg,png` | Binary bytes | Delta vs `jpeg,png` |
|---|---:|---:|
| none (`jpeg,png` baseline) | 5,816,496 | 0 |
| `bmp` | 5,852,592 | +36,096 |
| `pnm` | 5,854,784 | +38,288 |
| `tga` | 5,834,992 | +18,496 |
| `tiff` | 5,816,496 | +0 |
| `webp` | 6,011,568 | +195,072 |
| `gif` | 5,874,960 | +58,464 |
| `exr` | 6,351,936 | +535,440 |
| `avif` | 6,764,432 | +947,936 |

Notes:

- `tiff` shows `+0` because it is already enabled transitively by `arboard` (`image-data` path used through `egui-winit` clipboard support).
- `png` is also pulled transitively, so `jpeg` is effectively the main optional toggle between `jpeg,png` and `png`-only builds.

## Icon Font Comparison

Measured with the current `release-small` profile and image formats `bmp,gif,jpeg,png,pnm,tga,tiff`:

| Configuration | Binary bytes | `ls -lh` size | Delta |
|---|---:|---:|---:|
| before `egui-phosphor` | 9,290,464 | 8.9M | - |
| + `egui-phosphor` regular font registered | 9,785,920 | 9.3M | +495,456 |

The size increase is mostly in `__TEXT.__const`, consistent with embedding the regular icon font.

## Current Result

With `release-small`, image formats `bmp,gif,jpeg,png,pnm,tga,tiff`, and the regular `egui-phosphor` font registered, the binary is currently **9.3M** (`9,785,920` bytes).

## Native macOS HEIC Loading

Measured on macOS arm64 against the same `dev` commit and toolchain:

| Configuration | Binary bytes | Delta |
|---|---:|---:|
| `dev` baseline | 10,806,336 | 0 |
| + native ImageIO HEIC fallback | 10,807,376 | +1,040 |

The HEIC decoder is supplied by macOS and is not embedded in the executable. The resulting binary increase is about
0.01%.
