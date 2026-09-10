zv release archive

This archive contains the stripped zv desktop binary and release metadata.

Manual install:
  1. Extract this archive.
  2. Copy zv (or zv.exe) to a directory on PATH.
  3. Ensure the executable bit is set on Linux or macOS.

The archived C++ zv-client and Python APIs are not included in Rust releases.

macOS Finder integration:
  After placing zv at its permanent location, run zv --install-app to build
  /Applications/Zv.app. It launches that installed binary. Apple Command Line
  Tools and the macOS SDK are required; full Xcode is not. If missing, zv shows
  xcode-select --install and asks before opening Apple's installer. Keep the
  terminal open: zv waits for the tools and then continues automatically.
  See docs/macos-app.md in the source repository for permissions and details.
