# macOS application launcher

Install the `zv` executable at its permanent location first (the release installer
uses `~/.local/bin/zv`), then run:

```sh
zv --install-app
```

This builds a small native Cocoa launcher and installs `/Applications/Zv.app`.
The app launches the absolute executable path recorded during installation;
it does not contain another copy of the Rust viewer or depend on Finder's PATH.
Replacing the executable at that path updates the viewer used by the app.
If you move it, rerun `--install-app` from the new location. Symlinks may be
resolved by macOS when finding the running executable, so use a stable binary
location rather than a symlink into a versioned directory.

## Prerequisites and installation

Apple's standalone **Command Line Tools** package includes Clang and the macOS
SDK. Full Xcode and a Rust installation are not required. If the tools cannot be
found, ZV prints the proposed command:

```sh
xcode-select --install
```

In an interactive terminal, ZV asks `Run this command? [y/N]` before opening
Apple's installer. ZV stays running and checks for Clang and the macOS SDK every
two seconds, then automatically continues building and installing Zv.app as soon
as they are available. Keep the terminal open; no second invocation is needed.
If you cancel Apple's installer, press Ctrl-C to stop waiting.
A declined prompt or noninteractive invocation exits with instructions and a
nonzero status. If the tools are already installed but unavailable, check
`xcode-select -p` and `xcrun --sdk macosx --show-sdk-path`.

The launcher is compiled, its plist validated, and the bundle ad-hoc signed
before it replaces an existing installation. If `/Applications` needs
administrator access, ZV shows and asks to run a `sudo` command that installs
only the staged bundle. Run the initial command as your normal user.

An unrelated `/Applications/Zv.app` or a symlink at that path is never replaced.
Installation is serialized with `/Applications/.Zv.install-lock`; if the process
is killed during installation, remove that empty lock directory before retrying.
A failed replacement restores the previous app. Recovery files are retained and
their location printed if rollback fails. An unchanged, valid installation is
left intact, retaining its exact launcher signature.

## Finder and Full Disk Access

The bundle plist declares that Zv can open JPEG, PNG, GIF, BMP, TIFF, HEIC/HEIF,
TGA and Netpbm images. The installer does not explicitly register the app with
Launch Services or manage file associations. Choose associations yourself in
Finder, using **Open With** or **Get Info > Open with > Change All**.

One Finder opening of several files starts one viewer with those files. Later
openings start additional viewers. Opening the app itself starts an empty
viewer. A background launcher remains alive while its viewers run, receives
further file-opening events, and exits after its last viewer exits. The viewer
provides the visible Dock icon and UI. Reopening a running launcher activates
its most recently started viewer.

To grant access, add **/Applications/Zv.app** to **System Settings > Privacy &
Security > Full Disk Access**, then restart its viewers. The launcher starts the
viewer directly as a child to preserve macOS's responsible-process chain.
Permission inheritance must be checked on the target macOS version with a real
protected file; automated tests do not grant or change privacy permissions.
A terminal-launched viewer has a separate launch context. Rebuilding the
launcher can change its ad-hoc signing identity and require granting access
again; a stable bundle identifier does not guarantee persistence of grants.

Remove `/Applications/Zv.app` to uninstall the launcher. This leaves the `zv`
command-line binary in place.

## Development validation

`cargo test --locked` on macOS includes compilation/signing of a temporary bundle,
installation/update checks, tampering detection, rollback, unrelated-app and
symlink rejection, plist path escaping, and continuation after asynchronous developer-tools
installation. It never installs into `/Applications`.
The icon is the existing 32px ZV icon from the archived application's
`Icon_xxd.cpp`, repackaged as an ICNS resource.

With a logged-in macOS GUI session, run:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 debug-scripts/test-macos-app.py
```

This uses a native recording viewer in a temporary bundle under `tmp/`. It checks
real Launch Services multi-file and repeated openings, exact arguments with
spaces/Unicode, no extra empty viewer at startup, direct parentage, plain launch,
and launcher exit after its children exit. It unregisters/removes the test bundle.

Manual acceptance: install from the final binary location, repeat installation,
open images with Finder, grant the app Full Disk Access and open a protected
image, replace the external binary at the same path and repeat, then rebuild the
launcher and check whether macOS requests a new grant. Also check the missing
binary error dialog after moving the target executable.
