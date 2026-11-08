/* Tests des modules Python graphiques turtle et matplotlib.pyplot, et de la
 * traduction de "from module import *". Les scripts tournent dans le vrai
 * interpreteur PikaPython ; le dessin se fait dans une image en memoire
 * (stubs/fb_draw.c) dont on verifie les pixels.
 *
 * FB_DUMP=prefixe ./test_pymods enregistre chaque ecran montre (PPM). */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "pika_wrapper.h"

extern "C"
{
    extern unsigned short fb_pixels[240][320];
    extern int fb_wait_count, fb_show_count, fb_text_count;
    extern char fb_last_text[128];
    void fb_reset(void);
}

static int failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        failures++;
}

/* Lance un script (comme l'app Python) et renvoie sa sortie texte. */
static std::string run(const char *script)
{
    fb_reset();
    pika_draw_reset();
    pika_run(script);
    return std::string(pika_get_output());
}

static bool has(const std::string &s, const char *needle)
{
    return s.find(needle) != std::string::npos;
}

static int count_color(unsigned short c, int x0 = 0, int y0 = 0, int x1 = 319, int y1 = 239)
{
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            n += fb_pixels[y][x] == c;
    return n;
}

static const unsigned short RED = 0xF800, BLUE = 0x001F, WHITE = 0xFFFF, BLACK = 0x0000;
static const unsigned short C0 = ((0x1F >> 3) << 11) | ((0x77 >> 2) << 5) | (0xB4 >> 3); /* bleu matplotlib */

static void test_turtle(void)
{
    std::string out;

    out = run("import turtle\nturtle.forward(100)\nprint(turtle.position())\n");
    check(has(out, "100.0"), "turtle: forward(100) moves to x=100");
    check(fb_pixels[120][160] == BLACK && fb_pixels[120][210] == BLACK && fb_pixels[120][260] == BLACK,
          "turtle: forward draws a black segment from the center to the right");
    check(fb_pixels[120][270] == WHITE && fb_pixels[110][210] == WHITE, "turtle: nothing drawn elsewhere");
    check(pika_draw_used(), "turtle: the app shows the drawing at the end of the script");

    out = run("from turtle import *\nleft(90)\nfd(50)\nprint(round(xcor()), round(ycor()), heading())\n");
    check(has(out, "0 50 90"), "turtle: from turtle import * with left/fd aliases");
    check(fb_pixels[95][160] == BLACK, "turtle: up is towards the top of the screen");

    out = run("from turtle import *\npenup()\nforward(50)\npendown()\nprint(isdown())\n");
    check(count_color(BLACK) == 0 && has(out, "True"), "turtle: penup moves without drawing");

    out = run("from turtle import *\nfor i in range(4):\n    forward(60)\n    left(90)\n"
              "print(round(xcor()), round(ycor()), round(heading()))\n");
    check(has(out, "0 0 0"), "turtle: a square brings the turtle back home");

    out = run("from turtle import *\ncircle(50)\nx, y = pos()\nprint(round(x), round(y))\n");
    check(has(out, "0 0"), "turtle: circle() comes back to its starting point");
    check(fb_pixels[20][160] == BLACK || fb_pixels[21][160] == BLACK || fb_pixels[19][160] == BLACK,
          "turtle: circle(50) reaches the top of the circle (center 50 px above)");

    run("from turtle import *\ncolor('red', 'blue')\nbegin_fill()\nfor i in range(4):\n    forward(40)\n"
        "    left(90)\nend_fill()\n");
    check(fb_pixels[100][180] == BLUE, "turtle: begin_fill/end_fill fills the inside with the fill color");
    check(fb_pixels[120][180] == RED, "turtle: the outline keeps the pen color");

    run("import turtle\nt = turtle.Turtle()\nt.pensize(5)\nt.pencolor(1, 0, 0)\nt.forward(40)\n");
    check(fb_pixels[122][180] == RED && fb_pixels[118][180] == RED, "turtle: Turtle() object and pensize(5)");

    run("import turtle\nturtle.colormode(255)\nturtle.color((0, 0, 255))\nturtle.dot(20)\n");
    check(fb_pixels[120][160] == BLUE && fb_pixels[120][168] == BLUE, "turtle: colormode(255) and dot(20)");

    out = run("import turtle\nturtle.color('#ff0000')\nturtle.forward(10)\nturtle.done()\nprint('after')\n");
    check(fb_wait_count == 1 && !pika_draw_used() && has(out, "after"),
          "turtle: done() shows the drawing once (no second wait in the app)");

    run("import turtle\nturtle.write('Hello')\n");
    check(fb_text_count == 1 && !std::strcmp(fb_last_text, "Hello"), "turtle: write()");

    out = run("import turtle\nturtle.color('bleu')\nprint('not reached')\n");
    check(has(out, "couleur inconnue") && !has(out, "not reached"), "turtle: unknown color raises an error");

    out = run("import turtle\nturtle.forward('a')\nprint('not reached')\n");
    check(has(out, "TypeError") && !has(out, "not reached"), "turtle: forward('a') raises TypeError");

    out = run("import turtle\nturtle.radians()\nturtle.left(math.pi)\nprint(round(turtle.heading(), 4))\n");
    check(has(out, "3.1416"), "turtle: radians()");

    /* Etat remis a zero entre deux scripts. */
    run("import turtle\nturtle.color('red')\nturtle.penup()\nturtle.goto(50, 50)\n");
    out = run("import turtle\nprint(turtle.position(), turtle.isdown())\nturtle.forward(10)\n");
    check(has(out, "(0.0") && has(out, "True") && fb_pixels[120][165] == BLACK,
          "turtle: state is reset before each script");
}

static void test_pyplot(void)
{
    std::string out;

    out = run("import matplotlib.pyplot as plt\nplt.plot([0, 1, 2], [0, 1, 0])\nplt.show()\nprint('ok')\n");
    check(has(out, "ok") && fb_wait_count == 1, "pyplot: show() waits for a key");
    check(count_color(C0) > 50, "pyplot: the curve is drawn in the default blue");
    check(!pika_draw_used(), "pyplot: no second wait after show()");

    out = run("import matplotlib.pyplot as plt\nplt.plot([1, 2, 3])\n");
    check(fb_wait_count == 0 && pika_draw_used() && count_color(C0) > 50,
          "pyplot: a figure without show() is shown at the end of the script");

    run("from matplotlib.pyplot import *\nplot([0, 10], [0, 10], 'r')\naxis('off')\nshow()\n");
    check(count_color(RED) > 100, "pyplot: from matplotlib.pyplot import *, 'r' format");
    /* Sans axes, la droite va (presque) d'un coin a l'autre : marges de 5 %. */
    check(fb_pixels[239 - 2 - 11][2 + 16] == RED || fb_pixels[239 - 2 - 11][2 + 15] == RED ||
              fb_pixels[239 - 2 - 12][2 + 16] == RED || fb_pixels[239 - 2 - 10][2 + 16] == RED,
          "pyplot: autoscale with 5% margins");

    run("import matplotlib.pyplot as plt\nplt.plot([0, 1], [0, 0], 'k')\nplt.axis([0, 1, -1, 1])\n"
        "plt.axis('off')\nplt.show()\n");
    check(count_color(BLACK, 0, 118, 319, 122) > 250, "pyplot: axis([xmin, xmax, ymin, ymax])");

    run("import matplotlib.pyplot as plt\nplt.scatter([1, 2, 3], [1, 2, 3], color='red')\nplt.show()\n");
    check(count_color(RED) >= 3 * 9, "pyplot: scatter draws one disc per point");

    out = run("import matplotlib.pyplot as plt\nc, b = plt.hist([1, 2, 2, 3, 3, 3], bins=3)\nprint(c)\n"
              "print(len(b))\nplt.show()\n");
    check(has(out, "[1, 2, 3]") && has(out, "4"), "pyplot: hist() returns counts and bin edges");
    check(count_color(C0) > 1000, "pyplot: hist() draws bars");

    out = run("import matplotlib.pyplot as plt\nplt.bar(['A', 'B'], [2, 4], color='blue')\nplt.show()\n");
    check(count_color(BLUE) > 1000, "pyplot: bar() with category names");

    out = run("import matplotlib.pyplot as plt\nplt.plot([1, 2], [1])\nprint('not reached')\n");
    check(has(out, "meme longueur") && !has(out, "not reached"), "pyplot: x/y length mismatch raises an error");

    out = run("import matplotlib.pyplot as plt\nplt.plot([1, 2], [1, 2], color='zzz')\nprint('not reached')\n");
    check(has(out, "couleur inconnue") && !has(out, "not reached"), "pyplot: unknown color raises an error");

    out = run("import matplotlib.pyplot as plt\nplt.plot([0, 4], [1, 3])\nprint(plt.axis())\nplt.show()\n");
    check(has(out, "(-0.2") && has(out, "4.2") && has(out, "0.9") && has(out, "3.1"),
          "pyplot: axis() returns the bounds with 5% margins");

    run("import matplotlib.pyplot as plt\nplt.plot([0, 1], [0, 0], 'k--')\nplt.axis('off')\nplt.show()\n");
    int black = count_color(BLACK, 0, 115, 319, 125);
    check(black > 100 && black < 250, "pyplot: '--' gives a dashed line");

    run("import matplotlib.pyplot as plt\nplt.plot([0, 1], [0, 1])\nplt.title('T')\nplt.grid(True)\n"
        "plt.xlabel('x')\nplt.ylabel('y')\nplt.text(0.5, 0.5, 'P')\nplt.arrow(0, 1, 0.5, -0.5)\nplt.show()\n");
    check(fb_text_count > 8, "pyplot: title, labels, text and tick labels");

    /* La figure est videe par show() et entre deux scripts. */
    run("import matplotlib.pyplot as plt\nplt.plot([0, 1], [0, 1], 'r')\n");
    run("import matplotlib.pyplot as plt\nplt.plot([0, 1], [0, 1], 'b')\nplt.show()\n");
    check(count_color(RED) == 0, "pyplot: the figure is cleared between scripts");
}

static void test_imports(void)
{
    std::string out;

    out = run("from math import *\nprint(sqrt(16), floor(pi))\n");
    check(has(out, "4.0") && has(out, "3"), "import *: math");

    out = run("from random import *\nseed(1)\nl = [1, 2, 3, 4]\nshuffle(l)\n"
              "print(sorted(l), choice((7,)), random() < 1, randint(5, 5))\n");
    check(has(out, "[1, 2, 3, 4] 7 True 5"), "import *: random, with choice() and shuffle()");

    out = run("import random\nprint(random.choice('a'), random.randint(2, 2))\n");
    check(has(out, "a 2"), "random module still usable after from random import *");

    run("turtle = 1\nmath = 2\nrandom = 3\n");
    out = run("import turtle\nturtle.forward(1)\nprint(math.sqrt(4), random.randint(1, 1))\n");
    check(has(out, "2.0") && has(out, "1"), "modules are restored when a script overwrote their names");

    /* PikaPython donne le numero de ligne des erreurs de syntaxe. */
    out = run("from turtle import *\nx = 1\nforward(1 +)\n");
    check(has(out, "[3]"), "import * keeps line numbers in error messages");

    out = run("from turtle import *  # comment\nprint('ok')\n");
    check(has(out, "ok"), "import * followed by a comment");

    out = run("if True:\n    from math import *\n    print(cos(0))\n");
    check(has(out, "1.0"), "indented import *");

    out = run("from draw import *\nfill_rect(0, 0, 4, 4, RED)\nprint('ok')\n");
    check(has(out, "ok") && fb_pixels[1][1] == RED, "import *: draw");

    out = run("from inconnu import *\n");
    check(has(out, "SyntaxError"), "import * of an unknown module is left untouched");
}

int main(void)
{
    pika_init();
    test_turtle();
    test_pyplot();
    test_imports();
    if (failures)
    {
        std::printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
    std::printf("ALL TESTS PASSED\n");
    return 0;
}
