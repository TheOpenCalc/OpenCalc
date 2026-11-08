/* Module Python "draw" (dessin sur l'ecran) et fonctions de base manquantes
 * dans PikaPython (sum, round, sorted).
 *
 * Le precompilateur PikaPython n'etant pas disponible sous Linux, le module
 * est cree a l'execution : un objet "draw" est attache a la racine de
 * l'interpreteur (ce qui suffit pour "import draw") et ses fonctions natives
 * sont declarees avec class_defineMethod. Le dessin lui-meme passe par les
 * fonctions oc_draw_* (Core/py_draw_bridge.cpp sur la calculatrice, stubs
 * enregistreurs dans les tests). */
#include "PikaObj.h"
#include "TinyObj.h"
#include "pika_wrapper.h"
#include "py_gfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int to_int(Args *a, char *name)
{
    pika_float f = args_getFloat(a, name);
    if (f == _PIKA_FLOAT_ERR)
        return 0;
    return (int)lround(f);
}

static void begin(void)
{
    pyg_begin();
}

static void m_clear(PikaObj *self, Args *a)
{
    begin();
    oc_draw_clear(to_int(a, "c"));
}

static void m_set_pixel(PikaObj *self, Args *a)
{
    begin();
    oc_draw_rect(to_int(a, "x"), to_int(a, "y"), 1, 1, to_int(a, "c"));
}

static void m_fill_rect(PikaObj *self, Args *a)
{
    begin();
    oc_draw_rect(to_int(a, "x"), to_int(a, "y"), to_int(a, "w"), to_int(a, "h"), to_int(a, "c"));
}

static void m_line(PikaObj *self, Args *a)
{
    begin();
    oc_draw_line(to_int(a, "x0"), to_int(a, "y0"), to_int(a, "x1"), to_int(a, "y1"), to_int(a, "c"));
}

static void m_circle(PikaObj *self, Args *a)
{
    begin();
    oc_draw_circle(to_int(a, "x"), to_int(a, "y"), to_int(a, "r"), to_int(a, "c"), 0);
}

static void m_fill_circle(PikaObj *self, Args *a)
{
    begin();
    oc_draw_circle(to_int(a, "x"), to_int(a, "y"), to_int(a, "r"), to_int(a, "c"), 1);
}

static void m_draw_string(PikaObj *self, Args *a)
{
    begin();
    char *s = args_getStr(a, "s");
    oc_draw_text(s ? s : "", to_int(a, "x"), to_int(a, "y"), to_int(a, "c"));
}

/* color(r, g, b) -> entier RGB565, utilisable partout ou une couleur est attendue. */
static void m_color(PikaObj *self, Args *a)
{
    int r = to_int(a, "r"), g = to_int(a, "g"), b = to_int(a, "b");
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    method_returnInt(a, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void m_show(PikaObj *self, Args *a)
{
    begin();
    oc_draw_show();
}

static void m_wait_key(PikaObj *self, Args *a)
{
    begin();
    int k = oc_draw_wait_key();
    pyg_shown();
    method_returnInt(a, k);
}

/* Fonctions Python absentes de PikaPython, definies en Python au demarrage. */
static const char *k_prelude =
    "def sum(it, start=0):\n"
    "    s = start\n"
    "    for v in it:\n"
    "        s = s + v\n"
    "    return s\n"
    "def round(x, n=0):\n"
    "    m = 10 ** n\n"
    "    if x >= 0:\n"
    "        r = int(x * m + 0.5)\n"
    "    else:\n"
    "        r = -int(-x * m + 0.5)\n"
    "    if n == 0:\n"
    "        return r\n"
    "    return r / m\n"
    "def _sorted_gt(a, b, rev):\n"
    "    if rev:\n"
    "        return a < b\n"
    "    return a > b\n"
    "def sorted(it, reverse=False):\n"
    "    l = []\n"
    "    for v in it:\n"
    "        l.append(v)\n"
    "    n = len(l)\n"
    "    for i in range(1, n):\n"
    "        k = l[i]\n"
    "        j = i - 1\n"
    "        while j >= 0:\n"
    "            if not _sorted_gt(l[j], k, reverse):\n"
    "                break\n"
    "            l[j + 1] = l[j]\n"
    "            j = j - 1\n"
    "        l[j + 1] = k\n"
    "    return l\n"
    /* turtle.Turtle() et turtle.Screen() renvoient le module : une seule
     * tortue, comme sur les calculatrices. */
    "def _oc_turtle_self(*a):\n"
    "    return turtle\n"
    "turtle.Turtle = _oc_turtle_self\n"
    "turtle.Pen = _oc_turtle_self\n"
    "turtle.Screen = _oc_turtle_self\n"
    /* random est charge ici pour que "from random import *" trouve le
     * module (choice et shuffle y sont ajoutes en C, voir plus bas). */
    "import random\n"
    /* Alias caches des modules : voir k_restore dans Core/pika_wrapper.c. */
    "_oc_m_math = math\n"
    "_oc_m_random = random\n"
    "_oc_m_turtle = turtle\n"
    "_oc_m_draw = draw\n"
    "_oc_m_matplotlib = matplotlib\n";

/* random.choice(seq) et random.shuffle(liste), absents de PikaPython. Ils
 * utilisent rand(), comme le module random de PikaPython (meme graine). */
static void m_choice(PikaObj *self, Args *a)
{
    Arg *seq = args_getArg(a, "seq");
    if (seq && arg_getType(seq) == ARG_TYPE_STRING)
    {
        char *s = arg_getStr(seq);
        size_t n = strlen(s);
        if (n > 0)
        {
            char c[2] = {s[rand() % n], 0};
            method_returnStr(a, c);
            return;
        }
    }
    else if (seq && (arg_isList(seq) || arg_isTuple(seq)))
    {
        PikaObj *o = arg_getObj(seq);
        int n = arg_isList(seq) ? (int)pikaList_getSize(o) : (int)pikaTuple_getSize(o);
        if (n > 0)
        {
            int i = rand() % n;
            Arg *e = arg_isList(seq) ? pikaList_get(o, i) : pikaTuple_getArg(o, i);
            method_returnArg(a, arg_copy(e));
            return;
        }
    }
    else
    {
        pyg_error(self, "TypeError: choice() attend une liste, un tuple ou une chaine");
        return;
    }
    pyg_error(self, "IndexError: choice() sur une sequence vide");
}

static void m_shuffle(PikaObj *self, Args *a)
{
    Arg *l = args_getArg(a, "l");
    if (l == NULL || !arg_isList(l))
    {
        pyg_error(self, "TypeError: shuffle() attend une liste");
        return;
    }
    PikaObj *o = arg_getObj(l);
    for (int i = (int)pikaList_getSize(o) - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        Arg *ai = arg_copy(pikaList_get(o, i));
        Arg *aj = arg_copy(pikaList_get(o, j));
        pikaList_set(o, i, aj);
        pikaList_set(o, j, ai);
    }
}

void pika_extras_install(PikaObj *root)
{
    PikaObj *d = newNormalObj(New_TinyObj);
    class_defineMethod(d, "clear", "c", m_clear);
    class_defineMethod(d, "set_pixel", "x,y,c", m_set_pixel);
    class_defineMethod(d, "fill_rect", "x,y,w,h,c", m_fill_rect);
    class_defineMethod(d, "line", "x0,y0,x1,y1,c", m_line);
    class_defineMethod(d, "circle", "x,y,r,c", m_circle);
    class_defineMethod(d, "fill_circle", "x,y,r,c", m_fill_circle);
    class_defineMethod(d, "draw_string", "s,x,y,c", m_draw_string);
    class_defineMethod(d, "color", "r,g,b", m_color);
    class_defineMethod(d, "show", "", m_show);
    class_defineMethod(d, "wait_key", "", m_wait_key);
    obj_setInt(d, "WIDTH", 320);
    obj_setInt(d, "HEIGHT", 240);
    obj_setInt(d, "BLACK", 0x0000);
    obj_setInt(d, "WHITE", 0xFFFF);
    obj_setInt(d, "RED", 0xF800);
    obj_setInt(d, "GREEN", 0x07E0);
    obj_setInt(d, "BLUE", 0x001F);
    obj_setInt(d, "YELLOW", 0xFFE0);
    obj_setInt(d, "CYAN", 0x07FF);
    obj_setInt(d, "MAGENTA", 0xF81F);
    obj_setInt(d, "ORANGE", 0xFD20);
    obj_setInt(d, "GRAY", 0x8410);
    obj_setObj(root, "draw", d);
    py_turtle_install(root);
    py_plot_install(root);
    obj_run(root, (char *)k_prelude);
    PikaObj *rnd = obj_getObj(root, "random");
    if (rnd != NULL)
    {
        class_defineMethod(rnd, "choice", "seq", m_choice);
        class_defineMethod(rnd, "shuffle", "l", m_shuffle);
    }
}
