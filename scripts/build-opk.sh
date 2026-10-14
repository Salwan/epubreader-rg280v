#!/usr/bin/env bash
set -euo pipefail
ROOT="$(dirname "$(dirname "$(readlink -f "$0")")")"
export OD_SDK="$ROOT/third_party/sdk/gcw0-toolchain"
export SOURCE_DATE_EPOCH=1663804800
export LC_ALL=C
python3 "$ROOT/scripts/bootstrap.py"
BUILD="${1:-$ROOT/build/rg280v}"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/rg280v.cmake" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$BUILD"
python3 "$ROOT/scripts/package.py" --build "$BUILD"
