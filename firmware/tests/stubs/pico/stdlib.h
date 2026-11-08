#ifndef TEST_STUB_PICO_STDLIB_H
#define TEST_STUB_PICO_STDLIB_H
#include <stdint.h>
#include <time.h>

static inline void sleep_ms(uint32_t ms)
{
    (void)ms;
}
static inline uint64_t get_absolute_time(void)
{
    return (uint64_t)(clock() * 1000 / CLOCKS_PER_SEC);
}
static inline uint64_t to_ms_since_boot(uint64_t t)
{
    return t;
}
#endif
