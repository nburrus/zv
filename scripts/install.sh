#!/bin/sh

set -eu

REPO="${ZV_GITHUB_REPO:-nburrus/zv}"
INSTALL_DIR="${ZV_INSTALL_DIR:-$HOME/.local/bin}"
VERSION="${ZV_VERSION:-}"
TMP_DIR=""

usage() {
    cat <<EOF
Install the Rust zv release for Linux or macOS.

Usage: install.sh [--version <tag>] [--install-dir <path>] [--help]
EOF
}

fail() { echo "error: $*" >&2; exit 1; }
cleanup() { [ -z "$TMP_DIR" ] || rm -rf "$TMP_DIR"; }
need_cmd() { command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"; }

download_to() {
    if command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2"; return; fi
    if command -v wget >/dev/null 2>&1; then wget -qO "$2" "$1"; return; fi
    fail "missing required command: curl or wget"
}

resolve_latest_version() {
    curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/${REPO}/releases/latest" | awk -F/ '{print $NF}'
}

detect_target() {
    case "$(uname -s):$(uname -m)" in
        Darwin:arm64) echo macos-arm64 ;;
        Darwin:x86_64) echo macos-x86_64 ;;
        Linux:x86_64|Linux:amd64) echo linux-x86_64 ;;
        *) fail "unsupported platform: $(uname -s) $(uname -m)" ;;
    esac
}

verify_checksum() {
    expected="$(awk -v name="$(basename "$2")" '$2 == name || $2 == ("./" name) {print $1; exit}' "$1")"
    [ -n "$expected" ] || fail "missing checksum entry"
    if command -v shasum >/dev/null 2>&1; then actual="$(shasum -a 256 "$2" | awk '{print $1}')"; else actual="$(sha256sum "$2" | awk '{print $1}')"; fi
    [ "$expected" = "$actual" ] || fail "checksum verification failed"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version) VERSION="$2"; shift 2 ;;
        --install-dir) INSTALL_DIR="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

need_cmd tar
need_cmd mktemp
TARGET="$(detect_target)"
[ -n "$VERSION" ] || VERSION="$(resolve_latest_version)"
case "$VERSION" in v*) ;; *) fail "version must be a Git tag like v0.2.0" ;; esac

ARCHIVE="zv-${VERSION}-${TARGET}.tar.gz"
BASE_URL="https://github.com/${REPO}/releases/download/${VERSION}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zv-install.XXXXXX")"
trap cleanup EXIT INT TERM HUP
download_to "${BASE_URL}/${ARCHIVE}" "${TMP_DIR}/${ARCHIVE}"
download_to "${BASE_URL}/SHA256SUMS" "${TMP_DIR}/SHA256SUMS"
verify_checksum "${TMP_DIR}/SHA256SUMS" "${TMP_DIR}/${ARCHIVE}"
tar -xzf "${TMP_DIR}/${ARCHIVE}" -C "$TMP_DIR"
[ -f "${TMP_DIR}/zv" ] || fail "archive did not contain zv"
mkdir -p "$INSTALL_DIR"
cp "${TMP_DIR}/zv" "${INSTALL_DIR}/zv"
chmod 755 "${INSTALL_DIR}/zv"
echo "Installed ${INSTALL_DIR}/zv"
