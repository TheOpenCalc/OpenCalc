// Host regression test for the in-place text editor (update_fill_box in
// Core/widgets.cpp). It exercises the overflow / cursor-desync fixes (C1/C2),
// the template insertion (M4), the insertion cap and the context-aware comma
// key for binom()/perm(). Build with `make test_editor` (optionally ASan via
// `make test_editor_asan`).
#include "widgets.h"
#include "ui.h"
#include <cstdio>
#include <cstring>

// --- Minimal host stubs for symbols referenced by widgets.cpp ------------
uint16_t g_cursor_bg = 0;
int scan_keypad() { return -1; }
int scan_text_char() { return -1; }
void fill_rect(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t) {}
void fill_screen(uint16_t) {}
void flush_display() {}
void draw_char(uint16_t, uint16_t, char *, uint16_t, uint16_t, uint8_t) {}
int utf8_glyph_count(const char *s, int n)
{
    int c = 0;
    for (int i = 0; i < n && s[i]; i++)
        c += ((unsigned char)s[i] & 0xC0) != 0x80;
    return c;
}
int matrix_parse_literal(const char *, int, int, matrix_lit *) { return 0; }
void display_equation(char *, int, int, int, int, int, bool, int, int, int) {}
bool display_get_cursor(int *) { return false; }
int min(int a, int b) { return a < b ? a : b; }
int max(int a, int b) { return a > b ? a : b; }

static int fails = 0;
#define CHECK(cond, msg)                                          \
    do {                                                          \
        if (!(cond)) {                                            \
            printf("FAIL: %s (text=\"%s\" t_size=%d curso=%d)\n", \
                   msg, fb ? fb->text : "", fb ? fb->t_size : -1, \
                   fb ? fb->curso_pos : -1);                      \
            fails++;                                              \
        } else {                                                  \
            printf("OK  : %s\n", msg);                            \
        }                                                         \
    } while (0)

static void clear(fill_box *fb)
{
    fb->curso_pos = fb->t_size;
    while (fb->t_size > 0)
        update_fill_box(fb, BACK, false);
}

// "1+binom(5,2)+3" with the cursor at the end.
static void build_binom_expr(fill_box *fb)
{
    clear(fb);
    update_fill_box(fb, ONE, false);
    update_fill_box(fb, PLUS, false);
    update_fill_box(fb, FUNC_BINOM, false);
    update_fill_box(fb, FIVE, false);
    update_fill_box(fb, COMA, false);
    update_fill_box(fb, TWO, false);
    fb->curso_pos = fb->t_size;
    update_fill_box(fb, PLUS, false);
    update_fill_box(fb, THREE, false);
}

int main()
{
    fill_box *fb = create_fill_box(0, 0, 10, 100, 0);

    update_fill_box(fb, ONE, false);
    update_fill_box(fb, TWO, false);
    update_fill_box(fb, PLUS, false);
    update_fill_box(fb, THREE, false);
    CHECK(fb->t_size == 4 && strcmp(fb->text, "12+3") == 0, "type 12+3");

    fb->curso_pos = 1;
    update_fill_box(fb, BACK, false);
    CHECK(fb->t_size == 3 && strcmp(fb->text, "2+3") == 0 && fb->curso_pos == 0,
          "backspace mid-text deletes char before cursor");

    update_fill_box(fb, BACK, false);
    CHECK(fb->t_size == 3 && fb->curso_pos == 0, "backspace at cursor 0 is a no-op");

    update_fill_box(fb, E, false);
    CHECK(fb->t_size == 4 && fb->curso_pos == 1 && strcmp(fb->text, "e2+3") == 0,
          "E inserts exactly one char");

    for (int i = 0; i < 50; i++)
    {
        update_fill_box(fb, E, false);
        update_fill_box(fb, BACK, false);
    }
    CHECK(fb->t_size == 4 && strcmp(fb->text, "e2+3") == 0, "E/BACK x50 stable");

    update_fill_box(fb, FUNC_DET, false);
    CHECK(strcmp(fb->text, "edet()2+3") == 0, "det() template keeps tail");
    CHECK(fb->curso_pos == 5, "cursor before ')' after det(");

    clear(fb);
    CHECK(fb->t_size == 0 && fb->curso_pos == 0 && fb->text[0] == '\0', "backspace clears text");

    for (int i = 0; i < 1005; i++)
        update_fill_box(fb, ONE, false);
    CHECK(fb->t_size == 997, "insertions capped at 997 chars");
    update_fill_box(fb, BACK, false);
    CHECK(fb->t_size == 996, "backspace works at capacity");

    for (int i = 0; i < 500; i++)
    {
        update_fill_box(fb, ONE, false);
        update_fill_box(fb, BACK, false);
    }
    CHECK(fb->t_size == 996, "insert/BACK x500 at capacity stable");

    clear(fb);
    update_fill_box(fb, FUNC_BINOM, false);
    CHECK(strcmp(fb->text, "binom(,)") == 0 && fb->curso_pos == 6,
          "binom(,) template inserted with separator");
    update_fill_box(fb, FIVE, false);
    CHECK(strcmp(fb->text, "binom(5,)") == 0 && fb->curso_pos == 7,
          "argument typed before separator");
    update_fill_box(fb, COMA, false);
    CHECK(strcmp(fb->text, "binom(5,)") == 0 && fb->curso_pos == 8,
          "COMA steps over the existing separator");
    update_fill_box(fb, TWO, false);
    CHECK(strcmp(fb->text, "binom(5,2)") == 0, "binom complete");

    clear(fb);
    update_fill_box(fb, FUNC_PERM, false);
    CHECK(strcmp(fb->text, "perm(,)") == 0 && fb->curso_pos == 5,
          "perm(,) template inserted with separator");
    update_fill_box(fb, COMA, false);
    CHECK(strcmp(fb->text, "perm(,)") == 0 && fb->curso_pos == 6,
          "COMA steps over the existing perm separator");

    clear(fb);
    update_fill_box(fb, ONE, false);
    update_fill_box(fb, COMA, false);
    update_fill_box(fb, FIVE, false);
    CHECK(strcmp(fb->text, "1.5") == 0, "COMA outside binom is decimal point");

    // The argument separator of a binom()/perm() call cannot be deleted while
    // the second argument is still there.
    clear(fb);
    update_fill_box(fb, FUNC_BINOM, false);
    update_fill_box(fb, FIVE, false);
    update_fill_box(fb, COMA, false);
    update_fill_box(fb, TWO, false);
    fb->curso_pos = 8; // right after the separator
    update_fill_box(fb, BACK, false);
    CHECK(strcmp(fb->text, "binom(5,2)") == 0 && fb->curso_pos == 7,
          "separator of a binom with arguments is protected (BACK steps in)");

    // Once the second argument is gone the separator becomes removable.
    fb->curso_pos = 9; // after the second argument
    update_fill_box(fb, BACK, false); // removes '2'
    CHECK(strcmp(fb->text, "binom(5,)") == 0, "second argument removed");
    update_fill_box(fb, BACK, false);
    CHECK(strcmp(fb->text, "binom(5,)") == 0 && fb->curso_pos == 7,
          "separator kept while the first argument remains (BACK steps in)");
    update_fill_box(fb, BACK, false); // removes '5'
    update_fill_box(fb, BACK, false); // empty call: removed as a whole
    CHECK(fb->t_size == 0 && fb->curso_pos == 0, "empty binom removed by BACK");

    // A decimal comma is never protected.
    clear(fb);
    update_fill_box(fb, ONE, false);
    update_fill_box(fb, COMA, false);
    update_fill_box(fb, FIVE, false);
    update_fill_box(fb, BACK, false);
    CHECK(strcmp(fb->text, "1.") == 0, "decimal comma stays deletable");

    // Only digits may be typed inside the argument list of a binom()/perm().
    clear(fb);
    update_fill_box(fb, FUNC_BINOM, false);
    update_fill_box(fb, FIVE, false);
    update_fill_box(fb, PLUS, false);
    CHECK(strcmp(fb->text, "binom(5,)") == 0 && fb->curso_pos == 7,
          "operator refused inside binom");
    update_fill_box(fb, OPENING_PARENTHESIS, false);
    CHECK(strcmp(fb->text, "binom(5,)") == 0, "parenthesis refused inside binom");
    update_fill_box(fb, X, false);
    CHECK(strcmp(fb->text, "binom(5,)") == 0, "variable refused inside binom");
    update_fill_box(fb, FUNC_PERM, false);
    CHECK(strcmp(fb->text, "binom(5,)") == 0, "nested call refused inside binom");
    update_fill_box(fb, ONE, true);
    CHECK(strcmp(fb->text, "binom(5,)") == 0, "second-function token refused inside binom");
    update_fill_box(fb, COMA, true);
    CHECK(strcmp(fb->text, "binom(5,)") == 0 && fb->curso_pos == 7,
          "second-function comma refused inside binom");
    update_fill_box(fb, COMA, false);
    CHECK(strcmp(fb->text, "binom(5,)") == 0 && fb->curso_pos == 8,
          "separator still stepped over");
    update_fill_box(fb, TWO, false);
    update_fill_box(fb, COMA, false);
    CHECK(strcmp(fb->text, "binom(5,2)") == 0 && fb->curso_pos == 9,
          "decimal comma refused inside binom");

    // Deleting either parenthesis deletes the whole call (name included) while
    // the text around it is preserved.
    build_binom_expr(fb);
    fb->curso_pos = 8; // right after "binom("
    update_fill_box(fb, BACK, false);
    CHECK(strcmp(fb->text, "1+binom(5,2)+3") == 0 && fb->curso_pos == 8,
          "opening parenthesis of a non-empty binom is protected");

    build_binom_expr(fb);
    fb->curso_pos = 12; // right after the closing ')'
    update_fill_box(fb, BACK, false);
    CHECK(strcmp(fb->text, "1+binom(5,2)+3") == 0 && fb->curso_pos == 11,
          "BACK after the ')' of a non-empty binom steps into it");

    clear(fb);
    update_fill_box(fb, FUNC_PERM, false);
    update_fill_box(fb, BACK, false);
    CHECK(fb->t_size == 0 && fb->text[0] == '\0',
          "deleting the perm '(' removes the whole call");

    clear(fb);
    update_fill_box(fb, FUNC_DET, false);
    fb->curso_pos = fb->t_size; // after ')'
    update_fill_box(fb, BACK, false);
    CHECK(strcmp(fb->text, "det(") == 0 && fb->curso_pos == 4,
          "deleting a non-binom ')' removes only it");

    // The menus point ->text at a string literal; releasing such a box must
    // not free the literal (it would corrupt the heap and crash the menu).
    {
        text_box *tb = create_text_box(0, 0, 10, 100, 0, false);
        tb->text = (char *)"Probabilites";
        tb->t_size = 11;
        free_text_box(tb);
        printf("OK  : freeing a text box holding a literal\n");
    }
    {
        text_box *tb = create_text_box(0, 0, 10, 100, 0, false);
        free_text_box(tb);
        printf("OK  : freeing a text box holding its own buffer\n");
    }

    // Analyse : somme / produit / derivee / integrale.
    clear(fb);
    update_fill_box(fb, FUNC_SUM, false);
    CHECK(strcmp(fb->text, "somme(,,)") == 0 && fb->curso_pos == 6,
          "somme(,,) template, cursor on the expression");
    update_fill_box(fb, X, false);
    update_fill_box(fb, COMA, false);
    CHECK(strcmp(fb->text, "somme(X,,)") == 0 && fb->curso_pos == 8,
          "COMA steps over the 1st somme separator");
    update_fill_box(fb, ONE, false);
    update_fill_box(fb, COMA, false);
    CHECK(strcmp(fb->text, "somme(X,1,)") == 0 && fb->curso_pos == 10,
          "COMA steps over the 2nd somme separator");
    update_fill_box(fb, ONE, false);
    update_fill_box(fb, ZERO, false);
    CHECK(strcmp(fb->text, "somme(X,1,10)") == 0, "somme complete");
    fb->curso_pos = 10; // juste apres la 2e virgule
    update_fill_box(fb, BACK, false);
    CHECK(strcmp(fb->text, "somme(X,1,10)") == 0, "BACK refuses to delete a separator");
    CHECK(cursor_in_calculus_call(fb->text, 7), "cursor inside somme detected");
    CHECK(!cursor_in_calculus_call("1+2", 2), "no calculus call in 1+2");

    clear(fb);
    update_fill_box(fb, FUNC_INTEG, false);
    CHECK(strcmp(fb->text, "integrale(,,)") == 0 && fb->curso_pos == 10,
          "integrale(,,) template");
    update_fill_box(fb, OPENING_PARENTHESIS, false);
    update_fill_box(fb, ONE, false);
    update_fill_box(fb, COMA, false);
    update_fill_box(fb, FIVE, false);
    CHECK(strstr(fb->text, "(1.5") != NULL,
          "COMA in a plain parenthesis inside integrale is a decimal point");

    clear(fb);
    update_fill_box(fb, FUNC_DERIV, false);
    CHECK(strcmp(fb->text, "derivee(,)") == 0 && fb->curso_pos == 8, "derivee(,) template");
    update_fill_box(fb, BACK, false);
    CHECK(fb->t_size == 0, "BACK on an empty derivee removes the whole call");

    // Les fleches ne s'arretent jamais dans un nom de fonction.
    clear(fb);
    update_fill_box(fb, FUNC_SUM, false);
    fill_box_left(fb);
    CHECK(fb->curso_pos == 0, "LEFT from the first argument jumps before 'somme'");
    update_fill_box(fb, TWO, false);
    CHECK(strcmp(fb->text, "2somme(,,)") == 0 && fb->curso_pos == 1,
          "digit typed before the call, name intact");
    fill_box_right(fb);
    CHECK(fb->curso_pos == 7, "RIGHT jumps over the name into the first argument");
    clear(fb);
    update_fill_box(fb, ONE, false);
    update_fill_box(fb, PLUS, false);
    update_fill_box(fb, FUNC_PGCD, false);
    fill_box_left(fb);
    CHECK(fb->curso_pos == 2, "LEFT also skips other call names (pgcd)");

    clear(fb);
    update_fill_box(fb, FUNC_PROD, false);
    CHECK(strcmp(fb->text, "produit(,,)") == 0 && fb->curso_pos == 8, "produit(,,) template");

    // ---- Ans : un seul bloc ----
    clear(fb);
    update_fill_box(fb, TWO, false);
    update_fill_box(fb, ANS, false);
    update_fill_box(fb, PLUS, false);
    update_fill_box(fb, ONE, false);
    CHECK(strcmp(fb->text, "2ans+1") == 0, "type 2ans+1");
    fb->curso_pos = 4; // juste apres "ans"
    update_fill_box(fb, BACK, false);
    CHECK(strcmp(fb->text, "2+1") == 0 && fb->curso_pos == 1, "BACK after ans removes the whole word");
    clear(fb);
    update_fill_box(fb, ANS, false);
    update_fill_box(fb, BACK, false);
    CHECK(fb->t_size == 0 && fb->text[0] == '\0', "BACK on lone ans leaves nothing (no acosh)");
    clear(fb);
    update_fill_box(fb, ONE, false);
    update_fill_box(fb, ANS, false);
    update_fill_box(fb, TWO, false);
    fb->curso_pos = 4;
    fill_box_left(fb);
    CHECK(fb->curso_pos == 1, "LEFT skips over ans");
    fill_box_right(fb);
    CHECK(fb->curso_pos == 4, "RIGHT skips over ans");
    fb->curso_pos = 2;
    update_fill_box(fb, FIVE, false);
    CHECK(strcmp(fb->text, "1ans2") == 0, "no typing inside ans");
    clear(fb);
    update_fill_box(fb, FUNC_TRANS, false);
    fb->curso_pos = 5; // apres "trans"
    update_fill_box(fb, BACK, false);
    CHECK(strncmp(fb->text, "tr", 2) != 0 || strcmp(fb->text, "tr()") != 0, "trans( is not treated as ans");

    printf(fails ? "FAILURES: %d\n" : "ALL PASS (%d failures)\n", fails);
    return fails ? 1 : 0;
}
