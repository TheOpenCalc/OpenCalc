#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
FIRMWARE_DIR="$ROOT_DIR/firmware"
OUT_DIR="$ROOT_DIR/web-sim/src/wasm"
PYK_DIR="$FIRMWARE_DIR/third_party/pikapython"

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

PYC_FLAGS=(
  -O2
  -std=gnu99
  -DPIKA_CONFIG_ENABLE
  -I"$FIRMWARE_DIR"
  -I"$FIRMWARE_DIR/headers"
  -I"$PYK_DIR"
  -I"$PYK_DIR/pikascript-core"
  -I"$PYK_DIR/pikascript-api"
)

mkdir -p "$OUT_DIR/obj"

# Phase 1: compile PikaPython C sources (and the pika_wrapper) as C.
(
  cd "$OUT_DIR/obj"
  emcc -c "${PYC_FLAGS[@]}" \
    "$FIRMWARE_DIR/Core"/pika_wrapper.c \
    "$FIRMWARE_DIR/Core"/py_draw.c \
    "$FIRMWARE_DIR/Core"/py_gfx.c \
    "$FIRMWARE_DIR/Core"/py_turtle.c \
    "$FIRMWARE_DIR/Core"/py_plot.c \
    "$PYK_DIR/pikascript-core"/*.c \
    "$PYK_DIR/pikascript-api"/*.c \
    "$PYK_DIR/pikascript-lib/PikaStdLib"/*.c \
    "$PYK_DIR/pikascript-lib/math"/*.c \
    "$PYK_DIR/pikascript-lib/time"/*.c \
    "$PYK_DIR/pikascript-lib/random"/*.c \
    "$PYK_DIR/pikascript-lib/pika_libc"/*.c \
  ) || {
    echo "PikaPython C compile failed" >&2
    exit 1
  }

# Phase 2: compile the C++ firmware sources and link everything.
em++ -O2 -std=c++17 \
  -DOPENCALC_WASM \
  -DPIKA_CONFIG_ENABLE \
  -I"$FIRMWARE_DIR/wasm/stubs" \
  -I"$FIRMWARE_DIR" \
  -I"$FIRMWARE_DIR/Apps"\
  -I"$FIRMWARE_DIR/Core"\
  -I"$FIRMWARE_DIR/headers" \
  -I"$PYK_DIR" \
  -I"$PYK_DIR/pikascript-core" \
  -I"$PYK_DIR/pikascript-api" \
  "$FIRMWARE_DIR/Core"/*.cpp \
  "$FIRMWARE_DIR/Apps"/*.cpp \
  "$FIRMWARE_DIR/wasm/wasm_input.cpp" \
  "$OUT_DIR/obj"/*.o \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=OpenCalcModule \
  -sEXPORTED_FUNCTIONS='[_main,_opencalc_key_down,_opencalc_text_char,_opencalc_framebuffer,_opencalc_fb_width,_opencalc_fb_height,_opencalc_brightness]' \
  -sEXPORTED_RUNTIME_METHODS='[ccall,cwrap,HEAPU16]' \
  -sALLOW_MEMORY_GROWTH=1 \
  -sASYNCIFY \
  -o "$OUT_DIR/opencalc.js"

echo "WASM build complete: $OUT_DIR/opencalc.js"
