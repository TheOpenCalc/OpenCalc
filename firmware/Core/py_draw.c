/* Module Python "draw" (dessin sur l'ecran) et fonctions de base manquantes
 * dans PikaPython (sum, round, sorted, pow, enumerate, zip...).
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
#include <stdio.h>
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
    /* Somme compensee (Neumaier), comme sum() depuis Python 3.12 :
     * sum([0.1] * 10) vaut 1.0. */
    "def sum(it, start=0):\n"
    "    s = start\n"
    "    c = 0.0\n"
    "    fl = False\n"
    "    for v in it:\n"
    "        if type(v) == float or type(s) == float:\n"
    "            fl = True\n"
    "            t = s + v\n"
    "            if abs(s) >= abs(v):\n"
    "                c = c + ((s - t) + v)\n"
    "            else:\n"
    "                c = c + ((v - t) + s)\n"
    "            s = t\n"
    "        else:\n"
    "            s = s + v\n"
    "    if fl:\n"
    "        return s + c\n"
    "    return s\n"
    "def repr(v):\n"
    "    if type(v) == str:\n"
    "        if \"'\" in v and not '\"' in v:\n"
    "            return '\"' + v + '\"'\n"
    "        return \"'\" + v.replace(\"'\", \"\\\\'\") + \"'\"\n"
    "    return str(v)\n"
    /* s[a:b:c] (Core/py_rewrite.c) */
    "def _oc_slice(o, a=None, b=None, c=None):\n"
    "    n = len(o)\n"
    "    if c is None:\n"
    "        c = 1\n"
    "    if c == 0:\n"
    "        raise ValueError('slice step cannot be zero')\n"
    "    if c > 0:\n"
    "        lo = 0\n"
    "        hi = n\n"
    "    else:\n"
    "        lo = -1\n"
    "        hi = n - 1\n"
    "    if a is None:\n"
    "        if c > 0:\n"
    "            a = lo\n"
    "        else:\n"
    "            a = hi\n"
    "    else:\n"
    "        if a < 0:\n"
    "            a = a + n\n"
    "        a = min(max(a, lo), hi)\n"
    "    if b is None:\n"
    "        if c > 0:\n"
    "            b = hi\n"
    "        else:\n"
    "            b = lo\n"
    "    else:\n"
    "        if b < 0:\n"
    "            b = b + n\n"
    "        b = min(max(b, lo), hi)\n"
    "    r = []\n"
    "    i = a\n"
    "    while (c > 0 and i < b) or (c < 0 and i > b):\n"
    "        r.append(o[i])\n"
    "        i = i + c\n"
    "    if type(o) == str:\n"
    "        return ''.join(r)\n"
    "    if type(o) == tuple:\n"
    "        return tuple(r)\n"
    "    return r\n"
    /* x[a:b:c] = v et del x[a:b] (Core/py_rewrite.c) : modifie la liste */
    "def _oc_setslice(o, a, b, c, v):\n"
    "    if type(o) != list:\n"
    "        raise TypeError('object does not support slice assignment')\n"
    "    v = list(v)\n"
    "    n = len(o)\n"
    "    if c is None or c == 1:\n"
    "        if a is None:\n"
    "            a = 0\n"
    "        elif a < 0:\n"
    "            a = max(a + n, 0)\n"
    "        if b is None:\n"
    "            b = n\n"
    "        elif b < 0:\n"
    "            b = max(b + n, 0)\n"
    "        a = min(a, n)\n"
    "        b = min(max(b, a), n)\n"
    "        for i in range(b - a):\n"
    "            o.pop(a)\n"
    "        for i in range(len(v)):\n"
    "            o.insert(a + i, v[i])\n"
    "        return\n"
    "    idx = _oc_slice(list(range(n)), a, b, c)\n"
    "    if len(v) == 0:\n"
    "        for i in sorted(idx, reverse=True):\n"
    "            o.pop(i)\n"
    "        return\n"
    "    if len(v) != len(idx):\n"
    "        raise ValueError('attempt to assign sequence of size ' + str(len(v)) + ' to extended slice of size ' + str(len(idx)))\n"
    "    for k in range(len(idx)):\n"
    "        j = idx[k]\n" /* PikaPython ne sait pas affecter a o[idx[k]] */
    "        o[j] = v[k]\n"
    "def _oc_delitem(o, k):\n"
    "    if type(o) == list:\n"
    "        if k < 0:\n"
    "            k = k + len(o)\n"
    "        if k < 0 or k >= len(o):\n"
    "            raise IndexError('list assignment index out of range')\n"
    "        o.pop(k)\n"
    "    elif type(o) == dict:\n"
    "        if not k in o:\n"
    "            raise KeyError(k)\n"
    "        o.remove(k)\n"
    "    else:\n"
    "        raise TypeError('object does not support item deletion')\n"
    /* max/min avec key= et default=, sur tout iterable (range compris) */
    "def _oc_minmax(a, kw, big):\n"
    "    key = kw.get('key')\n"
    "    it = a\n"
    "    if len(a) == 1:\n"
    "        it = a[0]\n"
    "    first = True\n"
    "    best = None\n"
    "    bk = None\n"
    "    for v in it:\n"
    "        k = v\n"
    "        if key is not None:\n"
    "            k = key(v)\n"
    "        if first:\n"
    "            best = v\n"
    "            bk = k\n"
    "            first = False\n"
    "        elif big and k > bk:\n"
    "            best = v\n"
    "            bk = k\n"
    "        elif not big and k < bk:\n"
    "            best = v\n"
    "            bk = k\n"
    "    if first:\n"
    "        if 'default' in kw:\n"
    "            return kw['default']\n"
    "        raise ValueError('arg is an empty sequence')\n"
    "    return best\n"
    "def max(*a, **kw):\n"
    "    return _oc_minmax(a, kw, True)\n"
    "def min(*a, **kw):\n"
    "    return _oc_minmax(a, kw, False)\n"
    "def pow(a, b, m=None):\n"
    "    if m is None:\n"
    "        return a ** b\n"
    "    r = 1\n"
    "    a = a % m\n"
    "    while b > 0:\n"
    "        if b % 2 == 1:\n"
    "            r = r * a % m\n"
    "        a = a * a % m\n"
    "        b = b // 2\n"
    "    return r % m\n"
    "def divmod(a, b):\n"
    "    return (a // b, a % b)\n"
    "def _oc_base(n, b, p):\n"
    "    if n == 0:\n"
    "        return p + '0'\n"
    "    s = ''\n"
    "    neg = n < 0\n"
    "    if neg:\n"
    "        n = -n\n"
    "    while n > 0:\n"
    "        s = '0123456789abcdef'[n % b] + s\n"
    "        n = n // b\n"
    "    if neg:\n"
    "        return '-' + p + s\n"
    "    return p + s\n"
    "def bin(n):\n"
    "    return _oc_base(n, 2, '0b')\n"
    "def oct(n):\n"
    "    return _oc_base(n, 8, '0o')\n"
    "def hex(n):\n"
    "    return _oc_base(n, 16, '0x')\n"
    "def enumerate(it, start=0):\n"
    "    l = []\n"
    "    i = start\n"
    "    for v in it:\n"
    "        l.append((i, v))\n"
    "        i = i + 1\n"
    "    return l\n"
    "def zip(*its):\n"
    "    ls = []\n"
    "    for it in its:\n"
    "        l = []\n"
    "        for v in it:\n"
    "            l.append(v)\n"
    "        ls.append(l)\n"
    "    n = -1\n"
    "    for l in ls:\n"
    "        if n < 0 or len(l) < n:\n"
    "            n = len(l)\n"
    "    r = []\n"
    "    for i in range(max(n, 0)):\n"
    "        t = []\n"
    "        for l in ls:\n"
    "            t.append(l[i])\n"
    "        r.append(tuple(t))\n"
    "    return r\n"
    "def any(it):\n"
    "    for v in it:\n"
    "        if v:\n"
    "            return True\n"
    "    return False\n"
    "def all(it):\n"
    "    for v in it:\n"
    "        if not v:\n"
    "            return False\n"
    "    return True\n"
    "def map(f, *its):\n"
    "    if len(its) == 1:\n"
    "        r = []\n"
    "        for v in its[0]:\n"
    "            r.append(f(v))\n"
    "        return r\n"
    "    r = []\n"
    "    for t in zip(*its):\n"
    "        r.append(f(*t))\n"
    "    return r\n"
    "def filter(f, it):\n"
    "    r = []\n"
    "    for v in it:\n"
    "        if f is None:\n"
    "            if v:\n"
    "                r.append(v)\n"
    "        elif f(v):\n"
    "            r.append(v)\n"
    "    return r\n"
    "def reversed(it):\n"
    "    l = []\n"
    "    for v in it:\n"
    "        l.append(v)\n"
    "    l.reverse()\n"
    "    return l\n"
    "class set:\n"
    "    def __init__(self, it=None):\n"
    "        self._l = []\n"
    "        if it is not None:\n"
    "            for v in it:\n"
    "                self.add(v)\n"
    "    def add(self, v):\n"
    "        if not v in self._l:\n"
    "            self._l.append(v)\n"
    "    def remove(self, v):\n"
    "        self._l.remove(v)\n"
    "    def discard(self, v):\n"
    "        if v in self._l:\n"
    "            self._l.remove(v)\n"
    "    def __contains__(self, v):\n"
    "        return v in self._l\n"
    "    def __len__(self):\n"
    "        return len(self._l)\n"
    "    def __iter__(self):\n"
    "        self._i = 0\n"
    "        return self\n"
    "    def __next__(self):\n"
    "        if self._i >= len(self._l):\n"
    "            return None\n"
    "        self._i = self._i + 1\n"
    "        return self._l[self._i - 1]\n"
    "    def union(self, o):\n"
    "        r = set(self._l)\n"
    "        for v in o:\n"
    "            r.add(v)\n"
    "        return r\n"
    "    def intersection(self, o):\n"
    "        r = set()\n"
    "        for v in self._l:\n"
    "            if v in o:\n"
    "                r.add(v)\n"
    "        return r\n"
    "    def difference(self, o):\n"
    "        r = set()\n"
    "        for v in self._l:\n"
    "            if not v in o:\n"
    "                r.add(v)\n"
    "        return r\n"
    "    def __str__(self):\n"
    "        if len(self._l) == 0:\n"
    "            return 'set()'\n"
    "        s = ''\n"
    "        for v in self._l:\n"
    "            if s != '':\n"
    "                s = s + ', '\n"
    "            s = s + repr(v)\n"
    "        return '{' + s + '}'\n"
    /* math, random, time : fonctions manquantes */
    "def _oc_gcd(a, b):\n"
    "    a = abs(a)\n"
    "    b = abs(b)\n"
    "    while b:\n"
    "        a, b = b, a % b\n"
    "    return a\n"
    "def _oc_factorial(n):\n"
    "    if n < 0:\n"
    "        raise ValueError('factorial() not defined for negative values')\n"
    "    r = 1\n"
    "    for k in range(2, n + 1):\n"
    "        r = r * k\n"
    "    return r\n"
    "def _oc_comb(n, k):\n"
    "    if k < 0 or k > n:\n"
    "        return 0\n"
    "    r = 1\n"
    "    for i in range(1, min(k, n - k) + 1):\n"
    "        r = r * (n - i + 1) // i\n"
    "    return r\n"
    "def _oc_isqrt(n):\n"
    "    if n < 0:\n"
    "        raise ValueError('isqrt() argument must be nonnegative')\n"
    "    x = int(math.sqrt(n))\n"
    "    while x * x > n:\n"
    "        x = x - 1\n"
    "    while (x + 1) * (x + 1) <= n:\n"
    "        x = x + 1\n"
    "    return x\n"
    "def _oc_hypot(*c):\n"
    "    s = 0\n"
    "    for v in c:\n"
    "        s = s + v * v\n"
    "    return math.sqrt(s)\n"
    "def _oc_isclose(a, b, rel_tol=1e-09, abs_tol=0.0):\n"
    "    return abs(a - b) <= max(rel_tol * max(abs(a), abs(b)), abs_tol)\n"
    "math.gcd = _oc_gcd\n"
    "math.factorial = _oc_factorial\n"
    "math.comb = _oc_comb\n"
    "math.isqrt = _oc_isqrt\n"
    "math.hypot = _oc_hypot\n"
    "math.isclose = _oc_isclose\n"
    "math.inf = 1e308 * 10\n"
    "math.nan = math.inf - math.inf\n"
    /* sorted() s'appuie sur list.sort (C, stable, reverse= et key=). */
    "def sorted(it, reverse=False, key=None):\n"
    "    l = []\n"
    "    for v in it:\n"
    "        l.append(v)\n"
    "    l.sort(reverse=reverse, key=key)\n"
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
    "import time\n"
    "_oc_randint = random.randint\n"
    "def _oc_randrange(a, b=None, step=1):\n"
    "    if b is None:\n"
    "        b = a\n"
    "        a = 0\n"
    "    if step > 0:\n"
    "        n = (b - a + step - 1) // step\n"
    "    else:\n"
    "        n = (a - b - step - 1) // (-step)\n"
    "    if n <= 0:\n"
    "        raise ValueError('empty range for randrange()')\n"
    "    return a + step * _oc_randint(0, n - 1)\n"
    "def _oc_sample(pop, k):\n"
    "    l = []\n"
    "    for v in pop:\n"
    "        l.append(v)\n"
    "    if k < 0 or k > len(l):\n"
    "        raise ValueError('Sample larger than population')\n"
    "    for i in range(k):\n"
    "        j = _oc_randint(i, len(l) - 1)\n"
    "        l[i], l[j] = l[j], l[i]\n"
    "    return l[0:k]\n"
    "random.randrange = _oc_randrange\n"
    "random.sample = _oc_sample\n"
    "time.monotonic = time.time\n"
    /* Alias caches des modules : voir k_restore dans Core/pika_wrapper.c. */
    "_oc_m_math = math\n"
    "_oc_m_random = random\n"
    "_oc_m_turtle = turtle\n"
    "_oc_m_draw = draw\n"
    "_oc_m_matplotlib = matplotlib\n"
    "_oc_m_time = time\n";

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

/* round(x[, n]) comme Python : arrondi au pair (round(2.5) = 2), et avec n
 * chiffres l'arrondi correct de la valeur binaire exacte (round(2.675, 2) =
 * 2.67), via printf. L'ancienne version en Python arrondissait 2.5 a 3. */
static double oc_round_half_even(double x)
{
    double f = floor(x);
    double d = x - f;
    if (d > 0.5)
        return f + 1;
    if (d < 0.5)
        return f;
    return fmod(f, 2.0) == 0 ? f : f + 1;
}

static void m_round(PikaObj *self, Args *a)
{
    Arg *x = args_getArg(a, "x");
    PikaTuple *nt = args_getTuple(a, "n");
    int has_n = nt && pikaTuple_getSize(nt) > 0 &&
                arg_getType(pikaTuple_getArg(nt, 0)) != ARG_TYPE_NONE;
    int n = has_n ? (int)arg_getInt(pikaTuple_getArg(nt, 0)) : 0;
    ArgType t = x ? arg_getType(x) : ARG_TYPE_NONE;
    if (t == ARG_TYPE_INT || t == ARG_TYPE_BOOL)
    {
        int64_t v = t == ARG_TYPE_BOOL ? arg_getBool(x) : arg_getInt(x);
        if (n >= 0)
        {
            method_returnInt(a, v);
            return;
        }
        double p = pow(10.0, -n);
        method_returnInt(a, (int64_t)(oc_round_half_even((double)v / p) * p));
        return;
    }
    if (t != ARG_TYPE_FLOAT)
    {
        pyg_error(self, "TypeError: round() attend un nombre");
        return;
    }
    double v = arg_getFloat(x);
    if (!has_n)
    {
        if (!isfinite(v))
        {
            pyg_error(self, "OverflowError: cannot convert float infinity to integer");
            return;
        }
        method_returnInt(a, (int64_t)oc_round_half_even(v));
        return;
    }
    if (!isfinite(v))
    {
        method_returnFloat(a, v);
        return;
    }
    if (n >= 0 && n <= 30)
    {
        char buf[400];
        snprintf(buf, sizeof(buf), "%.*f", n, v);
        method_returnFloat(a, strtod(buf, NULL));
        return;
    }
    if (n > 30)
    {
        method_returnFloat(a, v);
        return;
    }
    double p = pow(10.0, -n);
    method_returnFloat(a, oc_round_half_even(v / p) * p);
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
    class_defineMethod(root, "round", "x,*n", m_round);
    obj_run(root, (char *)k_prelude);
    PikaObj *rnd = obj_getObj(root, "random");
    if (rnd != NULL)
    {
        class_defineMethod(rnd, "choice", "seq", m_choice);
        class_defineMethod(rnd, "shuffle", "l", m_shuffle);
    }
}
