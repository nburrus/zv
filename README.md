[![CMake Build and Test](https://github.com/nburrus/zv/actions/workflows/cmake_build_and_test.yml/badge.svg)](https://github.com/nburrus/zv/actions/workflows/cmake_build_and_test.yml)

# zv

**This project is an early work in progress, NOT READY FOR WIDE SHARING yet.**

Very lightweight and cross-platform image viewer, inspired by the good old
[xv](http://www.trilon.com/xv/). The project was born after I found myself still
trying to build the 1999 `xv` shareware in 2021 as none of the more recent
alternatives were as efficient.

The computer vision community is the main target audience, and `zv` has unique
features to navigate large collection of images (e.g. machine learning datasets
or results) and easily compare multiple images with synchronized zooms and
pointers to inspect pixel-level differences.

It also has a standalone C and Python API to be used as an alternative to OpenCV
`imshow`.

**Goals:**

- Be the default tool for computer vision practitioners to quickly inspect images.

- Small, statically-linked desktop binary that can be easily distributed.

- Linux, macOS and Windows support.

- Lightweight and fast to load, lazy loading of images so it can open thousands of them.

- Easily compare multiple images at the pixel level, e.g to inspect the output
  of image processing algorithms.

- Support only a small set of the most useful manipulation routines and annotations.

- xv-like keyboard shortcuts for the main commands.

- Python-API and standalone C-API to also use it as an in-app image viewer / logger.

- Client-server mode to visualize images on a remote server (e.g. machine learning server).

**Non-goals:**

- Become a photo viewer app with library management, etc.

- Become a fully-featured image manipulation program ([GIMP](https://www.gimp.org/)).

- Become a fully-featured scientific image viewer ([ImageJ](https://imagej.nih.gov/ij/), [napari](https://napari.org/))

## Demo

*Grid Layout (2x2) to visualize 4 images at a time, with synchronized zoom and multiple cursors.*
![ZV Layout Demo](misc/zv_grid_zoom.gif)

## Status

- Pre-pre-alpha. I use it on a daily basis, but it has lots of rough edges and
  nothing is stabilized yet. The code is still prototype quality.

- Only tested on Linux and macOS so far, but it should be straightforward to
  build on Windows later on as all the dependencies are cross-platform.

## Install

Single-command install for the latest release:

```bash
curl -fsSL https://raw.githubusercontent.com/nburrus/zv/main/scripts/install.sh | bash
```

Install a specific version:

```bash
curl -fsSL https://raw.githubusercontent.com/nburrus/zv/main/scripts/install.sh | bash -s -- --version v0.1.1
```

By default, the installer places `zv` and `zv-client` in `~/.local/bin`. Override that with:

```bash
curl -fsSL https://raw.githubusercontent.com/nburrus/zv/main/scripts/install.sh | bash -s -- --install-dir ~/bin
```

Currently supported installer targets:

- macOS arm64
- Linux x86_64

Manual install is also straightforward: download the matching release tarball, extract it, and copy `zv` and `zv-client` into a directory on your `PATH`, for example `~/.local/bin`.

On Linux, `zv` is distributed as a single binary, but it still relies on system graphics and windowing libraries such as OpenGL and X11.

To uninstall:

```bash
rm -f ~/.local/bin/zv
rm -f ~/.local/bin/zv-client
```

## Python API (standalone mode)

Creating a zv viewer directly from Python.

```
import zv
import numpy as np

app = zv.App()
app.initialize()

viewer = app.getViewer()
blue_im = np.zeros((256,256,4), dtype=np.uint8)
blue_im[:,:,3] = 255
viewer.addImage ("All Blue", blue_im)

viewer.addImageFromFile ("myimage.png")

viewer.setLayout(1,2) # one row, two columns

while app.numViewers > 0:
    app.updateOnce(1.0 / 30.0)
```

## Client-Server logging API

```
import numpy as np
from zv.log import zvlog

zvlog.start () # will create an instance as a subprocess
# Alternative: connect to an existing server.
# zvlog.start (('localhost', 4207))

zvlog.image("random1", np.random.default_rng().random(size=(256,256,3), dtype=np.float32))
zvlog.waitUntilWindowsAreClosed()
```

A similar logging API is available in C/C++, without external dependencies, but the zv binary needs to be in the PATH.

## Remote usage with zv-client, zv, zv-proxy, and ssh -R

If you want to browse images that live on a remote machine while keeping the GUI on your local machine, use:

- `zv-client` on the remote machine
- either `zv` or `zv-proxy` on the local machine
- `ssh -R` to expose the local proxy port back to the remote machine

### Step 1: direct connection to a local zv instance

If a single forwarded session is enough, you can connect the remote client directly to a local `zv` server.

In the common case, the defaults already line up:

- `zv` listens on `127.0.0.1:4207`
- `zv-client` connects to `127.0.0.1:4207`

So in practice you usually only need:

1. On your local machine:

```bash
zv
```

2. From your local machine:

```bash
ssh -R 4207:127.0.0.1:4207 user@remote-host
```

3. On the remote machine:

```bash
zv-client /path/to/image1.png /path/to/image2.jpg
```

In this setup:

- `zv-client` connects to `127.0.0.1:4207` on the remote machine
- `ssh -R` forwards that connection to `127.0.0.1:4207` on your local machine
- the local `zv` process receives the images and displays them
- if you need to customize this, `zv` exposes `--interface`, `--port`, and `--require-server`, and `zv-client` exposes `--host` and `--port`

### Step 2: use zv-proxy to spawn a dedicated local zv per connection

If you want each remote client connection to create its own local `zv` instance automatically, insert `zv-proxy` between the SSH tunnel and `zv`.

Again, the defaults are set up so the minimal version is usually enough:

1. On your local machine:

```bash
uv run python/zv-proxy.py
```

2. Reuse the same `ssh -R` and `zv-client` steps as above.

End-to-end flow:

- `zv-client` connects to `127.0.0.1:4207` on the remote machine
- `ssh -R` forwards that connection to `127.0.0.1:4207` on your local machine
- `zv-proxy` accepts the connection, starts a local `zv -p <ephemeral-port> --require-server`, and proxies the traffic
- the images are then displayed by a local `zv` window on your machine
- if you need to customize this, `zv-proxy` exposes `--listen-host`, `--listen-port`, `--zv-host`, and `--zv-cmd`, while `zv-client` still exposes `--host` and `--port`

Notes:

- `zv-proxy` lives at `python/zv-proxy.py` in this repository and currently expects `uv run`.
- If `zv` is not on your local `PATH`, replace `--zv-cmd "zv"` with the full path to the binary.
- The proxy handles multiple client connections and spawns one separate local `zv` process per connection.

## Building

Standard cmake. No external dependency should be required as everything is included in the repo. Example command line:

```
mkdir build && cd build && cmake .. && make
```

## Dependencies

There are no external dependencies to install as they are all snapshotted in the repository. But here is the list to give credits:

- [GLFW](https://www.glfw.org/): desktop windows and GL context creation.
- [Dear ImGui](https://github.com/ocornut/imgui): immediate mode GUI.
- [stb_image](https://github.com/nothings/stb): image loading and saving.
- [Clip](https://github.com/dacap/clip): copy/paste images from clipboard.
- [cppuserprefs](https://github.com/nburrus/cppuserprefs): storage of user preferences.
- [gl3w](https://github.com/skaslev/gl3w): tiny OpenGL loader.
- [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog): pure ImGui file dialogs.
- [nanobind](https://github.com/wjakob/nanobind): generate Python bindings.

A lot of the visualization code was adapted from [DaltonLens](https://github.com/DaltonLens/DaltonLens).
