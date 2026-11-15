#include "ui.h"
#include "Tools.h"
#include "colors.h"
#include "pika_wrapper.h"
#include "python.h"
#include "pico/stdlib.h"
#ifndef OPENCALC_WASM
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/address_mapped.h"
extern void core1_flash_begin(void);
extern void core1_flash_end(void);
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 6 Ko par programme (environ 200 lignes) ; 2 Ko auparavant. */
#define PY_CODE_CAP 6144
#define PY_OUT_CAP 4096
/* Police du code et de la sortie : 1 = 7x9 (FONT_7X9), 0 = 5x7 compacte.
 * Le titre, l'aide et les onglets restent en 5x7 (PY_SMALL_W). */
#define PY_USE_7X9 1
#if PY_USE_7X9
#define PY_FONT FONT_7X9
#define PY_CHAR_H 10
#define PY_CHAR_W 8
#define PY_BODY_ROWS 18
#else
#define PY_FONT 1
#define PY_CHAR_H 8
#define PY_CHAR_W 5
#define PY_BODY_ROWS 23
#endif
#define PY_SMALL_W 5
#define PY_HEADER_ROWS 3
#define PY_BODY_TOP (PY_HEADER_ROWS * PY_CHAR_H)
#define PY_GUTTER_DIGITS 4
#define PY_GUTTER_W ((PY_GUTTER_DIGITS + 1) * PY_CHAR_W)
#define PY_CHARS_PER_LINE ((SCREEN_WIDTH - PY_GUTTER_W) / PY_CHAR_W)
#define PY_OUT_MAX_LINES 512
#define PY_EDITOR_BG BACKGROUND_COLOR
#define PY_SEL_BG 0xfff0
#define PY_MAX_FILES 4
#define PY_FILE_NAME_MAX 16

#define PY_VIEW_EDIT 0
#define PY_VIEW_OUT 1
#define PY_VIEW_FILES 2

/* Programmes en RAM : alloues seulement pendant que l'app est ouverte (24 Ko
 * rendus au calcul formel le reste du temps). Une sortie par HOME les garde ;
 * ils sont reutilises a la prochaine ouverture. */
static char (*py_codes)[PY_CODE_CAP] = NULL;
static int py_lens[PY_MAX_FILES];
static int py_curs[PY_MAX_FILES];
static int py_tops[PY_MAX_FILES];
static char py_names[PY_MAX_FILES][PY_FILE_NAME_MAX];
static int py_nfiles = 1;
static int py_cur_file = 0;
static int py_sel_file = 0;

#define py_code (py_codes[py_cur_file])
#define py_code_len (py_lens[py_cur_file])
#define py_cur (py_curs[py_cur_file])
#define py_top_line (py_tops[py_cur_file])

static bool py_snd = false;
static char py_out[PY_OUT_CAP];
static int py_out_len = 0;
static int py_view = PY_VIEW_EDIT;
static int py_out_lines[PY_OUT_MAX_LINES];
static int py_out_nlines = 0;
static int py_out_scroll = 0;
static char py_pending[256];
static int py_pending_n = 0;

/* --- Persistance des fichiers Python dans la flash interne ---
 * Les fichiers (nom + code + longueur, jusqu'à PY_MAX_FILES) sont sérialisés
 * dans une zone dédiée de la flash afin de survivre aux redémarrages. Un
 * numéro magique + une somme de contrôle permettent de détecter une zone non
 * écrite (flash vierge = 0xFF) ou corrompue : dans ce cas on retombe sur le
 * fichier d'exemple par défaut.
 *
 * Carte de la flash (binaire ~448 Ko, donc tout est bien au-delà) :
 *   0x000FF000 (1020 Ko)  mode examen,  1 secteur (4 Ko)
 *   0x00100000 (1024 Ko)  réglages,     1 secteur (4 Ko)
 *   0x00101000 (1028 Ko)  ancienne zone des fichiers py (2 Ko par
 *                          fichier), lue une fois puis effacee
 *   0x00104000 (1040 Ko)  tamagotchi,  1 secteur (4 Ko)
 *   0x00105000 (1044 Ko)  fichiers py,  7 secteurs (28 Ko, 6 Ko par fichier)
 * ATTENTION : 1024 Ko + 1 secteur = 1028 Ko. Les fichiers py ne doivent PAS
 * démarrer avant 1028 Ko sinon un effacement des réglages détruit leur en-tête.
 * Aucune des zones ci-dessus ne doit se chevaucher. */
#define PY_FLASH_OFFSET (1044u * 1024u)
#define PY_STORE_MAGIC 0x50595436u /* 'PYT6' : format a 6 Ko par fichier */
/* Ancien format (avant 6 Ko) : relu au demarrage pour garder les programmes. */
#define PY_OLD_FLASH_OFFSET (1028u * 1024u)
#define PY_OLD_FLASH_SECTORS 3
#define PY_OLD_STORE_MAGIC 0x50595420u /* 'PYT ' */
#define PY_OLD_CODE_CAP 2048

typedef struct
{
    uint32_t magic;
    uint32_t checksum;
    int32_t nfiles;
} py_store_hdr_t;

typedef struct
{
    char name[PY_FILE_NAME_MAX];
    int32_t len;
    char code[PY_CODE_CAP];
} py_store_file_t;

#define PY_STORE_SIZE (sizeof(py_store_hdr_t) + PY_MAX_FILES * sizeof(py_store_file_t))
#define PY_FLASH_SECTORS ((PY_STORE_SIZE + 4095u) / 4096u)

typedef struct
{
    char name[PY_FILE_NAME_MAX];
    int32_t len;
    char code[PY_OLD_CODE_CAP];
} py_old_store_file_t;
#define PY_OLD_STORE_SIZE (sizeof(py_store_hdr_t) + PY_MAX_FILES * sizeof(py_old_store_file_t))

#ifndef OPENCALC_WASM
/* flash_range_program() exige une taille multiple de FLASH_PAGE_SIZE (256 o),
 * sinon en-tête/données ne sont pas forcément écrites sur la flash. */
#define PY_STORE_BYTES (((PY_STORE_SIZE) + (FLASH_PAGE_SIZE - 1u)) & ~(FLASH_PAGE_SIZE - 1u))
#endif

static uint32_t py_store_checksum(const uint8_t *buf, int n)
{
    uint32_t sum = 0;
    for (int i = 0; i < n; i++)
        sum += buf[i];
    return sum;
}

static void python_files_save_impl(void)
{
    if (py_codes == NULL)
        return; /* app fermee : rien en RAM a enregistrer */
#ifndef OPENCALC_WASM
    /* Tampon pris sur le tas le temps de l'ecriture (24 Ko) : il ne reste
     * pas en RAM statique. */
    uint8_t *buf = (uint8_t *)malloc(PY_STORE_BYTES);
    if (buf == NULL)
        return;
    memset(buf, 0xFF, PY_STORE_BYTES);

    py_store_hdr_t *hdr = (py_store_hdr_t *)buf;
    hdr->magic = PY_STORE_MAGIC;
    hdr->nfiles = py_nfiles;
    hdr->checksum = 0;

    for (int f = 0; f < PY_MAX_FILES; f++)
    {
        py_store_file_t *sf = (py_store_file_t *)(buf + sizeof(py_store_hdr_t) + f * sizeof(py_store_file_t));
        memset(sf, 0, sizeof(*sf));
        strncpy(sf->name, py_names[f], PY_FILE_NAME_MAX - 1);
        sf->len = py_lens[f];
        if (py_lens[f] > 0)
            memcpy(sf->code, py_codes[f], py_lens[f]);
    }

    hdr->checksum = py_store_checksum(buf, PY_STORE_SIZE);

    /* Rien n'a change depuis la derniere sauvegarde : pas d'ecriture (chaque
     * execution reecrivait la flash et l'usait pour rien). */
    if (memcmp(buf, (const void *)(XIP_BASE + PY_FLASH_OFFSET), PY_STORE_BYTES) != 0)
    {
        uint32_t ints = save_and_disable_interrupts();
        core1_flash_begin();
        flash_range_erase(PY_FLASH_OFFSET, PY_FLASH_SECTORS * FLASH_SECTOR_SIZE);
        flash_range_program(PY_FLASH_OFFSET, buf, PY_STORE_BYTES);
        core1_flash_end();
        restore_interrupts(ints);
    }
    free(buf);
#endif
}

void python_files_save(void)
{
    python_files_save_impl();
}

void python_files_load(void)
{
#ifndef OPENCALC_WASM
    const uint8_t *buf = (const uint8_t *)(XIP_BASE + PY_FLASH_OFFSET);
    const py_store_hdr_t *hdr = (const py_store_hdr_t *)buf;
    size_t file_size = sizeof(py_store_file_t);
    int cap = PY_CODE_CAP;
    if (hdr->magic != PY_STORE_MAGIC || hdr->checksum != py_store_checksum(buf, PY_STORE_SIZE))
    {
        /* Ancienne zone (2 Ko par fichier) : reprise des programmes. */
        buf = (const uint8_t *)(XIP_BASE + PY_OLD_FLASH_OFFSET);
        hdr = (const py_store_hdr_t *)buf;
        if (hdr->magic != PY_OLD_STORE_MAGIC || hdr->checksum != py_store_checksum(buf, PY_OLD_STORE_SIZE))
            return;
        file_size = sizeof(py_old_store_file_t);
        cap = PY_OLD_CODE_CAP;
    }
    if (hdr->nfiles < 1 || hdr->nfiles > PY_MAX_FILES)
        return;

    py_nfiles = (int)hdr->nfiles;
    for (int f = 0; f < PY_MAX_FILES; f++)
    {
        const uint8_t *rec = buf + sizeof(py_store_hdr_t) + f * file_size;
        const char *rec_name = (const char *)rec;
        int32_t rec_len;
        memcpy(&rec_len, rec + PY_FILE_NAME_MAX, sizeof(rec_len));
        const char *rec_code = (const char *)(rec + PY_FILE_NAME_MAX + sizeof(int32_t));
        int len = (int)rec_len;
        if (len < 0 || len > cap - 1)
            len = 0;
        strncpy(py_names[f], rec_name, PY_FILE_NAME_MAX - 1);
        py_names[f][PY_FILE_NAME_MAX - 1] = '\0';
        py_lens[f] = len;
        py_curs[f] = 0;
        py_tops[f] = 0;
        if (len > 0)
            memcpy(py_codes[f], rec_code, len);
        py_codes[f][len] = '\0';
    }
#endif
}

void python_files_erase(void)
{
#ifndef OPENCALC_WASM
    uint32_t ints = save_and_disable_interrupts();
    core1_flash_begin();
    flash_range_erase(PY_FLASH_OFFSET, PY_FLASH_SECTORS * FLASH_SECTOR_SIZE);
    /* l'ancienne zone aussi : sinon ses programmes reviendraient */
    flash_range_erase(PY_OLD_FLASH_OFFSET, PY_OLD_FLASH_SECTORS * FLASH_SECTOR_SIZE);
    core1_flash_end();
    restore_interrupts(ints);
#endif

    /* Réinitialise l'état en RAM à un unique fichier vide. */
    py_nfiles = 1;
    py_cur_file = 0;
    py_sel_file = 0;
    for (int f = 0; f < PY_MAX_FILES; f++)
    {
        py_lens[f] = 0;
        py_curs[f] = 0;
        py_tops[f] = 0;
        if (py_codes != NULL) /* appele au demarrage, app fermee */
            py_codes[f][0] = '\0';
        snprintf(py_names[f], PY_FILE_NAME_MAX, "prog%d.py", f + 1);
    }
}

static int py_line_start(int li)
{
    int pos = 0;
    for (int l = 0; l < li && pos < py_code_len; l++)
    {
        while (pos < py_code_len && py_code[pos] != '\n')
            pos++;
        if (pos < py_code_len)
            pos++;
    }
    return pos;
}

static int py_line_end(int li)
{
    int pos = py_line_start(li);
    while (pos < py_code_len && py_code[pos] != '\n')
        pos++;
    return pos;
}

static int py_n_lines(void)
{
    int n = 1;
    for (int i = 0; i < py_code_len; i++)
        if (py_code[i] == '\n')
            n++;
    return n;
}

static void py_cursor_lc(int *line, int *col)
{
    *line = 0;
    *col = 0;
    for (int i = 0; i < py_cur && i < py_code_len; i++)
    {
        if (py_code[i] == '\n')
        {
            (*line)++;
            *col = 0;
        }
        else
        {
            (*col)++;
        }
    }
}

static void py_cursor_from_lc(int line, int col)
{
    int li = 0;
    int pos = 0;
    while (li < line && pos < py_code_len)
    {
        if (py_code[pos] == '\n')
            li++;
        pos++;
    }
    int c = 0;
    while (pos < py_code_len && py_code[pos] != '\n' && c < col)
    {
        pos++;
        c++;
    }
    py_cur = pos;
}

static void py_insert_text(const char *s)
{
    int n = (int)strlen(s);
    if (py_code_len + n >= PY_CODE_CAP)
        return;
    if (py_cur < py_code_len)
        memmove(py_code + py_cur + n, py_code + py_cur, py_code_len - py_cur);
    memcpy(py_code + py_cur, s, n);
    py_code_len += n;
    py_cur += n;
    py_code[py_code_len] = '\0';
}

static void py_newline(void)
{
    int line, col;
    py_cursor_lc(&line, &col);
    int ls = py_line_start(line);
    int indent = 0;
    while (ls + indent < py_cur && py_code[ls + indent] == ' ')
        indent++;
    /* Apres "for ...:", "if ...:", "def ...:" : un niveau de plus. */
    int last = py_cur - 1;
    while (last >= ls && py_code[last] == ' ')
        last--;
    if (last >= ls && py_code[last] == ':')
        indent += 4;
    py_insert_text("\n");
    char spaces[64];
    if (indent > 63)
        indent = 63;
    for (int i = 0; i < indent; i++)
        spaces[i] = ' ';
    spaces[indent] = '\0';
    py_insert_text(spaces);
}

static void py_backspace(void)
{
    if (py_cur > 0)
    {
        memmove(py_code + py_cur - 1, py_code + py_cur, py_code_len - py_cur);
        py_code_len--;
        py_cur--;
        py_code[py_code_len] = '\0';
    }
}

static void py_file_autoname(int f)
{
    for (int n = 1;; n++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "prog%d.py", n);
        bool used = false;
        for (int i = 0; i < py_nfiles; i++)
            if (strcmp(py_names[i], buf) == 0)
                used = true;
        if (!used)
        {
            strncpy(py_names[f], buf, PY_FILE_NAME_MAX - 1);
            py_names[f][PY_FILE_NAME_MAX - 1] = '\0';
            return;
        }
    }
}

static void py_file_new(void)
{
    if (py_nfiles >= PY_MAX_FILES)
        return;
    int f = py_nfiles;
    py_lens[f] = 0;
    py_curs[f] = 0;
    py_tops[f] = 0;
    py_codes[f][0] = '\0';
    py_file_autoname(f);
    py_nfiles++;
    py_cur_file = f;
    py_sel_file = f;
    py_view = PY_VIEW_EDIT;
}

static void py_file_delete(void)
{
    if (py_nfiles <= 1)
        return;
    int f = py_sel_file;
    for (int i = f; i < py_nfiles - 1; i++)
    {
        memcpy(py_codes[i], py_codes[i + 1], PY_CODE_CAP);
        py_lens[i] = py_lens[i + 1];
        py_curs[i] = py_curs[i + 1];
        py_tops[i] = py_tops[i + 1];
        strncpy(py_names[i], py_names[i + 1], PY_FILE_NAME_MAX - 1);
        py_names[i][PY_FILE_NAME_MAX - 1] = '\0';
    }
    py_nfiles--;
    if (py_cur_file > f)
        py_cur_file--;
    if (py_cur_file >= py_nfiles)
        py_cur_file = py_nfiles - 1;
    if (py_sel_file >= py_nfiles)
        py_sel_file = py_nfiles - 1;
}

static int py_file_nlines(int f)
{
    int n = 1;
    for (int i = 0; i < py_lens[f]; i++)
        if (py_codes[f][i] == '\n')
            n++;
    return n;
}

static int py_files_key(int key)
{
    if (py_snd)
    {
        switch (key)
        {
        case SECOND:
            py_snd = false;
            return 0;
        case OK:
        case ENTER:
            py_snd = false;
            py_file_new();
            python_files_save();
            return 0;
        case BACK:
            py_snd = false;
            py_file_delete();
            python_files_save();
            return 0;
        default:
            py_snd = false;
            return 0;
        }
    }
    switch (key)
    {
    case SECOND:
        py_snd = true;
        break;
    case UP:
        if (py_sel_file > 0)
            py_sel_file--;
        break;
    case DOWN:
        if (py_sel_file < py_nfiles - 1)
            py_sel_file++;
        break;
    case OK:
    case ENTER:
        py_cur_file = py_sel_file;
        py_view = PY_VIEW_EDIT;
        break;
    case LEFT:
        py_view = PY_VIEW_EDIT;
        break;
    case BACK:
        return 1;
    default:
        break;
    }
    return 0;
}

/* Sortie trop longue : comme une console, on garde la FIN et on le signale
 * par une premiere ligne "[...]" (avant : tout ce qui depassait 4 Ko etait
 * perdu sans le dire, et c'etait justement la fin qui manquait). */
static const char k_py_cut[] = "[...]\n";
#define PY_CUT_LEN ((int)sizeof(k_py_cut) - 1)

static bool py_out_has_cut(void)
{
    return py_out_len >= PY_CUT_LEN && memcmp(py_out, k_py_cut, PY_CUT_LEN) == 0;
}

/* Retire les plus anciennes lignes jusqu'a laisser "need" octets libres. */
static void py_out_make_room(int need)
{
    if (py_out_len + need <= PY_OUT_CAP - 1)
        return;
    /* Nouvelle longueur : marqueur + (py_out_len - cut) + need. */
    int cut = (py_out_has_cut() ? PY_CUT_LEN : 0) + PY_OUT_CAP / 4;
    while (PY_CUT_LEN + py_out_len - cut + need > PY_OUT_CAP - 1)
        cut += PY_OUT_CAP / 4;
    if (cut > py_out_len)
        cut = py_out_len;
    while (cut < py_out_len && py_out[cut - 1] != '\n')
        cut++;
    int rest = py_out_len - cut;
    memmove(py_out + PY_CUT_LEN, py_out + cut, (size_t)rest);
    memcpy(py_out, k_py_cut, PY_CUT_LEN);
    py_out_len = PY_CUT_LEN + rest;
    py_out[py_out_len] = '\0';
}

static void py_append_out_n(const char *s, int n)
{
    int maxn = PY_OUT_CAP - 1 - 2 * PY_CUT_LEN;
    if (n > maxn)
    {
        s += n - maxn; /* morceau geant : sa fin seulement */
        n = maxn;
    }
    py_out_make_room(n);
    for (int i = 0; i < n; i++)
    {
        if (s[i] == '\r' || s[i] == '\0')
            continue;
        py_out[py_out_len++] = s[i];
    }
    py_out[py_out_len] = '\0';
}

static void py_append_out(const char *s)
{
    py_append_out_n(s, (int)strlen(s));
}

static void py_index_out(void)
{
    /* Au-dela de PY_OUT_MAX_LINES lignes, les plus anciennes disparaissent
     * (meme regle que pour la place). */
    int count = 0;
    for (int i = 0; i < py_out_len; i++)
        if (py_out[i] == '\n')
            count++;
    if (count >= PY_OUT_MAX_LINES)
    {
        int drop = count - (PY_OUT_MAX_LINES - 2);
        int pos = py_out_has_cut() ? PY_CUT_LEN : 0;
        while (drop > 0 && pos < py_out_len)
        {
            if (py_out[pos] == '\n')
                drop--;
            pos++;
        }
        int rest = py_out_len - pos;
        memmove(py_out + PY_CUT_LEN, py_out + pos, (size_t)rest);
        memcpy(py_out, k_py_cut, PY_CUT_LEN);
        py_out_len = PY_CUT_LEN + rest;
        py_out[py_out_len] = '\0';
    }
    py_out_nlines = 0;
    int pos = 0;
    while (pos < py_out_len && py_out_nlines < PY_OUT_MAX_LINES)
    {
        py_out_lines[py_out_nlines++] = pos;
        while (pos < py_out_len && py_out[pos] != '\n')
            pos++;
        if (pos < py_out_len)
            pos++;
    }
    if (py_out_nlines == 0)
        py_out_lines[py_out_nlines++] = 0;
}

static void py_draw_lineno(int row, int lineno, uint16_t bg)
{
    if (lineno < 0)
        return;
    if (lineno > 9999)
        lineno = 9999;
    int X = SCREEN_HEIGHT - row * PY_CHAR_H - 1;
    char buf[8];
    snprintf(buf, sizeof(buf), "%4d", lineno);
    draw_char(X, 2, buf, COLOR_GRAY, bg, PY_FONT);
}

static void py_draw_title(int row, const char *title)
{
    int len = (int)strlen(title);
    int X = SCREEN_HEIGHT - row * PY_CHAR_H - 1;
    fill_rect(X - (PY_CHAR_H - 1), 0, PY_CHAR_H, SCREEN_WIDTH, PY_EDITOR_BG);
    int tw = len * text_advance(1);
    int y = (SCREEN_WIDTH - tw) / 2;
    char c[2] = {0, 0};
    for (int i = 0; i < len; i++)
    {
        c[0] = title[i];
        draw_char(X, y + i * text_advance(1), c, COLOR_BLACK, PY_EDITOR_BG, 1);
    }
}

static void py_draw_tabs(void)
{
    static const char *names[3] = {"Files", "Edit", "Out"};
    int active = (py_view == PY_VIEW_EDIT) ? 1 : (py_view == PY_VIEW_OUT) ? 2 : 0;
    int tw = 104;
    int gap = 0;
    int x = (SCREEN_WIDTH - (3 * tw + 2 * gap)) / 2;
    for (int i = 0; i < 3; i++)
    {
        uint16_t fill = (i == active) ? PY_SEL_BG : COLOR_WHITE;
        fill_rect(0, x, 24, tw, fill);
        fill_rect(0, x, 1, tw, COLOR_BLACK);
        fill_rect(23, x, 1, tw, COLOR_BLACK);
        fill_rect(0, x, 24, 1, COLOR_BLACK);
        fill_rect(0, x + tw - 1, 24, 1, COLOR_BLACK);
        fill_rect(0, x, 1, 1, BACKGROUND_COLOR);
        fill_rect(0, x + tw - 1, 1, 1, BACKGROUND_COLOR);
        fill_rect(23, x, 1, 1, BACKGROUND_COLOR);
        fill_rect(23, x + tw - 1, 1, 1, BACKGROUND_COLOR);
        int len = (int)strlen(names[i]);
        int ty = x + (tw - len * PY_SMALL_W) / 2;
        char c[2] = {0, 0};
        for (int j = 0; j < len; j++)
        {
            c[0] = names[i][j];
            draw_char(15, ty + j * PY_SMALL_W, c, COLOR_BLACK, fill, 1);
        }
        x += tw + gap;
    }
}

static void py_draw_hint(int row, const char *s)
{
    int len = (int)strlen(s);
    if (len > (SCREEN_WIDTH - 4) / PY_SMALL_W)
        len = (SCREEN_WIDTH - 4) / PY_SMALL_W;
    int X = SCREEN_HEIGHT - row * PY_CHAR_H - 1;
    fill_rect(X - (PY_CHAR_H - 1), 0, PY_CHAR_H, SCREEN_WIDTH, PY_EDITOR_BG);
    int y = SCREEN_WIDTH - 2 - len * PY_SMALL_W;
    char c[2] = {0, 0};
    for (int i = 0; i < len; i++)
    {
        c[0] = s[i];
        draw_char(X, y + i * PY_SMALL_W, c, COLOR_GRAY, PY_EDITOR_BG, 1);
    }
}

static void py_draw_line(int row, const char *s, int len, uint16_t fg, uint16_t bg, int cursor_col, int left_px)
{
    int X = SCREEN_HEIGHT - row * PY_CHAR_H - 1;
    int y = 2 + left_px;
    fill_rect(X - (PY_CHAR_H - 1), 0, PY_CHAR_H, SCREEN_WIDTH, bg);
    if (len > PY_CHARS_PER_LINE)
        len = PY_CHARS_PER_LINE;
    char c[2] = {0, 0};
    for (int i = 0; i < len; i++)
    {
        c[0] = s[i];
        if (c[0] == '\t')
            c[0] = ' ';
        draw_char(X, y + i * PY_CHAR_W, c, fg, bg, PY_FONT);
    }
    if (cursor_col >= 0 && cursor_col <= len && X >= PY_CHAR_H)
    {
        int cy = y + cursor_col * PY_CHAR_W;
        fill_rect(X - (PY_CHAR_H - 1), cy, 2, PY_CHAR_W, COLOR_BLACK);
    }
}

static bool py_cursor_visible(void)
{
    return ((to_ms_since_boot(get_absolute_time()) / 400) % 2) == 0;
}

static char py_second_letter(int key)
{
    switch (key)
    {
    case COS:
        return 'a';
    case SIN:
        return 'b';
    case TAN:
        return 'c';
    case SQRT:
        return 'd';
    case POW:
        return 'e';
    case SEVEN:
        return 'f';
    case EIGHT:
        return 'g';
    case NINE:
        return 'h';
    case OPENING_PARENTHESIS:
        return 'i';
    case CLOSING_PARENTHESIS:
        return 'j';
    case FOUR:
        return 'k';
    case FIVE:
        return 'l';
    case SIX:
        return 'm';
    case MINUS:
        return 'n';
    case DIVIDE:
        return 'o';
    case ONE:
        return 'p';
    case TWO:
        return 'q';
    case THREE:
        return 'r';
    case PLUS:
        return 's';
    case TIMES:
        return 't';
    case ZERO:
        return 'u';
    case COMA:
        return 'v';
    case PI:
        return 'w';
    case X:
        return 'x';
    case LN:
        return 'l';
    case E:
        return 'y';
    case ENTER:
        return 'z';
    case BACK:
        return ' ';
    case TOOLS:
        return ':';
    case PASS4:
        return '\'';
    case NOT2:
        return 'z';
    default:
        return 0;
    }
}

/* ---------- Garde-fous pendant l'execution (voir Core/pika_wrapper.c) ---------- */

extern "C" void pika_platform_printf(char *fmt, ...);
extern "C" void pika_vm_exit(void);

/* Lignes de la sortie a partir de py_out_scroll, sur les rangees
 * [first_row, first_row + nrows) de l'ecran. */
static void py_draw_out_rows(int first_row, int nrows)
{
    for (int r = 0; r < nrows; r++)
    {
        int li = py_out_scroll + r;
        if (li < py_out_nlines)
        {
            int ls = py_out_lines[li];
            int e = ls;
            while (e < py_out_len && py_out[e] != '\n')
                e++;
            py_draw_line(first_row + r, py_out + ls, e - ls, COLOR_BLACK, BACKGROUND_COLOR, -1, 0);
        }
        else
            py_draw_line(first_row + r, " ", 0, COLOR_BLACK, BACKGROUND_COLOR, -1, 0);
    }
}

/* ---------- Sortie au fil de l'eau pendant l'execution ----------
 * Chaque ligne affichee par le script arrive ici (pika_set_output_hook) et
 * l'ecran [Run] montre les dernieres lignes, comme une console. Avant, rien
 * n'apparaissait avant la fin du script. */
#define PY_LIVE_MS 100 /* au plus un rafraichissement par 100 ms */
static uint32_t py_live_ms = 0;
static bool py_live_dirty = false;

static void py_live_draw(void)
{
    py_index_out();
    py_out_scroll = py_out_nlines - PY_BODY_ROWS;
    if (py_out_scroll < 0)
        py_out_scroll = 0;
    py_draw_title(0, "Python [Run]");
    py_draw_hint(2, "BK=ARRETER");
    py_draw_out_rows(PY_BODY_TOP / PY_CHAR_H, PY_BODY_ROWS);
    flush_display();
    py_live_ms = to_ms_since_boot(get_absolute_time());
    py_live_dirty = false;
}

/* Redessine si du texte attend et que le dernier dessin est assez ancien.
 * Rien quand le script dessine (module draw, tortue, matplotlib). */
static void py_live_tick(bool force)
{
    if (!py_live_dirty || pika_draw_used())
        return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (force || now - py_live_ms >= PY_LIVE_MS)
        py_live_draw();
}

static void py_live_hook(const char *s, int n, int newline)
{
    py_append_out_n(s, n);
    if (newline)
        py_append_out_n("\n", 1);
    py_live_dirty = true;
    py_live_tick(false);
}

/* Appelee regulierement par l'interpreteur : BACK (ou HOME) arrete le script. */
extern "C" int oc_py_poll_stop(void)
{
    py_live_tick(false); /* texte en attente pendant un long calcul */
#ifdef OPENCALC_HOST_TEST
    /* tests/test_python.cpp met en file des BACK pour quitter l'appli apres
     * l'execution : ils ne doivent pas interrompre le script. */
    return 0;
#endif
#ifdef OPENCALC_WASM
    static unsigned n = 0;
    if ((++n & 15u) == 0)
        sleep_ms(1); /* laisse le navigateur livrer les touches */
#endif
    int k = scan_keypad_nowait();
    return k == BACK || k == HOME;
}

static char py_input_key_char(int key)
{
    switch (key)
    {
    case ZERO: return '0';
    case ONE: return '1';
    case TWO: return '2';
    case THREE: return '3';
    case FOUR: return '4';
    case FIVE: return '5';
    case SIX: return '6';
    case SEVEN: return '7';
    case EIGHT: return '8';
    case NINE: return '9';
    case COMA: return '.';
    case MINUS: return '-';
    case PLUS: return '+';
    case TIMES: return '*';
    case DIVIDE: return '/';
    case E: return 'e';
    case X: return 'x';
    default: return 0;
    }
}

/* input() : ligne de saisie en bas de l'ecran. OK valide, BACK efface, BACK
 * sur une ligne vide arrete le script. 2ND + touche = lettre. */
extern "C" int oc_py_input_line(const char *prompt, char *buf, int cap)
{
    int len = 0;
    bool snd = false;
    buf[0] = '\0';
    if (!prompt)
        prompt = "";
    /* Ce que le script a deja affiche reste visible au-dessus de la saisie. */
    py_index_out();
    const int out_rows = PY_BODY_ROWS - 2;
    py_out_scroll = py_out_nlines - out_rows;
    if (py_out_scroll < 0)
        py_out_scroll = 0;
    const int body = PY_BODY_TOP / PY_CHAR_H;
    while (true)
    {
        fill_screen(PY_EDITOR_BG);
        py_draw_title(0, "Python [input]");
        py_draw_hint(2, snd ? "2ND: lettre" : "OK=VALIDER BK=EFFACER 2ND=ABC");
        if (py_out_len > 0)
            py_draw_out_rows(body, out_rows);
        int plen = (int)strlen(prompt);
        py_draw_line(body + out_rows, prompt, plen, COLOR_BLACK, PY_EDITOR_BG, -1, 0);
        int first = len > PY_CHARS_PER_LINE - 1 ? len - (PY_CHARS_PER_LINE - 1) : 0;
        py_draw_line(body + out_rows + 1, buf + first, len - first, COLOR_BLACK, PY_SEL_BG, len - first, 0);
        flush_display();

        int ch = scan_text_char();
        int key = -1;
        if (ch < 0)
        {
            key = scan_keypad();
            if (key == -1)
                continue;
        }
        char add = 0;
        if (ch == '\n' || ch == '\r' || key == OK || key == ENTER)
        {
            if (snd && key == ENTER)
                add = 'z';
            else
                break;
        }
        else if (ch == '\b' || (key == BACK && !snd))
        {
            if (len > 0)
                buf[--len] = '\0';
            else
            {
                pika_vm_exit(); /* BACK sur une ligne vide : arret du script */
                pika_platform_printf((char *)"%s\n", prompt);
                return 0;
            }
            continue;
        }
        else if (ch >= 32 && ch <= 126)
            add = (char)ch;
        else if (key == SECOND)
        {
            snd = !snd;
            continue;
        }
        else if (key == HOME)
        {
            pika_vm_exit();
            return 0;
        }
        else if (snd)
            add = py_second_letter(key);
        else
            add = py_input_key_char(key);
        snd = false;
        if (add && len < cap - 1)
        {
            buf[len++] = add;
            buf[len] = '\0';
        }
    }
    /* Echo dans la sortie, comme une console. */
    pika_platform_printf((char *)"%s%s\n", prompt, buf);
    return len;
}

static void py_run_script(void)
{
    py_snd = false;
    py_out_len = 0;
    py_out[0] = '\0';
    /* Enregistre AVANT l'execution : si le script fige ou plante la machine,
     * le programme tape n'est pas perdu. */
    python_files_save();
    /* HOME ne doit pas sortir par longjmp au milieu de l'interpreteur : il
     * est lu par oc_py_poll_stop et arrete le script proprement. */
    bool home_was_armed = g_home_armed;
    g_home_armed = false;
    fill_screen(PY_EDITOR_BG);
    py_draw_title(0, "Python [Run]");
    py_draw_hint(2, "BK=ARRETER");
    flush_display();
    pika_draw_reset();
    /* La sortie arrive ligne par ligne dans py_out (py_live_hook). */
    py_live_ms = to_ms_since_boot(get_absolute_time());
    py_live_dirty = false;
    pika_set_output_hook(py_live_hook);
    pika_run(py_code);
    pika_set_output_hook(NULL);
    g_home_armed = home_was_armed;
    if (pika_draw_used())
    {
        /* Le script a dessine (module draw) : on montre l'ecran jusqu'a la
         * prochaine touche, puis on affiche la sortie texte. */
        flush_display();
        int k = -1;
        while (k == -1)
            k = scan_keypad();
        pika_draw_reset();
    }
    /* Dernieres lignes vides retirees (comme pika_get_output). */
    while (py_out_len > 0 && py_out[py_out_len - 1] == '\n')
        py_out[--py_out_len] = '\0';
    if (py_out_len == 0)
        py_append_out("(no output)");
    py_index_out();
    py_view = PY_VIEW_OUT;
    py_out_scroll = py_out_nlines - PY_BODY_ROWS;
    if (py_out_scroll < 0)
        py_out_scroll = 0;

}

static void py_handle_char(int c)
{
    if (c == '\n' || c == '\r')
    {
        py_newline();
        return;
    }
    if (c == '\b')
    {
        py_backspace();
        return;
    }
    if (c == '\t')
    {
        py_insert_text("    ");
        return;
    }
    if (c >= 32 && c <= 126)
    {
        if (c == '^')
            py_insert_text("**");
        else
        {
            char s[2] = {(char)c, '\0'};
            py_insert_text(s);
        }
    }
}

static void py_handle_key(int key)
{
    if (py_snd)
    {
        switch (key)
        {
        case SECOND:
            py_snd = false;
            return;
        case OK:
        case ENTER:
            py_run_script();
            return;
        case EQUAL:
            py_insert_text("==");
            return;
        case LEFT:
            py_snd = false;
            py_sel_file = py_cur_file;
            py_view = PY_VIEW_FILES;
            return;
        default:
        {
            char l = py_second_letter(key);
            if (l != 0)
            {
                char s[2] = {l, '\0'};
                py_insert_text(s);
            }
            return;
        }
        }
    }

    switch (key)
    {
    case SECOND:
        py_snd = true;
        break;
    case ZERO:
    case ONE:
    case TWO:
    case THREE:
    case FOUR:
    case FIVE:
    case SIX:
    case SEVEN:
    case EIGHT:
    case NINE:
    {
        /* Les codes des touches ne se suivent pas (ZERO = 1, ONE = 7,
         * FOUR = 14...) : '0' + (key - ZERO) ecrivait "678=>?DEF" pour
         * 1..9. Une table par touche. */
        char d = '0';
        switch (key)
        {
        case ONE: d = '1'; break;
        case TWO: d = '2'; break;
        case THREE: d = '3'; break;
        case FOUR: d = '4'; break;
        case FIVE: d = '5'; break;
        case SIX: d = '6'; break;
        case SEVEN: d = '7'; break;
        case EIGHT: d = '8'; break;
        case NINE: d = '9'; break;
        default: break;
        }
        char s[2] = {d, '\0'};
        py_insert_text(s);
    }
    break;
    case PLUS:
        py_insert_text("+");
        break;
    case MINUS:
        py_insert_text("-");
        break;
    case TIMES:
        py_insert_text("*");
        break;
    case DIVIDE:
        py_insert_text("/");
        break;
    case POW:
    case NOT2:
        py_insert_text("**");
        break;
    case OPENING_PARENTHESIS:
        py_insert_text("(");
        break;
    case CLOSING_PARENTHESIS:
        py_insert_text(")");
        break;
    case EQUAL:
        py_insert_text("=");
        break;
    case COMA:
        py_insert_text(",");
        break;
    case X:
        py_insert_text("x");
        break;
    case E:
        py_insert_text("e");
        break;
    case PI:
        py_insert_text("math.pi"); /* "p" seul n'est pas un nom Python */
        break;
    case COS:
        py_insert_text("math.cos(");
        break;
    case SIN:
        py_insert_text("math.sin(");
        break;
    case TAN:
        py_insert_text("math.tan(");
        break;
    case SQRT:
        py_insert_text("math.sqrt(");
        break;
    case LN:
        py_insert_text("math.log(");
        break;
    case FACT:
        py_insert_text("!");
        break;
    case TOOLS:
        py_insert_text("#");
        break;
    case BACK:
        if (py_code_len == 0)
            return;
        py_backspace();
        break;
    case OK:
    case ENTER:
        py_newline();
        break;
    case LEFT:
        if (py_cur > 0)
            py_cur--;
        break;
    case RIGHT:
        if (py_cur < py_code_len)
            py_cur++;
        break;
    case UP:
    {
        int line, col;
        py_cursor_lc(&line, &col);
        if (line > 0)
            py_cursor_from_lc(line - 1, col);
    }
    break;
    case DOWN:
    {
        int line, col;
        py_cursor_lc(&line, &col);
        if (line + 1 < py_n_lines())
            py_cursor_from_lc(line + 1, col);
    }
    break;
    default:
        break;
    }
}

static int python_app_body(void);

void python_app_release(void)
{
    free(py_codes);
    py_codes = NULL;
}

int python_app(void)
{
    if (py_codes == NULL)
    {
        py_codes = (char (*)[PY_CODE_CAP])malloc((size_t)PY_MAX_FILES * PY_CODE_CAP);
        if (py_codes == NULL)
        {
            fill_screen(BACKGROUND_COLOR);
            draw_char(140, 20, (char *)"Memoire insuffisante", COLOR_BLACK, BACKGROUND_COLOR, 2);
            flush_display();
            sleep_ms(1500);
            return 0;
        }
    }
    int r = python_app_body();
    free(py_codes);
    py_codes = NULL;
    return r;
}

static int python_app_body(void)
{
    py_out_len = 0;
    py_out[0] = '\0';
    py_out_scroll = 0;
    py_snd = false;

    py_nfiles = 1;
    py_cur_file = 0;
    py_sel_file = 0;
    py_view = PY_VIEW_FILES;
    for (int f = 0; f < PY_MAX_FILES; f++)
    {
        py_lens[f] = 0;
        py_curs[f] = 0;
        py_tops[f] = 0;
        py_codes[f][0] = '\0';
        snprintf(py_names[f], PY_FILE_NAME_MAX, "prog%d.py", f + 1);
    }

    const char *sample =
        "for i in range(5):\n"
        "    print(i, i * i)\n";
    memcpy(py_codes[0], sample, strlen(sample));
    py_lens[0] = (int)strlen(sample);
    py_curs[0] = py_lens[0];

    /* Restaure les fichiers sauvegardés en flash s'il y en a ; sinon on garde
     * l'état par défaut (fichier d'exemple). */
    python_files_load();

    fill_screen(BACKGROUND_COLOR);

    while (1)
    {
        if (py_view == PY_VIEW_EDIT)
        {
            py_draw_title(0, "Python [Edit]");
            draw_char(SCREEN_HEIGHT - 1, SCREEN_WIDTH - 18, (char *)(py_snd ? "ABC" : "123"),
                      py_snd ? COLOR_RED : COLOR_BLACK, PY_EDITOR_BG, 1);
            py_draw_hint(2, "2ND=ABC 2ND+OK=RUN 2ND+LEFT=FILES BK=DEL");

            int nlines = py_n_lines();
            int cline = 0, ccol = 0;
            py_cursor_lc(&cline, &ccol);
            if (cline < py_top_line)
                py_top_line = cline;
            if (cline >= py_top_line + PY_BODY_ROWS)
                py_top_line = cline - PY_BODY_ROWS + 1;
            if (py_top_line < 0)
                py_top_line = 0;

            for (int r = 0; r < PY_BODY_ROWS; r++)
            {
                int li = py_top_line + r;
                int cursor_col = (li == cline && py_cursor_visible()) ? ccol : -1;
                int row_idx = PY_BODY_TOP / PY_CHAR_H + r;
                if (li < nlines)
                {
                    int ls = py_line_start(li);
                    int le = py_line_end(li);
                    py_draw_line(row_idx, py_code + ls, le - ls,
                                 COLOR_BLACK, PY_EDITOR_BG, cursor_col, PY_GUTTER_W);
                    py_draw_lineno(row_idx, li + 1, PY_EDITOR_BG);
                }
                else
                {
                    py_draw_line(row_idx, " ", 0,
                                 COLOR_BLACK, PY_EDITOR_BG, -1, PY_GUTTER_W);
                }
            }
        }
        else if (py_view == PY_VIEW_FILES)
        {
            py_draw_title(0, "Python [Files]");
            py_draw_hint(2, "OK=OPEN BK=EXIT 2ND+OK=NEW 2ND+BK=DEL");
            for (int r = 0; r < PY_BODY_ROWS; r++)
            {
                int row_idx = PY_BODY_TOP / PY_CHAR_H + r;
                if (r < py_nfiles)
                {
                    uint16_t bg = (r == py_sel_file) ? PY_SEL_BG : COLOR_WHITE;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%-14s %2dL",
                             py_names[r], py_file_nlines(r));
                    py_draw_line(row_idx, buf, (int)strlen(buf),
                                 COLOR_BLACK, bg, -1, 0);
                }
                else
                {
                    py_draw_line(row_idx, " ", 0,
                                 COLOR_GRAY, PY_EDITOR_BG, -1, 0);
                }
            }
        }
        else
        {
            py_draw_title(0, "Python [Out]");
            py_draw_hint(2, "UP/DOWN=SCROLL OK=BACK");

            int max_scroll = py_out_nlines - PY_BODY_ROWS;
            if (max_scroll < 0)
                max_scroll = 0;
            if (py_out_scroll > max_scroll)
                py_out_scroll = max_scroll;
            if (py_out_scroll < 0)
                py_out_scroll = 0;

            py_draw_out_rows(PY_BODY_TOP / PY_CHAR_H, PY_BODY_ROWS);
        }

        py_draw_tabs();

        flush_display();
        if (py_pending_n > 0 && py_view != PY_VIEW_FILES)
        {
            char pc = py_pending[0];
            for (int i = 1; i < py_pending_n; i++)
                py_pending[i - 1] = py_pending[i];
            py_pending_n--;
            if (py_view == PY_VIEW_OUT)
                py_view = PY_VIEW_EDIT;
            py_handle_char(pc);
            continue;
        }

        int ch = scan_text_char();
        if (ch >= 0)
        {
            if (py_view == PY_VIEW_FILES)
            {
                if (py_pending_n < (int)sizeof(py_pending))
                    py_pending[py_pending_n++] = (char)ch;
                continue;
            }
            scan_keypad_nowait();
            if (py_view == PY_VIEW_OUT)
                py_view = PY_VIEW_EDIT;
            py_handle_char(ch);
        }
        else
        {
            int key = scan_keypad();
            if (key >= 0)
            {
                int c2 = scan_text_char();
                if (c2 >= 0)
                {
                    if (py_view == PY_VIEW_FILES)
                        continue;
                    if (py_view == PY_VIEW_OUT)
                        py_view = PY_VIEW_EDIT;
                    py_handle_char(c2);
                    continue;
                }
                if (py_view == PY_VIEW_FILES)
                {
                    if (py_files_key(key))
                    {
                        python_files_save();
                        return 0;
                    }
                    continue;
                }
                if (py_view == PY_VIEW_OUT)
                {
                    if (key == UP)
                    {
                        if (py_out_scroll > 0)
                            py_out_scroll--;
                        continue;
                    }
                    if (key == DOWN)
                    {
                        int max_scroll = py_out_nlines - PY_BODY_ROWS;
                        if (max_scroll < 0)
                            max_scroll = 0;
                        if (py_out_scroll < max_scroll)
                            py_out_scroll++;
                        continue;
                    }
                    if (key == SECOND)
                    {
                        py_snd = true;
                        continue;
                    }
                    if (py_snd && (key == OK || key == ENTER))
                    {
                        py_run_script();
                        continue;
                    }
                    if (py_snd && key == LEFT)
                    {
                        py_snd = false;
                        py_sel_file = py_cur_file;
                        py_view = PY_VIEW_FILES;
                        continue;
                    }
                    py_snd = false;
                    py_view = PY_VIEW_EDIT;
                    if (key == OK || key == ENTER || key == BACK)
                        continue;
                }
                if (key == BACK && py_code_len == 0)
                {
                    if (py_nfiles == 1)
                    {
                        python_files_save();
                        return 0;
                    }
                    py_sel_file = py_cur_file;
                    py_view = PY_VIEW_FILES;
                    continue;
                }
                py_handle_key(key);
            }
        }
        sleep_ms(20);
    }
}
