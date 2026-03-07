#include "headers/ui.h"
#include "headers/Evaluator.h"
#include "headers/sequences.h"
#include "headers/stack.h"
#include <math.h>
#include "headers/menu.h"
#include <algorithm>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cstdio>
#include "headers/Solver.h"
#include <stdio.h>
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
    for (int i = 0; i < nb_eq - 1; i++) {
        int    max_row = i;
        double max_val = fabs(input[i][i]);
        for (int k = i + 1; k < nb_eq; k++) {
            if (fabs(input[k][i]) > max_val) {
                max_val = fabs(input[k][i]);
                max_row = k;
            }
        }
        if (max_row != i) {
            double *tmp    = input[i];
            input[i]       = input[max_row];
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

static double eval_multi(token *in, int n,
                         double *values, int nb_var, int *var_indices)
{
    Stack_d nb_stack;
    init(&nb_stack, n + 4);
    double a, b;

    for (int i = 0; i < n; i++) {
        if (in[i].type == 'X') {
            int ch = (int)(in[i].value) + (int)'a';
            double val = 0.0;
            for (int v = 0; v < nb_var; v++)
                if (ch == 'A' + var_indices[v]) { val = values[v]; break; }
            push(&nb_stack, val);
        } else if (in[i].type == 'n') {
            push(&nb_stack, in[i].value);
        } else if (is_in(in[i].type, (char *)"+-*/^")) {
            a = peek(&nb_stack); pop(&nb_stack);
            b = peek(&nb_stack); pop(&nb_stack);
            switch (in[i].type) {
                case '+': push(&nb_stack, a + b);     break;
                case '*': push(&nb_stack, a * b);     break;
                case '-': push(&nb_stack, b - a);     break;
                case '/':
                    if (a == 0) { free(&nb_stack); return NAN; }
                    push(&nb_stack, b / a);            break;
                case '^': push(&nb_stack, pow(b, a)); break;
                default: break;
            }
        } else {
            a = peek(&nb_stack); pop(&nb_stack);
            switch (in[i].type) {
                case 'r': a = sqrt(a);   break;
                case 'l': a = log(a);    break;
                case 'c': a = cos(a);    break;
                case 's': a = sin(a);    break;
                case 't': a = tan(a);    break;
                case 'u': a = acos(a);   break;
                case 'v': a = asin(a);   break;
                case 'w': a = atan(a);   break;
                case 'f': a = cosh(a);   break;
                case 'g': a = sinh(a);   break;
                case 'h': a = tanh(a);   break;
                case 'i': a = acosh(a);  break;
                case 'j': a = asinh(a);  break;
                case 'k': a = atanh(a);  break;
                case '!': a = fact(a);   break;
                case 'p': push(&nb_stack, a); a = 3.14159265358979323846; break;
                case 'e': push(&nb_stack, a); a = 2.71828182845904523536; break;
                default: break;
            }
            push(&nb_stack, a);
        }
    }

    a = peek(&nb_stack);
    free(&nb_stack);
    return a;
}


static double *solve_nonlinear(token **left_tok, int *left_sz,
                               token **right_tok, int *right_sz,
                               int nb_eq, int nb_var, int *var_indices)
{
    const double H        = 1e-6;
    const int    MAX_ITER = 100;
    const double TOL      = 1e-8;

    double *x = (double *)malloc(nb_var * sizeof(double));
    bool converged = false;

  double starts[] = {1.0, -1.0, 0.1, 10.0};
for (int start = 0; start < 4 && !converged; start++) {
    for (int v = 0; v < nb_var; v++)
        x[v] = starts[start];
        for (int iter = 0; iter < MAX_ITER && !converged; iter++) {

            double *F = (double *)malloc(nb_eq * sizeof(double));
            double  max_f = 0.0;
            for (int i = 0; i < nb_eq; i++) {
                F[i] = eval_multi(left_tok[i],  left_sz[i],  x, nb_var, var_indices)
                     - eval_multi(right_tok[i], right_sz[i], x, nb_var, var_indices);
                if (fabs(F[i]) > max_f) max_f = fabs(F[i]);
            }

            if (max_f < TOL) {
                free(F);
                converged = true;
                break;
            }

            double **J = init_2d_Mat(nb_var + 1, nb_eq, 0.0);
            for (int j = 0; j < nb_var; j++) {
                double *xh = (double *)malloc(nb_var * sizeof(double));
                for (int v = 0; v < nb_var; v++) xh[v] = x[v];
                xh[j] += H;
                for (int i = 0; i < nb_eq; i++) {
                    double Fh = eval_multi(left_tok[i],  left_sz[i],  xh, nb_var, var_indices)
                              - eval_multi(right_tok[i], right_sz[i], xh, nb_var, var_indices);
                    J[i][j] = (Fh - F[i]) / H;
                }
                free(xh);
            }
            for (int i = 0; i < nb_eq; i++) J[i][nb_var] = -F[i];

            double *dx = solve(J, nb_var, nb_eq);

            for (int v = 0; v < nb_var; v++) x[v] += dx[v];

            free(F);
            free(dx);
            for (int i = 0; i < nb_eq; i++) free(J[i]);
            free(J);
        }
    }

    return x;
}


void printdouble2D(double **input, int L, int H)
{
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < H; j++)
            printf("%f\t\t", input[i][j]);
        printf("\n");
    }
}

int Solver()
{
    stdio_init_all();
    bool snd = false;

    text_box *Solution  = create_text_box(160, 220, 20, 160, 1, false);
    text_box *Equations = create_text_box(0,   220, 20, 160, 1, false);
    Solution->text  = "Solut";   Solution->t_size  = 5;
    Equations->text = "Equatio"; Equations->t_size = 7;

    fill_box **arr_solution = (fill_box **)malloc(sizeof(fill_box *) * 30);
    fill_box **arr_fill_box = (fill_box **)malloc(sizeof(fill_box *) * 30);
    for (int i = 0; i < 30; i++) {
        arr_fill_box[i] = nullptr;
        arr_solution[i] = create_fill_box(0, 20, 40, 320, 3);
    }

    int  selected_fill_box = 0;
    arr_fill_box[0] = create_fill_box(0, 20, 40, 320, 3);
    int  first_display = 0;
    bool show_solution = false;

    fill_screen(BACKGROUND_COLOR);
text_box *accuracy_box = create_text_box(0, 0, 20, 160, 1, false);
accuracy_box->text = "            ";  
accuracy_box->t_size = 0;
    while (1) {
           if (!show_solution) {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++)
                display_fill_box(arr_fill_box[i],
                                 160 - (i - std::max(first_display, 0)) * 41,
                                 i == selected_fill_box, -1, ' ');
        } else {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++)
                display_fill_box(arr_solution[i],
                                 160 - (i - std::max(first_display, 0)) * 41,
                                 i == selected_fill_box, -1, ' ');
            display_text_box(accuracy_box, 0, 0, false); 
        }

        display_text_box(Equations, 0, 0, !show_solution && selected_fill_box == -1);
        display_text_box(Solution,  0, 0,  show_solution && selected_fill_box == -1);

        int last_pressed = scan_keypad();
        while (last_pressed == -1) {
            last_pressed = scan_keypad();
            blink_cursor();
        }

        switch (last_pressed) {

        case BACK:
            if (!show_solution && selected_fill_box >= 0 &&
                arr_fill_box[selected_fill_box]->t_size > 0) {
                arr_fill_box[selected_fill_box]->t_size--;
                arr_fill_box[selected_fill_box]->text[arr_fill_box[selected_fill_box]->t_size] = '\0';
            } else {
                return 0;
            }
            break;

        case UP:
            if (show_solution) {
                selected_fill_box--;
            } else {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                if (selected_fill_box < first_display)
                    first_display = selected_fill_box;
            }
            break;

        case DOWN:
            if (!show_solution) {
                selected_fill_box = std::min(29, selected_fill_box + 1);
                if (arr_fill_box[selected_fill_box] == nullptr)
                    arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                if (first_display + 4 < selected_fill_box)
                    first_display++;
            } else {
                selected_fill_box++;
            }
            break;

        case RIGHT:
        {
            show_solution = true;

            int NB_EQ = 0;
            for (int i = 0; i < 30; i++) {
                if (arr_fill_box[i] != nullptr && arr_fill_box[i]->t_size > 0) NB_EQ++;
                else break;
            }

            bool var_present[26] = {false};
            for (int i = 0; i < NB_EQ; i++)
                for (int c = 0; c < arr_fill_box[i]->t_size; c++) {
                    char ch = arr_fill_box[i]->text[c];
                    if (ch >= 'A' && ch <= 'Z') var_present[ch - 'A'] = true;
                }
            int var_index[26];
            int NB_VAR = 0;
            for (int v = 0; v < 26; v++)
                if (var_present[v]) var_index[NB_VAR++] = v;

            if (NB_VAR == 0 || NB_EQ == 0) break;

            token **left_tok  = (token **)malloc(NB_EQ * sizeof(token *));
            token **right_tok = (token **)malloc(NB_EQ * sizeof(token *));
            int   *left_sz    = (int *)malloc(NB_EQ * sizeof(int));
            int   *right_sz   = (int *)malloc(NB_EQ * sizeof(int));

            for (int i = 0; i < NB_EQ; i++) {
                int k = 0;
                while (k < arr_fill_box[i]->t_size && arr_fill_box[i]->text[k] != '=') k++;

                int ts = 0;
                token *tl    = parse_string_to_token(arr_fill_box[i]->text, k, &ts);
                left_tok[i]  = shunting_yard(tl, ts);
                left_sz[i]   = ts;
                free(tl);

                int ts2 = 0;
                token *tr    = parse_string_to_token(&arr_fill_box[i]->text[k + 1],
                                                     arr_fill_box[i]->t_size - k - 1, &ts2);
                right_tok[i] = shunting_yard(tr, ts2);
                right_sz[i]  = ts2;
                free(tr);
            }

            double **mat = init_2d_Mat(NB_EQ, NB_VAR + 1, 0.0);
            for (int i = 0; i < NB_EQ; i++) {
                for (int v = 0; v < NB_VAR; v++) {
                    int letter = var_index[v];
                    double lc = evaluate_npi(left_tok[i],  left_sz[i],  1, 'A' + letter)
                              - evaluate_npi(left_tok[i],  left_sz[i],  0, 'A' + letter);
                    double rc = evaluate_npi(right_tok[i], right_sz[i], 1, 'A' + letter)
                              - evaluate_npi(right_tok[i], right_sz[i], 0, 'A' + letter);
                    mat[i][v] = lc - rc;
                }
                double lk = evaluate_npi(left_tok[i],  left_sz[i],  0, 'A' + var_index[0]);
                double rk = evaluate_npi(right_tok[i], right_sz[i], 0, 'A' + var_index[0]);
                mat[i][NB_VAR] = rk - lk;
            }
            double *sol = solve(mat, NB_VAR, NB_EQ);
            for (int i = 0; i < NB_EQ; i++) free(mat[i]);
            free(mat);

            double max_res = 0.0;
            for (int i = 0; i < NB_EQ; i++) {
                double lv = eval_multi(left_tok[i],  left_sz[i],  sol, NB_VAR, var_index);
                double rv = eval_multi(right_tok[i], right_sz[i], sol, NB_VAR, var_index);
                double r  = fabs(lv - rv);
                if (r > max_res) max_res = r;
            }

            if (max_res > 1e-4) {
                free(sol);
                sol = solve_nonlinear(left_tok,  left_sz,
                                      right_tok, right_sz,
                                      NB_EQ, NB_VAR, var_index);
            }

            bool is_exact = (max_res <= 1e-4);

            for (int v = 0; v < NB_VAR; v++) {
                arr_solution[v]->text[0] = 'A' + var_index[v];
                arr_solution[v]->text[1] = '=';
                int written = double_to_string_scientific(sol[v], arr_solution[v]->text + 2);
                arr_solution[v]->t_size = written + 2;
            }

            if (is_exact) {
                accuracy_box->text = "Exact";
                accuracy_box->t_size = 5;
            } else {
                accuracy_box->text = "Approche";
                accuracy_box->t_size = 8;
            }

            free(sol);
            for (int i = 0; i < NB_EQ; i++) { free(left_tok[i]); free(right_tok[i]); }
            free(left_tok); free(right_tok); free(left_sz); free(right_sz);
        }
            break;
        case LEFT:
            show_solution = false;
            break;

        case SECOND:
            toggle(&snd);
            break;

        default:
            if (selected_fill_box >= 0)
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            break;
        }
        sleep_ms(50);
    }
}