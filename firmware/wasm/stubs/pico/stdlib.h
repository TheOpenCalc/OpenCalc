#pragma once

#include <stdint.h>

// Minimal Pico SDK stubs for Emscripten builds.

#define GPIO_OUT 1
#define GPIO_IN 0
#define GPIO_FUNC_SPI 2

static inline void stdio_init_all(void) {}

static inline void gpio_init(int pin) { (void)pin; }
static inline void gpio_set_dir(int pin, int dir) { (void)pin; (void)dir; }
static inline void gpio_put(int pin, int value) { (void)pin; (void)value; }
static inline int gpio_get(int pin) { (void)pin; return 1; }
static inline void gpio_pull_up(int pin) { (void)pin; }
static inline void gpio_set_function(int pin, int fn) { (void)pin; (void)fn; }

// Time helpers used by UI code.
typedef struct {
    double ms;
} absolute_time_t;

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
static inline absolute_time_t get_absolute_time(void) {
    absolute_time_t t = { emscripten_get_now() };
    return t;
}
static inline uint64_t to_ms_since_boot(absolute_time_t t) {
    return (uint64_t)t.ms;
}
static inline void sleep_ms(uint32_t ms) {
    emscripten_sleep(ms);
}
#else
static inline absolute_time_t get_absolute_time(void) {
    absolute_time_t t = { 0.0 };
    return t;
}
static inline uint64_t to_ms_since_boot(absolute_time_t t) {
    return (uint64_t)t.ms;
}
static inline void sleep_ms(uint32_t ms) { (void)ms; }
#endif
