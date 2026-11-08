#include <stdint.h>
#include <stdbool.h>
#include <emscripten/emscripten.h>
#include "pico/stdlib.h"
#include "headers/menu.h"

// Simple fixed-size ring buffer for key events.
static const int kQueueSize = 64;
static int g_key_queue[kQueueSize];
static int g_key_head = 0;
static int g_key_tail = 0;

// Simple fixed-size ring buffer for raw text characters.
static int g_char_queue[kQueueSize];
static int g_char_head = 0;
static int g_char_tail = 0;

static bool queue_is_empty()
{
    return g_key_head == g_key_tail;
}

static bool queue_is_full()
{
    return ((g_key_tail + 1) % kQueueSize) == g_key_head;
}

extern "C" {
EMSCRIPTEN_KEEPALIVE void opencalc_key_down(int key)
{
    if (queue_is_full()) {
        return;
    }
    g_key_queue[g_key_tail] = key;
    g_key_tail = (g_key_tail + 1) % kQueueSize;
}

EMSCRIPTEN_KEEPALIVE void opencalc_text_char(int c)
{
    if (c < 0 || c > 255) {
        return;
    }
    int next = (g_char_tail + 1) % kQueueSize;
    if (next == g_char_head) {
        return;
    }
    g_char_queue[g_char_tail] = c;
    g_char_tail = next;
}
}

static bool char_queue_is_empty()
{
    return g_char_head == g_char_tail;
}

int scan_text_char()
{
    if (char_queue_is_empty()) {
        return -1;
    }
    int c = g_char_queue[g_char_head];
    g_char_head = (g_char_head + 1) % kQueueSize;
    return c;
}

int scan_keypad_nowait()
{
    if (queue_is_empty()) {
        return -1;
    }
    int key = g_key_queue[g_key_head];
    g_key_head = (g_key_head + 1) % kQueueSize;
    return key;
}

void init_keypad()
{
    // No-op in the web build; input comes from JS.
}

int scan_keypad()
{
    if (queue_is_empty()) {
        // Yield to the browser; keeps busy loops from freezing the page.
        sleep_ms(16);
        return -1;
    }
    int key = g_key_queue[g_key_head];
    g_key_head = (g_key_head + 1) % kQueueSize;
    return key;
}
