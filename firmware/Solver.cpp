#include "headers/ui.h"
#include "headers/Evaluator.h"
#include "headers/sequences.h"
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
    for (int i = first_nb; i < n; i++) {
        b[i] -= a[i] * K;
    }
}

void normalize(double *a, int size, int n)
{
    double K = 1 / a[n];
    for (int i = 0; i < size; i++) {
        a[i] *= K;
    }
}

bool comp(double *a, double *b, int n)
{
    double c = b[0] - a[0];
    int i = 0;
    while (abs(c) < 0.001 && i < n) {
        c += b[i] - a[i];
        i++;
    }
    return c > 0;
}

void insertion_sort(double **input, int nb_element, int size_element)
{
    int start = 0;
    while (start != nb_element) {
        int k = start;
        for (int i = start + 1; i < nb_element; i++) {
            printf("%i\n", i);
            if (comp(input[k], input[i], size_element)) {
                double *temp = input[k];
                input[k] = input[i];
                input[i] = temp;
            }
        }
        start++;
    }
}

double *solve(double **input, int nb_var, int nb_eq)
{
    insertion_sort(input, nb_eq, nb_var);
    for (int i = 0; i < nb_eq - 1; i++) {
        for (int k = i + 1; k < nb_eq; k++) {
            simplify_first((input[i]), (input[k]), nb_var + 1, i);
        }
    }
    printdouble2D(input, 5, 6);
    printf("\n\n");
    for (int i = 0; i < nb_eq; i++) {
        normalize(input[i], nb_var + 1, i);
    }

    printdouble2D(input, 5, 6);
    printf("\n\n");
    double *solution = (double*) malloc(sizeof(double) * nb_var);
    for (int i = 0; i < nb_var; i++) {
        solution[i] = input[i][nb_var];
    }

    for (int i = 0; i < nb_var; i++) {
        printf("%f \n", solution[i]);
    }

    for (int i = nb_eq - 1; i >= 0; i--) {
        for (int k = 0; k < nb_var; k++) {
            if (k != i) {
                solution[i] -= solution[k] * input[i][k];
            }
        }
    }

    return solution;
}

void printdouble2D(double **input, int L, int H)
{
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < H; j++) {
            printf("%f\t\t", input[i][j]);
        }
        printf("\n");
    }
}

int Solver()
{
    stdio_init_all();
    bool snd = false;
    srand(time(NULL));
    double **c = (double**) malloc(sizeof(double*) * 5);
    for (int i = 0; i < 5; i++) {
        c[i] = (double*) malloc(sizeof(double) * 6);
        for (int j = 0; j < 6; j++) {
            c[i][j] = (rand() % 20000) / 500.0;
        }
    }
    printdouble2D(c, 5, 6);

    printf("\n\n");

    solve(c, 5, 5);

    // solve(c,5,5);

    //////////////////////////////////////////
    //////////////////////////////////////////
    //////////////////////////////////////////

    text_box *Solution = create_text_box(160, 220, 20, 160, 1, false);
    text_box *Equations = create_text_box(0, 220, 20, 160, 1, false);
    Solution->text = "Solut";
    Solution->t_size = 5;
    Equations->text = "Equatio";
    Equations->t_size = 7;

    fill_box **arr_solution = (fill_box**) malloc(sizeof(fill_box*) * 30);
    fill_box **arr_fill_box = (fill_box**) malloc(sizeof(fill_box*) * 30);
    for (int i = 0; i < 30; i++) {
        arr_fill_box[i] = nullptr;
        arr_solution[i] = create_fill_box(0, 20, 40, 320, 3);
    }

    int selected_fill_box = 0;
    arr_fill_box[0] = create_fill_box(0, 20, 40, 320, 3);
    int first_display = 0;
    bool show_solution = false;

    double cursor_pos = 0;

    fill_screen(BACKGROUND_COLOR);

    while (1) {
        int last_pressed = scan_keypad();

        if (!show_solution) {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++) {
                display_fill_box(arr_fill_box[i], 160 - (i - std::max(first_display, 0)) * 41, i == selected_fill_box, -1, ' ');
            }
        } else {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++) {
                display_fill_box(arr_solution[i], 160 - (i - std::max(first_display, 0)) * 41, i == selected_fill_box, -1, ' ');
            }
        }

        display_text_box(Equations, 0,0, !show_solution && selected_fill_box == -1);
        display_text_box(Solution,0, 0, show_solution && selected_fill_box == -1);
        last_pressed = scan_keypad();

        while (last_pressed == -1) {
            last_pressed = scan_keypad();
            blink_cursor();
        }
        
        switch (last_pressed) {
        case BACK :
            if (!show_solution && arr_fill_box[selected_fill_box]->t_size > 0) {
                arr_fill_box[selected_fill_box]->t_size--;
                arr_fill_box[selected_fill_box]->text[arr_fill_box[selected_fill_box]->t_size] = '\0';
            } else {
                return 0;
            }
            break;
        case UP :
            if (show_solution) {
                selected_fill_box -= 1;
            } else {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                if (selected_fill_box < first_display) {
                    first_display = selected_fill_box;
                }
            }
            break;
        case DOWN :
            if (!show_solution) {
                selected_fill_box = std::min(100, selected_fill_box + 1);
                if (arr_fill_box[selected_fill_box] == nullptr) {
                    arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                }
                if (first_display + 4 < selected_fill_box) {
                    first_display++;
                }
            } else {
                selected_fill_box++;
            }
            break;
        case RIGHT :
        {
            int NB_VAR = 2;
            int NB_EQ = 2;
            show_solution = true;

            double **mat = init_2d_Mat(20, 10, 0);

            for (int i = 0; i <= NB_EQ; i++) {
                if (arr_fill_box[i] != nullptr) {
                    int k = 0;
                    while (k < arr_fill_box[i]->t_size && arr_fill_box[i]->text[k] != '=') {
                        k++;
                    }
                    int tokenized_size = 0;

                    for (int letter = 0; letter < NB_VAR; letter++) {
                        token *t = parse_string_to_token(arr_fill_box[i]->text, k, &tokenized_size);

                        token *out = shunting_yard(t, tokenized_size);

                        double temp = evaluate_npi(out, tokenized_size, 1, 'A' + letter) - evaluate_npi(out, tokenized_size, 0, 'A' + letter);
                        arr_solution[i]->t_size = double_to_string_scientific(temp, (arr_solution[i]->text));

                        int tokenized_sizeb = 0;
                        token *tb = parse_string_to_token(&((arr_fill_box[i]->text)[k + 1]), arr_fill_box[i]->t_size - k - 1, &tokenized_sizeb);
                        token *outb = shunting_yard(tb, tokenized_sizeb);
                        double tempb = evaluate_npi(outb, tokenized_sizeb, 1, 'A' + letter)-evaluate_npi(outb, tokenized_sizeb, 0, 'A' + letter);

                        mat[i][letter] = temp - tempb;
                    }
                    token *t = parse_string_to_token(arr_fill_box[i]->text, k, &tokenized_size);

                    token *out = shunting_yard(t, tokenized_size);

                    double temp = evaluate_npi(out, tokenized_size, 0, 'A');

                    arr_solution[i]->t_size = double_to_string_scientific(temp, (arr_solution[i]->text));

                    int tokenized_sizeb = 0;
                    token *tb = parse_string_to_token(&((arr_fill_box[i]->text)[k + 1]), arr_fill_box[i]->t_size - k - 1, &tokenized_sizeb);
                    token *outb = shunting_yard(tb, tokenized_sizeb);
                    double tempb = evaluate_npi(outb, tokenized_sizeb, 0, 'A');
                    mat[i][NB_VAR ] = tempb - temp;
                }
            }

            double *sol = solve(mat, NB_VAR, NB_EQ);
            for (int i = 0; i < NB_VAR; i++) {
                arr_solution[i]->t_size = double_to_string_scientific(sol[i], (arr_solution[i]->text));
            }
        }
            break;
        case LEFT :
            show_solution = false;
            break;
        case SECOND :
            toggle(&snd);
        default :
            update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            break;
        }
        sleep_ms(50);
    }
}
