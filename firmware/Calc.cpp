
#include "headers/ui.h"
#include "headers/Calc.h"
#include "headers/Evaluator.h"
#include "headers/menu.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>
#include <cstdlib>
int Calc (){

    
fill_box ** history = (fill_box **)malloc(sizeof(fill_box *)* (HISTORY_SIZE+1));
    for(int i = 0 ; i <= HISTORY_SIZE; i ++){
        history[i]=create_fill_box(0,0,40,320,2);
        history[i]->color = i%2==0 ? FRONTGROUND_COLOR : FRONTGROUND_COLOR_BIS;
    }
    int cur_last_history=0;
    int cur_selected = 0;
    fill_screen(BACKGROUND_COLOR);  // Bleu

    while (true) {
        
        for(int i = cur_selected-4;i<=cur_selected;i++){

           int a = i%HISTORY_SIZE;
            if (a<0){
                a+=HISTORY_SIZE;
            }

            if(history[a]!=nullptr){      
                display_fill_box(history[a],160-(i-cur_selected+4)*40,(a-cur_selected)%HISTORY_SIZE==0,-1,' ');
            }

    }
    int last_pressed =scan_keypad();
    last_pressed=scan_keypad();
    while(last_pressed==-1){
                blink_cursor();

        last_pressed =scan_keypad();
    }

    sleep_ms(150);

    
    switch (last_pressed)
    {
    case UP:
        cur_selected=(cur_selected-1)%HISTORY_SIZE;
        break;
    case DOWN :
        cur_selected=(cur_selected+1)%HISTORY_SIZE;
        break;
        case OK: case ENTER:{
        int l = cur_last_history;
                    cur_last_history=(cur_last_history+1)%HISTORY_SIZE;

                    if(cur_last_history<0)
                        cur_last_history+=HISTORY_SIZE+1;
                    if(history[cur_last_history]==nullptr){
                        history[cur_last_history]=create_fill_box(0,0,20,320,2);
                    }
                    int tokenized_size;

                    token * t = parse_string_to_token(history[l]->text,history[l]->t_size,&tokenized_size);

                    token * out = shunting_yard(t,tokenized_size);


                    double temp = evaluate_npi(out,tokenized_size);
                     history[cur_last_history]->t_size = double_to_string_scientific(temp, (history[cur_last_history]->text));
                     cur_selected=cur_last_history;
                }
                history[cur_last_history]->curso_pos=history[cur_last_history]->t_size;
                break;
    case LEFT :
        cur_selected=cur_last_history;
        history[cur_last_history]->curso_pos--;
    break;
    case RIGHT : 
        cur_selected=cur_last_history;
        history[cur_last_history]->curso_pos=min(history[cur_last_history]->curso_pos+1,history[cur_last_history]->t_size);
break;
    case X:
        break;    
        case BACK:
        if(history[cur_last_history]->t_size>0){
            update_fill_box(history[cur_last_history],last_pressed);
        }
        else{
            return 0;
        }
        break;
    default:
    update_fill_box(history[cur_last_history],last_pressed);
    
    
        break;
    }
    if(cur_selected<0)
        cur_selected+=50;
           

}
    return 0;

}