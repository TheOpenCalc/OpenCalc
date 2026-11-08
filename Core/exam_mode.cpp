#include "exam_mode.h"
#include "tz_config.h"
#include "Tools.h"
#include "ui.h"
#include "python.h"
#include "pico/stdlib.h"
#include "display.h"
#include "colors.h"
#include <string.h>

#ifndef OPENCALC_WASM
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "core1.h"
#endif

static volatile bool g_exam_active = false;
static volatile uint32_t g_exam_led_last_toggle = 0;
static volatile bool g_exam_led_state = false;

#ifdef OPENCALC_WASM
/* Cible wasm : pas de flash, le flag vit en RAM (flash vierge = 0xFF). */
static exam_flag_t g_exam_flag = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
#endif

static const exam_flag_t *exam_flag_read(void)
{
#ifdef OPENCALC_WASM
    return &g_exam_flag;
#else
    return (const exam_flag_t *)(XIP_BASE + EXAM_FLASH_TARGET_OFFSET);
#endif
}

bool exam_mode_is_active(void)
{
    const exam_flag_t *flag = exam_flag_read();
    g_exam_active = (flag->magic == EXAM_MAGIC);
    return g_exam_active;
}

bool exam_mode_banner_active(void)
{
    return g_exam_active;
}

static void exam_flag_write(exam_flag_t *flag)
{
#ifdef OPENCALC_WASM
    g_exam_flag = *flag;
#else
    /* flash_range_program() exige une taille multiple de FLASH_PAGE_SIZE
     * (256 o) : on programme une page dont le reste reste à 0xFF. */
    uint8_t buf[(sizeof(exam_flag_t) + (FLASH_PAGE_SIZE - 1u)) & ~(FLASH_PAGE_SIZE - 1u)];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, flag, sizeof(*flag));

    uint32_t ints = save_and_disable_interrupts();
    core1_flash_begin();
    flash_range_erase(EXAM_FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(EXAM_FLASH_TARGET_OFFSET, buf, sizeof(buf));
    core1_flash_end();
    restore_interrupts(ints);
#endif
}

void exam_mode_disable(void)
{
    exam_flag_t empty;
    memset(&empty, 0xFF, sizeof(empty));
    exam_flag_write(&empty);
    g_exam_active = false;
}

void exam_mode_activate(uint32_t duration_minutes)
{
    exam_flag_t flag;
    flag.magic = EXAM_MAGIC;
    flag.activated_at = to_ms_since_boot(get_absolute_time());
    flag.duration_minutes = duration_minutes;

    exam_flag_write(&flag);
    g_exam_active = true;

#ifndef OPENCALC_WASM
    tz_config_lock_all();
#endif
}

void exam_mode_clear_ram(void)
{
    extern int g_angle_mode;
    extern int g_decimals;
    extern int g_brightness;
    g_angle_mode = 0;
    g_decimals = 6;
    g_brightness = 20;
}

void exam_mode_led_init(void)
{
    gpio_init(EXAM_LED_GPIO);
    gpio_set_dir(EXAM_LED_GPIO, GPIO_OUT);
    gpio_put(EXAM_LED_GPIO, 0);
    g_exam_led_last_toggle = to_ms_since_boot(get_absolute_time());
    g_exam_led_state = false;
}

void exam_mode_led_update(void)
{
    /* Matériel : le clignotement est désormais géré par core1 via
     * exam_mode_led_tick(). Fonction conservée pour ne pas modifier les
     * appels existants dans menu.cpp / grapher.cpp. */
#ifndef OPENCALC_WASM
    return;
#else
    if (!g_exam_active)
        return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t interval = g_exam_led_state ? EXAM_LED_ON_MS : EXAM_LED_OFF_MS;

    if (now - g_exam_led_last_toggle >= interval)
    {
        g_exam_led_state = !g_exam_led_state;
        gpio_put(EXAM_LED_GPIO, g_exam_led_state ? 1 : 0);
        g_exam_led_last_toggle = now;
    }
#endif
}

void exam_mode_led_tick(void)
{
    if (!g_exam_active)
        return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t interval = g_exam_led_state ? EXAM_LED_ON_MS : EXAM_LED_OFF_MS;

    if (now - g_exam_led_last_toggle >= interval)
    {
        g_exam_led_state = !g_exam_led_state;
        gpio_put(EXAM_LED_GPIO, g_exam_led_state ? 1 : 0);
        g_exam_led_last_toggle = now;
    }
}

void exam_mode_enter(void)
{
#ifndef OPENCALC_WASM
    tz_config_init();
#endif
    exam_mode_clear_ram();
    python_files_erase();
    exam_mode_led_init();
}

bool exam_mode_combo_confirmed(void)
{
    fill_screen(COLOR_RED);
    draw_char(40, 60, (char *)"MODE EXAMEN", COLOR_WHITE, COLOR_RED, 3);
    draw_char(70, 30, (char *)"Maintenez OK", COLOR_WHITE, COLOR_RED, 2);
    draw_char(90, 30, (char *)"3 secondes...", COLOR_WHITE, COLOR_RED, 2);
    draw_char(130, 30, (char *)"BACK: Annuler", COLOR_WHITE, COLOR_RED, 2);
    flush_display();

    uint32_t hold_start = 0;
    bool holding = false;

    while (true)
    {
        int key = scan_keypad();

        if (key == BACK)
        {
            fill_screen(BACKGROUND_COLOR);
            flush_display();
            return false;
        }

        if (key == OK)
        {
            if (!holding)
            {
                hold_start = to_ms_since_boot(get_absolute_time());
                holding = true;
            }
        }
        else
        {
            holding = false;
        }

        if (holding)
        {
            uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - hold_start;

            fill_rect(110, 30, 15, 260, COLOR_RED);
            int bar_width = (int)((elapsed * 200) / COMBO_HOLD_MS);
            if (bar_width > 200)
                bar_width = 200;
            fill_rect(120, 60, 5, bar_width, COLOR_WHITE);
            flush_display();

            if (elapsed >= COMBO_HOLD_MS)
            {
                fill_screen(COLOR_GREEN);
                draw_char(50, 60, (char *)"MODE EXAMEN", COLOR_BLACK, COLOR_GREEN, 3);
                draw_char(80, 40, (char *)"ACTIVE !", COLOR_BLACK, COLOR_GREEN, 3);
                draw_char(120, 20, (char *)"Toutes les donnees", COLOR_BLACK, COLOR_GREEN, 2);
                draw_char(140, 20, (char *)"ont ete effacees.", COLOR_BLACK, COLOR_GREEN, 2);
                draw_char(170, 20, (char *)"Reflash via BOOTSEL", COLOR_BLACK, COLOR_GREEN, 1);
                draw_char(180, 20, (char *)"pour quitter le mode", COLOR_BLACK, COLOR_GREEN, 1);
                flush_display();
                sleep_ms(3000);
                return true;
            }
        }

        sleep_ms(50);
    }
}
