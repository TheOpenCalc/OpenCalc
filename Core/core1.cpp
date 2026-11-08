#ifndef OPENCALC_WASM

#include "core1.h"
#include "Tools.h"
#include "display.h"
#include "exam_mode.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

queue_t opencalc_key_q;
queue_t opencalc_flush_q;
queue_t opencalc_free_q;

/* Handshake d'accès à la flash : core0 positionne g_core1_pause, core1 entre
 * dans un spin entièrement en RAM (aucun fetch flash), puis core0 peut
 * effacer/programmer la flash en toute sécurité. */
static volatile bool g_core1_pause = false;
static volatile bool g_core1_paused = false;

/* Buffer de staging SPI de core1 (le vrai byte-swap se fait ici, comme avant
 * dans flush_display). */
#define CORE1_FLUSH_BUF_PIXELS 1024
static uint8_t core1_flush_buf[CORE1_FLUSH_BUF_PIXELS * 2];

#define CORE1_DEBOUNCE_SAMPLES 3
#define CORE1_REPEAT_MS 120
#define CORE1_SCAN_PERIOD_MS 2

extern uint16_t opencalc_framebuffers[CORE1_NUM_FBS][SCREEN_HEIGHT * SCREEN_WIDTH];

/* --- Scanner clavier ------------------------------------------------------- */

static int core1_raw_scan(void)
{
    for (int row = 0; row < ROWS; row++)
    {
        gpio_put(row_pins[row], 0);
        __asm volatile("" ::: "memory");
        for (int col = 0; col < COLS; col++)
        {
            if (gpio_get(col_pins[col]) == 0)
            {
                gpio_put(row_pins[row], 1);
                return row * COLS + col;
            }
        }
        gpio_put(row_pins[row], 1);
    }
    return -1;
}

/* Anti-rebond + répétition (comportement proche de l'ancien scan_keypad :
 * tant qu'une touche est maintenue, elle est re-émise toutes les ~120 ms,
 * ce dont dépend par exemple le combo « maintenir OK » du mode examen). */
static void core1_scan_and_push(void)
{
    static int stable_raw = -1;
    static int stable_count = CORE1_DEBOUNCE_SAMPLES;
    static int reported = -1;
    static uint32_t last_report_ms = 0;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    int raw = core1_raw_scan();

    if (raw == stable_raw)
    {
        if (stable_count < CORE1_DEBOUNCE_SAMPLES)
        {
            stable_count++;
            return;
        }
    }
    else
    {
        stable_raw = raw;
        stable_count = 1;
        if (stable_count < CORE1_DEBOUNCE_SAMPLES)
            return;
    }

    if (stable_raw != reported)
    {
        reported = stable_raw;
        last_report_ms = now;
        if (reported >= 0)
        {
            int key = reported;
            queue_try_add(&opencalc_key_q, &key);
        }
    }    else if (reported >= 0 && (int)(now - last_report_ms) >= CORE1_REPEAT_MS)
    {
        last_report_ms = now;
        /* Ne répète que si l'appli a consommé les événements précédents :
         * sinon la file se remplit et la touche "continue" après relâchement. */
        if (queue_is_empty(&opencalc_key_q))
        {
            int key = reported;
            queue_try_add(&opencalc_key_q, &key);
        }
    }
}

/* --- Flush SPI ------------------------------------------------------------- */

/* Luminosite logicielle : la broche LED de l'ecran est cablee sur
 * l'alimentation (pas de PWM possible), on assombrit donc les pixels a l'envoi.
 * Niveau 0..20 -> facteur 25 %..100 % (20 = image intacte). */
extern int g_brightness;

static inline uint16_t dim_color(uint16_t c, uint32_t f /* 0..256 */)
{
    uint32_t r = ((c >> 11) & 0x1F) * f >> 8;
    uint32_t g = ((c >> 5) & 0x3F) * f >> 8;
    uint32_t b = (c & 0x1F) * f >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void core1_flush_one(core1_flush_job_t *job)
{
    const uint16_t *fb = opencalc_framebuffers[job->buffer];
    int level = g_brightness;
    if (level < 0)
        level = 0;
    if (level > 20)
        level = 20;
    uint32_t factor = 64 + (uint32_t)(192 * level) / 20; /* 64..256 */

    ili_cmd(0x2A);
    ili_data((uint8_t)(job->x0 >> 8));
    ili_data((uint8_t)(job->x0 & 0xFF));
    ili_data((uint8_t)(job->x1 >> 8));
    ili_data((uint8_t)(job->x1 & 0xFF));

    ili_cmd(0x2B);
    ili_data((uint8_t)(job->y0 >> 8));
    ili_data((uint8_t)(job->y0 & 0xFF));
    ili_data((uint8_t)(job->y1 >> 8));
    ili_data((uint8_t)(job->y1 & 0xFF));

    ili_cmd(0x2C);

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    for (int row = job->y0; row <= job->y1; row++)
    {
        int remaining = job->x1 - job->x0 + 1;
        int col = job->x0;
        while (remaining > 0)
        {
            int batch = (remaining > CORE1_FLUSH_BUF_PIXELS) ? CORE1_FLUSH_BUF_PIXELS : remaining;
            for (int i = 0; i < batch; i++)
            {
                uint16_t c = fb[row * SCREEN_HEIGHT + col + i];
                if (factor < 256)
                    c = dim_color(c, factor);
                core1_flush_buf[i * 2] = (uint8_t)(c >> 8);
                core1_flush_buf[i * 2 + 1] = (uint8_t)(c & 0xFF);
            }
            spi_write_blocking(spi0, core1_flush_buf, batch * 2);
            remaining -= batch;
            col += batch;
        }
    }

    gpio_put(PIN_CS, 1);

    int freed = job->buffer;
    queue_add_blocking(&opencalc_free_q, &freed);
}

static void core1_flush_dispatch(void)
{
    core1_flush_job_t job;
    if (queue_try_remove(&opencalc_flush_q, &job))
        core1_flush_one(&job);
}

/* --- Pause flash ----------------------------------------------------------- */

static void __time_critical_func(core1_pause_spin)(void)
{
    /* Exécuté uniquement en RAM : pendant un erase/program flash aucun fetch
     * XIP n'est possible pour core1. */
    g_core1_paused = true;
    while (g_core1_pause)
    {
        __asm volatile("nop");
    }
    g_core1_paused = false;
}

void core1_flash_begin(void)
{
    g_core1_pause = true;
    while (!g_core1_paused)
    {
        __asm volatile("nop");
        __sev();
    }
}

void core1_flash_end(void)
{
    g_core1_pause = false;
    __sev();
}

/* --- Boucle principale de core1 ------------------------------------------- */

static void core1_main(void)
{
    while (1)
    {
        if (g_core1_pause)
        {
            core1_pause_spin();
            continue;
        }
        /* Le flush SPI (long) passe avant le clavier : à ~20 ms/écran plein,
         * un appui éventuellement échantillonné 20 ms plus tard reste bien
         * plus réactif que l'ancien balayage (~140 ms). */
        core1_flush_dispatch();
        core1_scan_and_push();
        exam_mode_led_tick();
        sleep_ms(CORE1_SCAN_PERIOD_MS);
    }
}

void core1_init(void)
{
    queue_init(&opencalc_key_q, sizeof(int), CORE1_KEY_QUEUE_CAP);
    queue_init(&opencalc_flush_q, sizeof(core1_flush_job_t), CORE1_FLUSH_QUEUE_CAP);
    queue_init(&opencalc_free_q, sizeof(int), CORE1_FREE_QUEUE_CAP);

    /* Tampon 0 = tampon actif au démarrage ; seul le tampon 1 est « libre ». */
    int seed = 1;
    queue_add_blocking(&opencalc_free_q, &seed);

    multicore_launch_core1(core1_main);
}

#endif /* OPENCALC_WASM */