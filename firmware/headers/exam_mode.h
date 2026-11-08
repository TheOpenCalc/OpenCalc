#ifndef EXAM_MODE_H
#define EXAM_MODE_H

#include <stdint.h>
#include <stdbool.h>

#define EXAM_LED_GPIO 26

#define EXAM_MAGIC 0x4558414Du

#define EXAM_FLASH_TARGET_OFFSET (1020u * 1024u)

#define EXAM_LED_ON_MS 500
#define EXAM_LED_OFF_MS 500

/* Petite icône carrée (mode examen) affichée en haut à droite de l'écran,
 * sur TOUTES les apps et le menu, en complément de la LED GPIO26.
 * Convention de coordonnées (même que display_battery / fill_rect) :
 *   - x = ligne verticale (bornée à SCREEN_HEIGHT=240)
 *   - y = colonne horizontale (bornée à SCREEN_WIDTH=320)
 * On la place en haut (ligne 2) et à droite (colonne ~310), coin haut droite. */
#define EXAM_ICON_W 8
#define EXAM_ICON_X0 2
#define EXAM_ICON_X1 (EXAM_ICON_X0 + EXAM_ICON_W - 1)
#define EXAM_ICON_Y0 (SCREEN_WIDTH - EXAM_ICON_W - 2)
#define EXAM_ICON_Y1 (EXAM_ICON_Y0 + EXAM_ICON_W - 1)

#define COMBO_HOLD_MS 3000

typedef struct
{
    uint32_t magic;
    uint32_t activated_at;
    uint32_t duration_minutes;
} exam_flag_t;

bool exam_mode_is_active(void);

/* Retourne vrai dès que le mode examen est actif (getter léger, sans lecture
 * flash). Utilisé par l'affichage pour dessiner le bandeau indicateur. */
bool exam_mode_banner_active(void);

void exam_mode_activate(uint32_t duration_minutes);

void exam_mode_disable(void);

void exam_mode_enter(void);

void exam_mode_led_init(void);

void exam_mode_led_update(void);

/* Appelé par core1 pour piloter le clignotement de la LED d'examen. */
void exam_mode_led_tick(void);

void exam_mode_clear_ram(void);

bool exam_mode_combo_confirmed(void);

#endif
