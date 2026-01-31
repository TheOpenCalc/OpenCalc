#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
FIRMWARE_DIR="$ROOT_DIR/firmware"
OUT_DIR="$ROOT_DIR/web-sim/src/wasm"

mkdir -p "$OUT_DIR"

if ! command -v em++ >/dev/null 2>&1; then
  if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
    # shellcheck disable=SC1090
    source "$EMSDK/emsdk_env.sh"
  elif [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    # shellcheck disable=SC1090
    source "$HOME/emsdk/emsdk_env.sh"
  fi
fi

if ! command -v em++ >/dev/null 2>&1; then
  echo "em++ not found. Run: source ~/emsdk/emsdk_env.sh" >&2
  exit 127
fi

em++ -O2 -std=c++17 \
  -DOPENCALC_WASM \
  -I"$FIRMWARE_DIR/wasm/stubs" \
  -I"$FIRMWARE_DIR" \
  -I"$FIRMWARE_DIR/headers" \
  "$FIRMWARE_DIR"/*.cpp \
  "$FIRMWARE_DIR/wasm/wasm_input.cpp" \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=OpenCalcModule \
  -sEXPORTED_FUNCTIONS='[_main,_opencalc_key_down,_opencalc_framebuffer,_opencalc_fb_width,_opencalc_fb_height]' \
  -sEXPORTED_RUNTIME_METHODS='[ccall,cwrap,HEAPU16]' \
  -sALLOW_MEMORY_GROWTH=1 \
  -sASYNCIFY \
  -o "$OUT_DIR/opencalc.js"

echo "WASM build complete: $OUT_DIR/opencalc.js"
