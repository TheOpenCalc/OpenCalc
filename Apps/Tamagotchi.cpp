#include "ui.h"
#include "Tamagotchi.h"
#include "Tools.h"
#include "ui_kit.h"
#include "colors.h"
#include "display.h"
#include "pico/stdlib.h"
#ifndef OPENCALC_WASM
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/address_mapped.h"
extern void core1_flash_begin(void);
extern void core1_flash_end(void);
#endif
#include <cstdlib>
#include <cstdio>
#include <cstring>

/* Tamagotchi "Calci".
 *
 * Jauges : faim, joie, energie, sante (0..100), plus le poids et les pieces.
 *
 * Onglets (GAUCHE/DROITE ou 2ND) :
 *   - Calcul   : calcul mental. Nourrit et rapporte des pieces ; la difficulte
 *                suit l'age de Calci et la serie de bonnes reponses.
 *   - Jeu      : mini-jeux "Plus ou moins" et "Suite logique" (joie, pieces,
 *                fait maigrir, fatigue).
 *   - Boutique : depenser ses pieces (pomme, gateau, cafe, ballon, chapeau).
 *   - Soins    : laver (jauge de proprete), caresser, soigner.
 *   - Dodo     : eteindre la lumiere pour que Calci recupere de l'energie.
 *
 * Cycle de vie : oeuf -> bebe -> enfant (10 repas) -> adulte (30 repas).
 * La forme adulte depend des "fautes de soin" (faim a zero, maladie, crasse,
 * appel ignore, epuisement) : Genie, Adulte ou Grognon. Si la sante tombe a
 * zero, Calci s'en va et un nouvel oeuf (generation suivante) apparait.
 *
 * L'etat est sauvegarde en flash a la sortie (BACK ou HOME). */

/* ------------------------------ Reglages --------------------------------- */

#define TAMA_TICK_MS 4000          /* un "tick" de vie toutes les 4 s */
#define TAMA_EGG_MS 12000          /* duree de l'oeuf */
#define TAMA_CALL_MS 30000         /* temps pour repondre a un appel */
#define TAMA_PET_COOLDOWN_MS 5000  /* delai entre deux caresses */
#define TAMA_ANSWER_MAX 6
#define TAMA_FRAME_MS 250
#define TAMA_STAGE_CHILD 10
#define TAMA_STAGE_ADULT 30
#define TAMA_GAME_ENERGY 10
#define TAMA_AWAY_MAX_MS (20u * 60u * 1000u)
#define TAMA_TICKS_PER_DAY 75      /* 1 "jour" = 5 min de jeu */

/* Zone flash dediee (voir la carte dans Apps/settings.cpp). */
#define TAMA_FLASH_OFFSET (1040u * 1024u)
#define TAMA_MAGIC 0x54414D33u /* 'TAM3' */

/* -------------------------------- Etat ----------------------------------- */

enum { TAB_CALC = 0, TAB_GAME, TAB_SHOP, TAB_CARE, TAB_SLEEP, TAB_COUNT };
enum { ST_EGG = 0, ST_BABY, ST_CHILD, ST_ADULT };
enum { AD_GENIUS = 0, AD_NORMAL, AD_GRUMPY };
enum { GM_NONE = 0, GM_GUESS, GM_SEQ };

/* Tout ce qui est sauvegarde : uniquement des int32 (checksum simple). */
typedef struct
{
    int32_t hunger, happiness, energy, health, weight, coins;
    int32_t meals, best_streak, clean, sick, sleeping; /* clean : proprete 0..100 */
    int32_t stage, adult_type, mistakes, generation, age_ticks;
    int32_t has_hat, egg_ms_left, dead;
} tama_state_t;

static tama_state_t S;
static bool tama_init = false;
static bool state_dirty = false;

static int streak = 0;
static int tab = TAB_CALC;
static int shop_sel = 0;
static int care_sel = 0;
static int game_sel = 0;

static char answer_buf[TAMA_ANSWER_MAX + 1];
static int answer_len = 0;

/* Calcul */
static int problem_a = 0, problem_b = 0;
static char problem_op = '+';
static int problem_answer = 0;

/* Mini-jeux */
static int game_mode = GM_NONE;
static int guess_secret = 0, guess_max = 50, guess_tries = 0;
static int seq_terms[4];
static int seq_answer = 0;

/* Message temporaire */
static char feedback[44];
static uint16_t feedback_col = COLOR_BLACK;
static uint32_t feedback_until = 0;

/* Appel d'attention ("!" au-dessus de Calci) */
static bool calling = false;
static uint32_t call_deadline = 0;

static uint32_t last_tick_ms = 0;
static uint32_t left_at_ms = 0;
static uint32_t ate_at_ms = 0;
static bool ate_recently = false;
static uint32_t next_blink_ms = 0;
static uint32_t blink_until = 0;
static uint32_t last_pet_ms = 0;
static int anim_frame = 0;

static uint32_t now_ms()
{
    return to_ms_since_boot(get_absolute_time());
}

static int clamp100(int v)
{
    return v < 0 ? 0 : (v > 100 ? 100 : v);
}

static void set_feedback(const char *msg, uint16_t col, uint32_t dur)
{
    snprintf(feedback, sizeof(feedback), "%s", msg);
    feedback_col = col;
    feedback_until = now_ms() + dur;
}

static int ideal_weight()
{
    switch (S.stage)
    {
    case ST_BABY: return 5;
    case ST_CHILD: return 10;
    case ST_ADULT: return 15;
    default: return 5;
    }
}

static bool overweight()
{
    return S.weight > ideal_weight() + 8;
}

static void add_mistake()
{
    S.mistakes++;
    state_dirty = true;
}

/* Salit Calci ; le laisser devenir completement crasseux est une faute. */
static void get_dirty(int n)
{
    if (n <= 0 || S.clean <= 0)
        return;
    S.clean = clamp100(S.clean - n);
    if (S.clean == 0)
    {
        add_mistake();
        set_feedback("Calci est crasseux ! (Soins)", COLOR_MAROON, 2000);
    }
    state_dirty = true;
}

/* ----------------------------- Sauvegarde -------------------------------- */

typedef struct
{
    uint32_t magic;
    tama_state_t st;
    uint32_t checksum;
} tama_store_t;

static uint32_t tama_checksum(const tama_store_t *s)
{
    const uint32_t *w = (const uint32_t *)&s->st;
    uint32_t sum = s->magic;
    for (unsigned i = 0; i < sizeof(tama_state_t) / 4; i++)
        sum = sum * 31u + w[i];
    return sum;
}

static bool tama_load()
{
#ifndef OPENCALC_WASM
    const tama_store_t *s = (const tama_store_t *)(XIP_BASE + TAMA_FLASH_OFFSET);
    if (s->magic != TAMA_MAGIC || s->checksum != tama_checksum(s))
        return false;
    S = s->st;
    S.hunger = clamp100(S.hunger);
    S.happiness = clamp100(S.happiness);
    S.energy = clamp100(S.energy);
    S.health = clamp100(S.health);
    if (S.stage < ST_EGG || S.stage > ST_ADULT)
        S.stage = ST_BABY;
    S.clean = clamp100(S.clean);
    if (S.coins < 0)
        S.coins = 0;
    return true;
#else
    return false;
#endif
}

void tamagotchi_save()
{
    left_at_ms = now_ms();
    if (!tama_init || !state_dirty)
        return;
    state_dirty = false;
#ifndef OPENCALC_WASM
    tama_store_t s;
    memset(&s, 0xFF, sizeof(s));
    s.magic = TAMA_MAGIC;
    s.st = S;
    s.checksum = tama_checksum(&s);

    /* flash_range_program() veut un multiple de FLASH_PAGE_SIZE (256 o). */
    uint8_t buf[(sizeof(s) + (FLASH_PAGE_SIZE - 1u)) & ~(FLASH_PAGE_SIZE - 1u)];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &s, sizeof(s));

    uint32_t ints = save_and_disable_interrupts();
    core1_flash_begin();
    flash_range_erase(TAMA_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(TAMA_FLASH_OFFSET, buf, sizeof(buf));
    core1_flash_end();
    restore_interrupts(ints);
#endif
}

static void new_game()
{
    int gen = S.generation + 1;
    int best = S.best_streak;
    memset(&S, 0, sizeof(S));
    S.hunger = 70;
    S.happiness = 60;
    S.energy = 80;
    S.health = 100;
    S.clean = 100;
    S.weight = 5;
    S.coins = 5;
    S.stage = ST_EGG;
    S.adult_type = AD_NORMAL;
    S.egg_ms_left = TAMA_EGG_MS;
    S.generation = gen;
    S.best_streak = best;
    streak = 0;
    calling = false;
    game_mode = GM_NONE;
    state_dirty = true;
}

/* ------------------------------ Calculs ---------------------------------- */

static void clear_answer()
{
    answer_len = 0;
    answer_buf[0] = '\0';
}

/* Niveau 0..3 : base selon l'age, +1 toutes les 4 bonnes reponses. */
static int difficulty()
{
    int base = S.stage - 1;
    if (base < 0)
        base = 0;
    if (S.stage == ST_ADULT && S.adult_type == AD_GENIUS)
        base++;
    int d = base + streak / 4;
    return d > 3 ? 3 : d;
}

static void generate_problem()
{
    int a, b;
    char op;

    switch (difficulty())
    {
    case 0:
        op = (rand() % 2 == 0) ? '+' : '-';
        a = 1 + rand() % 9;
        b = 1 + rand() % 9;
        break;
    case 1:
    {
        const char ops[] = {'+', '-', 'x'};
        op = ops[rand() % 3];
        if (op == 'x') { a = 2 + rand() % 8; b = 2 + rand() % 8; }
        else { a = 5 + rand() % 20; b = 2 + rand() % 15; }
        break;
    }
    case 2:
    {
        const char ops[] = {'+', '-', 'x', '/'};
        op = ops[rand() % 4];
        if (op == 'x') { a = 3 + rand() % 10; b = 2 + rand() % 9; }
        else if (op == '/') { b = 2 + rand() % 9; a = b * (2 + rand() % 9); }
        else { a = 10 + rand() % 50; b = 5 + rand() % 40; }
        break;
    }
    default:
    {
        const char ops[] = {'+', '-', 'x', '/'};
        op = ops[rand() % 4];
        if (op == 'x') { a = 6 + rand() % 14; b = 3 + rand() % 10; }
        else if (op == '/') { b = 3 + rand() % 12; a = b * (3 + rand() % 13); }
        else { a = 25 + rand() % 75; b = 10 + rand() % 60; }
        break;
    }
    }
    if (op == '-' && b > a)
    {
        int t = a;
        a = b;
        b = t;
    }

    problem_a = a;
    problem_b = b;
    problem_op = op;
    switch (op)
    {
    case '+': problem_answer = a + b; break;
    case '-': problem_answer = a - b; break;
    case 'x': problem_answer = a * b; break;
    default: problem_answer = a / b; break;
    }
    clear_answer();
}

/* Suite logique : 4 termes, trouver le 5e. */
static void generate_sequence()
{
    int t[5];
    int kind = rand() % (S.stage >= ST_CHILD ? 4 : 2);
    switch (kind)
    {
    case 0: /* arithmetique */
    {
        int a = 1 + rand() % 20, d = 2 + rand() % 8;
        if (rand() % 3 == 0)
            d = -d;
        for (int i = 0; i < 5; i++)
            t[i] = a + i * d;
        break;
    }
    case 1: /* geometrique */
    {
        int a = 1 + rand() % 5, r = 2 + rand() % 2;
        t[0] = a;
        for (int i = 1; i < 5; i++)
            t[i] = t[i - 1] * r;
        break;
    }
    case 2: /* deux pas alternes */
    {
        int a = 1 + rand() % 10, p = 1 + rand() % 6, q = 2 + rand() % 8;
        t[0] = a;
        for (int i = 1; i < 5; i++)
            t[i] = t[i - 1] + ((i % 2) ? p : q);
        break;
    }
    default: /* carres */
    {
        int k = 1 + rand() % 6;
        for (int i = 0; i < 5; i++)
            t[i] = (i + k) * (i + k);
        break;
    }
    }
    for (int i = 0; i < 4; i++)
        seq_terms[i] = t[i];
    seq_answer = t[4];
    clear_answer();
}

/* ------------------------------ Evolution -------------------------------- */

static void evolve_if_needed()
{
    if (S.stage == ST_BABY && S.meals >= TAMA_STAGE_CHILD)
    {
        S.stage = ST_CHILD;
        S.weight += 4;
        set_feedback("Calci est devenu un enfant !", COLOR_PURPLE, 2500);
    }
    else if (S.stage == ST_CHILD && S.meals >= TAMA_STAGE_ADULT)
    {
        S.stage = ST_ADULT;
        S.weight += 4;
        if (S.mistakes <= 2)
        {
            S.adult_type = AD_GENIUS;
            set_feedback("Calci devient un Genie !", COLOR_PURPLE, 3000);
        }
        else if (S.mistakes <= 6)
        {
            S.adult_type = AD_NORMAL;
            set_feedback("Calci est adulte !", COLOR_PURPLE, 3000);
        }
        else
        {
            S.adult_type = AD_GRUMPY;
            set_feedback("Calci devient Grognon... (fautes)", COLOR_MAROON, 3000);
        }
    }
    state_dirty = true;
}

/* Un tick de vie. away = temps passe hors de l'appli : Calci ne peut pas
 * mourir ni appeler pendant ce temps. */
static void life_tick(bool away)
{
    if (S.dead || S.stage == ST_EGG)
        return;

    S.age_ticks++;
    int age = S.age_ticks;
    int prev_hunger = S.hunger;

    if (S.sleeping)
    {
        S.energy = clamp100(S.energy + 4);
        if (age % 2 == 0)
            S.hunger = clamp100(S.hunger - 1);
        if (S.energy >= 100)
        {
            S.sleeping = 0;
            set_feedback("Calci se reveille en forme !", COLOR_BLUE, 2000);
        }
    }
    else
    {
        S.hunger = clamp100(S.hunger - 1);
        if (age % 3 == 0)
            S.energy = clamp100(S.energy - 1);
        if (age % 15 == 0 && S.weight > ideal_weight() - 4)
            S.weight--;
        if (age % 2 == 0)
            get_dirty(1);
    }

    if (age % 2 == 0)
        S.happiness = clamp100(S.happiness - (1 + (S.clean < 30) + (S.clean == 0) + (S.hunger < 25) + S.sick));

    if (prev_hunger > 0 && S.hunger == 0)
    {
        add_mistake();
        set_feedback("Calci meurt de faim !", COLOR_RED, 2000);
    }

    /* Sante */
    int dmg = 0;
    if (S.hunger == 0) dmg += 2;
    if (S.sick) dmg += 1;
    if (overweight() && age % 3 == 0) dmg += 1;
    if (dmg == 0 && S.hunger >= 40 && S.clean >= 50 && age % 2 == 0)
        S.health = clamp100(S.health + 1);
    S.health = clamp100(S.health - dmg);
    if (away && S.health < 10)
        S.health = 10;

    /* Maladie : la salete, une sante faible et le surpoids l'attirent. */
    if (!S.sick)
    {
        int risk = (S.clean < 30 ? 25 : 0) + (S.clean == 0 ? 25 : 0) + (S.health < 30 ? 50 : 0) + (overweight() ? 12 : 0);
        if (risk > 0 && rand() % 1000 < risk)
        {
            S.sick = 1;
            add_mistake();
            set_feedback("Calci est malade ! (Soins)", COLOR_RED, 2500);
        }
    }

    /* Epuisement */
    if (!S.sleeping && S.energy == 0)
    {
        S.sleeping = 1;
        S.happiness = clamp100(S.happiness - 10);
        calling = false;
        add_mistake();
        set_feedback("Calci s'effondre de fatigue...", COLOR_MAROON, 2500);
    }

    /* Appel d'attention aleatoire (~ toutes les 100 s en moyenne) */
    if (!away && !calling && !S.sleeping && rand() % 25 == 0)
    {
        calling = true;
        call_deadline = now_ms() + TAMA_CALL_MS;
        set_feedback("Calci t'appelle ! Joue ou caresse-le", COLOR_BLUE, 2500);
    }

    if (S.health <= 0)
    {
        S.dead = 1;
        S.sleeping = 0;
        calling = false;
        game_mode = GM_NONE;
    }
    state_dirty = true;
}

/* Temps passe hors de l'appli (depuis l'allumage seulement : pas d'horloge
 * sauvegardee). Il compte a moitie et au plus 20 min. */
static void apply_absence()
{
    if (left_at_ms == 0 || S.dead)
        return;
    uint32_t away = now_ms() - left_at_ms;
    if (away > TAMA_AWAY_MAX_MS)
        away = TAMA_AWAY_MAX_MS;
    if (S.stage == ST_EGG)
    {
        S.egg_ms_left -= (int32_t)away;
        return;
    }
    int ticks = (int)(away / (TAMA_TICK_MS * 2));
    for (int i = 0; i < ticks; i++)
        life_tick(true);
}

static void satisfy_call()
{
    if (calling)
    {
        calling = false;
        S.happiness = clamp100(S.happiness + 5);
    }
}

static void ate(int hunger_gain, int joy, int weight)
{
    S.hunger = clamp100(S.hunger + hunger_gain);
    S.happiness = clamp100(S.happiness + joy);
    S.weight += weight;
    S.meals++;
    ate_recently = true;
    ate_at_ms = now_ms();
    get_dirty(2); /* on se tache en mangeant */
    evolve_if_needed();
    state_dirty = true;
}

/* Renvoie true (et affiche pourquoi) si Calci ne peut pas agir. */
static bool blocked(bool allow_sick)
{
    if (S.stage == ST_EGG)
    {
        set_feedback("L'oeuf va bientot eclore...", COLOR_GRAY, 1200);
        return true;
    }
    if (S.sleeping)
    {
        set_feedback("Chut... Calci dort.", COLOR_BLUE, 1200);
        return true;
    }
    if (!allow_sick && S.sick)
    {
        set_feedback("Calci est malade, soigne-le !", COLOR_RED, 1500);
        return true;
    }
    return false;
}

/* -------------------------------- Dessin --------------------------------- */

#define PET_BOTTOM 166 /* ligne du sol */
#define PET_CX 110     /* centre horizontal de Calci */

static uint16_t bar_color(int v)
{
    if (v < 20) return COLOR_RED;
    if (v < 50) return 0xFD20;
    if (v < 80) return COLOR_YELLOW;
    return COLOR_LIME;
}

static void draw_bar(int X, int Y, const char *label, int value)
{
    const int W = 74, H = 7;
    uk_text(X, Y, label, COLOR_BLACK, 1);
    int bx = X + 40;
    uk_rect(bx - 1, Y - 1, W + 2, H + 2, COLOR_BLACK);
    uk_rect(bx, Y, W, H, COLOR_WHITE);
    uk_rect(bx, Y, W * value / 100, H, bar_color(value));
}

static void draw_heart(int X, int Y, int s, uint16_t col)
{
    static const char *h[4] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 5; c++)
            if (h[r][c] == 'X')
                uk_rect(X + c * s, Y + r * s, s, s, col);
}

static void draw_egg()
{
    /* oeuf qui tremble de plus en plus avant d'eclore */
    static const char *egg[12] = {
        "....XX....", "...XXXX...", "..XXXXXX..", ".XXXXXXXX.",
        ".XXXXXXXX.", "XXXXXXXXXX", "XXXXXXXXXX", "XXXXXXXXXX",
        "XXXXXXXXXX", ".XXXXXXXX.", "..XXXXXX..", "...XXXX...",
    };
    const int b = 7;
    int shake = 0;
    if (S.egg_ms_left < TAMA_EGG_MS / 2)
        shake = (anim_frame % 2) ? 2 : -2;
    if (S.egg_ms_left < TAMA_EGG_MS / 5)
        shake *= 2;
    int X0 = PET_CX - 5 * b + shake;
    int Y0 = PET_BOTTOM - 12 * b;
    uk_rect(X0 + b, PET_BOTTOM + 1, 8 * b, 3, 0xBDF7);
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 10; c++)
            if (egg[r][c] == 'X')
            {
                uint16_t col = 0xFFDB;
                bool border = c == 0 || c == 9 || egg[r][c - 1] != 'X' || egg[r][c + 1] != 'X' ||
                              r == 0 || r == 11 || egg[r - 1][c] != 'X' || egg[r + 1][c] != 'X';
                if (border)
                    col = 0xC5B4;
                else if (r == 5 || r == 8)
                    col = 0x7E7F; /* rayures bleues */
                uk_rect(X0 + c * b, Y0 + r * b, b, b, col);
            }
    if (S.egg_ms_left < TAMA_EGG_MS / 3)
    {
        /* fissure */
        uk_line(X0 + 2 * b, Y0 + 5 * b, X0 + 4 * b, Y0 + 6 * b, COLOR_BLACK);
        uk_line(X0 + 4 * b, Y0 + 6 * b, X0 + 5 * b, Y0 + 5 * b, COLOR_BLACK);
        uk_line(X0 + 5 * b, Y0 + 5 * b, X0 + 7 * b, Y0 + 6 * b, COLOR_BLACK);
    }
}

/* Calci : sprite 10x10 symetrique (demi-ligne de 5 cases), ligne 0 en haut. */
static void draw_calci()
{
    static const char half[10][6] = {
        "...12", "..122", ".1222", "12222", "12222",
        "12222", "12222", ".1222", "..122", "...11",
    };

    const int st = S.stage;
    const int block = st == ST_BABY ? 6 : (st == ST_CHILD ? 8 : 9);
    const int size = block * 10;
    bool sleeping = S.sleeping;
    uint32_t t = now_ms();
    bool happy = ate_recently || (S.happiness >= 70 && !S.sick);
    bool sad = !sleeping && (S.hunger < 25 || S.happiness < 25 || S.sick);
    bool blink = !sleeping && t < blink_until;
    bool grumpy = st == ST_ADULT && S.adult_type == AD_GRUMPY;

    int bounce = 0;
    if (!sleeping)
    {
        int amp = happy ? 6 : (sad ? 1 : 3);
        bounce = (anim_frame % 2) ? -amp : 0;
    }
    static const int sway[8] = {0, 4, 8, 4, 0, -4, -8, -4};
    int dx = (sleeping || S.sick) ? 0 : sway[(anim_frame / 2) % 8];
    /* un peu plus large quand il est trop gros */
    int fat = overweight() ? block / 2 : 0;

    int X0 = PET_CX - size / 2 + dx;
    int Y0 = PET_BOTTOM - size + bounce;

    uint16_t body, edge;
    if (st == ST_BABY) { body = 0xFE19; edge = 0xA145; }
    else if (st == ST_CHILD) { body = 0x9F53; edge = 0x4A87; }
    else if (S.adult_type == AD_GENIUS) { body = 0x5D7F; edge = 0x18AF; }
    else if (S.adult_type == AD_NORMAL) { body = 0xFE60; edge = 0xA3A0; }
    else { body = 0xB4D7; edge = 0x5110; }
    if (S.sick) { body = 0xAEF2; edge = 0x5AA8; } /* teint verdatre */
    else if (sad) { body = 0xC618; edge = 0x630C; }

    uk_rect(X0 + block, PET_BOTTOM + 1, size - 2 * block, 3, 0xBDF7);

    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 10; c++)
        {
            char px = half[r][c < 5 ? c : 9 - c];
            if (px == '.')
                continue;
            int x = X0 + c * block + (c < 5 ? -fat : fat);
            uk_rect(x, Y0 + r * block, block - 1, block - 1, px == '2' ? body : edge);
        }
    if (fat)
        uk_rect(X0 + block, Y0 + 3 * block, size - 2 * block, 4 * block, body);

    /* Taches de salete (de plus en plus nombreuses) */
    if (S.clean < 60)
    {
        static const int spots[6][2] = {{3, 8}, {7, 2}, {2, 5}, {6, 8}, {8, 4}, {4, 2}};
        int n = S.clean < 15 ? 6 : (S.clean < 30 ? 4 : 2);
        for (int i = 0; i < n; i++)
            uk_rect(X0 + spots[i][0] * block + 1, Y0 + spots[i][1] * block + 1, block - 2,
                    block / 2 + 1, 0x8A22);
    }

    /* Yeux */
    int ey = Y0 + 3 * block;
    for (int side = 0; side < 2; side++)
    {
        int ex = X0 + (side ? 6 : 2) * block;
        if (sleeping || blink)
            uk_rect(ex, ey + block - 3, 2 * block - 1, 2, COLOR_BLACK);
        else if (S.sick)
        {
            /* yeux en spirale -> "x x" */
            uk_line(ex + 2, ey, ex + block + 4, ey + block + 2, COLOR_BLACK);
            uk_line(ex + block + 4, ey, ex + 2, ey + block + 2, COLOR_BLACK);
        }
        else if (happy && ate_recently)
        {
            uk_line(ex, ey + block, ex + block, ey + 2, COLOR_BLACK);
            uk_line(ex + block, ey + 2, ex + 2 * block - 2, ey + block, COLOR_BLACK);
        }
        else
        {
            uk_rect(ex + block / 2, ey, block, block + 2, COLOR_BLACK);
            uk_rect(ex + block / 2 + 1, ey + 1, 2, 2, COLOR_WHITE);
        }
        /* sourcils fronces du Grognon */
        if (grumpy && !sleeping)
        {
            if (side == 0)
                uk_line(ex + 1, ey - 5, ex + 2 * block - 2, ey - 1, COLOR_BLACK);
            else
                uk_line(ex + 1, ey - 1, ex + 2 * block - 2, ey - 5, COLOR_BLACK);
        }
    }

    /* Lunettes du Genie */
    if (st == ST_ADULT && S.adult_type == AD_GENIUS && !sleeping)
    {
        for (int side = 0; side < 2; side++)
            uk_frame(X0 + (side ? 6 : 2) * block, ey - 3, 2 * block, block + 8, COLOR_BLACK);
        uk_rect(X0 + 4 * block, ey + 1, 2 * block, 2, COLOR_BLACK);
    }

    /* Bouche */
    int my = Y0 + 6 * block;
    int mx = X0 + 4 * block;
    if (sleeping)
        uk_rect(mx + block / 2, my, block, block / 2, COLOR_BLACK);
    else if (happy && !grumpy)
    {
        uk_rect(mx - block / 2, my, 3 * block, 2, COLOR_BLACK);
        uk_rect(mx, my + 2, 2 * block, block / 2, COLOR_RED);
    }
    else if (sad || grumpy)
    {
        uk_rect(mx, my + block / 2, 2 * block, 2, COLOR_BLACK);
        uk_rect(mx - 2, my + block / 2 + 2, 2, 3, COLOR_BLACK);
        uk_rect(mx + 2 * block, my + block / 2 + 2, 2, 3, COLOR_BLACK);
    }
    else
        uk_rect(mx, my + 2, 2 * block, 2, COLOR_BLACK);

    if (happy && !sleeping && !grumpy)
    {
        uk_rect(X0 + 1 * block, Y0 + 5 * block, block, block / 2, 0xFB2C);
        uk_rect(X0 + 8 * block, Y0 + 5 * block, block, block / 2, 0xFB2C);
    }

    /* Chapeau haut-de-forme */
    if (S.has_hat)
    {
        int hx = X0 + 3 * block, hy = Y0 - 2;
        uk_rect(hx - block / 2, hy, 5 * block, 3, 0x2104);
        uk_rect(hx + block / 2, hy - 2 * block, 3 * block, 2 * block, 0x2104);
        uk_rect(hx + block / 2, hy - 6, 3 * block, 3, COLOR_RED);
    }

    if (sleeping)
    {
        int z = anim_frame % 3;
        for (int i = 0; i <= z; i++)
            uk_text(X0 + size + 2 + i * 9, Y0 + 10 - i * 12, "z", COLOR_WHITE, 1 + i);
    }

    /* Mouches qui tournent autour quand il est crasseux */
    if (S.clean < 25 && !sleeping)
    {
        static const int path[8][2] = {{0, 0}, {6, -4}, {12, 0}, {8, 5}, {2, 6}, {-4, 2}, {-6, -3}, {-2, -6}};
        for (int f = 0; f < 2; f++)
        {
            const int *p = path[(anim_frame + f * 4) % 8];
            int fx = X0 + (f ? size - 4 : 4) + p[0];
            int fy = Y0 + 6 + f * 14 + p[1];
            uk_rect(fx, fy, 3, 3, COLOR_BLACK);
            uk_rect(fx - 2, fy - 2, 2, 2, COLOR_SILVER);
            uk_rect(fx + 3, fy - 2, 2, 2, COLOR_SILVER);
        }
    }

    /* Croix medicale quand il est malade */
    if (S.sick)
    {
        int cx = X0 - 20, cy = Y0 + 4;
        uk_rect(cx, cy, 14, 14, COLOR_WHITE);
        uk_rect(cx + 5, cy + 2, 4, 10, COLOR_RED);
        uk_rect(cx + 2, cy + 5, 10, 4, COLOR_RED);
    }

    /* Bulle "!" d'appel */
    if (calling && (anim_frame % 4) != 3)
    {
        int bx = X0 + size + 4, by = Y0 - 6;
        uk_rect(bx, by, 14, 18, COLOR_WHITE);
        uk_frame(bx, by, 14, 18, COLOR_BLACK);
        uk_rect(bx + 6, by + 3, 3, 8, COLOR_RED);
        uk_rect(bx + 6, by + 13, 3, 3, COLOR_RED);
    }

    if (ate_recently)
    {
        int rise = (int)(t - ate_at_ms) / 30;
        draw_heart(X0 - 14, Y0 + 10 - rise, 3, COLOR_RED);
        draw_heart(X0 + size, Y0 + 20 - rise, 2, 0xFB2C);
    }
}

static const char *stage_name()
{
    switch (S.stage)
    {
    case ST_EGG: return "Oeuf";
    case ST_BABY: return "Bebe";
    case ST_CHILD: return "Enfant";
    default:
        return S.adult_type == AD_GENIUS ? "Genie" : (S.adult_type == AD_GRUMPY ? "Grognon" : "Adulte");
    }
}

static void draw_side_panel()
{
    const int X = 222, Y = 66, W = 94, H = 84;
    uk_rect(X, Y, W, H, COLOR_WHITE);
    uk_frame(X, Y, W, H, COLOR_SILVER);
    uk_text_center(X, Y + 5, W, stage_name(), COLOR_BLACK, 2);

    char line[24];
    snprintf(line, sizeof(line), "Age %d j", S.age_ticks / TAMA_TICKS_PER_DAY);
    uk_text(X + 6, Y + 25, line, COLOR_GRAY, 1);
    snprintf(line, sizeof(line), "Poids %d kg", (int)S.weight);
    uk_text(X + 6, Y + 35, line, overweight() ? COLOR_RED : COLOR_GRAY, 1);
    snprintf(line, sizeof(line), "Pieces %d", (int)S.coins);
    uk_text(X + 6, Y + 45, line, 0xA460, 1);
    snprintf(line, sizeof(line), "Proprete %d%%", (int)S.clean);
    uk_text(X + 6, Y + 55, line, S.clean < 30 ? COLOR_RED : COLOR_GRAY, 1);
    snprintf(line, sizeof(line), "Fautes %d", (int)S.mistakes);
    uk_text(X + 6, Y + 65, line, S.mistakes > 6 ? COLOR_RED : COLOR_GRAY, 1);
}

static const char *mood_message()
{
    if (S.stage == ST_EGG) return "Un oeuf... il bouge !";
    if (S.sleeping) return "Zzz... Calci dort.";
    if (S.sick) return "Calci est malade...";
    if (S.hunger == 0) return "Calci meurt de faim !";
    if (S.health < 30) return "Calci est tres faible...";
    if (calling) return "Calci veut de l'attention !";
    if (S.clean < 30) return "Calci est tout sale... un bain ?";
    if (S.energy < 20) return "Calci est fatigue.";
    if (S.hunger < 25) return "Calci a tres faim !";
    if (overweight()) return "Calci est un peu trop gros.";
    if (S.happiness < 25) return "Calci s'ennuie...";
    if (S.happiness >= 70) return "Calci est tres heureux !";
    return "Calci est content.";
}

/* Element choisi avec HAUT/BAS : "<  Nom  >" puis une ligne de detail. */
static void draw_picker(const char *name, const char *detail, uint16_t detail_col)
{
    const int Y = 184;
    char line[40];
    snprintf(line, sizeof(line), "^ %s v", name);
    uk_text_center(0, Y + 4, SCREEN_WIDTH, line, COLOR_BLACK, 2);
    uk_text_center(0, Y + 26, SCREEN_WIDTH, detail, detail_col, 1);
}

struct shop_item
{
    const char *name;
    int price;
    const char *desc;
};
static const shop_item SHOP[] = {
    {"Pomme", 2, "Faim +15  Poids +1"},
    {"Gateau", 5, "Faim +20  Joie +15  Poids +3"},
    {"Cafe", 4, "Energie +30  Sante -3"},
    {"Ballon", 6, "Joie +25  Energie -5"},
    {"Chapeau", 25, "Tres chic ! (une seule fois)"},
};
#define SHOP_N ((int)(sizeof(SHOP) / sizeof(SHOP[0])))

static const char *CARE[] = {"Laver", "Caresser", "Soigner"};
#define CARE_N 3
#define CURE_PRICE 8

static const char *GAMES[] = {"Plus ou moins", "Suite logique"};
#define GAMES_N 2

static void draw_action_panel()
{
    const int Y = 184;
    char line[48];

    switch (tab)
    {
    case TAB_CALC:
    {
        const char *ans = answer_len ? answer_buf : "?";
        snprintf(line, sizeof(line), "%d %c %d = %s", problem_a, problem_op, problem_b, ans);
        uk_text_center(0, Y + 2, SCREEN_WIDTH, line, COLOR_BLACK, 2);
        if (anim_frame % 2 == 0)
            uk_rect((SCREEN_WIDTH + uk_text_w(line, 2)) / 2 + 2, Y + 2, 2, 16, COLOR_BLACK);
        snprintf(line, sizeof(line), "Niveau %d   Serie %d   Record %d", difficulty() + 1, streak,
                 (int)S.best_streak);
        uk_text_center(0, Y + 26, SCREEN_WIDTH, line, COLOR_GRAY, 1);
        break;
    }
    case TAB_GAME:
        if (game_mode == GM_GUESS)
        {
            snprintf(line, sizeof(line), "1-%d ? %s", guess_max, answer_len ? answer_buf : "");
            uk_text_center(0, Y + 2, SCREEN_WIDTH, line, COLOR_BLACK, 2);
            snprintf(line, sizeof(line), "Essais restants : %d", guess_tries);
            uk_text_center(0, Y + 26, SCREEN_WIDTH, line, COLOR_GRAY, 1);
        }
        else if (game_mode == GM_SEQ)
        {
            snprintf(line, sizeof(line), "%d,%d,%d,%d,%s", seq_terms[0], seq_terms[1], seq_terms[2],
                     seq_terms[3], answer_len ? answer_buf : "?");
            uk_text_center(0, Y + 2, SCREEN_WIDTH, line, COLOR_BLACK, 2);
            uk_text_center(0, Y + 26, SCREEN_WIDTH, "Quel est le terme suivant ?", COLOR_GRAY, 1);
        }
        else
        {
            snprintf(line, sizeof(line), "Coute %d energie. Gagne joie et pieces.", TAMA_GAME_ENERGY);
            draw_picker(GAMES[game_sel], line, COLOR_GRAY);
        }
        break;
    case TAB_SHOP:
    {
        const shop_item &it = SHOP[shop_sel];
        char name[24];
        snprintf(name, sizeof(name), "%s %dp", it.name, it.price);
        bool owned = shop_sel == SHOP_N - 1 && S.has_hat;
        draw_picker(name, owned ? "Deja achete" : it.desc,
                    it.price > S.coins ? COLOR_RED : COLOR_GRAY);
        break;
    }
    case TAB_CARE:
    {
        const char *detail = "";
        if (care_sel == 0)
        {
            snprintf(line, sizeof(line), "Proprete %d%%  ->  100%%", (int)S.clean);
            detail = line;
        }
        else if (care_sel == 1)
            detail = calling ? "Calci attend un calin !" : "Joie +4";
        else
        {
            snprintf(line, sizeof(line), "%d pieces. %s", CURE_PRICE,
                     S.sick ? "Calci en a besoin !" : "Calci va bien.");
            detail = line;
        }
        draw_picker(CARE[care_sel], detail, COLOR_GRAY);
        break;
    }
    default:
        uk_text_center(0, Y + 4, SCREEN_WIDTH, S.sleeping ? "OK : rallumer" : "OK : eteindre",
                       COLOR_BLACK, 2);
        snprintf(line, sizeof(line), "Energie %d / 100", (int)S.energy);
        uk_text_center(0, Y + 26, SCREEN_WIDTH, line, COLOR_GRAY, 1);
        break;
    }
}

static void draw_game_over()
{
    fill_screen(0x2104);
    uk_header("~ Adieu Calci ~");
    uk_text_center(0, 40, SCREEN_WIDTH, "Calci est parti...", COLOR_WHITE, 2);
    /* petite tombe */
    uk_rect(140, 70, 40, 50, COLOR_GRAY);
    uk_rect(146, 64, 28, 8, COLOR_GRAY);
    uk_rect(157, 78, 6, 26, COLOR_WHITE);
    uk_rect(149, 86, 22, 6, COLOR_WHITE);
    uk_rect(120, 120, 80, 4, 0x4A49);

    char line[48];
    snprintf(line, sizeof(line), "Generation %d - %s", (int)S.generation, stage_name());
    uk_text_center(0, 136, SCREEN_WIDTH, line, COLOR_SILVER, 1);
    snprintf(line, sizeof(line), "Age %d j   Repas %d   Fautes %d", S.age_ticks / TAMA_TICKS_PER_DAY,
             (int)S.meals, (int)S.mistakes);
    uk_text_center(0, 150, SCREEN_WIDTH, line, COLOR_SILVER, 1);
    snprintf(line, sizeof(line), "Meilleure serie de calculs : %d", (int)S.best_streak);
    uk_text_center(0, 164, SCREEN_WIDTH, line, COLOR_SILVER, 1);
    uk_text_center(0, 188, SCREEN_WIDTH, "Nourris-le et garde-le propre !", COLOR_YELLOW, 1);
    uk_footer("OK : nouvel oeuf   BACK : quitter");
}

static void draw_screen()
{
    if (S.dead)
    {
        draw_game_over();
        return;
    }

    fill_screen(BACKGROUND_COLOR);

    char title[24];
    if (S.generation > 1)
        snprintf(title, sizeof(title), "~ Calci %d ~", (int)S.generation);
    else
        snprintf(title, sizeof(title), "~ Calci ~");
    uk_header(title);
    static const char *tabs[TAB_COUNT] = {"Calcul", "Jeu", "Boutique", "Soins", "Dodo"};
    uk_tabs(tabs, TAB_COUNT, tab, true);

    draw_bar(6, 40, "Faim", S.hunger);
    draw_bar(6, 52, "Joie", S.happiness);
    draw_bar(164, 40, "Energie", S.energy);
    draw_bar(164, 52, "Sante", S.health);

    /* Nuit : la piece est dans le noir */
    if (S.sleeping)
        uk_rect(0, 63, SCREEN_WIDTH, PET_BOTTOM + 5 - 63, 0x18E7);

    uk_rect(0, PET_BOTTOM + 4, SCREEN_WIDTH, 1, COLOR_SILVER);

    if (S.stage == ST_EGG)
        draw_egg();
    else
        draw_calci();
    draw_side_panel();

    const char *msg = mood_message();
    uint16_t col = COLOR_GRAY;
    if (now_ms() < feedback_until)
    {
        msg = feedback;
        col = feedback_col;
    }
    uk_text_center(0, PET_BOTTOM + 8, SCREEN_WIDTH, msg, col, 1);

    draw_action_panel();

    const char *hint;
    if (tab == TAB_CALC || (tab == TAB_GAME && game_mode != GM_NONE))
        hint = "0-9 - : nombre  OK valider  BACK effacer";
    else if (tab == TAB_SLEEP)
        hint = "OK : lumiere   </> : onglet   BACK";
    else
        hint = "^v : choisir   OK : valider   </> : onglet";
    uk_footer(hint);
}

/* ------------------------------- Actions --------------------------------- */

static void submit_calc()
{
    if (blocked(true))
        return;
    if (answer_len == 0 || (answer_len == 1 && answer_buf[0] == '-'))
        return;
    if (atoi(answer_buf) == problem_answer)
    {
        streak++;
        if (streak > S.best_streak)
            S.best_streak = streak;
        S.energy = clamp100(S.energy - 2);
        int gain = 1 + difficulty() + (streak >= 5 ? 2 : 0);
        S.coins += gain;
        char msg[44];
        if (S.hunger >= 95)
        {
            /* gourmandise : il mange quand meme, ca fait grossir */
            snprintf(msg, sizeof(msg), "+%d pieces. Calci n'a plus faim !", gain);
            set_feedback(msg, 0xA460, 1500);
            ate(0, 2, 1);
        }
        else
        {
            snprintf(msg, sizeof(msg), "Bravo ! Miam ! +%d pieces", gain);
            if (streak >= 3)
                snprintf(msg, sizeof(msg), "Serie de %d ! +%d pieces", streak, gain);
            set_feedback(msg, COLOR_GREEN, 1200);
            ate(15, 3 + (streak >= 5 ? 3 : 0), (S.meals % 3 == 0) ? 1 : 0);
        }
    }
    else
    {
        char msg[44];
        snprintf(msg, sizeof(msg), "Rate ! %d %c %d = %d", problem_a, problem_op, problem_b,
                 problem_answer);
        set_feedback(msg, COLOR_RED, 2000);
        streak = 0;
        S.happiness = clamp100(S.happiness - 5);
        state_dirty = true;
    }
    generate_problem();
}

static void start_game()
{
    if (blocked(false))
        return;
    if (S.energy < TAMA_GAME_ENERGY + 5)
    {
        set_feedback("Calci est trop fatigue pour jouer.", COLOR_MAROON, 1500);
        return;
    }
    S.energy = clamp100(S.energy - TAMA_GAME_ENERGY);
    state_dirty = true;
    if (game_sel == 0)
    {
        game_mode = GM_GUESS;
        guess_max = S.stage == ST_BABY ? 30 : 50;
        guess_secret = 1 + rand() % guess_max;
        guess_tries = 6;
        clear_answer();
        set_feedback("Calci pense a un nombre...", COLOR_BLUE, 1500);
    }
    else
    {
        game_mode = GM_SEQ;
        generate_sequence();
    }
}

static void finish_game(bool won, int coins, int joy, int weight_loss)
{
    S.happiness = clamp100(S.happiness + joy);
    S.coins += coins;
    S.weight -= weight_loss;
    get_dirty(5); /* jouer salit */
    if (S.weight < 1)
        S.weight = 1;
    satisfy_call();
    game_mode = GM_NONE;
    clear_answer();
    (void)won;
    state_dirty = true;
}

static void submit_game()
{
    if (answer_len == 0 || (answer_len == 1 && answer_buf[0] == '-'))
        return;
    int v = atoi(answer_buf);
    char msg[44];
    if (game_mode == GM_GUESS)
    {
        if (v == guess_secret)
        {
            int coins = 2 + guess_tries;
            snprintf(msg, sizeof(msg), "Trouve ! +%d pieces, Calci adore !", coins);
            set_feedback(msg, COLOR_GREEN, 2000);
            finish_game(true, coins, 20, 2);
            return;
        }
        guess_tries--;
        if (guess_tries <= 0)
        {
            snprintf(msg, sizeof(msg), "Perdu ! C'etait %d.", guess_secret);
            set_feedback(msg, COLOR_RED, 2000);
            finish_game(false, 0, 5, 1);
            return;
        }
        set_feedback(v < guess_secret ? "Plus grand !" : "Plus petit !", COLOR_BLUE, 4000);
        clear_answer();
    }
    else
    {
        if (v == seq_answer)
        {
            set_feedback("Exact ! +4 pieces", COLOR_GREEN, 1500);
            finish_game(true, 4, 12, 1);
        }
        else
        {
            snprintf(msg, sizeof(msg), "Non, c'etait %d.", seq_answer);
            set_feedback(msg, COLOR_RED, 2000);
            finish_game(false, 0, 3, 1);
        }
    }
}

static void buy()
{
    const shop_item &it = SHOP[shop_sel];
    if (blocked(true))
        return;
    if (shop_sel == SHOP_N - 1 && S.has_hat)
    {
        set_feedback("Calci a deja un chapeau.", COLOR_GRAY, 1200);
        return;
    }
    if (S.coins < it.price)
    {
        set_feedback("Pas assez de pieces ! (Calcul)", COLOR_RED, 1500);
        return;
    }
    bool food = shop_sel <= 1;
    if (food && S.hunger >= 95)
    {
        set_feedback("Calci n'a plus faim.", COLOR_GRAY, 1200);
        return;
    }
    if (food && S.sick)
    {
        set_feedback("Calci est malade, il refuse.", COLOR_RED, 1200);
        return;
    }
    S.coins -= it.price;
    switch (shop_sel)
    {
    case 0: ate(15, 2, 1); set_feedback("Croc ! Une bonne pomme.", COLOR_GREEN, 1200); break;
    case 1: ate(20, 15, 3); set_feedback("Miam, du gateau !", COLOR_GREEN, 1200); break;
    case 2:
        S.energy = clamp100(S.energy + 30);
        S.health = clamp100(S.health - 3);
        set_feedback("Calci est en pleine forme !", COLOR_GREEN, 1200);
        break;
    case 3:
        S.happiness = clamp100(S.happiness + 25);
        S.energy = clamp100(S.energy - 5);
        satisfy_call();
        set_feedback("Calci joue au ballon !", COLOR_GREEN, 1200);
        break;
    default:
        S.has_hat = 1;
        S.happiness = clamp100(S.happiness + 10);
        set_feedback("Quel chic !", COLOR_PURPLE, 1500);
        break;
    }
    state_dirty = true;
}

static void care()
{
    if (S.stage == ST_EGG)
    {
        blocked(true);
        return;
    }
    if (care_sel == 0)
    {
        if (S.clean >= 90)
        {
            set_feedback("Calci est deja propre.", COLOR_GRAY, 1000);
            return;
        }
        /* plus il etait sale, plus le bain lui fait plaisir */
        S.happiness = clamp100(S.happiness + (100 - S.clean) / 10);
        S.clean = 100;
        set_feedback("Splash ! Calci est tout propre.", COLOR_GREEN, 1200);
    }
    else if (care_sel == 1)
    {
        if (blocked(true))
            return;
        uint32_t t = now_ms();
        if (last_pet_ms && t - last_pet_ms < TAMA_PET_COOLDOWN_MS && !calling)
        {
            set_feedback("Calci en a assez des calins.", COLOR_GRAY, 1200);
            return;
        }
        last_pet_ms = t;
        S.happiness = clamp100(S.happiness + 4);
        satisfy_call();
        ate_recently = true; /* coeurs */
        ate_at_ms = t;
        set_feedback("Ronron...", 0xFB2C, 1000);
    }
    else
    {
        if (!S.sick)
        {
            set_feedback("Calci n'est pas malade.", COLOR_GRAY, 1200);
            return;
        }
        if (S.coins < CURE_PRICE)
        {
            set_feedback("Pas assez de pieces ! (Calcul)", COLOR_RED, 1500);
            return;
        }
        S.coins -= CURE_PRICE;
        S.sick = 0;
        S.health = clamp100(S.health + 20);
        set_feedback("Calci est gueri !", COLOR_GREEN, 1500);
    }
    state_dirty = true;
}

static void toggle_light()
{
    if (S.stage == ST_EGG)
    {
        blocked(true);
        return;
    }
    if (S.sleeping)
    {
        S.sleeping = 0;
        if (S.energy < 50)
        {
            S.happiness = clamp100(S.happiness - 5);
            set_feedback("Calci est reveille trop tot, grr.", COLOR_MAROON, 1500);
        }
        else
            set_feedback("Debout Calci !", COLOR_BLUE, 1200);
    }
    else
    {
        if (S.energy >= 90)
        {
            set_feedback("Calci n'est pas fatigue.", COLOR_GRAY, 1200);
            return;
        }
        S.sleeping = 1;
        game_mode = GM_NONE;
        calling = false;
        set_feedback("Bonne nuit Calci...", COLOR_BLUE, 1200);
    }
    state_dirty = true;
}

static void on_ok()
{
    switch (tab)
    {
    case TAB_CALC: submit_calc(); break;
    case TAB_GAME:
        if (game_mode == GM_NONE)
            start_game();
        else
            submit_game();
        break;
    case TAB_SHOP: buy(); break;
    case TAB_CARE: care(); break;
    default: toggle_light(); break;
    }
}

static bool typing()
{
    return tab == TAB_CALC || (tab == TAB_GAME && game_mode != GM_NONE);
}

static void append_char(char d)
{
    if (!typing() || answer_len >= TAMA_ANSWER_MAX)
        return;
    if (d == '-' && answer_len != 0)
        return;
    answer_buf[answer_len++] = d;
    answer_buf[answer_len] = '\0';
}

static int key_digit(int key)
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
    case MINUS: return '-';
    default: return 0;
    }
}

static void switch_tab(int dir)
{
    /* quitter l'onglet Jeu abandonne la partie en cours */
    if (tab == TAB_GAME && game_mode != GM_NONE)
    {
        game_mode = GM_NONE;
        set_feedback("Partie abandonnee.", COLOR_GRAY, 1000);
    }
    tab = (tab + dir + TAB_COUNT) % TAB_COUNT;
    clear_answer();
}

static void move_sel(int dir)
{
    if (tab == TAB_SHOP)
        shop_sel = (shop_sel + dir + SHOP_N) % SHOP_N;
    else if (tab == TAB_CARE)
        care_sel = (care_sel + dir + CARE_N) % CARE_N;
    else if (tab == TAB_GAME && game_mode == GM_NONE)
        game_sel = (game_sel + dir + GAMES_N) % GAMES_N;
}

/* Lit une touche sans bloquer. Les caracteres tapes au clavier du PC
 * (simulateur) sont renvoyes en negatif, comme uk_wait_key(). */
static int poll_key()
{
    int ch = scan_text_char();
    if (ch >= 32 && ch <= 126)
    {
        scan_keypad_nowait();
        return -ch;
    }
    return scan_keypad_nowait();
}

/* --------------------------------- App ----------------------------------- */

static void update_time(uint32_t t, uint32_t dt)
{
    if (S.dead)
        return;

    if (S.stage == ST_EGG)
    {
        S.egg_ms_left -= (int32_t)dt;
        if (S.egg_ms_left <= 0)
        {
            S.egg_ms_left = 0;
            S.stage = ST_BABY;
            ate_recently = true;
            ate_at_ms = t;
            generate_problem();
            set_feedback("Calci est ne ! Nourris-le (Calcul)", COLOR_PURPLE, 3000);
            state_dirty = true;
        }
        last_tick_ms = t;
        return;
    }

    while (t - last_tick_ms >= TAMA_TICK_MS && !S.dead)
    {
        last_tick_ms += TAMA_TICK_MS;
        life_tick(false);
    }


    if (calling && (int32_t)(t - call_deadline) >= 0)
    {
        calling = false;
        S.happiness = clamp100(S.happiness - 10);
        add_mistake();
        set_feedback("Calci se sent delaisse... (faute)", COLOR_MAROON, 2000);
    }
}

int Tamagotchi()
{
    if (!tama_init)
    {
        srand((unsigned int)now_ms());
        memset(&S, 0, sizeof(S));
        if (!tama_load())
            new_game();
        streak = 0;
        tab = TAB_CALC;
        generate_problem();
        tama_init = true;
    }
    else
    {
        apply_absence();
    }

    /* HOME est gere ici pour sauvegarder avant de quitter (le saut de
     * home_check sauterait la sauvegarde). Le menu le rearme au retour. */
    g_home_armed = false;

    uint32_t t = now_ms();
    uint32_t prev = t;
    last_tick_ms = t;
    next_blink_ms = t + 2000;
    ate_recently = false;
    feedback_until = 0;
    calling = false;
    game_mode = GM_NONE;

    uint32_t next_frame = 0;
    bool redraw = true;

    while (true)
    {
        t = now_ms();
        bool was_dead = S.dead;
        update_time(t, t - prev);
        prev = t;
        if (S.dead != was_dead)
            redraw = true;

        if (ate_recently && t - ate_at_ms > 900)
            ate_recently = false;
        if (t >= next_blink_ms)
        {
            blink_until = t + 200;
            next_blink_ms = t + 2000 + (uint32_t)(rand() % 3000);
            redraw = true;
        }
        if ((int32_t)(t - next_frame) >= 0)
        {
            anim_frame++;
            next_frame = t + TAMA_FRAME_MS;
            redraw = true;
        }

        if (redraw)
        {
            draw_screen();
            flush_display();
            redraw = false;
        }

        int key = poll_key();
        if (key == -1)
        {
            sleep_ms(15);
            continue;
        }
        redraw = true;

        if (key < 0) /* caractere du clavier PC */
        {
            char c = (char)(-key);
            if ((c >= '0' && c <= '9') || c == '-')
            {
                if (!S.dead)
                    append_char(c);
                continue;
            }
            if (c != '\n' && c != '\r')
                continue;
            key = OK;
        }

        if (key == HOME || (key == BACK && !(typing() && answer_len > 0)))
        {
            tamagotchi_save();
            if (key == HOME)
            {
                /* Vide les repetitions de HOME (touche maintenue), sinon le
                 * menu principal lancerait le combo du mode examen. */
                for (int k = 0; k < 6; k++)
                {
                    sleep_ms(50);
                    while (scan_keypad_nowait() != -1)
                        k = 0;
                }
            }
            return 0;
        }

        if (S.dead)
        {
            if (key == OK || key == ENTER || key == EQUAL)
            {
                new_game();
                generate_problem();
                tab = TAB_CALC;
                last_tick_ms = now_ms();
            }
            continue;
        }

        switch (key)
        {
        case BACK:
            answer_buf[--answer_len] = '\0';
            break;
        case SECOND:
        case RIGHT:
            switch_tab(1);
            break;
        case LEFT:
            switch_tab(-1);
            break;
        case UP:
            move_sel(-1);
            break;
        case DOWN:
            move_sel(1);
            break;
        case OK:
        case ENTER:
        case EQUAL:
            on_ok();
            break;
        default:
        {
            int d = key_digit(key);
            if (d)
                append_char((char)d);
            break;
        }
        }
    }
}
