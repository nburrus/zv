# Installer And Release Distribution Plan

## Goal

Provide a simple, single-command install flow for `zv` on macOS and Linux based on GitHub release artifacts and a small installer script that installs `zv` into `~/.local/bin` by default.

The intended user-facing flow is:

```bash
curl -fsSL https://raw.githubusercontent.com/nburrus/zv/main/scripts/install.sh | bash
```

## Constraints And Current State

- `zv` already builds as a single executable target named `zv`.
- CI currently builds and tests the project, but does not publish installable release artifacts.
- Linux is not fully static in the strict sense because platform libraries such as X11/OpenGL/XCB are still linked dynamically.
- The first version should optimize for a reliable install path, not for full package-manager integration.

## Scope For First Implementation

### Supported targets

Start with these release targets:

- macOS arm64
- Linux x86_64

Defer Linux arm64 until the basic flow is working end-to-end.

### Deliverables

Implement:

- A tag-triggered GitHub Actions release workflow
- Per-platform release archives containing the `zv` binary
- A `SHA256SUMS` file for published artifacts
- A POSIX shell installer script at `scripts/install.sh`
- README installation documentation

Do not implement yet:

- Homebrew formula
- `.deb` / `.rpm` packages
- macOS notarization
- automatic in-place upgrades beyond reinstalling

## Implementation Notes

This plan is now partially implemented in-repo.

- Installer script: `scripts/install.sh`
- Release workflow: `.github/workflows/release_installers.yml`
- Archive helper file: `docs/release/README-install.txt`
- Release publishing action: `softprops/action-gh-release`

The implementation uses explicit tarball staging in CI and does not use CPack for release publishing.

## Release Artifact Design

Each release should publish one tarball per supported target plus checksums.

Suggested filenames:

- `zv-vX.Y.Z-macos-arm64.tar.gz`
- `zv-vX.Y.Z-linux-x86_64.tar.gz`
- `SHA256SUMS`

Each tarball should contain:

- `zv`
- `LICENSE`
- `README-install.txt`

Keep the archive layout flat so the installer can extract the binary with minimal logic.

## Installer Script Design

Create `scripts/install.sh` with the following behavior.

### Inputs

Supported flags:

- `--version <tag>` to install a specific release, defaulting to the latest release
- `--install-dir <path>` to override the destination directory
- `--help`

Supported environment variables:

- `ZV_VERSION`
- `ZV_INSTALL_DIR`
- `ZV_GITHUB_REPO` for testing forks if useful

### Platform detection

Detect:

- OS from `uname -s`
- architecture from `uname -m`

Map to release target names:

- `Darwin` + `arm64` => `macos-arm64`
- `Linux` + `x86_64` => `linux-x86_64`

Fail with a clear error for unsupported combinations.

### Download flow

1. Resolve the version:
   - use `--version` or `ZV_VERSION` if provided
   - otherwise resolve the redirected tag from `https://github.com/<repo>/releases/latest`
2. Build the archive URL for the current target
3. Download the tarball
4. Download `SHA256SUMS`
5. Verify the tarball checksum
6. Extract `zv`
7. Install into `${ZV_INSTALL_DIR:-$HOME/.local/bin}`
8. Mark executable
9. Print a concise success message and PATH guidance if needed

### Tooling assumptions

Prefer minimal dependencies:

- `curl` or `wget`
- `tar`
- `shasum -a 256` or `sha256sum`
- `mktemp`

The script should:

- use `set -eu`
- clean up temporary files on exit
- avoid shell-specific features beyond POSIX `sh` where practical

## GitHub Actions Release Workflow

Add a new workflow, separate from CI, triggered by version tags.

Suggested trigger:

```yaml
on:
  push:
    tags:
      - 'v*'
```

### Jobs

Use a matrix for:

- `macos-14` for Apple Silicon macOS
- `ubuntu-22.04` for Linux x86_64

Each matrix job should:

1. Check out the repository
2. Install build dependencies needed for that runner
3. Configure a `Release` build
4. Build `zv`
5. Stage a release directory with `zv` and `LICENSE`
6. Create the target tarball with the standard filename
7. Upload the tarball as a workflow artifact

Then add a follow-up release job that:

1. Downloads all per-platform artifacts
2. Generates `SHA256SUMS`
3. Creates or updates the GitHub release for the pushed tag
4. Uploads tarballs and `SHA256SUMS` as release assets

Implementation detail:

- Ubuntu installs `cmake`, `nasm`, `libpng-dev`, and the required X11/OpenGL development packages
- macOS installs `nasm` with Homebrew

## Documentation Changes

Update `README.md` with a short installation section that includes:

- the one-line installer command
- a manual download/install alternative
- supported platforms
- a note that Linux still relies on system graphics/windowing libraries
- a simple uninstall command:

```bash
rm -f ~/.local/bin/zv
```

## Verification Plan

### Local verification

After implementation:

- run the installer script against a local or test release URL
- verify install into a temporary directory
- verify `--version` and default latest resolution
- verify unsupported OS/arch messages

### CI verification

Before cutting a real release:

- push a test tag on a branch or fork
- confirm the workflow publishes all assets
- test the install script from the raw GitHub URL against that release

### Manual smoke tests

Test on:

- Apple Silicon macOS
- Ubuntu desktop environment

Validate:

- binary launches
- install directory is correct
- reinstall cleanly overwrites the previous binary

## Execution Order

1. Add `scripts/install.sh`
2. Add the new release workflow
3. Add checksum generation and release publishing
4. Update `README.md`
5. Run local validation for the installer logic
6. Cut a test release tag and validate the full flow

## Status

- Done: add `scripts/install.sh`
- Done: add tag-driven release workflow
- Done: generate and publish `SHA256SUMS`
- Done: update `README.md`
- Pending: validate the full flow against a real GitHub tag and release

## Open Decisions

Decide during implementation:

- whether to keep using CPack at all for release archives or replace it with explicit tarball staging in CI
- whether the installer should use the GitHub API or the `/releases/latest` redirect flow to resolve the latest version
- whether to install into `~/.local/bin` on macOS as well, or prefer `~/bin` if present

My current recommendation:

- do not use CPack for this flow
- build release tarballs explicitly in CI
- install to `~/.local/bin` consistently on both macOS and Linux

This recommendation was adopted in the current implementation.
