---
trigger: model_decision
description: Project goals, design and dev guidelines
---

# CLAUDE.md - AI Assistant Guide

## Project Overview

**ZV** is a lightweight, cross-platform image viewer for computer vision practitioners. Inspired by classic `xv` but modernized for ML/CV workflows.

- **Status**: Pre-alpha (daily use, rough edges)
- **License**: BSD
- **Languages**: C++17 (core), Python (bindings via nanobind)
- **Build**: CMake 3.5.1+

**Goals**: Fast startup, lazy loading, pixel-level inspection, multi-image comparison, remote ML server visualization

**Non-goals**: Not a photo editor, not GIMP/ImageJ replacement

## Architecture

### Core Components (libzv/)

- **Viewer** (`Viewer.h/.cpp`, 472 lines): Central controller, manages image windows/layouts
- **App** (`App.h/.cpp`, 5400 lines): Application orchestration, event loop
- **ImageWindow** (`ImageWindow.h/.cpp`, 1731 lines): ImGui rendering, zoom/pan, tools, events
- **Server** (`Server.h/.cpp`, 501 lines): TCP server for client-server mode
- **Image.h**: Templated image container (SRGBA, LinearRGB, LMS, YCbCr, HSV, Lab)
- **OpenGL** (`OpenGL.h/.cpp`, 547 lines): GPU rendering, shader management
- **ImguiGLFWWindow** (579 lines): ImGui + GLFW integration
- **ColorConversion.cpp** (21952 bytes): Advanced color space conversions

### Key Directories

```
libzv/          # Core library (60 files, ~20k lines)
zv/             # Desktop app entry point
zv-client/      # CLI client for remote connections
client/zv/      # Standalone C/C++ client library
python/         # Python bindings (nanobind)
deps/           # All dependencies vendored (GLFW, ImGui, libjpeg-turbo, STB, etc.)
tests/          # Test suite
```

## Technologies

- **Graphics**: OpenGL 3.3+, GLFW, Dear ImGui
- **Image I/O**: STB Image, libjpeg-turbo
- **Network**: znet (header-only TCP)
- **Python**: nanobind
- **Dependencies**: All vendored in `deps/` (no package manager needed)

## Build

```bash
mkdir build && cd build && cmake -G Ninja .. && ninja
```

**Outputs**: `libzv` (static lib), `zv` (GUI), `zv-client` (CLI), `_zv` (Python module)

### Python module

Incrementally:

```bash
# Incremental build of the C++ lib
cd build && ninja _zv

# Test the import
source .venv/bin/activate && python -c "import _zv"

# Build and install the full module
source .venv/bin/activate && uv pip install -e .
```

## Code Conventions

- **Naming**: Classes PascalCase, methods camelCase, members `m_` prefix
- **Memory**: RAII, smart pointers, Image class owns memory
- **GUI**: ImGui immediate mode (no retained state)
- **Logging**: `zvLog()` macros

## Common Tasks

**Add image format**: Extend `Image_stb.cpp`, update `loadImageFromFile()`

**Add interactive tool**: Inherit `InteractiveTool`, implement mouse/render methods, register in `ImageWindow`

**Add network message**: Update `Message.h`, `Server.cpp`, `Client.cpp`, sync Python bindings

**Extend Python API**: Add to libzv, expose in `python/zv_python.cpp`, update `__init__.py`

## Important Files

- **Entry points**: `zv/main.cpp`, `zv-client/main.cpp`, `python/zv_python.cpp`
- **Hot paths**: `ImageWindow::render()`, `OpenGL.cpp`, `Image_stb.cpp`, `ColorConversion.cpp`
- **Platform-specific**: `PlatformSpecific_{macOS,linux,windows}.cpp` (30-41 lines each)

## API Examples

**C++**:
```cpp
zv::Viewer viewer;
viewer.addImageWindow("image.jpg");
viewer.setLayout(2, 2);
```

**Python**:
```python
import zv
viewer = zv.Viewer()
viewer.add_image_window("image.jpg")
zv.App.get().exec_()
```

## Git Conventions

- Do not add `Co-Authored-By:` lines to commit messages.

## Debug Artifacts

- Put temporary GUI debug scripts, screenshots, and generated test images under repo-local `tmp/`. This directory is ignored and avoids needing permission for `/private/tmp` cleanup.

## Key Principles

- Read existing code before modifying
- All dependencies vendored - update subtrees if needed
- C++ API changes may require Python binding updates
- Multi-platform: check Linux/macOS/Windows compatibility
- Performance: lazy loading, GPU caching, async network
- Security: validate image input, network protocol, file paths
