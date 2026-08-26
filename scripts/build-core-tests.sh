#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/core-tests"
cmake -S "$ROOT" -B "$BUILD" -DPAVM_BUILD_GUI=OFF -DPAVM_BUILD_TESTS=ON -DPAVM_USE_BUNDLED_CURL=OFF
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure
