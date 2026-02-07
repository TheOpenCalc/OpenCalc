#include "headers/elements.h"
#include "headers/ui.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>
#include "menu.h"
#include <stdlib.h>

void display_table()
{
    fill_screen(BACKGROUND_COLOR);  // Bleu
    int hauteur[33] = {7, 6, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 7};
    text_box ***table = (text_box***) malloc(sizeof(text_box*) * 32);
    int k = 0;
    char *name[] = {"Fr", "Cs", "Rb", "K", "Na", "Li", "H", "Ra", "Ba", "Sr", "Ca", "Mg", "Be", "Ac", "La", "Th", "Ce",
                    "Pa", "Pr", "U", "Nd", "Np", "Pm", "Pu", "Sm", "Am", "Eu", "Cm", "Gd", "Bk", "Tb", "Cf", "Dy", "Es",
                    "Ho", "Fm", "Er", "Md", "Tm", "No", "Yb", "Lr", "Lu", "Y", "Sc", "Rf", "Hf", "Zr", "Ti", "Db", "Ta",
                    "Nb", "V", "Sg", "W", "Mo", "Cr", "Bh", "Re", "Tc", "Mn", "Hs", "Os", "Ru", "Fe", "Mt", "Ir", "Rh",
                    "Co", "Ds", "Pt", "Pd", "Ni", "Rg", "Au", "Ag", "Cu", "Cn", "Hg", "Cd", "Zn", "Nh", "Tl", "In", "Ga",
                    "Al", "B", "Fl", "Pb", "Sn", "Ge", "Si", "C", "Mc", "Bi", "SB", "As", "P", "N", "Lv", "Po", "Te",
                    "Se", "S", "O", "Ts", "At", "I", "Br", "Cl", "F", "Og", "Rn", "Xe", "Kr", "Ar", "Ne", "He"};

    char *appearance[]=
                    {"colorless gas","colorless gas, exhibiting a red-orange glow when placed in a high-voltage electric field",
                        "silvery-white","white-gray metallic","black-brown","null","colorless gas, liquid or solid","null","null",
                        "colorless gas exhibiting an orange-red glow when placed in a high voltage electric field","silvery white metallic",
                        "shiny grey solid","silvery gray metallic","crystalline, reflective with bluish-tinged faces",
                        "colourless, waxy white, yellow, scarlet, red, violet, black","lemon yellow sintered microcrystals",
                        "pale yellow-green gas","colorless gas exhibiting a lilac/violet glow when placed in a high voltage electric field",
                        "silvery gray","null","silvery white","silvery grey-white metallic","blue-silver-grey metal","silvery metallic",
                        "silvery metallic","lustrous metallic with a grayish tinge","hard lustrous gray metal",
                        "lustrous, metallic, and silver with a gold tinge","red-orange metallic luster","silver-gray","silver-white",
                        "grayish-white","metallic grey","black, red, and gray (not pictured) allotropes","null",
                        "colorless gas, exhibiting a whitish glow in a high electric field","grey white","null","silvery white",
                        "silvery white","gray metallic, bluish when oxidized","gray metallic","shiny gray metal","silvery white metallic",
                        "silvery white metallic","silvery white","lustrous white metal","silvery bluish-gray metallic","silvery lustrous gray",
                        "silvery-white (beta, \u03b2) or gray (alpha, \u03b1)","silvery lustrous gray","null","lustrous metallic gray, violet as a gas",
                        "colorless gas, exhibiting a blue glow when placed in a high voltage electric field","silvery gold","null","silvery white",
                        "silvery white","grayish white","silvery white","metallic","silvery white","null","silvery white","silvery white",
                        "silvery white","silvery white","silvery white","silvery gray","null","silvery white","steel gray","gray blue",
                        "grayish white, lustrous","silvery-grayish","silvery, blue cast","silvery white","silvery white","metallic yellow",
                        "silvery","silvery white","metallic gray","lustrous silver","silvery","unknown, probably metallic",
                        "colorless gas, occasionally glows green or red in discharge tubes","null","silvery white metallic","null",
                        "silvery, often with black tarnish","bright, silvery metallic luster","null","silvery metallic",
                        "silvery white, tarnishing to dark gray in air","silvery white","silvery metallic, glows purple in the dark",
                        "silvery","silvery","silver-colored","null","null","null","null","null","null","null","null","null","null","null",
                        "null","null","null","null","null","null","null","null","null"};
   
   
    int atomic_mass[]=
                        {1.008,4.0026022,6.94,9.01218315,10.81,12.011,14.007,15.999,18.9984031636,20.17976,22.989769282,24.305,26.98153857,
                        28.085,30.9737619985,32.06,35.45,39.9481,39.09831,40.0784,44.9559085,47.8671,50.94151,51.99616,54.9380443,55.8452,58.9331944,58.69344,
                        63.5463,65.382,69.7231,72.6308,74.9215956,78.9718,79.904,83.7982,85.46783,87.621,88.905842,91.2242,92.906372,95.951,98,101.072,
                        102.905502,106.421,107.86822,112.4144,114.8181,118.7107,121.7601,127.603,126.904473,131.2936,132.905451966,137.3277,138.905477,
                        140.1161,140.907662,144.2423,145,150.362,151.9641,157.253,158.925352,162.5001,164.930332,167.2593,168.934222,173.0451,174.96681,
                        178.492,180.947882,183.841,186.2071,190.233,192.2173,195.0849,196.9665695,200.5923,204.38,207.21,208.980401,209,210,222,223,226,
                        227,232.03774,231.035882,238.028913,237,244,243,247,247,251,252,257,258,259,266,267,268,269,270,269,278,281,282,285,286,289,289,
                        293,294,294,315};

    int boiling_point []=
                        {20.271,4.222,1603,2742,4200,-1000,77.355,90.188,85.03,27.104,1156.09,1363,2743,3538,-1000,717.8,239.11,87.302,1032,1757,3109,
                            3560,3680,2944,2334,3134,3200,3003,2835,1180,2673,3106,-1000,958,332,119.93,961,1650,3203,4650,5017,4912,4538,4423,3968,
                            3236,2435,1040,2345,2875,1908,1261,457.4,165.051,944,2118,3737,3716,3403,3347,3273,2173,1802,3273,3396,2840,2873,3141,2223,
                            1469,3675,4876,5731,6203,5869,5285,4403,4098,3243,629.88,1746,2022,1837,1235,610,211.5,950,2010,3500,5061,4300,4404,4447,
                            3505,2880,3383,2900,1743,1269,-1000,-1000,-1000,-1000,5800,-1000,-1000,-1000,-1000,-1000,-1000,-1000,3570,1430,420,1400,1085,
                            883,350,630};
    int density [] =
                        {0.08988,0.1786,0.534,1.85,2.08,1.821,1.251,1.429,1.696,0.9002,0.968,1.738,2.7,2.329,1.823,2.07,3.2,1.784,0.862,1.55,2.985,
                            4.506,6,7.19,7.21,7.874,8.9,8.908,8.96,7.14,5.91,5.323,5.727,4.81,3.1028,3.749,1.532,2.64,4.472,6.52,8.57,10.28,11,12.45,
                            12.41,12.023,10.49,8.65,7.31,7.365,6.697,6.24,4.933,5.894,1.93,3.51,6.162,6.77,6.77,7.01,7.26,7.52,5.264,7.9,8.23,8.54,
                            8.79,9.066,9.32,6.9,9.841,13.31,16.69,19.25,21.02,22.59,22.56,21.45,19.3,13.534,11.85,11.34,9.78,9.196,6.35,9.73,1.87,5.5,
                            10,11.724,15.37,19.1,20.45,19.816,12,13.51,14.78,15.1,8.84,-1,-1,-1,-1,23.2,29.3,35,37.1,40.7,37.4,34.8,28.7,23.7,16,14,
                            13.5,12.9,7.17,4.95,3};

    char * discoverer[]=
                        {"Henry Cavendish","Pierre Janssen","Johan August Arfwedson","Louis Nicolas Vauquelin","Joseph Louis Gay-Lussac","Ancient Egypt",
                        "Daniel Rutherford","Carl Wilhelm Scheele","Andr\u00e9-Marie Amp\u00e8re","Morris Travers","Humphry Davy","Joseph Black","null",
                        "J\u00f6ns Jacob Berzelius","Hennig Brand","Ancient china","Carl Wilhelm Scheele","Lord Rayleigh","Humphry Davy","Humphry Davy",
                        "Lars Fredrik Nilson","William Gregor","Andr\u00e9s Manuel del R\u00edo","Louis Nicolas Vauquelin","Torbern Olof Bergman",
                        "5000 BC","Georg Brandt","Axel Fredrik Cronstedt","Middle East","India","Lecoq de Boisbaudran","Clemens Winkler","Bronze Age",
                        "J\u00f6ns Jakob Berzelius","Antoine J\u00e9r\u00f4me Balard","William Ramsay","Robert Bunsen","William Cruickshank (chemist)",
                        "Johan Gadolin","Martin Heinrich Klaproth","Charles Hatchett","Carl Wilhelm Scheele","Emilio Segr\u00e8","Karl Ernst Claus",
                        "William Hyde Wollaston","William Hyde Wollaston","unknown, before 5000 BC","Karl Samuel Leberecht Hermann","Ferdinand Reich",
                        "unknown, before 3500 BC","unknown, before 3000 BC","Franz-Joseph M\u00fcller von Reichenstein","Bernard Courtois",
                        "William Ramsay","Robert Bunsen","Carl Wilhelm Scheele","Carl Gustaf Mosander","Martin Heinrich Klaproth","Carl Auer von Welsbach",
                        "Carl Auer von Welsbach","Chien Shiung Wu","Lecoq de Boisbaudran","Eug\u00e8ne-Anatole Demar\u00e7ay","Jean Charles Galissard de Marignac",
                        "Carl Gustaf Mosander","Lecoq de Boisbaudran","Marc Delafontaine","Carl Gustaf Mosander","Per Teodor Cleve",
                        "Jean Charles Galissard de Marignac","Georges Urbain","Dirk Coster","Anders Gustaf Ekeberg","Carl Wilhelm Scheele","Masataka Ogawa",
                        "Smithson Tennant","Smithson Tennant","Antonio de Ulloa","Middle East","unknown, before 2000 BCE","William Crookes","Middle East",
                        "Claude Fran\u00e7ois Geoffroy","Pierre Curie","Dale R. Corson","Friedrich Ernst Dorn","Marguerite Perey","Pierre Curie",
                        "Friedrich Oskar Giesel","J\u00f6ns Jakob Berzelius","William Crookes","Martin Heinrich Klaproth","Edwin McMillan","Glenn T. Seaborg",
                        "Glenn T. Seaborg","Glenn T. Seaborg","Lawrence Berkeley National Laboratory","Lawrence Berkeley National Laboratory",
                        "Lawrence Berkeley National Laboratory","Lawrence Berkeley National Laboratory","Lawrence Berkeley National Laboratory",
                        "Joint Institute for Nuclear Research","Lawrence Berkeley National Laboratory","Joint Institute for Nuclear Research",
                        "Joint Institute for Nuclear Research","Lawrence Berkeley National Laboratory","Gesellschaft f\u00fcr Schwerionenforschung",
                        "Gesellschaft f\u00fcr Schwerionenforschung","Gesellschaft f\u00fcr Schwerionenforschung","Gesellschaft f\u00fcr Schwerionenforschung",
                        "Gesellschaft f\u00fcr Schwerionenforschung","Gesellschaft f\u00fcr Schwerionenforschung","RIKEN","Joint Institute for Nuclear Research",
                        "Joint Institute for Nuclear Research","Joint Institute for Nuclear Research","Joint Institute for Nuclear Research",
                        "Joint Institute for Nuclear Research","GSI Helmholtz Centre for Heavy Ion Research"};
    int col = 0;
    int L, C = 0;
    int shift = 0;
    for (int i = 0; i < 32; i++) {
        table[i] = (text_box**) malloc(sizeof(text_box*) * hauteur[i]);
        for (int j = 0; j < hauteur[i]; j++) {
            table[i][j] = create_text_box(31 * i + 10,  31 * j + 10, 30,30,0,false);



            table[i][j]->t_size = 2;

            if (k < 6) {
                table[i][j]->col = 0xf000;
            } else if (k == 6) {
                table[i][j]->col = 0x07e0;
            } else if (k < 13) {
                table[i][j]->col = 0xf674;
            } else if (k < 43) {
                table[i][j]->col = k % 2 == 0 ? 0xfddf : 0xfcd9;
            } else if (k == 73 || k == 69 || k == 65 || k == 81|| k == 87 || k == 93 || k == 99 || k == 105 || k == 111) {
                table[i][j]->col = FRONTGROUND_COLOR;
            } else if (k < 78) {
                table[i][j]->col = 0xfeba;
            } else if ((k >= 78 && k <= 85) || k == 88 || k == 89 || k == 94 || k == 100) {
                table[i][j]->col = BACKGROUND_COLOR;
            } else if (k == 86 || k == 91 || k == 90 || k == 96 || k == 95 || k == 106 || k == 111) {
                table[i][j]->col = 0xce53;
            } else if (k < 105) {
                table[i][j]->col = 0x9ff3;
            } else if (k <= 110) {
                table[i][j]->col = 0xfff3;
            } else {
                table[i][j]->col = 0xbf3f;
            }

            if (k < 118) {

                table[i][j]->text=name[k];

            }
            k++;
        }
    }
    int last_pressed = scan_keypad();
    text_box *data = create_text_box(5, 200, 40, 40, 2, true);
    while (true) {
        if (C - ((shift / 31)) < 5) {
            fill_rect(105, 5, 128, 160, BACKGROUND_COLOR);
        } else {
            fill_rect(105, 175, 128, 160, BACKGROUND_COLOR);
        }        
        for (int i = max(C - 10, 0); i < min(C + 10, 32); i++) {
            for (int j = 0; j < hauteur[i]; j++) {
              display_text_box(table[i][j],0,0,C==i && L==j);
            }
        }
        
       /* if (C - ((shift / 31)) > 5) {
            fill_rect(105, 175, 128, 160, 0);
        } else {
            fill_rect(105, 5, 128, 160, 0);
        }*/
        last_pressed = scan_keypad();
        while (last_pressed == -1) {
            last_pressed = scan_keypad();
        }

        switch (last_pressed) {
        case BACK :
            return;
            break;
        case RIGHT :
            C = min(C + 1, 31);
            L = min(hauteur[C] - 1, L);
            break;
        case LEFT :
            C = max(0 ,C - 1);
            L = min(hauteur[C] - 1, L);
            break;
        case UP :
            L = min(L + 1, hauteur[C] - 1);
            break;
        case DOWN :
            L = max(L - 1, 0);
            break;
        default :
            break;
        }

        if (shift + 300 < 31 * C) {
            fill_screen(BACKGROUND_COLOR);
            shift += 31;
            for (int i = 0; i < 32; i++) {
                for (int j = 0; j < hauteur[i]; j++) {
                    table[i][j]->x -= 31;
                }
            }
        } else if (shift > 31 * C) {
            fill_screen(BACKGROUND_COLOR);
            shift -= 31;
            for (int i = 0; i < 32; i++) {
                for (int j = 0; j < hauteur[i]; j++) {
                    table[i][j]->x += 31;
                }
            }
        }
    }
}

