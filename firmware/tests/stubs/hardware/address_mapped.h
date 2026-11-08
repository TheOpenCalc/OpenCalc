#pragma once

#include <stdint.h>

extern uint8_t *opencalc_xip_base;

#define XIP_BASE ((uintptr_t)opencalc_xip_base)