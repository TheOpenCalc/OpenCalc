#include "ui.h"
#include "Evaluator.h"
#include "sequences.h"
#include "stack.h"
#include <math.h>
#include "menu.h"
#include <algorithm>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cstdio>
#include "Solver.h"
#include <stdio.h>
#include "colors.h"

#include <time.h>

void simplify_first(double *a, double *b, int n, int first_nb)
{
    double K = b[first_nb] / a[first_nb];
    for (int i = first_nb; i < n; i++)
        b[i] -= a[i] * K;
}

void normalize(double *a, int size, int n)
{
    double K = 1.0 / a[n];
    for (int i = 0; i < size; i++)
        a[i] *= K;
}

double *solve(double **input, int nb_var, int nb_eq)
{
    for (int i = 0; i < nb_eq - 1; i++)
    {
        int max_row = i;
        double max_val = fabs(input[i][i]);
        for (int k = i + 1; k < nb_eq; k++)
        {
            if (fabs(input[k][i]) > max_val)
            {
                max_val = fabs(input[k][i]);
                max_row = k;
            }
        }
        if (max_row != i)
        {
            double *tmp = input[i];
            input[i] = input[max_row];
            input[max_row] = tmp;
        }
        for (int k = i + 1; k < nb_eq; k++)
            if (fabs(input[i][i]) > 1e-12)
                simplify_first(input[i], input[k], nb_var + 1, i);
    }
    for (int i = 0; i < nb_eq; i++)
        if (fabs(input[i][i]) > 1e-12)
            normalize(input[i], nb_var + 1, i);

    double *solution = (double *)malloc(sizeof(double) * nb_var);
    for (int i = 0; i < nb_var; i++)
        solution[i] = input[i][nb_var];

    for (int i = nb_eq - 1; i >= 0; i--)
        for (int k = i + 1; k < nb_var; k++)
            solution[i] -= solution[k] * input[i][k];

    return solution;
}

static const complex SOLVER_PI = {3.14159265358979323846264338327, 0};
static const complex SOLVER_E = {2.71828182845904523536, 0};
static const complex SOLVER_I = {0, 1};

static complex **init_c_Mat(int L, int H, complex default_value)
{
    complex **a = (complex **)malloc(sizeof(complex *) * H);
    for (int i = 0; i < H; i++)
    {
        a[i] = (complex *)malloc(sizeof(complex) * L);
        for (int j = 0; j < L; j++)
        {
            a[i][j] = default_value;
        }
    }
    return a;
}

complex *solve_c(complex **input, int nb_var, int nb_eq)
{
    for (int i = 0; i < nb_eq - 1; i++)
    {
        int max_row = i;
        double max_val = cmod(input[i][i]).re;
        for (int k = i + 1; k < nb_eq; k++)
        {
            if (cmod(input[k][i]).re > max_val)
            {
                max_val = cmod(input[k][i]).re;
                max_row = k;
            }
        }
        if (max_row != i)
        {
            complex *tmp = input[i];
            input[i] = input[max_row];
            input[max_row] = tmp;
        }
        for (int k = i + 1; k < nb_eq; k++)
            if (cmod(input[i][i]).re > 1e-12)
            {
                complex K = cdiv(input[k][i], input[i][i]);
                for (int j = i; j < nb_var + 1; j++)
                    input[k][j] = csub(input[k][j], cmul(K, input[i][j]));
            }
    }
    for (int i = 0; i < nb_eq; i++)
        if (cmod(input[i][i]).re > 1e-12)
        {
            complex K = cdiv((complex){1, 0}, input[i][i]);
            for (int j = i; j < nb_var + 1; j++)
                input[i][j] = cmul(K, input[i][j]);
        }

    complex *solution = (complex *)malloc(sizeof(complex) * nb_var);
    for (int i = 0; i < nb_var; i++)
        solution[i] = input[i][nb_var];

    for (int i = nb_eq - 1; i >= 0; i--)
        for (int k = i + 1; k < nb_var; k++)
            solution[i] = csub(solution[i], cmul(solution[k], input[i][k]));

    return solution;
}

static complex eval_c_multi(token *in, int n,
                            complex *values, int nb_var, int *var_indices)
{
    stack_c nb_stack;
    init(&nb_stack, n + 4);
    complex a, b;

    for (int i = 0; i < n; i++)
    {
        if (in[i].type == 'X')
        {
            int ch = (int)(in[i].value) + (int)'a';
            complex val = {0, 0};
            for (int v = 0; v < nb_var; v++)
                if (ch == 'A' + var_indices[v])
                {
                    val = values[v];
                    break;
                }
            push(&nb_stack, val);
        }
        else if (in[i].type == 'n')
        {
            complex t = {in[i].value, 0};
            push(&nb_stack, t);
        }
        else if (is_in(in[i].type, (char *)"+-*/^"))
        {
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type)
            {
            case '+':
                push(&nb_stack, cadd(a, b));
                break;
            case '*':
                push(&nb_stack, cmul(a, b));
                break;
            case '-':
                push(&nb_stack, csub(b, a));
                break;
            case '/':
                if (cmod(a).re == 0)
                {
                    free(&nb_stack);
                    complex nan = {NAN, 0};
                    return nan;
                }
                push(&nb_stack, cdiv(b, a));
                break;
            case '^':
                push(&nb_stack, cpow(b, a));
                break;
            default:
                break;
            }
        }
        else if (in[i].type == 'p' || in[i].type == 'e' || in[i].type == 'i' || in[i].type == 'z')
        {
            if (in[i].type == 'p')
                push(&nb_stack, SOLVER_PI);
            else if (in[i].type == 'e')
                push(&nb_stack, SOLVER_E);
            else if (in[i].type == 'z')
                push(&nb_stack, g_ans);
            else
                push(&nb_stack, SOLVER_I);
        }
        else if (in[i].type == 'q' || in[i].type == 'y')
        { // functions requiring 2 arguments
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            if (in[i].type == 'q')
                push(&nb_stack, (complex){comb(b.re, a.re), 0});
            else
                push(&nb_stack, (complex){perm(b.re, a.re), 0});
        }
        else
        {
            a = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type)
            {
            case 'r':
                a = csqrt(a);
                break;
            case 'l':
                a = clog(a);
                break;
            case 'o':
                a = clog10(a);
                break;
            case 'c':
                a = ccos(angle_in_c(a));
                break;
            case 's':
                a = csin(angle_in_c(a));
                break;
            case 't':
                a = ctan(angle_in_c(a));
                break;
            case 'u':
                a = angle_out_c(cacos(a));
                break;
            case 'v':
                a = angle_out_c(casin(a));
                break;
            case 'w':
                a = angle_out_c(catan(a));
                break;
            case 'f':
                a = ccosh(a);
                break;
            case 'g':
                a = csinh(a);
                break;
            case 'h':
                a = ctanh(a);
                break;
            case 'a':
                a = cacosh(a);
                break;
            case 'j':
                a = casinh(a);
                break;
            case 'k':
                a = catanh(a);
                break;
            case 'b':
                a = cconj(a);
                break;
            case '~':
                a.re = -a.re;
                a.im = -a.im;
                break;
            case 'd':
                a = carg(a);
                break;
            case 'm':
                a = cmod(a);
                break;
            case '!':
                a = cfact(a);
                break;
            default:
                break;
            }
            push(&nb_stack, a);
        }
    }

    a = peek(&nb_stack);
    free(&nb_stack);
    return a;
}

static complex *solve_nonlinear_c(token **left_tok, int *left_sz,
                                  token **right_tok, int *right_sz,
                                  int nb_eq, int nb_var, int *var_indices)
{
    const double H = 1e-6;
    const int MAX_ITER = 100;
    const double TOL = 1e-8;

    complex *x = (complex *)malloc(nb_var * sizeof(complex));
    complex *best = (complex *)malloc(nb_var * sizeof(complex));
    bool converged = false;
    double best_res = INFINITY;

    // Imaginary-axis and complex starts first: real starts get stuck near the
    // (singular) origin for complex roots such as X^2 = -1.
    complex starts[] = {
        {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
        {1, 0}, {-1, 0}, {0.1, 0}, {10, 0},
    };
    for (int start = 0; start < 10 && !converged; start++)
    {
        for (int v = 0; v < nb_var; v++)
            x[v] = starts[start];

        double best_f_start = INFINITY;
        int since_improve = 0;
        for (int iter = 0; iter < MAX_ITER && !converged; iter++)
        {
            complex *F = (complex *)malloc(nb_eq * sizeof(complex));
            double max_f = 0.0;
            for (int i = 0; i < nb_eq; i++)
            {
                F[i] = csub(eval_c_multi(left_tok[i], left_sz[i], x, nb_var, var_indices),
                            eval_c_multi(right_tok[i], right_sz[i], x, nb_var, var_indices));
                double fi = cmod(F[i]).re;
                if (fi > max_f)
                    max_f = fi;
            }

            if (max_f < best_res)
            {
                best_res = max_f;
                for (int v = 0; v < nb_var; v++)
                    best[v] = x[v];
            }

            if (max_f < TOL)
            {
                free(F);
                converged = true;
                break;
            }

            if (max_f < best_f_start - 1e-12)
            {
                best_f_start = max_f;
                since_improve = 0;
            }
            else
                since_improve++;
            if (since_improve >= 20)
            { // stuck (e.g. singular Jacobian): try the next start
                free(F);
                break;
            }

            complex **J = init_c_Mat(nb_var + 1, nb_eq, (complex){0, 0});
            for (int j = 0; j < nb_var; j++)
            {
                complex *xh = (complex *)malloc(nb_var * sizeof(complex));
                for (int v = 0; v < nb_var; v++)
                    xh[v] = x[v];
                xh[j] = (complex){x[j].re + H, x[j].im};
                for (int i = 0; i < nb_eq; i++)
                {
                    complex Fh = csub(eval_c_multi(left_tok[i], left_sz[i], xh, nb_var, var_indices),
                                      eval_c_multi(right_tok[i], right_sz[i], xh, nb_var, var_indices));
                    J[i][j] = cdiv(csub(Fh, F[i]), (complex){H, 0});
                }
                free(xh);
            }
            for (int i = 0; i < nb_eq; i++)
                J[i][nb_var] = csub((complex){0, 0}, F[i]);

            complex *dx = solve_c(J, nb_var, nb_eq);

            for (int v = 0; v < nb_var; v++)
                x[v] = cadd(x[v], dx[v]);

            free(F);
            free(dx);
            for (int i = 0; i < nb_eq; i++)
                free(J[i]);
            free(J);
        }
    }

    if (!converged)
        for (int v = 0; v < nb_var; v++)
            x[v] = best[v];
    free(best);
    return x;
}

int Solver()
{
    stdio_init_all();
    bool snd = false;

    text_box *Solution = create_text_box(160, 220, 20, 160, 1, false);
    text_box *Equations = create_text_box(0, 220, 20, 160, 1, false);
    Solution->text = "Solut";
    Solution->t_size = 5;
    Equations->text = "Equatio";
    Equations->t_size = 7;

    fill_box **arr_solution = (fill_box **)malloc(sizeof(fill_box *) * 30);
    fill_box **arr_fill_box = (fill_box **)malloc(sizeof(fill_box *) * 30);
    for (int i = 0; i < 30; i++)
    {
        arr_fill_box[i] = nullptr;
        arr_solution[i] = create_fill_box(0, 20, 40, 320, 3);
    }

    int selected_fill_box = 0;
    arr_fill_box[0] = create_fill_box(0, 20, 40, 320, 3);
    int first_display = 0;
    bool show_solution = false;

    fill_screen(BACKGROUND_COLOR);
    text_box *accuracy_box = create_text_box(0, 0, 20, 160, 1, false);
    accuracy_box->text = "            ";
    accuracy_box->t_size = 0;
    while (1)
    {
        int mv = 0;
        if (!show_solution)
        {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++)
            {
                if (arr_fill_box[i] == nullptr)
                    continue;
                int old_mv = mv;
                int tok_n = 0;
                int input_size;
                display_set_source(arr_fill_box[i]->text, arr_fill_box[i]->t_size);
                token *toks = parse_string_to_token(arr_fill_box[i]->text, arr_fill_box[i]->t_size, &tok_n);
                Parser p = {toks, tok_n, 0};

                while (p.pos < p.n)
                {
                    int before = p.pos;
                    ASTNode *node = parse_equation(&p);
                    if (p.pos == before)
                        p.pos++;
                    if (node)
                        mv += max(0, measure(node, 2).h - 16);
                }
                token_free_matrices(toks, tok_n);
                free(toks);
                arr_fill_box[i]->h = mv - old_mv + 40;
                //                display_fill_box(arr_fill_box[i], 160 - (i - selected_fill_box + 4) * 40 + old_mv, (i - selected_fill_box) % HISTORY_SIZE == 0, -1, ' ');
                display_fill_box(arr_fill_box[i],
                                 160 - (i - std::max(first_display, 0)) * 40 - mv,
                                 i == selected_fill_box, -1, ' ',false);
            }
        }
        else
        {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++)
                display_fill_box(arr_solution[i],
                                 160 - (i - std::max(first_display, 0)) * 40,
                                 i == selected_fill_box, -1, ' ',false);
            display_text_box(accuracy_box, 0, 0, false);
        }

        display_text_box(Equations, 0, 0, !show_solution && selected_fill_box == -1);
        display_text_box(Solution, 0, 0, show_solution && selected_fill_box == -1);

        int last_pressed = scan_keypad();
        while (last_pressed == -1)
        {
            last_pressed = scan_keypad();
            blink_cursor();
        }

        switch (last_pressed)
        {

        case BACK:
            if (!show_solution && selected_fill_box >= 0 &&
                arr_fill_box[selected_fill_box]->t_size > 0)
            {
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            }
            else
            {
                return 0;
            }
            break;

        case UP:
            if (show_solution)
            {
                selected_fill_box--;
            }
            else
            {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                if (selected_fill_box < first_display)
                    first_display = selected_fill_box;
            }
            break;

        case DOWN:
            if (!show_solution)
            {
                selected_fill_box = std::min(29, selected_fill_box + 1);
                if (arr_fill_box[selected_fill_box] == nullptr)
                    arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                if (first_display + 4 < selected_fill_box)
                    first_display++;
            }
            else
            {
                selected_fill_box++;
            }
            break;

        case RIGHT:
            if (selected_fill_box == -1)
            {
                show_solution = true;

                int NB_EQ = 0;
                for (int i = 0; i < 30; i++)
                {
                    if (arr_fill_box[i] != nullptr && arr_fill_box[i]->t_size > 0)
                        NB_EQ++;
                    else
                        break;
                }

                bool var_present[26] = {false};
                for (int i = 0; i < NB_EQ; i++)
                    for (int c = 0; c < arr_fill_box[i]->t_size; c++)
                    {
                        char ch = arr_fill_box[i]->text[c];
                        if (ch >= 'A' && ch <= 'Z')
                            var_present[ch - 'A'] = true;
                    }
                int var_index[26];
                int NB_VAR = 0;
                for (int v = 0; v < 26; v++)
                    if (var_present[v])
                        var_index[NB_VAR++] = v;

                if (NB_VAR == 0 || NB_EQ == 0)
                    break;

                token **left_tok = (token **)malloc(NB_EQ * sizeof(token *));
                token **right_tok = (token **)malloc(NB_EQ * sizeof(token *));
                int *left_sz = (int *)malloc(NB_EQ * sizeof(int));
                int *right_sz = (int *)malloc(NB_EQ * sizeof(int));

                for (int i = 0; i < NB_EQ; i++)
                {
                    int k = 0;
                    while (k < arr_fill_box[i]->t_size && arr_fill_box[i]->text[k] != '=')
                        k++;

                    int ts = 0;
                    token *tl = parse_string_to_token(arr_fill_box[i]->text, k, &ts);
                    left_tok[i] = shunting_yard(tl, ts);
                    left_sz[i] = count_yarded(tl, ts);
                    free(tl);

                    int ts2 = 0;
                    token *tr = parse_string_to_token(&arr_fill_box[i]->text[k + 1],
                                                      arr_fill_box[i]->t_size - k - 1, &ts2);
                    right_tok[i] = shunting_yard(tr, ts2);
                    right_sz[i] = count_yarded(tr, ts2);
                    free(tr);
                }

                complex **mat = init_c_Mat(NB_VAR + 1, NB_EQ, (complex){0, 0});
                for (int i = 0; i < NB_EQ; i++)
                {
                    for (int v = 0; v < NB_VAR; v++)
                    {
                        int letter = var_index[v];
                        complex lc = csub(evaluate_npi_c_var(left_tok[i], left_sz[i], (complex){1, 0}, 'A' + letter),
                                          evaluate_npi_c_var(left_tok[i], left_sz[i], (complex){0, 0}, 'A' + letter));
                        complex rc = csub(evaluate_npi_c_var(right_tok[i], right_sz[i], (complex){1, 0}, 'A' + letter),
                                          evaluate_npi_c_var(right_tok[i], right_sz[i], (complex){0, 0}, 'A' + letter));
                        mat[i][v] = csub(lc, rc);
                    }
                    complex lk = evaluate_npi_c_var(left_tok[i], left_sz[i], (complex){0, 0}, 'A' + var_index[0]);
                    complex rk = evaluate_npi_c_var(right_tok[i], right_sz[i], (complex){0, 0}, 'A' + var_index[0]);
                    mat[i][NB_VAR] = csub(rk, lk);
                }
                complex *sol = solve_c(mat, NB_VAR, NB_EQ);
                for (int i = 0; i < NB_EQ; i++)
                    free(mat[i]);
                free(mat);

                double max_res = 0.0;
                for (int i = 0; i < NB_EQ; i++)
                {
                    complex lv = eval_c_multi(left_tok[i], left_sz[i], sol, NB_VAR, var_index);
                    complex rv = eval_c_multi(right_tok[i], right_sz[i], sol, NB_VAR, var_index);
                    double r = cmod(csub(lv, rv)).re;
                    if (r > max_res)
                        max_res = r;
                }

                if (max_res > 1e-4)
                {
                    free(sol);
                    sol = solve_nonlinear_c(left_tok, left_sz,
                                            right_tok, right_sz,
                                            NB_EQ, NB_VAR, var_index);
                }

                bool is_exact = (max_res <= 1e-4);

                for (int v = 0; v < NB_VAR; v++)
                {
                    arr_solution[v]->text[0] = 'A' + var_index[v];
                    arr_solution[v]->text[1] = '=';
                    int written = complex_to_string(sol[v], arr_solution[v]->text + 2);
                    arr_solution[v]->t_size = written + 2;
                }

                if (is_exact)
                {
                    accuracy_box->text = "Exact";
                    accuracy_box->t_size = 5;
                }
                else
                {
                    accuracy_box->text = "Approche";
                    accuracy_box->t_size = 8;
                }

                free(sol);
                for (int i = 0; i < NB_EQ; i++)
                {
                    free(left_tok[i]);
                    free(right_tok[i]);
                }
                free(left_tok);
                free(right_tok);
                free(left_sz);
                free(right_sz);
            }
            else
            {
                fill_box_right(arr_fill_box[selected_fill_box]);
            }
            break;
        case LEFT:
            show_solution = false;
            if (selected_fill_box != -1)
            {
                fill_box_left(arr_fill_box[selected_fill_box]);
            }
            break;

        case SECOND:
            toggle(&snd);
            break;

        case TOOLS:
            if (selected_fill_box >= 0)
                update_fill_box(arr_fill_box[selected_fill_box], menu_tools(), snd);
            break;

        default:
            if (selected_fill_box >= 0)
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            break;
        }
        sleep_ms(50);
    }
}