# WASM Build Instructions

This document explains how to build the OpenCalc firmware to WebAssembly (WASM) for the web simulator.

## Prerequisites

- [Emscripten SDK](https://emscripten.org/) installed
- `em++` available in your shell

### Install Emscripten

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

## Build

From the repository root, run:

```bash
source ~/emsdk/emsdk_env.sh  # if not already activated
./firmware/wasm/build_wasm.sh
```

### Output Files

The build generates:
- `web-sim/src/wasm/opencalc.js` - JavaScript glue code
- `web-sim/src/wasm/opencalc.wasm` - Compiled WebAssembly binary

## Architecture

### Stub Headers (`firmware/wasm/stubs/`)

The web build uses stub headers to replace Pico SDK hardware calls:
- `pico/stdlib.h` - Stubs for GPIO, sleep functions
- `hardware/spi.h` - Stubs for SPI communication

### WASM Input (`firmware/wasm/wasm_input.cpp`)

Provides the interface between JavaScript and the firmware:
- `_opencalc_key_down(code)` - Send key press
- `_opencalc_framebuffer()` - Get framebuffer pointer
- `_opencalc_fb_width()` / `_opencalc_fb_height()` - Screen dimensions

## Running the Simulator

See `web-sim/README.md` for instructions on running the web simulator.

---

*This WASM build system was developed with assistance from [Claude](https://claude.ai) (Anthropic).*
