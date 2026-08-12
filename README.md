[![Rust CI](https://github.com/nburrus/zv/actions/workflows/rust_ci.yml/badge.svg)](https://github.com/nburrus/zv/actions/workflows/rust_ci.yml)

# zv

**This project is an early work in progress**

Lightweight and cross-platform image viewer, inspired by the good old [xv](http://www.trilon.com/xv/). The project was born after I found myself still
trying to build the 1999 `xv` shareware in 2021 as none of the more recent alternatives were as efficient.

The computer vision community is the main target audience, and `zv` has unique features to navigate large collection of images (e.g. machine learning datasets or results) and easily compare multiple images with synchronized zooms and pointers to inspect pixel-level differences.

The active implementation is written in Rust using egui and WGPU.

**Goals:**

- Be the default tool for computer vision practitioners to quickly inspect images.

- Small, statically-linked desktop binary that can be easily distributed.

- Linux, macOS and Windows support.

- Lightweight and fast to load, lazy loading of images so it can open thousands of them.

- Easily compare multiple images at the pixel level, e.g to inspect the output of image processing algorithms.

- Support only a small set of the most useful manipulation routines and annotations.

- xv-like keyboard shortcuts for the main commands.

- Client-server mode to visualize images on a remote server (e.g. machine learning server).

**Non-goals:**

- Become a photo viewer app with library management, etc.

- Become a fully-featured image manipulation program ([GIMP](https://www.gimp.org/)).

- Become a fully-featured scientific image viewer ([ImageJ](https://imagej.nih.gov/ij/), [napari](https://napari.org/))

## Install

Single-command install for the latest release:

```bash
curl -fsSL https://raw.githubusercontent.com/nburrus/zv/main/scripts/install.sh | bash
```

Install a specific version:

```bash
curl -fsSL https://raw.githubusercontent.com/nburrus/zv/main/scripts/install.sh | bash -s -- --version v0.2.0
```

By default, the installer places `zv` in `~/.local/bin`. Override that with:

```bash
curl -fsSL https://raw.githubusercontent.com/nburrus/zv/main/scripts/install.sh | bash -s -- --install-dir ~/bin
```

Currently supported installer targets:

- macOS arm64 and x86_64
- Linux x86_64
- Windows x86_64 (ZIP release or [PowerShell installer](scripts/install.ps1))

Manual install is also straightforward: download the matching release archive, extract it, and copy `zv` into a directory on your `PATH`, for example `~/.local/bin`.

On Linux, `zv` is distributed as a single binary, but it still relies on system graphics and windowing libraries such as OpenGL and X11.

To uninstall:

```bash
rm -f ~/.local/bin/zv
```

## Demo

*Grid Layout (2x2) to visualize 4 images at a time, with synchronized zoom and multiple cursors.*
![ZV Layout Demo](misc/zv_grid_zoom.gif)

## Features

- Lazy image loading and nearby-image preloading for large collections.
- Automatic mosaic and fixed multi-image grid layouts.
- Pixel inspection, synchronized zoom, and cursor information in the controls window.
- Native image-window sizing commands, including normal size, aspect ratio, and maxspect.
- BMP, GIF, JPEG, PNG, PNM, TGA, and TIFF loading; JPEG uses libjpeg-turbo when possible.
- WGPU rendering with nearest magnification, linear minification, and mipmapped downsampling.
- Image filtering, reordering, close/delete actions, and clipboard import/export.
- Line, arrow, rectangle, ellipse, and text annotations with selection, editing, undo, save, and discard.
- Color editing including levels, hue adjustment, grayscale, inversion, channel swaps, histogram equalization, and label colorization.
- Built-in `zv --server` and `zv --client` modes for remote image inspection.

## Status

- Alpha. I use it on a daily basis, but it has rough edges and nothing is stabilized yet. The code is still prototype quality.

- Rust CI and releases cover Linux x86_64, macOS arm64 and x86_64, and Windows x86_64. A graphical smoke test on each platform remains valuable before a release.

## Keyboard shortcuts

`zv` keeps xv-style single-key shortcuts for the main image-viewing commands
and uses shifted mnemonic keys for annotation tools.

On macOS, shortcuts shown with `Ctrl` use `Cmd` instead.

| Shortcut | Action |
| --- | --- |
| `Space` / `Down` | Next image |
| `Backspace` / `Up` | Previous image |
| `0` | Automatic mosaic layout |
| `1` ... `9` | Set a fixed image layout for the visible images |
| `n` | Normal image size |
| `m` | Maxpect image size |
| `>` / `<` | Double / halve image size |
| `.` / `,` | Grow / shrink image size by 10% |
| `a` | Restore aspect ratio |
| `e` | Open the color editor |
| `Ctrl+O` / `Cmd+O` | Open image |
| `Ctrl+W` / `Cmd+W` | Close image |
| `Ctrl+S` / `Cmd+S` | Save image |
| `Ctrl+Z` / `Cmd+Z` | Undo |
| `Ctrl+N` / `Cmd+N` | Create a new image from the clipboard |
| `Ctrl+C` / `Cmd+C` | Copy the current image to the clipboard |
| `Shift+T` | Add text annotation |
| `Shift+L` | Add line annotation |
| `Shift+R` | Add rectangle annotation |
| `Shift+E` | Add ellipse annotation |
| `Shift+A` | Add arrow annotation |
| `Esc` | Cancel the current tool or annotation placement mode |
| `Delete` | Delete the selected annotation |
| `Shift+Delete` | Delete the selected image from disk (after confirmation) |

## Remote usage with zv --client and --server

If you want to browse images that live on a remote machine while keeping the GUI on your local machine, use:

- `zv --server` on the local machine
- `zv --client --host server_ip` on the remote machine

It can be convenient to use `ssh -R` to avoid exposing ports, in that case the defaults line up:

- `zv --server` listens on `127.0.0.1:4207`
- `ssh -R 4207:127.0.0.1:4207 user@remote-host` to forward the 4207 port
- `zv --client` connects to `127.0.0.1:4207`, which is forwarded by ssh to the server

## Building

Build with the Rust toolchain pinned in [`rust-toolchain.toml`](rust-toolchain.toml). Example command line:

```
cargo run -- path/to/image.png
```

Useful development commands are `just check`, `just test`, and `just release-small`. Public artifacts always use the stripped `release-small` profile.

## Legacy C++ implementation

The historical C++ application, `zv-client`, Python bindings and proxy, C API, CMake build, vendored dependencies, and original documentation live in [`attic/zv-cpp`](attic/zv-cpp). They are preserved for reference and manual builds, but are not built by root CI or included in Rust releases. Rust networking is not wire-compatible with the archived C++ client/server protocol.
