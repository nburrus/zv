# macOS application launcher

Place `zv` at its permanent location, then run `zv --install-app` to install
`/Applications/Zv.app`. The app launches that binary; rerun the command if you
move it.

Zv.app is compiled and ad-hoc signed on your machine. This avoids having to
distribute notarized macOS app bundles.

Apple's Command Line Tools and macOS SDK are required, but full Xcode is not.
If missing, ZV asks to run `xcode-select --install`, waits, and continues
automatically. Keep the terminal open; Ctrl-C cancels waiting.

The bundle declares supported image types. Choose file associations in Finder.
For Full Disk Access, add `/Applications/Zv.app` in System Settings > Privacy &
Security. Rebuilding the launcher may require granting access again.

Delete `/Applications/Zv.app` to uninstall the launcher; the `zv` binary remains.
