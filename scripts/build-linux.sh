#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/linux"
DEPS="$ROOT/build/deps"
DIST="$ROOT/dist-linux"
CLEAN=0
INSTALL_DEPS=0
for arg in "$@"; do
  case "$arg" in
    --clean) CLEAN=1 ;;
    --install-deps) INSTALL_DEPS=1 ;;
    -h|--help) echo "Usage: scripts/build-linux.sh [--clean] [--install-deps]"; exit 0 ;;
    *) echo "Unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if (( INSTALL_DEPS )); then
  if command -v apt-get >/dev/null; then
    sudo apt-get update
    sudo apt-get install -y build-essential git cmake ninja-build python3 pkg-config libcurl4-openssl-dev libfontconfig1-dev libfreetype6-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxkbcommon-dev libwayland-dev
  elif command -v dnf >/dev/null; then
    sudo dnf install -y gcc-c++ git cmake ninja-build python3 pkgconf-pkg-config libcurl-devel fontconfig-devel freetype-devel libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libxkbcommon-devel wayland-devel
  elif command -v pacman >/dev/null; then
    sudo pacman -S --needed base-devel git cmake ninja python pkgconf curl fontconfig freetype2 libx11 libxext libxrandr libxcursor libxi libxkbcommon wayland
  else
    echo "Unsupported package manager; install the documented build dependencies manually." >&2
    exit 1
  fi
fi

python3 "$ROOT/scripts/validate-source.py" --root "$ROOT"
(( CLEAN )) && rm -rf "$BUILD" "$DIST"
mkdir -p "$BUILD" "$DEPS"

clone_or_update() {
  local url="$1" dir="$2" ref="$3"
  if [[ ! -d "$dir/.git" ]]; then git clone --filter=blob:none "$url" "$dir"; fi
  git -C "$dir" fetch --tags --prune origin
  git -C "$dir" checkout --force "$ref"
  git -C "$dir" submodule update --init --recursive
}

SKIA="$DEPS/skia"
SDL="$DEPS/sdl3"
SKIA_REF="${PAVM_SKIA_REF:-chrome/m126}"
SDL_REF="${PAVM_SDL_REF:-release-3.2.0}"
clone_or_update https://skia.googlesource.com/skia.git "$SKIA" "$SKIA_REF"
clone_or_update https://github.com/libsdl-org/SDL.git "$SDL" "$SDL_REF"

(cd "$SKIA" && python3 tools/git-sync-deps)
if ! command -v gn >/dev/null || ! command -v ninja >/dev/null; then
  DEPOT="$DEPS/depot_tools"
  [[ -d "$DEPOT/.git" ]] || git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT"
  export PATH="$DEPOT:$PATH"
fi
SKIA_OUT="$SKIA/out/PortableAVM-linux"
(cd "$SKIA" && gn gen "$SKIA_OUT" --args='is_official_build=true is_debug=false skia_enable_gpu=false' && ninja -C "$SKIA_OUT" skia)

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPAVM_BUILD_GUI=ON -DPAVM_BUILD_TESTS=ON \
  -DPAVM_USE_BUNDLED_SDL=ON -DPAVM_USE_BUNDLED_CURL=OFF \
  -DPAVM_SKIA_ROOT="$SKIA" -DPAVM_SKIA_OUT="$SKIA_OUT" -DPAVM_SDL_SOURCE_DIR="$SDL"
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure
rm -rf "$DIST"
cmake --install "$BUILD" --prefix "$DIST"
echo "[PortableAVM] Build complete: $DIST"
