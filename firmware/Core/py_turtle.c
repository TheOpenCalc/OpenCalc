/* Module Python "turtle" (graphisme tortue), compatible avec les scripts
 * des manuels de lycee et avec le module turtle de CPython / NumWorks.
 *
 * Repere : origine au centre de l'ecran (320x240), x vers la droite, y vers
 * le haut, cap 0 = vers la droite, angles positifs dans le sens direct.
 * Une seule tortue : turtle.Turtle() et turtle.Screen() renvoient le module
 * (voir le prelude de Core/py_draw.c). La tortue elle-meme n'est pas
 * dessinee. */
#include "py_gfx.h"
#include "pika_wrapper.h"
#include "TinyObj.h"
#include "builtins.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FILL_MAX 2048

typedef struct
{
    double x, y;    /* position, repere de la tortue */
    double heading; /* en degres */
    double full;    /* 360 (degres) ou 2*pi (radians) */
    int down;
    int width;
    int pen;
    int fill;
    int bg;
    int speed;
    int visible;
    int scale255;
    int filling;
    int nfill;
    float *fx, *fy; /* sommets du remplissage (coordonnees ecran) */
} turtle_state;

static turtle_state T;

static double sx(double x) { return PYG_W / 2 + x; }
static double sy(double y) { return PYG_H / 2 - y; }

static void fill_free(void)
{
    free(T.fx);
    free(T.fy);
    T.fx = T.fy = NULL;
    T.nfill = 0;
    T.filling = 0;
}

static void defaults(void)
{
    fill_free();
    T.x = T.y = 0.0;
    T.heading = 0.0;
    T.full = 360.0;
    T.down = 1;
    T.width = 1;
    T.pen = PYG_BLACK;
    T.fill = PYG_BLACK;
    T.bg = PYG_WHITE;
    T.speed = 3;
    T.visible = 1;
    T.scale255 = 0;
}

void py_turtle_reset(void)
{
    defaults();
}

/* ---------- Arguments ---------- */

static PikaTuple *targs(Args *a) { return args_getTuple(a, "a"); }
#define nargs pyg_nargs
#define argi pyg_arg
#define kwarg pyg_kw

/* Premier argument numerique obligatoire. */
static int need_num(PikaObj *self, Args *a, const char *fn, double *v)
{
    if (!pyg_tuple_num(targs(a), 0, v))
    {
        char msg[80];
        snprintf(msg, sizeof(msg), "TypeError: %s() attend un nombre", fn);
        pyg_error(self, msg);
        return 0;
    }
    return 1;
}

/* Point (x, y) donne par deux nombres ou par un tuple. */
static int need_point(PikaObj *self, Args *a, const char *fn, double *x, double *y)
{
    if (nargs(a) >= 2 && pyg_tuple_num(targs(a), 0, x) && pyg_tuple_num(targs(a), 1, y))
        return 1;
    double *v = NULL;
    int n = nargs(a) == 1 ? pyg_seq(argi(a, 0), &v) : -1;
    if (n == 2)
    {
        *x = v[0];
        *y = v[1];
        free(v);
        return 1;
    }
    free(v);
    char msg[80];
    snprintf(msg, sizeof(msg), "TypeError: %s() attend x, y", fn);
    pyg_error(self, msg);
    return 0;
}

/* Couleur donnee par les arguments first.. : nom, tuple ou r, g, b. */
static int color_args(Args *a, int first, int *out)
{
    int n = nargs(a) - first;
    double r, g, b;
    if (n >= 3 && pyg_tuple_num(targs(a), first, &r) && pyg_tuple_num(targs(a), first + 1, &g) &&
        pyg_tuple_num(targs(a), first + 2, &b))
    {
        *out = pyg_rgb(r, g, b, T.scale255);
        return 1;
    }
    if (n >= 1)
        return pyg_color(argi(a, first), T.scale255, out);
    return 0;
}

static void bad_color(PikaObj *self)
{
    pyg_error(self, "ValueError: couleur inconnue (ex. 'red', (1, 0, 0), '#ff0000')");
}

static double to_deg(double v) { return v * 360.0 / T.full; }
static double from_deg(double v) { return v * T.full / 360.0; }

static void ret_float(Args *a, double v) { method_returnFloat(a, (pika_float)v); }

/* ---------- Mouvement ---------- */

static void fill_add(double x, double y)
{
    if (!T.filling || T.fx == NULL || T.nfill >= FILL_MAX)
        return;
    T.fx[T.nfill] = (float)sx(x);
    T.fy[T.nfill] = (float)sy(y);
    T.nfill++;
}

static void move_to(double nx, double ny)
{
    pyg_begin();
    if (T.down)
        pyg_line(sx(T.x), sy(T.y), sx(nx), sy(ny), T.pen, T.width, 0);
    T.x = nx;
    T.y = ny;
    fill_add(nx, ny);
    if (T.speed != 0)
        pyg_flush_throttled();
}

static void go(double d)
{
    double h = T.heading * M_PI / 180.0;
    move_to(T.x + d * cos(h), T.y + d * sin(h));
}

static void rotate(double deg)
{
    T.heading = fmod(T.heading + deg, 360.0);
    if (T.heading < 0)
        T.heading += 360.0;
}

static void m_forward(PikaObj *self, Args *a)
{
    double d;
    if (need_num(self, a, "forward", &d))
        go(d);
}

static void m_backward(PikaObj *self, Args *a)
{
    double d;
    if (need_num(self, a, "backward", &d))
        go(-d);
}

static void m_left(PikaObj *self, Args *a)
{
    double d;
    if (need_num(self, a, "left", &d))
        rotate(to_deg(d));
}

static void m_right(PikaObj *self, Args *a)
{
    double d;
    if (need_num(self, a, "right", &d))
        rotate(-to_deg(d));
}

static void m_goto(PikaObj *self, Args *a)
{
    double x, y;
    if (need_point(self, a, "goto", &x, &y))
        move_to(x, y);
}

static void m_setx(PikaObj *self, Args *a)
{
    double x;
    if (need_num(self, a, "setx", &x))
        move_to(x, T.y);
}

static void m_sety(PikaObj *self, Args *a)
{
    double y;
    if (need_num(self, a, "sety", &y))
        move_to(T.x, y);
}

static void m_setheading(PikaObj *self, Args *a)
{
    double h;
    if (need_num(self, a, "setheading", &h))
    {
        T.heading = 0.0;
        rotate(to_deg(h));
    }
}

static void m_home(PikaObj *self, Args *a)
{
    move_to(0.0, 0.0);
    T.heading = 0.0;
}

/* Meme decoupage en segments que CPython : les figures sont identiques. */
static void m_circle(PikaObj *self, Args *a)
{
    double r, extent = T.full, steps_d = 0;
    if (!need_num(self, a, "circle", &r))
        return;
    Arg *e = argi(a, 1) ? argi(a, 1) : kwarg(a, "extent");
    if (e && arg_getType(e) != ARG_TYPE_NONE && !pyg_num(e, &extent))
    {
        pyg_error(self, "TypeError: circle() : extent doit etre un nombre");
        return;
    }
    Arg *s = argi(a, 2) ? argi(a, 2) : kwarg(a, "steps");
    if (s && arg_getType(s) != ARG_TYPE_NONE)
        pyg_num(s, &steps_d);
    double ext = to_deg(extent);
    int steps = (int)steps_d;
    if (steps <= 0)
    {
        double frac = fabs(ext) / 360.0;
        double m = 11.0 + fabs(r) / 6.0;
        if (m > 59.0)
            m = 59.0;
        steps = 1 + (int)(m * frac);
    }
    if (steps > 720)
        steps = 720;
    double w = ext / steps, w2 = 0.5 * w;
    double l = 2.0 * r * sin(w2 * M_PI / 180.0);
    if (r < 0)
    {
        l = -l;
        w = -w;
        w2 = -w2;
    }
    rotate(w2);
    for (int i = 0; i < steps; i++)
    {
        go(l);
        rotate(w);
    }
    rotate(-w2);
}

static void m_dot(PikaObj *self, Args *a)
{
    double size = T.width + 4 > 2 * T.width ? T.width + 4 : 2 * T.width;
    int first = 0, color = T.pen;
    if (argi(a, 0) && pyg_num(argi(a, 0), &size))
        first = 1;
    else if (argi(a, 0) && arg_getType(argi(a, 0)) == ARG_TYPE_NONE)
        first = 1;
    if (nargs(a) > first && !color_args(a, first, &color))
    {
        bad_color(self);
        return;
    }
    pyg_begin();
    pyg_disc(sx(T.x), sy(T.y), size / 2.0, color);
    if (T.speed != 0)
        pyg_flush_throttled();
}

/* ---------- Etat ---------- */

static void m_position(PikaObj *self, Args *a)
{
    method_returnArg(a, arg_newObj(New_PikaTupleFromVarArgs(arg_newFloat(T.x), arg_newFloat(T.y))));
}

static void m_xcor(PikaObj *self, Args *a) { ret_float(a, T.x); }
static void m_ycor(PikaObj *self, Args *a) { ret_float(a, T.y); }
static void m_heading(PikaObj *self, Args *a) { ret_float(a, from_deg(T.heading)); }

static void m_distance(PikaObj *self, Args *a)
{
    double x, y;
    if (need_point(self, a, "distance", &x, &y))
        ret_float(a, hypot(x - T.x, y - T.y));
}

static void m_towards(PikaObj *self, Args *a)
{
    double x, y;
    if (!need_point(self, a, "towards", &x, &y))
        return;
    double d = atan2(y - T.y, x - T.x) * 180.0 / M_PI;
    if (d < 0)
        d += 360.0;
    ret_float(a, from_deg(d));
}

static void m_penup(PikaObj *self, Args *a) { T.down = 0; }
static void m_pendown(PikaObj *self, Args *a) { T.down = 1; }
static void m_isdown(PikaObj *self, Args *a) { method_returnBool(a, T.down ? pika_true : pika_false); }

static void m_pensize(PikaObj *self, Args *a)
{
    double w;
    if (nargs(a) == 0)
    {
        method_returnInt(a, T.width);
        return;
    }
    if (!need_num(self, a, "pensize", &w))
        return;
    T.width = w < 1 ? 1 : (w > 40 ? 40 : (int)lround(w));
}

static void m_pencolor(PikaObj *self, Args *a)
{
    if (nargs(a) == 0)
        return;
    if (!color_args(a, 0, &T.pen))
        bad_color(self);
}

static void m_fillcolor(PikaObj *self, Args *a)
{
    if (nargs(a) == 0)
        return;
    if (!color_args(a, 0, &T.fill))
        bad_color(self);
}

/* color(c), color(r, g, b) ou color(trait, remplissage). */
static void m_color(PikaObj *self, Args *a)
{
    int n = nargs(a);
    if (n == 0)
        return;
    if (n == 2)
    {
        int p, f;
        if (!pyg_color(argi(a, 0), T.scale255, &p) || !pyg_color(argi(a, 1), T.scale255, &f))
        {
            bad_color(self);
            return;
        }
        T.pen = p;
        T.fill = f;
        return;
    }
    int c;
    if (!color_args(a, 0, &c))
    {
        bad_color(self);
        return;
    }
    T.pen = T.fill = c;
}

static void m_begin_fill(PikaObj *self, Args *a)
{
    fill_free();
    T.fx = (float *)malloc(sizeof(float) * FILL_MAX);
    T.fy = (float *)malloc(sizeof(float) * FILL_MAX);
    if (T.fx == NULL || T.fy == NULL)
    {
        fill_free();
        pyg_error(self, "MemoryError: begin_fill()");
        return;
    }
    T.filling = 1;
    fill_add(T.x, T.y);
}

static void m_end_fill(PikaObj *self, Args *a)
{
    if (T.filling && T.nfill >= 3)
    {
        pyg_begin();
        pyg_fill_poly(T.fx, T.fy, T.nfill, T.fill);
        /* Le contour est redessine par-dessus le remplissage. */
        if (T.down)
            for (int i = 0; i + 1 < T.nfill; i++)
                pyg_line(T.fx[i], T.fy[i], T.fx[i + 1], T.fy[i + 1], T.pen, T.width, 0);
        if (T.speed != 0)
            pyg_flush_throttled();
    }
    fill_free();
}

static void m_filling(PikaObj *self, Args *a) { method_returnBool(a, T.filling ? pika_true : pika_false); }

static void m_speed(PikaObj *self, Args *a)
{
    Arg *s = argi(a, 0);
    if (s == NULL)
    {
        method_returnInt(a, T.speed);
        return;
    }
    double v;
    if (pyg_num(s, &v))
    {
        T.speed = (v < 0.5 || v > 10.5) ? 0 : (int)lround(v);
        return;
    }
    if (arg_getType(s) == ARG_TYPE_STRING)
    {
        const char *n = arg_getStr(s);
        T.speed = !strcmp(n, "fastest") ? 0 : !strcmp(n, "fast") ? 10 : !strcmp(n, "normal") ? 6
                                         : !strcmp(n, "slow")   ? 3
                                                                : 1;
    }
}

/* tracer(0) : dessin instantane, comme speed(0). */
static void m_tracer(PikaObj *self, Args *a)
{
    double v;
    if (pyg_tuple_num(targs(a), 0, &v))
        T.speed = v == 0 ? 0 : 6;
}

static void m_update(PikaObj *self, Args *a)
{
    if (pika_draw_used())
        oc_draw_show();
}

static void m_hideturtle(PikaObj *self, Args *a) { T.visible = 0; }
static void m_showturtle(PikaObj *self, Args *a) { T.visible = 1; }
static void m_isvisible(PikaObj *self, Args *a) { method_returnBool(a, T.visible ? pika_true : pika_false); }
static void m_nop(PikaObj *self, Args *a) {}

static void m_colormode(PikaObj *self, Args *a)
{
    double v;
    if (!pyg_tuple_num(targs(a), 0, &v))
    {
        method_returnInt(a, T.scale255 ? 255 : 1);
        return;
    }
    T.scale255 = v > 1.5;
}

static void m_degrees(PikaObj *self, Args *a)
{
    double v = 360.0;
    pyg_tuple_num(targs(a), 0, &v);
    T.full = v > 0 ? v : 360.0;
}

static void m_radians(PikaObj *self, Args *a) { T.full = 2.0 * M_PI; }

static void m_bgcolor(PikaObj *self, Args *a)
{
    if (nargs(a) == 0)
        return;
    int c;
    if (!color_args(a, 0, &c))
    {
        bad_color(self);
        return;
    }
    T.bg = c;
    pyg_begin();
    oc_draw_clear(c);
}

static void m_clear(PikaObj *self, Args *a)
{
    pyg_begin();
    oc_draw_clear(T.bg);
}

static void m_reset(PikaObj *self, Args *a)
{
    int bg = T.bg;
    defaults();
    T.bg = bg;
    pyg_begin();
    oc_draw_clear(bg);
}

/* write(texte, move=False, align='left', font=(nom, taille, style)) */
static void m_write(PikaObj *self, Args *a)
{
    Arg *v = argi(a, 0);
    if (v == NULL)
        return;
    const char *s = arg_getType(v) == ARG_TYPE_STRING ? arg_getStr(v) : builtins_str(self, v);
    if (s == NULL)
        return;
    Arg *mv = argi(a, 1) ? argi(a, 1) : kwarg(a, "move");
    Arg *al = argi(a, 2) ? argi(a, 2) : kwarg(a, "align");
    Arg *ft = argi(a, 3) ? argi(a, 3) : kwarg(a, "font");
    int size = 1;
    if (ft && arg_isTuple(ft) && pikaTuple_getSize(arg_getObj(ft)) >= 2)
    {
        double fs;
        if (pyg_num(pikaTuple_getArg(arg_getObj(ft), 1), &fs) && fs >= 14)
            size = 2;
    }
    double w = pyg_text_w(s, size), x = sx(T.x);
    if (al && arg_getType(al) == ARG_TYPE_STRING)
    {
        if (!strcmp(arg_getStr(al), "center"))
            x -= w / 2;
        else if (!strcmp(arg_getStr(al), "right"))
            x -= w;
    }
    pyg_begin();
    pyg_text(x, sy(T.y) - 8 * size, s, T.pen, size);
    double m;
    if (mv && pyg_num(mv, &m) && m != 0)
    {
        int d = T.down;
        T.down = 0;
        move_to(T.x + w, T.y);
        T.down = d;
    }
    if (T.speed != 0)
        pyg_flush_throttled();
}

/* done(), mainloop(), exitonclick() : montre le dessin et attend une touche. */
static void m_done(PikaObj *self, Args *a)
{
    if (pika_draw_used())
        pyg_show_and_wait();
}

typedef struct
{
    const char *name;
    void (*fn)(PikaObj *, Args *);
} turtle_method;

static const turtle_method k_methods[] = {
    {"forward", m_forward},
    {"fd", m_forward},
    {"backward", m_backward},
    {"back", m_backward},
    {"bk", m_backward},
    {"left", m_left},
    {"lt", m_left},
    {"right", m_right},
    {"rt", m_right},
    {"goto", m_goto},
    {"setpos", m_goto},
    {"setposition", m_goto},
    {"teleport", m_goto},
    {"setx", m_setx},
    {"sety", m_sety},
    {"setheading", m_setheading},
    {"seth", m_setheading},
    {"home", m_home},
    {"circle", m_circle},
    {"dot", m_dot},
    {"position", m_position},
    {"pos", m_position},
    {"xcor", m_xcor},
    {"ycor", m_ycor},
    {"heading", m_heading},
    {"distance", m_distance},
    {"towards", m_towards},
    {"penup", m_penup},
    {"pu", m_penup},
    {"up", m_penup},
    {"pendown", m_pendown},
    {"pd", m_pendown},
    {"down", m_pendown},
    {"isdown", m_isdown},
    {"pensize", m_pensize},
    {"width", m_pensize},
    {"pencolor", m_pencolor},
    {"fillcolor", m_fillcolor},
    {"color", m_color},
    {"begin_fill", m_begin_fill},
    {"end_fill", m_end_fill},
    {"filling", m_filling},
    {"speed", m_speed},
    {"tracer", m_tracer},
    {"update", m_update},
    {"hideturtle", m_hideturtle},
    {"ht", m_hideturtle},
    {"showturtle", m_showturtle},
    {"st", m_showturtle},
    {"isvisible", m_isvisible},
    {"shape", m_nop},
    {"title", m_nop},
    {"setup", m_nop},
    {"screensize", m_nop},
    {"delay", m_nop},
    {"colormode", m_colormode},
    {"degrees", m_degrees},
    {"radians", m_radians},
    {"bgcolor", m_bgcolor},
    {"clear", m_clear},
    {"reset", m_reset},
    {"clearscreen", m_reset},
    {"write", m_write},
    {"done", m_done},
    {"mainloop", m_done},
    {"exitonclick", m_done},
    {"bye", m_nop},
};

#define N_METHODS (sizeof(k_methods) / sizeof(k_methods[0]))

const char *py_turtle_name(int i)
{
    return i >= 0 && i < (int)N_METHODS ? k_methods[i].name : NULL;
}

void py_turtle_install(PikaObj *root)
{
    defaults();
    PikaObj *t = newNormalObj(New_TinyObj);
    for (size_t i = 0; i < N_METHODS; i++)
        class_defineMethod(t, (char *)k_methods[i].name, "*a,**kw", k_methods[i].fn);
    obj_setObj(root, "turtle", t);
}
