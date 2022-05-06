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

- Become a fully-features scientific image viewer ([ImageJ](https://imagej.nih.gov/ij/), [napari](https://napari.org/))

## Demo

*Grid Layout (2x2) to visualize 4 images at a time, with synchronized zoom and multiple cursors.*
![ZV Layout Demo](misc/zv_grid_zoom.gif)

## Status

- Pre-pre-alpha. I use it on a daily basis, but it has lots of rough edges and
  nothing is stabilized yet. The code is still prototype quality.

- Only tested on Linux and macOS so far, but it should be straightforward to
  build on Windows later on as all the dependencies are cross-platform.

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

## Building

Standard cmake. No external dependency should be required as everything is included in the repo. Example command line:

```
mkdir build
cd build
cmake ..
make
```

## Dependencies

There are no external dependencies to install as they are all snapshotted in the repository. But here is the list to give credits:

- [GLFW](https://www.glfw.org/): desktop windows and GL context creation.
- [Dear ImGui](https://github.com/ocornut/imgui): immediate mode GUI.
- [Clip](https://github.com/dacap/clip): copy/paste images from clipboard.
- [cppuserprefs](https://github.com/nburrus/cppuserprefs): storage of user preferences.
- [gl3w](https://github.com/skaslev/gl3w): tiny OpenGL loader.
- [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog): portable File Dialog for ImGui.
- [pybind11](https://github.com/pybind/pybind11): generate Python bindings.

A lot of the visualization code was adapted from [DaltonLens](https://github.com/DaltonLens/DaltonLens).
