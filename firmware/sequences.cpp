#include "headers/ui.h"
#include "headers/Evaluator.h"
#include  "headers/sequences.h"
#include <math.h>
#include "headers/menu.h"
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <algorithm>
#include <cstdio>

#include "headers/grapher.h"

#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_LIME      0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_SILVER    0xC618
#define COLOR_GRAY      0x8410
#define COLOR_MAROON    0x8000
#define COLOR_OLIVE     0x8400
#define COLOR_GREEN     0x0400
#define COLOR_PURPLE    0x8010
#define COLOR_TEAL      0x0410
#define COLOR_NAVY      0x0010


int Sequencer(){
    bool snd = false;
    
    text_box * Graph = create_text_box(160,220,20,160,1,false);
    text_box * Formula = create_text_box(0,220,20,160,1,false);
    Graph->text="Sequ";
    Graph->t_size=5;
    Formula->text="Formula";
    Formula->t_size=7;

    fill_box * n = create_fill_box(0,20,40,320,3);
   const uint16_t palet[] = {
    COLOR_RED,
    COLOR_LIME,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_CYAN,
    COLOR_MAGENTA,
    COLOR_SILVER,
    COLOR_GRAY,
    COLOR_MAROON,
    COLOR_OLIVE,
    COLOR_GREEN,
    COLOR_PURPLE,
    COLOR_TEAL,
    COLOR_NAVY
};

  
    fill_box ** arr_fill_box = (fill_box **)malloc(sizeof(fill_box *)*100);
    for(int i =0 ; i <100;i++){
        arr_fill_box[i]=nullptr;
    }

    int selected_fill_box=0;
    arr_fill_box[0]=n; 
    int first_display =0;
    bool show_graph=false;
    double x_min = -5;
    double x_max = 5;
    double y_min = -5;
    double y_max = 5;
    double cursor_pos = 0;
    int          last_pressed=scan_keypad();
    while(1){
        fill_screen(BACKGROUND_COLOR);  // Bleu
        std::printf("%i\n",(int)show_graph);
        switch (last_pressed)
        {
            case SECOND:
            toggle(&snd);
            break;
        case BACK:
            if(!show_graph && arr_fill_box[selected_fill_box]->t_size >0){
                            arr_fill_box[selected_fill_box]->t_size--;
                            arr_fill_box[selected_fill_box]->text[arr_fill_box[selected_fill_box]->t_size]='\0';

                        }    else{
                            return 0;
                        }

        break;
        case UP:
         if(show_graph){
                            selected_fill_box-=1;
                        }else{
                        selected_fill_box= std::max(-1,selected_fill_box-1);
                        if(selected_fill_box<first_display)
                            first_display=selected_fill_box;
                        }
        break;
        case DOWN:
        if( !show_graph){
                        selected_fill_box= std::min(100,selected_fill_box+1);
                        if(arr_fill_box[selected_fill_box]==nullptr)
                            arr_fill_box[selected_fill_box]=create_fill_box(0,20,40,320,3);
                        if(first_display+4<selected_fill_box)
                            first_display++;
                    }else{

        selected_fill_box++;
                    }
        break;
        case RIGHT:
           if(show_graph && selected_fill_box!=-1){
                        cursor_pos+=(x_max-x_min)/100;
                    }else{
                    selected_fill_box=-1;
                    show_graph=true;
                    printf("GOOD");
                    }  
        break;
        case LEFT:
           if(show_graph && selected_fill_box!=-1){
                        cursor_pos-=(x_max-x_min)/100;
                    }else {
                    show_graph=false;
                    selected_fill_box=-1;
                    }
                    break;
        case X:
                    printf("AAA");
                    arr_fill_box[selected_fill_box]->text[arr_fill_box[selected_fill_box]->t_size++]='X';
        break;
        default:
        if(show_graph){
            if (last_pressed==PLUS){
                        x_min *= 2;
                        x_max *= 2;
                        y_min *= 2;
                        y_max *= 2;
            }else if (last_pressed==MINUS){
                        x_min *= 2;
                        x_max *= 2;
                        y_min *= 2;
                        y_max *= 2;
            }
        }else
        if (selected_fill_box>=0 && !show_graph) {
    update_fill_box(arr_fill_box[selected_fill_box],last_pressed,snd);
                   }
            break;
        }


        if(!show_graph){

        for(int i = std::max(first_display,0);i<first_display+6;i++){
            display_fill_box(arr_fill_box[i],160-(i-std::max(first_display,0))*41,i==selected_fill_box,i,'f');
        }

        }else{
            axis();
            for(int i = 0 ; i<100;i++){  
                printf("%i %i\n",i, (int)(arr_fill_box[i]!=nullptr));   
                if(arr_fill_box[i]!=nullptr){      
                    int yarded = count_yarded(arr_fill_box[i]->text);
                    int tokenized_size =0;
                    token * tokenized = parse_string_to_token(arr_fill_box[i]->text,arr_fill_box[i]->t_size,&tokenized_size);
                    token * out = shunting_yard(tokenized,tokenized_size);
                    graph(cursor_pos,tokenized,x_min,x_max,y_min,y_max,tokenized_size,   palet[i%14],i==selected_fill_box, false);
                }
            }
        }
        display_text_box(Formula,0,!show_graph && selected_fill_box==-1);
        display_text_box(Graph,0,show_graph&& selected_fill_box==-1);
        
         last_pressed=scan_keypad();
    while(last_pressed==-1){
        last_pressed =scan_keypad();
    }
    sleep_ms(150);

    }

    
}
