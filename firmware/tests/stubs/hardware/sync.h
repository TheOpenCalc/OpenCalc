#pragma once

#include <stdint.h>

static inline uint32_t save_and_disable_interrupts(void)
{
    return 0;
}

static inline void restore_interrupts(uint32_t status)
{
    (void)status;
}