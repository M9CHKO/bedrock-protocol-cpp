#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT/build"}"
INSTALL_DIR="${INSTALL_DIR:-"$ROOT/install"}"
GENERATOR="${GENERATOR:-Ninja}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
INSTALL_DEPS=0

for arg in "$@"; do
  case "$arg" in
    --deps) INSTALL_DEPS=1 ;;
    --debug) BUILD_TYPE=Debug ;;
    --release) BUILD_TYPE=Release ;;
    --no-install) INSTALL_DIR="" ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if [[ "$INSTALL_DEPS" == "1" ]]; then
  if command -v pkg >/dev/null 2>&1 && [[ "${PREFIX:-}" == *com.termux* ]]; then
    pkg update
    pkg install -y clang cmake ninja openssl zlib curl
  elif command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y build-essential cmake ninja-build zlib1g-dev libssl-dev curl
  else
    echo "dependency installer supports apt-based Linux and Termux only" >&2
  fi
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  ${INSTALL_DIR:+-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"}

cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN)"

if [[ -n "$INSTALL_DIR" ]]; then
  cmake --install "$BUILD_DIR"
fi

echo
echo "Bedrock Protocol C++ built successfully."
if [[ -n "$INSTALL_DIR" ]]; then
  echo "Installed to: $INSTALL_DIR"
fi
