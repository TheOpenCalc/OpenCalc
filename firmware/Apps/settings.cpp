#include "ui.h"
#include "menu.h"
#include "settings.h"
#include "widgets.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#ifndef OPENCALC_WASM
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/address_mapped.h"
extern void core1_flash_begin(void);
extern void core1_flash_end(void);
#endif
#include <malloc.h>
#include <string.h>
#include "colors.h"
#include "ui_kit.h"
#include "exam_mode.h"
#include "Tools.h"

int g_angle_mode = 0;
int g_decimals = 6;
/* 0..20 ; 20 = pleine luminosite (assombrissement logiciel, voir Core/core1.cpp). */
int g_brightness = 20;

int angle_mode_get(void)
{
    return g_angle_mode;
}

/* --- Persistance des réglages dans la flash interne ---
 * Les réglages sont stockés dans un secteur dédié de la flash, loin du
 * programme (~448 Ko) et du flag du mode examen (1020 Ko). On utilise un
 * numéro magique + une somme de contrôle pour détecter une zone non écrite
 * (flash vierge = 0xFF) ou corrompue, auquel cas on retombe sur les valeurs
 * par défaut.
 *
 * Carte de la flash :
 *   0x000FF000 (1020 Ko)  mode examen,  1 secteur (4 Ko)
 *   0x00100000 (1024 Ko)  réglages,     1 secteur (4 Ko)
 *   0x00101000 (1028 Ko)  fichiers py,  3 secteurs (12 Ko)
 *   0x00104000 (1040 Ko)  tamagotchi,  1 secteur (4 Ko)
 * Ce secteur couvre [0x100000, 0x101000) : ne RIEN écrire après, les fichiers
 * python démarrent à 0x101000. */
#define SETTINGS_FLASH_OFFSET (1024u * 1024u)
#define SETTINGS_MAGIC 0x53455447u /* 'SETG' */

typedef struct
{
    uint32_t magic;
    int32_t angle_mode;
    int32_t decimals;
    int32_t brightness;
    uint32_t checksum;
} settings_store_t;

static uint32_t settings_checksum(const settings_store_t *s)
{
    uint32_t sum = s->magic;
    sum += (uint32_t)s->angle_mode;
    sum += (uint32_t)s->decimals;
    sum += (uint32_t)s->brightness;
    return sum;
}

static void settings_save_impl(void)
{
#ifndef OPENCALC_WASM
    settings_store_t s;
    memset(&s, 0xFF, sizeof(s));
    s.magic = SETTINGS_MAGIC;
    s.angle_mode = g_angle_mode;
    s.decimals = g_decimals;
    s.brightness = g_brightness;
    s.checksum = settings_checksum(&s);

    /* flash_range_program() exige une taille multiple de FLASH_PAGE_SIZE
     * (256 o) : on programme une page dont le reste reste à 0xFF. */
    uint8_t buf[(sizeof(s) + (FLASH_PAGE_SIZE - 1u)) & ~(FLASH_PAGE_SIZE - 1u)];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &s, sizeof(s));

    uint32_t ints = save_and_disable_interrupts();
    core1_flash_begin();
    flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFFSET, buf, sizeof(buf));
    core1_flash_end();
    restore_interrupts(ints);
#endif
}

void settings_save(void)
{
    settings_save_impl();
}

void settings_load(void)
{
    int def_angle = g_angle_mode;
    int def_dec = g_decimals;
    int def_bright = g_brightness;

#ifndef OPENCALC_WASM
    const settings_store_t *s = (const settings_store_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET);
    if (s->magic == SETTINGS_MAGIC && s->checksum == settings_checksum(s))
    {
        if (s->angle_mode >= 0 && s->angle_mode <= 1)
            g_angle_mode = (int)s->angle_mode;
        if (s->decimals >= 0 && s->decimals <= 9)
            g_decimals = (int)s->decimals;
        if (s->brightness >= 0 && s->brightness <= 20)
            g_brightness = (int)s->brightness;
        return;
    }

    /* Zone invalide (premier démarrage ou corrompue) : réécris les défauts. */
    g_angle_mode = def_angle;
    g_decimals = def_dec;
    g_brightness = def_bright;
    settings_save_impl();
#endif
}

/* ------------------------------ Interface ------------------------------- */

/* Coordonnees "naturelles" (ui_kit.h) : X 0..319 de gauche a droite,
 * Y 0..239 de haut en bas. */
#define ST_TOP (UK_HEADER_H + 6)
#define ST_ROW_H 38
#define ST_CARD_X 6
#define ST_CARD_W (SCREEN_WIDTH - 2 * ST_CARD_X)
#define ST_CARD_H 34
#define ST_CTRL_X 150
#define ST_CTRL_W 156
#define ST_INFO_Y (ST_TOP + ST_NUM_ROWS * ST_ROW_H + 2)
#define ST_INFO_H (SCREEN_HEIGHT - UK_FOOTER_H - 4 - ST_INFO_Y)

#define ST_ACCENT UK_HEADER_BG
#define ST_TRACK 0xce79
#define ST_MUTED 0x6b4d

enum
{
    ROW_BRIGHTNESS = 0,
    ROW_ANGLE,
    ROW_DECIMALS,
    ROW_ABOUT,
    ST_NUM_ROWS
};

static const char *const ROW_LABELS[ST_NUM_ROWS] = {
    "Luminosite", "Angles", "Decimales", "A propos"};

/* Petit triangle plein (fleche gauche/droite) de hauteur 2*r+1. */
static void st_arrow(int X, int Ymid, int r, bool right, uint16_t c)
{
    for (int i = 0; i <= r; i++)
    {
        int h = 2 * (r - i) + 1;
        int x = right ? X + i : X + r - i;
        uk_rect(x, Ymid - (r - i), 1, h, c);
    }
}

static void st_draw_brightness(int y, bool sel)
{
    /* 10 segments, chacun couvre 2 niveaux (0..20). */
    const int seg_w = 9, gap = 2, n = 10;
    const int bar_x = ST_CTRL_X + 10;
    const int bar_y = y + 11, bar_h = 12;
    for (int i = 0; i < n; i++)
    {
        int sx = bar_x + i * (seg_w + gap);
        int lvl = g_brightness - 2 * i;
        uk_rect(sx, bar_y, seg_w, bar_h, ST_TRACK);
        if (lvl >= 2)
            uk_rect(sx, bar_y, seg_w, bar_h, ST_ACCENT);
        else if (lvl == 1)
            uk_rect(sx, bar_y, seg_w / 2 + 1, bar_h, ST_ACCENT);
    }
    uint16_t ac = sel ? ST_ACCENT : ST_TRACK;
    st_arrow(ST_CTRL_X, bar_y + bar_h / 2, 4, false, g_brightness > 0 ? ac : ST_TRACK);
    st_arrow(bar_x + n * (seg_w + gap) + 1, bar_y + bar_h / 2, 4, true, g_brightness < 20 ? ac : ST_TRACK);

    char pct[8];
    int v = g_brightness * 5;
    int k = 0;
    if (v >= 100)
        pct[k++] = '1';
    if (v >= 10)
        pct[k++] = '0' + (v / 10) % 10;
    pct[k++] = '0' + v % 10;
    pct[k++] = '%';
    pct[k] = '\0';
    uk_text_right(ST_CTRL_X + ST_CTRL_W, y + 13, pct, COLOR_BLACK, 1);
}

static void st_draw_angle(int y, bool sel)
{
    const int x = ST_CTRL_X, w = ST_CTRL_W, h = 24, yy = y + 5;
    const int half = w / 2;
    static const char *const names[2] = {"RAD", "DEG"};
    uk_rect(x, yy, w, h, COLOR_WHITE);
    for (int i = 0; i < 2; i++)
    {
        int sx = x + i * half;
        int sw = i ? w - half : half;
        bool on = g_angle_mode == i;
        if (on)
            uk_rect(sx + 2, yy + 2, sw - 4, h - 4, sel ? ST_ACCENT : COLOR_GRAY);
        uk_text_center(sx, yy + 4, sw, names[i], on ? COLOR_WHITE : ST_MUTED, 2);
    }
    uk_frame(x, yy, w, h, sel ? ST_ACCENT : COLOR_GRAY);
}

static void st_draw_decimals(int y, bool sel)
{
    const int x = ST_CTRL_X, w = ST_CTRL_W, yy = y + 5, h = 24;
    char v[2] = {(char)('0' + g_decimals), '\0'};
    uk_rect(x + 22, yy, w - 44, h, COLOR_WHITE);
    uk_frame(x + 22, yy, w - 44, h, sel ? ST_ACCENT : COLOR_GRAY);
    uk_text_center(x, yy + 4, w, v, COLOR_BLACK, 2);
    uint16_t ac = sel ? ST_ACCENT : ST_TRACK;
    st_arrow(x + 6, yy + h / 2, 6, false, g_decimals > 0 ? ac : ST_TRACK);
    st_arrow(x + w - 13, yy + h / 2, 6, true, g_decimals < 9 ? ac : ST_TRACK);
}

static void st_draw_about(int y, bool sel)
{
    uk_text_right(ST_CTRL_X + ST_CTRL_W - 14, y + 13, "OpenCalc 0.9.5", ST_MUTED, 1);
    st_arrow(ST_CTRL_X + ST_CTRL_W - 6, y + 17, 5, true, sel ? ST_ACCENT : COLOR_GRAY);
}

/* Apercu du nombre de decimales : pi arrondi. */
static void st_pi_preview(char *buf)
{
    static const char digits[] = "3.141592654";
    int d = g_decimals;
    if (d == 0)
    {
        strcpy(buf, "pi = 3");
        return;
    }
    strcpy(buf, "pi = ");
    /* digits contient 9 decimales deja arrondies a la 9e. */
    char tmp[16];
    memcpy(tmp, digits, 2 + d);
    tmp[2 + d] = '\0';
    /* Arrondi a d decimales (seule la 4e decimale -> 3.1416 change ici). */
    if (d < 9 && digits[2 + d] >= '5')
    {
        int j = 1 + d;
        while (j >= 0)
        {
            if (tmp[j] == '.')
            {
                j--;
                continue;
            }
            if (tmp[j] == '9')
            {
                tmp[j] = '0';
                j--;
                continue;
            }
            tmp[j]++;
            break;
        }
    }
    strcat(buf, tmp);
}

static void st_draw_info(int row)
{
    const int x = ST_CARD_X, y = ST_INFO_Y, w = ST_CARD_W, h = ST_INFO_H;
    uk_rect(x, y, w, h, COLOR_WHITE);
    uk_frame(x, y, w, h, ST_TRACK);
    uk_rect(x, y, 3, h, ST_ACCENT);
    const char *l1 = "", *l2 = "";
    char buf[32];
    switch (row)
    {
    case ROW_BRIGHTNESS:
        l1 = "Retroeclairage de l'ecran.";
        l2 = "Baisser la luminosite economise la batterie.";
        break;
    case ROW_ANGLE:
        l1 = "Unite utilisee par sin, cos, tan et leurs inverses.";
        l2 = g_angle_mode ? "Degres : un tour complet = 360." : "Radians : un tour complet = 2pi.";
        break;
    case ROW_DECIMALS:
        l1 = "Chiffres affiches apres la virgule.";
        st_pi_preview(buf);
        l2 = buf;
        break;
    case ROW_ABOUT:
        l1 = "Version, site web et etat du mode examen.";
        l2 = "OK pour afficher.";
        break;
    }
    uk_text(x + 10, y + 8, l1, COLOR_BLACK, 1);
    uk_text(x + 10, y + 22, l2, ST_MUTED, 1);
}

static void st_draw(int sel)
{
    fill_screen(BACKGROUND_COLOR);
    uk_header("Parametres");

    for (int i = 0; i < ST_NUM_ROWS; i++)
    {
        int y = ST_TOP + i * ST_ROW_H;
        bool s = i == sel;
        uk_rect(ST_CARD_X, y, ST_CARD_W, ST_CARD_H, s ? COLOR_WHITE : FRONTGROUND_COLOR);
        if (s)
        {
            uk_frame(ST_CARD_X, y, ST_CARD_W, ST_CARD_H, ST_ACCENT);
            uk_rect(ST_CARD_X, y, 4, ST_CARD_H, ST_ACCENT);
        }
        uk_text(ST_CARD_X + 12, y + 9, ROW_LABELS[i], s ? ST_ACCENT : COLOR_BLACK, 2);
        switch (i)
        {
        case ROW_BRIGHTNESS:
            st_draw_brightness(y, s);
            break;
        case ROW_ANGLE:
            st_draw_angle(y, s);
            break;
        case ROW_DECIMALS:
            st_draw_decimals(y, s);
            break;
        case ROW_ABOUT:
            st_draw_about(y, s);
            break;
        }
    }

    st_draw_info(sel);
    uk_footer(sel == ROW_ABOUT ? "^v: choisir   OK: ouvrir   BACK: quitter"
                               : "^v: choisir   </>: modifier   BACK: quitter");
    flush_display();
}

static void st_about(void)
{
    fill_screen(BACKGROUND_COLOR);
    uk_header("A propos");

    uk_rect(ST_CARD_X, 30, ST_CARD_W, 84, COLOR_WHITE);
    uk_frame(ST_CARD_X, 30, ST_CARD_W, 84, ST_TRACK);
    uk_text_center(0, 44, SCREEN_WIDTH, "OpenCalc", ST_ACCENT, 3);
    uk_text_center(0, 76, SCREEN_WIDTH, "Calculatrice libre et open source", COLOR_BLACK, 1);
    uk_text_center(0, 92, SCREEN_WIDTH, "opencalc.fr", ST_MUTED, 1);

    struct
    {
        const char *k;
        const char *v;
    } info[3] = {
        {"Version", "0.9.5"},
        {"Mode examen", exam_mode_is_active() ? "Actif" : "Inactif"},
        {"Angles / Decimales", nullptr},
    };
    char ad[16] = "RAD / 6";
    ad[0] = g_angle_mode ? 'D' : 'R';
    ad[1] = g_angle_mode ? 'E' : 'A';
    ad[2] = g_angle_mode ? 'G' : 'D';
    ad[6] = (char)('0' + g_decimals);
    info[2].v = ad;

    for (int i = 0; i < 3; i++)
    {
        int y = 122 + i * 28;
        uk_rect(ST_CARD_X, y, ST_CARD_W, 24, i % 2 ? FRONTGROUND_COLOR_BIS : FRONTGROUND_COLOR);
        uk_text(ST_CARD_X + 10, y + 8, info[i].k, COLOR_BLACK, 1);
        uk_text_right(ST_CARD_X + ST_CARD_W - 10, y + 8, info[i].v,
                      (i == 1 && exam_mode_is_active()) ? COLOR_RED : ST_ACCENT, 1);
    }

    uk_footer("OK / BACK: retour");
    flush_display();
    while (true)
    {
        int k = uk_wait_key();
        if (k == OK || k == ENTER || k == BACK)
            return;
    }
}

void settings()
{
    int sel = 0;
    bool dirty = false; /* reglages modifies : sauvegarde unique en sortie */
    /* HOME ferait un longjmp vers le menu sans passer par la sauvegarde : on
     * le traite ici comme BACK (le menu reprend la main au retour). */
    bool home_armed = g_home_armed;
    g_home_armed = false;

    while (true)
    {
        st_draw(sel);
        int key = uk_wait_key();

        switch (key)
        {
        case UP:
            sel = (sel - 1 + ST_NUM_ROWS) % ST_NUM_ROWS;
            break;
        case DOWN:
            sel = (sel + 1) % ST_NUM_ROWS;
            break;

        case LEFT:
        case RIGHT:
        {
            int d = key == RIGHT ? 1 : -1;
            if (sel == ROW_BRIGHTNESS)
            {
                int v = g_brightness + d;
                if (v >= 0 && v <= 20)
                {
                    g_brightness = v;
                    dirty = true;
                }
            }
            else if (sel == ROW_ANGLE)
            {
                int v = key == RIGHT ? 1 : 0;
                if (v != g_angle_mode)
                {
                    g_angle_mode = v;
                    dirty = true;
                }
            }
            else if (sel == ROW_DECIMALS)
            {
                int v = g_decimals + d;
                if (v >= 0 && v <= 9)
                {
                    g_decimals = v;
                    dirty = true;
                }
            }
            break;
        }

        case OK:
        case ENTER:
            if (sel == ROW_ANGLE)
            {
                g_angle_mode ^= 1;
                dirty = true;
            }
            else if (sel == ROW_ABOUT)
                st_about();
            break;

        case BACK:
        case HOME:
            /* Une seule ecriture flash (lente, coupe les interruptions) au
             * lieu d'une par appui sur une fleche. */
            if (dirty)
                settings_save();
            g_home_armed = home_armed;
            return;

        default:
            break;
        }
    }
}
