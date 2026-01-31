#pragma once

#include <stdint.h>

#ifdef OPENCALC_WASM
// Framebuffer exposed from wasm_input.cpp
extern uint16_t* opencalc_get_framebuffer();

#define FB_WIDTH 320
#define FB_HEIGHT 240
#endif
