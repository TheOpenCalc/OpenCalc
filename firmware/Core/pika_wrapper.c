#include "pika_wrapper.h"
#include "PikaPlatform.h"
#include "PikaObj.h"
#include "pikaScript.h"
#include "py_gfx.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PIKA_OUTPUT_SIZE 4096

static PikaObj *g_root = NULL;
static char g_output[PIKA_OUTPUT_SIZE];
static size_t g_out_len = 0;

void pika_platform_printf(char *fmt, ...)
{
    if (g_out_len >= PIKA_OUTPUT_SIZE - 1)
        return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(g_output + g_out_len, PIKA_OUTPUT_SIZE - g_out_len, fmt, args);
    va_end(args);
    if (n > 0)
    {
        g_out_len += (size_t)n;
        if (g_out_len > PIKA_OUTPUT_SIZE - 1)
            g_out_len = PIKA_OUTPUT_SIZE - 1;
    }
}

void pika_init(void)
{
    if (g_root != NULL)
        return;
    g_out_len = 0;
    g_root = pikaPythonInit();
    {
        extern void pika_extras_install(PikaObj * root);
        pika_extras_install(g_root);
    }
    g_out_len = 0;
}

/* ---------- "from module import *" ----------
 * PikaPython ne connait pas cette forme (erreur de syntaxe), tres utilisee
 * dans les manuels ("from turtle import *", "from math import *"). Ces
 * modules sont deja charges a la racine : la ligne est reecrite avant
 * l'execution en "a = m.a; b = m.b; ...", sur une seule ligne pour ne pas
 * decaler les numeros de ligne des erreurs. ("import" est refuse par
 * PikaPython dans une ligne a plusieurs instructions.) */

static const char *const k_math_names[] = {
    "pi", "e", "ceil", "fabs", "floor", "fmod", "remainder", "trunc", "exp", "log", "log2",
    "log10", "pow", "sqrt", "acos", "asin", "atan", "atan2", "cos", "sin", "tan", "degrees",
    "radians", "cosh", "sinh", "tanh", NULL};
static const char *const k_random_names[] = {
    "random", "randint", "randrange", "seed", "uniform", "choice", "shuffle", NULL};
static const char *const k_draw_names[] = {
    "clear", "set_pixel", "fill_rect", "line", "circle", "fill_circle", "draw_string", "color",
    "show", "wait_key", "WIDTH", "HEIGHT", "BLACK", "WHITE", "RED", "GREEN", "BLUE", "YELLOW",
    "CYAN", "MAGENTA", "ORANGE", "GRAY", NULL};
static const char *const k_turtle_extra[] = {"Turtle", "Screen", "Pen", NULL};

typedef struct
{
    char *buf;
    size_t len, cap;
} sbuf;

static int sb_add(sbuf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap)
    {
        size_t cap = (b->cap ? b->cap * 2 : 256) + n;
        char *nb = (char *)realloc(b->buf, cap);
        if (nb == NULL)
            return 0;
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
    return 1;
}

static int sb_str(sbuf *b, const char *s) { return sb_add(b, s, strlen(s)); }

static int is_sp(char c) { return c == ' ' || c == '\t'; }

static void add_bindings(sbuf *b, const char *mod, const char *const *names, int *first)
{
    for (int i = 0; names[i]; i++)
    {
        if (!*first)
            sb_str(b, "; ");
        *first = 0;
        sb_str(b, names[i]);
        sb_str(b, " = ");
        sb_str(b, mod);
        sb_str(b, ".");
        sb_str(b, names[i]);
    }
}

static void add_fn_bindings(sbuf *b, const char *mod, const char *(*name)(int), int *first)
{
    for (int i = 0; name(i); i++)
    {
        const char *one[2] = {name(i), NULL};
        add_bindings(b, mod, one, first);
    }
}

/* Ligne [p, end) de la forme "from MOD import *" : ecrit la traduction et
 * renvoie 1. */
static int rewrite_line(sbuf *b, const char *p, const char *end)
{
    const char *q = p;
    while (q < end && is_sp(*q))
        q++;
    const char *indent_end = q;
    if (end - q < 5 || strncmp(q, "from", 4) != 0 || !is_sp(q[4]))
        return 0;
    q += 4;
    while (q < end && is_sp(*q))
        q++;
    const char *mod = q;
    while (q < end && !is_sp(*q))
        q++;
    size_t mlen = (size_t)(q - mod);
    while (q < end && is_sp(*q))
        q++;
    if (end - q < 7 || strncmp(q, "import", 6) != 0)
        return 0;
    q += 6;
    while (q < end && is_sp(*q))
        q++;
    if (q >= end || *q != '*')
        return 0;
    q++;
    while (q < end && (is_sp(*q) || *q == '\r'))
        q++;
    if (q < end && *q != '#')
        return 0;

    char m[24];
    if (mlen == 0 || mlen >= sizeof(m))
        return 0;
    memcpy(m, mod, mlen);
    m[mlen] = '\0';
    const char *const *names = NULL;
    const char *(*fn)(int) = NULL;
    if (!strcmp(m, "math"))
        names = k_math_names;
    else if (!strcmp(m, "random"))
        names = k_random_names;
    else if (!strcmp(m, "draw"))
        names = k_draw_names;
    else if (!strcmp(m, "turtle"))
    {
        names = k_turtle_extra;
        fn = py_turtle_name;
    }
    else if (!strcmp(m, "matplotlib.pyplot"))
        fn = py_plot_name;
    else
        return 0;

    /* Alias cache du module (voir k_restore) : "random = random.random"
     * ne doit pas empecher de lire random.randint ensuite. */
    char alias[40];
    snprintf(alias, sizeof(alias), "_oc_m_%s", m);
    int first = 1;
    sb_add(b, p, (size_t)(indent_end - p));
    if (fn)
        add_fn_bindings(b, alias, fn, &first);
    if (names)
        add_bindings(b, alias, names, &first);
    return 1;
}

/* Les variables d'un script restent definies pour le suivant : si un script
 * a remplace "turtle" ou "random" (par exemple via "from random import *"),
 * on remet les modules avant chaque execution. Les alias _oc_m_* sont crees
 * par le prelude (Core/py_draw.c) et gardent les modules en vie. */
static const char *k_restore =
    "math = _oc_m_math; random = _oc_m_random; turtle = _oc_m_turtle; "
    "draw = _oc_m_draw; matplotlib = _oc_m_matplotlib\n";

/* Renvoie le script reecrit (a liberer), ou NULL s'il n'y a rien a changer. */
static char *rewrite_star_imports(const char *src)
{
    if (strstr(src, "import") == NULL || strchr(src, '*') == NULL)
        return NULL;
    sbuf b = {NULL, 0, 0};
    int changed = 0;
    const char *p = src;
    while (*p)
    {
        const char *end = strchr(p, '\n');
        if (end == NULL)
            end = p + strlen(p);
        if (rewrite_line(&b, p, end))
            changed = 1;
        else
            sb_add(&b, p, (size_t)(end - p));
        if (*end == '\n')
        {
            sb_add(&b, "\n", 1);
            end++;
        }
        p = end;
    }
    if (!changed || b.buf == NULL)
    {
        free(b.buf);
        return NULL;
    }
    return b.buf;
}

int pika_run(const char *script)
{
    if (g_root == NULL)
        pika_init();
    if (g_root == NULL)
        return -1;
    obj_run(g_root, (char *)k_restore);
    g_out_len = 0;
    char *rewritten = rewrite_star_imports(script);
    obj_run(g_root, rewritten ? rewritten : (char *)script);
    free(rewritten);
    py_plot_autoshow();
    while (g_out_len > 0 && (g_output[g_out_len - 1] == '\n' || g_output[g_out_len - 1] == '\r'))
        g_out_len--;
    g_output[g_out_len] = '\0';
    return 0;
}

const char *pika_get_output(void)
{
    g_output[g_out_len] = '\0';
    return g_output;
}

void pika_clear_output(void)
{
    g_out_len = 0;
    g_output[0] = '\0';
}

void pika_deinit(void)
{
    if (g_root != NULL)
    {
        obj_deinit(g_root);
        g_root = NULL;
    }
    g_out_len = 0;
}
