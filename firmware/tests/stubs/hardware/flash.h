#pragma once

#include <stdint.h>
#include <string.h>

#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_BLOCK_SIZE 32768u

extern uint8_t *opencalc_xip_base;

static inline void flash_range_erase(uint32_t flash_offs, size_t count)
{
    memset(opencalc_xip_base + flash_offs, 0xFF, count);
}

static inline void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count)
{
    memcpy(opencalc_xip_base + flash_offs, data, count);
}