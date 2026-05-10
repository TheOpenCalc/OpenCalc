#include "headers/ui.h"
#include "headers/Calc.h"
#include "headers/Evaluator.h"
#include "headers/menu.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>
#include <cstdlib>
#include "Tools.h"
#include "headers/colors.h"
int Calc()
{
    bool snd = false;
    fill_box **history = (fill_box **)malloc(sizeof(fill_box *) * (HISTORY_SIZE + 1));

    for (int i = 0; i <= HISTORY_SIZE; i++)
    {
        history[i] = create_fill_box(0, 0, 40, 320, 2);
        history[i]->color = i % 2 == 0 ? FRONTGROUND_COLOR : FRONTGROUND_COLOR_BIS;
    }

    int cur_last_history = 0;
    int cur_selected = 0;
    fill_screen(BACKGROUND_COLOR); // Bleu

    while (true)
    {
        int mv = 0;

        for (int i = cur_selected; i >= cur_selected - 5; i--)
        {
            int a = i % HISTORY_SIZE;
            if (a < 0)
            {
                a += HISTORY_SIZE;
            }
            if (history[a] != nullptr)
            {
                int old_mv = mv;
                int tok_n = 0;
                int input_size;
                token *toks = parse_string_to_token(history[a]->text, history[a]->t_size, &tok_n);
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
                history[a]->h = mv - old_mv + 40;
                display_fill_box(history[a], 160 - (i - cur_selected + 4) * 40 + old_mv, (a - cur_selected) % HISTORY_SIZE == 0, -1, ' ');
            }
        }

        int last_pressed = scan_keypad();

        while (last_pressed == -1)
        {
            blink_cursor();
            last_pressed = scan_keypad();
        }

        sleep_ms(50);

        switch (last_pressed)
        {
        case SECOND:
            toggle(&snd);
            break;
        case UP:
            cur_selected = (cur_selected - 1) % HISTORY_SIZE;
            break;
        case DOWN:
            cur_selected = min(cur_last_history, (cur_selected + 1) % HISTORY_SIZE);
            break;
        case OK:
        case ENTER:
        {
            int l = cur_last_history;
            cur_last_history = (cur_last_history + 1) % HISTORY_SIZE;
            if (cur_last_history < 0)
            {
                cur_last_history += HISTORY_SIZE + 1;
            }
            if (history[cur_last_history] == nullptr)
            {
                history[cur_last_history] = create_fill_box(0, 0, 20, 320, 2);
            }
            int tokenized_size;

            token *t = parse_string_to_token(history[l]->text, history[l]->t_size, &tokenized_size);

            token *out = shunting_yard(t, tokenized_size);
            int yarded = count_yarded(t, tokenized_size);
            double temp = evaluate_npi(out, yarded);
            history[cur_last_history]->t_size = double_to_string_scientific(temp, (history[cur_last_history]->text));
            cur_last_history++;
            cur_selected = cur_last_history;
            free(t);
            free(out);
        }
            history[cur_last_history]->curso_pos = history[cur_last_history]->t_size;
            break;
        case LEFT:
            cur_selected = cur_last_history;
            fill_box_left(history[cur_selected]);
            break;
        case RIGHT:
            cur_selected = cur_last_history;
            fill_box_right(history[cur_selected]);
            break;
        case X:
            break;
        case BACK:
            if (history[cur_last_history]->t_size > 0)
            {
                update_fill_box(history[cur_last_history], last_pressed, snd);
            }
            else
            {
                return 0;
            }
            break;
        case TOOLS:
        {

            update_fill_box(history[cur_last_history], menu_tools(), snd);
        }
        break;
        default:
            update_fill_box(history[cur_last_history], last_pressed, snd);
            break;
        }

        if (cur_selected < 0)
        {
            cur_selected += 50;
        }
    }
    return 0;
}
