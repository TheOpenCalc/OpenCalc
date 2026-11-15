#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "Tools.h"
#include "ui.h"
#include <stdlib.h>

/* Graine du generateur de alea()/randint() (voir Evaluator.cpp) : temps ecoule
 * depuis le demarrage, different a chaque premier appel. */
unsigned opencalc_seed(void)
{
    return (unsigned)to_ms_since_boot(get_absolute_time()) * 2654435761u + 0x7F4A7C15u;
}

#ifndef OPENCALC_WASM
#include "hardware/adc.h"
#endif

int battery_level(void)
{
#ifdef OPENCALC_WASM
    return 4;
#else
    static bool init = false;
    if (!init)
    {
        adc_init();
        adc_gpio_init(29);
        init = true;
    }
    adc_select_input(3);
    /* moyenne de 8 mesures : le bruit de l'ADC fait sinon clignoter l'icone */
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++)
        sum += adc_read();
    float v = (sum / 8.0f) * 3.3f / 4096.0f * 3.0f;
    if (v > 4.5f)
        return 5; /* USB : alimentation et charge */
    if (v >= 4.0f)
        return 4;
    if (v >= 3.85f)
        return 3;
    if (v >= 3.7f)
        return 2;
    if (v >= 3.55f)
        return 1;
    return 0;
#endif
}

jmp_buf g_home_jmp;
volatile bool g_home_armed = false;

void home_check(int key)
{
    if (key == HOME && g_home_armed)
    {
        g_home_armed = false;
        longjmp(g_home_jmp, 1);
    }
}

#ifndef OPENCALC_WASM
#include "core1.h"
#endif

#ifndef OPENCALC_WASM

void init_keypad()
{
    for (int r = 0; r < ROWS; r++)
    {
        gpio_init(row_pins[r]);
        gpio_set_dir(row_pins[r], GPIO_OUT);
        gpio_put(row_pins[r], 1); // inactif (HIGH)
    }

    for (int c = 0; c < COLS; c++)
    {
        gpio_init(col_pins[c]);
        gpio_set_dir(col_pins[c], GPIO_IN);
        gpio_pull_up(col_pins[c]); // pull-up activé
    }

    /* Clavier scanné en arrière-plan sur core1 : scan_keypad()/nowait() lisent
     * désormais la file d'événements remplie par core1. */
    core1_init();
}

/* Attend une touche au plus KEY_WAIT_MS, puis rend -1 (voir Tools.h).
 * Une attente sans limite ne rendait jamais -1 : les boucles d'attente des
 * applis n'appelaient blink_cursor() qu'une fois par touche (curseur fige,
 * visible ou non selon l'instant), et l'ecran du mode examen ne voyait pas
 * que OK avait ete relache. */
#define KEY_WAIT_MS 50

int scan_keypad()
{
    int key;
    absolute_time_t until = make_timeout_time_ms(KEY_WAIT_MS);
    while (!queue_try_remove(&opencalc_key_q, &key))
    {
        if (time_reached(until))
            return -1;
        sleep_ms(2);
    }
    home_check(key);
    return key;
}

#endif

#ifndef OPENCALC_WASM
int scan_text_char()
{
    return -1;
}

int scan_keypad_nowait()
{
    int key;
    if (queue_try_remove(&opencalc_key_q, &key))
    {
        home_check(key);
        return key;
    }
    return -1;
}
#endif

double **init_2d_Mat(int L, int H, double default_value)
{
    double **a = (double **)malloc(sizeof(double *) * H);
    for (int i = 0; i < H; i++)
    {
        a[i] = (double *)malloc(sizeof(double) * L);
        for (int j = 0; j < L; j++)
        {
            a[i][j] = default_value;
        }
    }
    return a;
}

void toggle(bool *snd)
{
    *snd = !(*snd);
    return;
}