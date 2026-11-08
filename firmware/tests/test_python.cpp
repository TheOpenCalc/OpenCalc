#include <cstdio>
#include <cstring>
#include <string>
#include "pika_wrapper.h"

int python_app(void);
void pyhost_send_char(int c);
void pyhost_send_key(int k);
void pyhost_drain(void);

#include "ui.h" /* stubs/ui.h: same key codes as the python app under test */

static int failures = 0;

static void expect(const std::string &out, const char *needle, const char *what)
{
    if (out.find(needle) == std::string::npos) {
        std::printf("FAIL %s: output does not contain \"%s\"\n", what, needle);
        failures++;
    } else {
        std::printf("PASS %s\n", what);
    }
}

static void run_editor_test(const char *script, int iterations, const char *needle,
                            const char *what)
{
    pyhost_drain();
    pyhost_send_key(SECOND); /* create an empty file from the Files view */
    pyhost_send_key(OK);
    for (const char *p = script; *p; p++)
        pyhost_send_char((unsigned char)*p);
    pyhost_send_key(SECOND);
    pyhost_send_key(OK);
    for (int i = 0; i < iterations; i++)
        pyhost_send_key(BACK);

    python_app();

    const char *out = pika_get_output();
    expect(std::string(out), needle, what);
}

int main(void)
{
    run_editor_test("print(1 + 2)\n", 400, "3", "print addition");
    run_editor_test("for i in range(3):\n    print(i, i*i)\n", 400, "1 1", "for loop with square");
    run_editor_test("print(math.sqrt(2))\n", 400, "1.414", "math.sqrt via pre-imported module");
    run_editor_test("print(2 ** 8)\n", 400, "256", "power operator");

    run_editor_test("print(sum([1, 2, 3, 4]))\n", 400, "10", "sum() builtin");
    run_editor_test("print(round(2.567, 2), round(-2.5))\n", 400, "2.57", "round() builtin");
    run_editor_test("print(sorted([3, 1, 2]), sorted([1, 3, 2], True))\n", 400, "[1, 2, 3] [3, 2, 1]", "sorted() builtin");

    {
        extern char g_draw_log[];
        g_draw_log[0] = '\0';
        run_editor_test("import draw\ndraw.fill_rect(1, 2, 3, 4, draw.RED)\n"
                        "draw.line(0, 0, 10, 5, draw.color(0, 0, 255))\n"
                        "draw.draw_string('Hi', 5, 6, 0)\nprint('ok')\n",
                        400, "ok", "draw module script runs");
        std::string log(g_draw_log);
        expect(log, "begin;rect 1 2 3 4 63488;line 0 0 10 5 31;text Hi 5 6 0", "draw primitives called");
    }

    if (failures == 0)
        std::printf("ALL TESTS PASSED\n");
    else
        std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
