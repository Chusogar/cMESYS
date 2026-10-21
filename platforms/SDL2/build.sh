#!/usr/bin/env bash
# Build cMESYS for desktop (SDL2).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/platforms/SDL2/build}"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build . -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"

echo
echo "Built: $BUILD_DIR/cmesys"
echo "Run:   $BUILD_DIR/cmesys"
echo "Test:  mkdir -p out && $BUILD_DIR/cmesys_host_test [frames]"
