#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -f "$ROOT_DIR/.env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source "$ROOT_DIR/.env"
  set +a
fi

ORTOOLS_ROOT="${ORTOOLS_ROOT:-.deps/or-tools}"
if [[ "$ORTOOLS_ROOT" != /* ]]; then
  ORTOOLS_ROOT="$ROOT_DIR/$ORTOOLS_ROOT"
fi
ORTOOLS_VERSION="${ORTOOLS_VERSION:-9.12}"
CONFIG_FILE="$ORTOOLS_ROOT/lib/cmake/ortools/ortoolsConfig.cmake"

if [[ -f "$CONFIG_FILE" ]]; then
  echo "OR-Tools is ready at $ORTOOLS_ROOT"
  exit 0
fi
if [[ -e "$ORTOOLS_ROOT" ]]; then
  echo "Incomplete OR-Tools directory: $ORTOOLS_ROOT" >&2
  echo "Move it aside, then run 'make setup-ortools' again." >&2
  exit 1
fi
if [[ "$ORTOOLS_VERSION" != "9.12" ]]; then
  echo "This project currently pins OR-Tools 9.12; found ORTOOLS_VERSION=$ORTOOLS_VERSION" >&2
  exit 1
fi

RELEASE_TAG="v9.12"
RELEASE_BUILD="9.12.4544"
OS_NAME="$(uname -s)"
CPU_NAME="$(uname -m)"
ARCHIVE_NAME=""

if [[ "$OS_NAME" == "Darwin" ]]; then
  case "$CPU_NAME" in
    arm64) ARCHIVE_NAME="or-tools_arm64_macOS-15.3.1_cpp_v${RELEASE_BUILD}.tar.gz" ;;
    x86_64) ARCHIVE_NAME="or-tools_x86_64_macOS-15.3.1_cpp_v${RELEASE_BUILD}.tar.gz" ;;
  esac
elif [[ "$OS_NAME" == "Linux" ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  if [[ "$CPU_NAME" == "x86_64" && "$ID" == "ubuntu" ]]; then
    case "$VERSION_ID" in
      20.04|22.04|24.04) ARCHIVE_NAME="or-tools_amd64_ubuntu-${VERSION_ID}_cpp_v${RELEASE_BUILD}.tar.gz" ;;
    esac
  elif [[ "$CPU_NAME" == "x86_64" && "$ID" == "debian" ]]; then
    case "$VERSION_ID" in
      11|12) ARCHIVE_NAME="or-tools_amd64_debian-${VERSION_ID}_cpp_v${RELEASE_BUILD}.tar.gz" ;;
    esac
  elif [[ "$CPU_NAME" == "aarch64" && "$ID" == "debian" && "$VERSION_ID" == "11" ]]; then
    ARCHIVE_NAME="or-tools_arm64_debian-11_cpp_v${RELEASE_BUILD}.tar.gz"
  fi
fi

if [[ -n "${ORTOOLS_ARCHIVE_URL:-}" ]]; then
  DOWNLOAD_URL="$ORTOOLS_ARCHIVE_URL"
  ARCHIVE_NAME="${DOWNLOAD_URL##*/}"
elif [[ -n "$ARCHIVE_NAME" ]]; then
  DOWNLOAD_URL="https://github.com/google/or-tools/releases/download/${RELEASE_TAG}/${ARCHIVE_NAME}"
else
  echo "No pinned OR-Tools binary for $OS_NAME/$CPU_NAME." >&2
  echo "Set ORTOOLS_ARCHIVE_URL in .env to a compatible C++ release archive." >&2
  exit 1
fi

mkdir -p "$(dirname "$ORTOOLS_ROOT")"
TEMP_DIR="$(mktemp -d "$(dirname "$ORTOOLS_ROOT")/ortools-install.XXXXXX")"
cleanup() { rm -rf "$TEMP_DIR"; }
trap cleanup EXIT

echo "Downloading OR-Tools $ORTOOLS_VERSION for $OS_NAME/$CPU_NAME..."
curl --fail --location --retry 3 --output "$TEMP_DIR/$ARCHIVE_NAME" "$DOWNLOAD_URL"
mkdir "$TEMP_DIR/extracted"
tar -xzf "$TEMP_DIR/$ARCHIVE_NAME" -C "$TEMP_DIR/extracted"

shopt -s nullglob dotglob
EXTRACTED=("$TEMP_DIR/extracted"/*)
if [[ ${#EXTRACTED[@]} -ne 1 || ! -d "${EXTRACTED[0]}" ]]; then
  echo "Unexpected OR-Tools archive layout." >&2
  exit 1
fi
mv "${EXTRACTED[0]}" "$ORTOOLS_ROOT"

if [[ ! -f "$CONFIG_FILE" ]]; then
  echo "OR-Tools CMake metadata was not found after extraction." >&2
  exit 1
fi
echo "OR-Tools installed at $ORTOOLS_ROOT"
echo "Next: make build-optimizer"
