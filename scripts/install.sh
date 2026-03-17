#!/bin/sh

set -eu

REPO="${ZV_GITHUB_REPO:-nburrus/zv}"
INSTALL_DIR="${ZV_INSTALL_DIR:-$HOME/.local/bin}"
VERSION="${ZV_VERSION:-}"
TMP_DIR=""

usage() {
    cat <<EOF
Install zv from GitHub release artifacts.

Usage:
  install.sh [--version <tag>] [--install-dir <path>] [--help]

Environment:
  ZV_VERSION         Release tag to install, e.g. v0.1.0
  ZV_INSTALL_DIR     Installation directory, default: ~/.local/bin
  ZV_GITHUB_REPO     GitHub repository in owner/name form, default: nburrus/zv
EOF
}

fail() {
    echo "error: $*" >&2
    exit 1
}

cleanup() {
    if [ -n "${TMP_DIR}" ] && [ -d "${TMP_DIR}" ]; then
        rm -rf "${TMP_DIR}"
    fi
}

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        fail "missing required command: $1"
    fi
}

download_to() {
    url="$1"
    out="$2"

    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$out"
        return 0
    fi

    if command -v wget >/dev/null 2>&1; then
        wget -qO "$out" "$url"
        return 0
    fi

    fail "missing required command: curl or wget"
}

resolve_latest_version() {
    latest_url="https://github.com/${REPO}/releases/latest"

    if command -v curl >/dev/null 2>&1; then
        effective_url="$(curl -fsSLI -o /dev/null -w '%{url_effective}' "$latest_url")" || fail "failed to resolve latest release"
    elif command -v wget >/dev/null 2>&1; then
        effective_url="$(wget -qSO- --max-redirect=0 "$latest_url" 2>&1 | awk '/^  Location: / {print $2}' | tail -n 1 | tr -d '\r')" || true
        [ -n "$effective_url" ] || fail "failed to resolve latest release"
    else
        fail "missing required command: curl or wget"
    fi

    version="${effective_url##*/}"
    [ -n "$version" ] || fail "could not determine latest release version"
    printf '%s\n' "$version"
}

detect_target() {
    os="$(uname -s)"
    arch="$(uname -m)"

    case "$os:$arch" in
        Darwin:arm64)
            printf '%s\n' "macos-arm64"
            ;;
        Linux:x86_64|Linux:amd64)
            printf '%s\n' "linux-x86_64"
            ;;
        *)
            fail "unsupported platform: ${os} ${arch}"
            ;;
    esac
}

verify_checksum() {
    checksum_file="$1"
    archive_file="$2"
    archive_name="$(basename "$archive_file")"
    expected="$(awk -v archive_name="$archive_name" '$2 == archive_name || $2 == ("./" archive_name) { print $1; exit }' "$checksum_file")"

    [ -n "$expected" ] || fail "missing checksum entry for ${archive_name}"

    if command -v shasum >/dev/null 2>&1; then
        actual="$(shasum -a 256 "$archive_file" | awk '{print $1}')"
        [ "$expected" = "$actual" ] || fail "checksum verification failed"
        return 0
    fi

    if command -v sha256sum >/dev/null 2>&1; then
        actual="$(sha256sum "$archive_file" | awk '{print $1}')"
        [ "$expected" = "$actual" ] || fail "checksum verification failed"
        return 0
    fi

    fail "missing required command: shasum or sha256sum"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            [ "$#" -ge 2 ] || fail "missing value for --version"
            VERSION="$2"
            shift 2
            ;;
        --install-dir)
            [ "$#" -ge 2 ] || fail "missing value for --install-dir"
            INSTALL_DIR="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
done

need_cmd tar
need_cmd mktemp

TARGET="$(detect_target)"

if [ -z "$VERSION" ]; then
    VERSION="$(resolve_latest_version)"
fi

case "$VERSION" in
    v*)
        ;;
    *)
        fail "version must be a Git tag like v0.1.0"
        ;;
esac

ARCHIVE_NAME="zv-${VERSION}-${TARGET}.tar.gz"
BASE_URL="https://github.com/${REPO}/releases/download/${VERSION}"
ARCHIVE_URL="${BASE_URL}/${ARCHIVE_NAME}"
CHECKSUMS_URL="${BASE_URL}/SHA256SUMS"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zv-install.XXXXXX")"
trap cleanup EXIT INT TERM HUP

ARCHIVE_PATH="${TMP_DIR}/${ARCHIVE_NAME}"
CHECKSUMS_PATH="${TMP_DIR}/SHA256SUMS"

echo "Installing zv ${VERSION} for ${TARGET}..."
download_to "$ARCHIVE_URL" "$ARCHIVE_PATH"
download_to "$CHECKSUMS_URL" "$CHECKSUMS_PATH"
verify_checksum "$CHECKSUMS_PATH" "$ARCHIVE_PATH"

mkdir -p "$INSTALL_DIR"
tar -xzf "$ARCHIVE_PATH" -C "$TMP_DIR"
[ -f "${TMP_DIR}/zv" ] || fail "archive did not contain zv"
install_path="${INSTALL_DIR}/zv"
cp "${TMP_DIR}/zv" "$install_path"
chmod 755 "$install_path"

case ":$PATH:" in
    *":${INSTALL_DIR}:"*)
        path_message=""
        ;;
    *)
        path_message="Add ${INSTALL_DIR} to PATH if you want to run zv directly."
        ;;
esac

echo "Installed ${install_path}"
if [ -n "$path_message" ]; then
    echo "$path_message"
fi
