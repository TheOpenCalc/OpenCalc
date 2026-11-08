#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cmath>
#include <vector>
#include "Evaluator.h"
#include "Matrix.h"

static int fails = 0;

static bool approx(double a, double b)
{
    return std::isnan(a) ? std::isnan(b) : fabs(a - b) < 1e-9;
}

static bool approx_c(complex a, complex b)
{
    return approx(a.re, b.re) && approx(a.im, b.im);
}

// Evaluate an expression through the full pipeline, expecting a scalar result.
static void run_scalar(const char *expr, double exp_re, double exp_im)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    calc_result cr = evaluate_calc(rpn, yarded);
    bool ok = !cr.is_matrix && approx(cr.scalar.re, exp_re) && approx(cr.scalar.im, exp_im);
    if (!ok)
        fails++;
    printf("[%s] %-34s => scalar %.10g%+.10gi (expected %.10g%+.10gi)\n",
           ok ? "OK " : "FAIL", expr, cr.scalar.re, cr.scalar.im, exp_re, exp_im);
    calc_result_free(&cr);
    token_free_matrices(toks, tok_n);
    free(toks);
    free(rpn);
}

// Evaluate an expression, expecting a matrix result of given shape/values.
static void check_matrix(const char *expr, int rows, int cols, const std::vector<complex> &expected)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    calc_result cr = evaluate_calc(rpn, yarded);
    bool ok = cr.is_matrix && cr.m && cr.m->rows == rows && cr.m->cols == cols;
    if (ok)
    {
        for (int r = 0; r < rows && ok; r++)
            for (int c = 0; c < cols; c++)
                if (!approx_c(mat_get(cr.m, r, c), expected[r * cols + c]))
                {
                    ok = false;
                    break;
                }
    }
    if (!ok)
        fails++;
    int ar = cr.is_matrix && cr.m ? cr.m->rows : -1;
    int ac = cr.is_matrix && cr.m ? cr.m->cols : -1;
    printf("[%s] %-34s => matrix %dx%d (expected %dx%d)\n",
           ok ? "OK " : "FAIL", expr, ar, ac, rows, cols);
    calc_result_free(&cr);
    token_free_matrices(toks, tok_n);
    free(toks);
    free(rpn);
}

// Evaluate an expression that must fail (dimension mismatch, singular, ...)
// and end up as a NaN scalar (never a matrix).
static void check_error(const char *expr)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    calc_result cr = evaluate_calc(rpn, yarded);
    bool ok = !cr.is_matrix && std::isnan(cr.scalar.re);
    if (!ok)
        fails++;
    printf("[%s] %-34s => ERROR (NaN scalar)\n", ok ? "OK " : "FAIL", expr);
    calc_result_free(&cr);
    token_free_matrices(toks, tok_n);
    free(toks);
    free(rpn);
}

static complex z(double re, double im)
{
    complex v = {re, im};
    return v;
}

int main()
{
    // ---- literals ----
    check_matrix("[[1,2],[3,4]]", 2, 2, {z(1, 0), z(2, 0), z(3, 0), z(4, 0)});
    check_matrix("[1,2,3]", 1, 3, {z(1, 0), z(2, 0), z(3, 0)});
    check_matrix("[[1.5,2.5],[3,4]]", 2, 2, {z(1.5, 0), z(2.5, 0), z(3, 0), z(4, 0)});
    check_matrix("[[-1,2],[3,-4]]", 2, 2, {z(-1, 0), z(2, 0), z(3, 0), z(-4, 0)});
    check_matrix("[[0,0],[0,0]]", 2, 2, {z(0, 0), z(0, 0), z(0, 0), z(0, 0)});
    check_matrix("[[-2.5,0.5],[1.5,-0.5]]", 2, 2, {z(-2.5, 0), z(0.5, 0), z(1.5, 0), z(-0.5, 0)});
    check_matrix("[[.5,-.5],[1.,2.]]", 2, 2, {z(0.5, 0), z(-0.5, 0), z(1, 0), z(2, 0)});

    // ---- empty cells (inserted templates stay valid while unfinished) ----
    check_matrix("[[,]]", 1, 2, {z(NAN, NAN), z(NAN, NAN)});
    check_matrix("[[1,]]", 1, 2, {z(1, 0), z(NAN, NAN)});
    check_matrix("[[,1]]", 1, 2, {z(NAN, NAN), z(1, 0)});
    check_matrix("[[1,,3]]", 1, 3, {z(1, 0), z(NAN, NAN), z(3, 0)});
    check_matrix("[[1,2],[,]]", 2, 2, {z(1, 0), z(2, 0), z(NAN, NAN), z(NAN, NAN)});
    run_scalar("det([[,]])", NAN, NAN);

    // ---- complex literals ----
    check_matrix("[[2+3i,-i],[i,1-2i]]", 2, 2, {z(2, 3), z(0, -1), z(0, 1), z(1, -2)});
    check_matrix("[4i,-1+i]", 1, 2, {z(0, 4), z(-1, 1)});
    check_matrix("[[-i,i]]", 1, 2, {z(0, -1), z(0, 1)});
    check_matrix("[[+i,+.5-2i]]", 1, 2, {z(0, 1), z(0.5, -2)});
    check_error("[[1+,2]]");
    check_error("[[]]");

    // ---- arithmetic ----
    check_matrix("[[1,2],[3,4]]+[[5,6],[7,8]]", 2, 2, {z(6, 0), z(8, 0), z(10, 0), z(12, 0)});
    check_matrix("[[1,2],[3,4]]-[[5,6],[7,8]]", 2, 2, {z(-4, 0), z(-4, 0), z(-4, 0), z(-4, 0)});
    check_matrix("[[1,2],[3,4]]*[[5,6],[7,8]]", 2, 2, {z(19, 0), z(22, 0), z(43, 0), z(50, 0)});
    check_matrix("[[1,2],[3,4]]^2", 2, 2, {z(7, 0), z(10, 0), z(15, 0), z(22, 0)});
    check_matrix("[[1,2],[3,4]]^0", 2, 2, {z(1, 0), z(0, 0), z(0, 0), z(1, 0)});
    check_matrix("[[1,2],[3,4]]^3", 2, 2, {z(37, 0), z(54, 0), z(81, 0), z(118, 0)});
    check_matrix("2*[[1,2],[3,4]]", 2, 2, {z(2, 0), z(4, 0), z(6, 0), z(8, 0)});
    check_matrix("[[1,2],[3,4]]*2", 2, 2, {z(2, 0), z(4, 0), z(6, 0), z(8, 0)});
    check_matrix("[[1,2],[3,4]]+2", 2, 2, {z(3, 0), z(4, 0), z(5, 0), z(6, 0)});
    check_matrix("2+[[1,2],[3,4]]", 2, 2, {z(3, 0), z(4, 0), z(5, 0), z(6, 0)});
    check_matrix("2-[[1,2],[3,4]]", 2, 2, {z(1, 0), z(0, 0), z(-1, 0), z(-2, 0)});
    check_matrix("[[1,2],[3,4]]-2", 2, 2, {z(-1, 0), z(0, 0), z(1, 0), z(2, 0)});
    check_matrix("[[4,2],[2,2]]/2", 2, 2, {z(2, 0), z(1, 0), z(1, 0), z(1, 0)});
    check_matrix("[[1,2],[3,4]]/[[1,0],[0,1]]", 2, 2, {z(1, 0), z(2, 0), z(3, 0), z(4, 0)});

    // ---- complex scalar x matrix (regressions: imaginary part must survive) ----
    check_matrix("(1+2i)*[[1,2]]", 1, 2, {z(1, 2), z(2, 4)});
    check_matrix("[[1,2]]*(1+2i)", 1, 2, {z(1, 2), z(2, 4)});
    check_matrix("i*[[1,2],[3,4]]", 2, 2, {z(0, 1), z(0, 2), z(0, 3), z(0, 4)});
    check_matrix("[[1,0],[0,1]]*(2-i)", 2, 2, {z(2, -1), z(0, 0), z(0, 0), z(2, -1)});
    check_matrix("[[1,2]]+(3+4i)", 1, 2, {z(4, 4), z(5, 4)});
    check_matrix("(3+4i)+[[1,2]]", 1, 2, {z(4, 4), z(5, 4)});
    check_matrix("[[1,2]]-(1-i)", 1, 2, {z(0, 1), z(1, 1)});
    check_matrix("(1-i)-[[1,2]]", 1, 2, {z(0, -1), z(-1, -1)});
    // 1/(1-i) = (1+i)/2
    check_matrix("[[1,2]]/(1-i)", 1, 2, {z(0.5, 0.5), z(1, 1)});
    // scalar / matrix is undefined
    check_error("(1+i)/[[1,0],[0,1]]");
    check_matrix("([[1,2]]+i)*1", 1, 2, {z(1, 1), z(2, 1)});
    check_error("[[1,2]]^i");

    // ---- matrix product (non square) ----
    check_matrix("[1,2,3]*[[1],[2],[3]]", 1, 1, {z(14, 0)});
    check_matrix("[[1],[2],[3]]*[1,2,3]", 3, 3, {z(1, 0), z(2, 0), z(3, 0), z(2, 0), z(4, 0), z(6, 0), z(3, 0), z(6, 0), z(9, 0)});
    check_matrix("[i]*[i]", 1, 1, {z(-1, 0)});

    // ---- named functions ----
    run_scalar("det([[1,2],[3,4]])", -2, 0);
    run_scalar("det([[1,2],[3,4]])+1", -1, 0);
    run_scalar("2*det([[1,2],[3,4]])", -4, 0);
    run_scalar("det([[1,0],[0,1]])", 1, 0);
    run_scalar("det([[i,0],[0,i]])", -1, 0); // i*i = -1
    run_scalar("det([[1+i,1],[1,1-i]])", 1, 0);
    check_matrix("inv([[1,2],[3,4]])", 2, 2, {z(-2, 0), z(1, 0), z(1.5, 0), z(-0.5, 0)});
    check_matrix("inv([[1,2],[3,4]])*[[1,2],[3,4]]", 2, 2, {z(1, 0), z(0, 0), z(0, 0), z(1, 0)});
    check_matrix("trans([[1,2],[3,4]])", 2, 2, {z(1, 0), z(3, 0), z(2, 0), z(4, 0)});
    check_matrix("trans([1,2,3])", 3, 1, {z(1, 0), z(2, 0), z(3, 0)});
    check_matrix("trans(inv([[1,2],[3,4]]))", 2, 2, {z(-2, 0), z(1.5, 0), z(1, 0), z(-0.5, 0)});
    check_matrix("inv(trans([[1,2],[3,4]]))", 2, 2, {z(-2, 0), z(1.5, 0), z(1, 0), z(-0.5, 0)});

    // ---- complex inverse / power ----
    // [[i,1],[1,i]]^-1 = 1/2 * [[-i,1],[1,-i]]
    check_matrix("inv([[i,1],[1,i]])", 2, 2, {z(0, -0.5), z(0.5, 0), z(0.5, 0), z(0, -0.5)});
    check_matrix("inv([[i,1],[1,i]])*[[i,1],[1,i]]", 2, 2, {z(1, 0), z(0, 0), z(0, 0), z(1, 0)});
    check_matrix("[[0,i],[i,0]]^2", 2, 2, {z(-1, 0), z(0, 0), z(0, 0), z(-1, 0)});
    // A = [[0,i],[i,0]], det = 1 => A^-1 = [[0,-i],[-i,0]]
    // (negative powers are not parser-reachable: unary minus is unsupported,
    // so use inv(); see also the 2^-1 corruption regression test below)
    check_matrix("inv([[0,i],[i,0]])", 2, 2, {z(0, 0), z(0, -1), z(0, -1), z(0, 0)});

    // ---- errors ----
    check_error("[[1,2],[3,4]]+[[1,2,3],[4,5,6]]");
    check_error("[[1,2],[3,4]]-[[1,2,3],[4,5,6]]");
    check_error("[[1,2],[3,4]]*[[1,2,3],[4,5,6],[7,8,9]]");
    check_error("det([[1,2]])");
    check_error("inv([[1,2],[2,4]])");
    check_error("inv([[i,2],[2i,4]])");
    check_error("[[1,2],[3,4]]^0.5");
    check_error("s([[1,2],[3,4]])");
    check_error("[[1,2]]/0");
    check_error("[[1,2]]/(1+i-1-i)");
    check_error("r([[1,2],[3,4]])");
    // Regression: the unary minus after '^' used to produce a malformed RPN
    // that underflowed the evaluation stack. It is now supported: 2^-1 = 0.5.
    run_scalar("2^-1", 0.5, 0);

    // ---- maximum dimension ----
    {
        // 99x99 literal must be accepted end-to-end (result ~ 1x1 sum of the det of a
        // tridiagonal 99x99 matrix would be messy; use det of a diagonal 99x99 instead).
        int dim = 99;
        char *expr = (char *)malloc(dim * dim * 4 + 128);
        char *p = expr;
        *p++ = 'd';
        *p++ = 'e';
        *p++ = 't';
        *p++ = '(';
        *p++ = '[';
        for (int r = 0; r < dim; r++)
        {
            *p++ = '[';
            for (int c = 0; c < dim; c++)
            {
                if (r == c)
                {
                    *p++ = '2';
                    if (c + 1 < dim)
                        *p++ = '.';
                }
                else
                    *p++ = '0';
                if (c + 1 < dim)
                    *p++ = ',';
            }
            *p++ = ']';
            if (r + 1 < dim)
                *p++ = ',';
        }
        *p++ = ']';
        *p++ = ')';
        *p = '\0';
        int n = p - expr;
        int tok_n = 0;
        token *toks = parse_string_to_token(expr, n, &tok_n);
        token *rpn = shunting_yard(toks, tok_n);
        int yarded = count_yarded(toks, tok_n);
        calc_result cr = evaluate_calc(rpn, yarded);
        bool ok = !cr.is_matrix && approx(cr.scalar.re, pow(2.0, dim)) && approx(cr.scalar.im, 0);
        if (!ok)
            fails++;
        printf("[%s] det(99x99 diag(2))              => %.10g (expected %g)\n",
               ok ? "OK " : "FAIL", cr.scalar.re, pow(2.0, dim));
        calc_result_free(&cr);
        token_free_matrices(toks, tok_n);
        free(toks);
        free(rpn);
        free(expr);

        // 100x100 must be rejected (MATRIX_MAX_DIM = 99)
        dim = 100;
        expr = (char *)malloc(dim * dim * 4 + 128);
        p = expr;
        *p++ = 'd';
        *p++ = 'e';
        *p++ = 't';
        *p++ = '(';
        *p++ = '[';
        for (int r = 0; r < dim; r++)
        {
            *p++ = '[';
            for (int c = 0; c < dim; c++)
            {
                *p++ = '0';
                if (c + 1 < dim)
                    *p++ = ',';
            }
            *p++ = ']';
            if (r + 1 < dim)
                *p++ = ',';
        }
        *p++ = ']';
        *p++ = ')';
        *p = '\0';
        n = p - expr;
        tok_n = 0;
        toks = parse_string_to_token(expr, n, &tok_n);
        rpn = shunting_yard(toks, tok_n);
        yarded = count_yarded(toks, tok_n);
        cr = evaluate_calc(rpn, yarded);
        ok = !cr.is_matrix && std::isnan(cr.scalar.re);
        if (!ok)
            fails++;
        printf("[%s] det(100x100)                    => ERROR (NaN)\n", ok ? "OK " : "FAIL");
        calc_result_free(&cr);
        token_free_matrices(toks, tok_n);
        free(toks);
        free(rpn);
        free(expr);
    }

    // ---- mat_to_string ----
    {
        matrix *m = mat_create(2, 2);
        complex vals[4] = {z(1, 0), z(2, 0), z(3, 0), z(4, 0)};
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 2; c++)
                mat_set(m, r, c, vals[r * 2 + c]);
        char buf[64];
        int len = mat_to_string(m, buf, 64);
        bool ok = strcmp(buf, "[[1,2],[3,4]]") == 0;
        if (!ok)
            fails++;
        printf("[%s] mat_to_string real 2x2          => \"%s\" (len %d)\n", ok ? "OK " : "FAIL", buf, len);

        complex cv[2] = {z(2, 3), z(0, -1)};
        mat_set(m, 0, 0, cv[0]);
        mat_set(m, 0, 1, cv[1]);
        len = mat_to_string(m, buf, sizeof(buf));
        ok = strcmp(buf, "[[2+3i,-i],[3,4]]") == 0;
        if (!ok)
            fails++;
        printf("[%s] mat_to_string complex cells     => \"%s\" (len %d)\n", ok ? "OK " : "FAIL", buf, len);
        mat_free(m);
    }

    // ---- Ans matrice ----
    {
        matrix *m = mat_create(2, 2);
        mat_set(m, 0, 0, z(1, 0));
        mat_set(m, 0, 1, z(2, 0));
        mat_set(m, 1, 0, z(3, 0));
        mat_set(m, 1, 1, z(4, 0));
        set_ans_matrix(m);
        mat_free(m);
        check_matrix("ans", 2, 2, {z(1, 0), z(2, 0), z(3, 0), z(4, 0)});
        check_matrix("2ans", 2, 2, {z(2, 0), z(4, 0), z(6, 0), z(8, 0)});
        check_matrix("ans+[[1,1],[1,1]]", 2, 2, {z(2, 0), z(3, 0), z(4, 0), z(5, 0)});
        check_matrix("ans*ans", 2, 2, {z(7, 0), z(10, 0), z(15, 0), z(22, 0)});
        run_scalar("det(ans)", -2, 0);
        // Ans := Ans (meme pointeur) ne doit pas liberer la source avant la copie.
        set_ans_matrix(g_ans_mat);
        check_matrix("ans", 2, 2, {z(1, 0), z(2, 0), z(3, 0), z(4, 0)});
        set_ans(z(5, 0));
        bool ok = g_ans_mat == NULL;
        if (!ok)
            fails++;
        printf("[%s] set_ans scalaire efface la matrice\n", ok ? "OK " : "FAIL");
        run_scalar("ans*2", 10, 0);
    }

    printf("\n%s (%d failures)\n", fails == 0 ? "ALL PASS" : "FAILURES", fails);
    return fails == 0 ? 0 : 1;
}
