#include "elements.h"
#include "ui.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "menu.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "colors.h"

/* Tableau periodique "large" (32 colonnes, lanthanides/actinides en ligne).
 *
 * Geometrie (meme convention que le reste de l'UI) :
 *   - colonne c : 0..31, ligne l : 0 = bas du tableau (periode 7), l = hauteur-1 = haut
 *   - periode = 7 - l
 * Toutes les donnees sont static const (en flash) : plus aucune copie sur la
 * pile a chaque ouverture de l'appli, et plus aucune fuite memoire. */

#define NB_ELEM 118
#define NB_COLS 32
#define CELL 31
#define VIS_COLS 10
#define TABLE_X0 10
#define TABLE_Y0 10

static const uint8_t hauteur[NB_COLS] = {
    7, 6, 4,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    4, 4, 4, 4, 4, 4, 4, 4, 4,
    6, 6, 6, 6, 6,
    7};

static const char *const symboles[NB_ELEM] = {
    "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca",
    "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr",
    "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd",
    "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg",
    "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm",
    "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"};

/* Sans accents : la police 5x7 est ASCII uniquement. */
static const char *const noms[NB_ELEM] = {
    "Hydrogene", "Helium", "Lithium", "Beryllium", "Bore", "Carbone", "Azote", "Oxygene", "Fluor", "Neon",
    "Sodium", "Magnesium", "Aluminium", "Silicium", "Phosphore", "Soufre", "Chlore", "Argon", "Potassium", "Calcium",
    "Scandium", "Titane", "Vanadium", "Chrome", "Manganese", "Fer", "Cobalt", "Nickel", "Cuivre", "Zinc",
    "Gallium", "Germanium", "Arsenic", "Selenium", "Brome", "Krypton", "Rubidium", "Strontium", "Yttrium", "Zirconium",
    "Niobium", "Molybdene", "Technetium", "Ruthenium", "Rhodium", "Palladium", "Argent", "Cadmium", "Indium", "Etain",
    "Antimoine", "Tellure", "Iode", "Xenon", "Cesium", "Baryum", "Lanthane", "Cerium", "Praseodyme", "Neodyme",
    "Promethium", "Samarium", "Europium", "Gadolinium", "Terbium", "Dysprosium", "Holmium", "Erbium", "Thulium", "Ytterbium",
    "Lutecium", "Hafnium", "Tantale", "Tungstene", "Rhenium", "Osmium", "Iridium", "Platine", "Or", "Mercure",
    "Thallium", "Plomb", "Bismuth", "Polonium", "Astate", "Radon", "Francium", "Radium", "Actinium", "Thorium",
    "Protactinium", "Uranium", "Neptunium", "Plutonium", "Americium", "Curium", "Berkelium", "Californium", "Einsteinium", "Fermium",
    "Mendelevium", "Nobelium", "Lawrencium", "Rutherfordium", "Dubnium", "Seaborgium", "Bohrium", "Hassium", "Meitnerium", "Darmstadtium",
    "Roentgenium", "Copernicium", "Nihonium", "Flerovium", "Moscovium", "Livermorium", "Tennesse", "Oganesson"};

static const float masse[NB_ELEM] = {
    1.008f, 4.0026022f, 6.94f, 9.01218315f, 10.81f, 12.011f, 14.007f, 15.999f, 18.9984031636f, 20.17976f,
    22.989769282f, 24.305f, 26.98153857f, 28.085f, 30.9737619985f, 32.06f, 35.45f, 39.9481f, 39.09831f, 40.0784f,
    44.9559085f, 47.8671f, 50.94151f, 51.99616f, 54.9380443f, 55.8452f, 58.9331944f, 58.69344f, 63.5463f, 65.382f,
    69.7231f, 72.6308f, 74.9215956f, 78.9718f, 79.904f, 83.7982f, 85.46783f, 87.621f, 88.905842f, 91.2242f,
    92.906372f, 95.951f, 98.f, 101.072f, 102.905502f, 106.421f, 107.86822f, 112.4144f, 114.8181f, 118.7107f,
    121.7601f, 127.603f, 126.904473f, 131.2936f, 132.905451966f, 137.3277f, 138.905477f, 140.1161f, 140.907662f, 144.2423f,
    145.f, 150.362f, 151.9641f, 157.253f, 158.925352f, 162.5001f, 164.930332f, 167.2593f, 168.934222f, 173.0451f,
    174.96681f, 178.492f, 180.947882f, 183.841f, 186.2071f, 190.233f, 192.2173f, 195.0849f, 196.9665695f, 200.5923f,
    204.38f, 207.21f, 208.980401f, 209.f, 210.f, 222.f, 223.f, 226.f, 227.f, 232.03774f,
    231.035882f, 238.028913f, 237.f, 244.f, 243.f, 247.f, 247.f, 251.f, 252.f, 257.f,
    258.f, 259.f, 266.f, 267.f, 268.f, 269.f, 270.f, 269.f, 278.f, 281.f,
    282.f, 285.f, 286.f, 289.f, 289.f, 293.f, 294.f, 294.f};

/* Point d'ebullition en K ; < 0 = inconnu / sublimation. */
static const float ebullition[NB_ELEM] = {
    20.271f, 4.222f, 1603.f, 2742.f, 4200.f, -1.f, 77.355f, 90.188f, 85.03f, 27.104f,
    1156.09f, 1363.f, 2743.f, 3538.f, -1.f, 717.8f, 239.11f, 87.302f, 1032.f, 1757.f,
    3109.f, 3560.f, 3680.f, 2944.f, 2334.f, 3134.f, 3200.f, 3003.f, 2835.f, 1180.f,
    2673.f, 3106.f, -1.f, 958.f, 332.f, 119.93f, 961.f, 1650.f, 3203.f, 4650.f,
    5017.f, 4912.f, 4538.f, 4423.f, 3968.f, 3236.f, 2435.f, 1040.f, 2345.f, 2875.f,
    1908.f, 1261.f, 457.4f, 165.051f, 944.f, 2118.f, 3737.f, 3716.f, 3403.f, 3347.f,
    3273.f, 2173.f, 1802.f, 3273.f, 3396.f, 2840.f, 2873.f, 3141.f, 2223.f, 1469.f,
    3675.f, 4876.f, 5731.f, 6203.f, 5869.f, 5285.f, 4403.f, 4098.f, 3243.f, 629.88f,
    1746.f, 2022.f, 1837.f, 1235.f, 610.f, 211.5f, 950.f, 2010.f, 3500.f, 5061.f,
    4300.f, 4404.f, 4447.f, 3505.f, 2880.f, 3383.f, 2900.f, 1743.f, 1269.f, -1.f,
    -1.f, -1.f, -1.f, 5800.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f,
    -1.f, 357.f, 1430.f, 420.f, 1400.f, 1085.f, 883.f, 350.f};

/* Masse volumique en g/cm3 ; < 0 = inconnue. */
static const float densite[NB_ELEM] = {
    0.08988f, 0.1786f, 0.534f, 1.85f, 2.08f, 1.821f, 1.251f, 1.429f, 1.696f, 0.9002f,
    0.968f, 1.738f, 2.7f, 2.329f, 1.823f, 2.07f, 3.2f, 1.784f, 0.862f, 1.55f,
    2.985f, 4.506f, 6.f, 7.19f, 7.21f, 7.874f, 8.9f, 8.908f, 8.96f, 7.14f,
    5.91f, 5.323f, 5.727f, 4.81f, 3.1028f, 3.749f, 1.532f, 2.64f, 4.472f, 6.52f,
    8.57f, 10.28f, 11.f, 12.45f, 12.41f, 12.023f, 10.49f, 8.65f, 7.31f, 7.365f,
    6.697f, 6.24f, 4.933f, 5.894f, 1.93f, 3.51f, 6.162f, 6.77f, 6.77f, 7.01f,
    7.26f, 7.52f, 5.264f, 7.9f, 8.23f, 8.54f, 8.79f, 9.066f, 9.32f, 6.9f,
    9.841f, 13.31f, 16.69f, 19.25f, 21.02f, 22.59f, 22.56f, 21.45f, 19.3f, 13.534f,
    11.85f, 11.34f, 9.78f, 9.196f, 6.35f, 9.73f, 1.87f, 5.5f, 10.f, 11.724f,
    15.37f, 19.1f, 20.45f, 19.816f, 12.f, 13.51f, 14.78f, 15.1f, 8.84f, -1.f,
    -1.f, -1.f, -1.f, 23.2f, 29.3f, 35.f, 37.1f, 40.7f, 37.4f, 34.8f,
    28.7f, 23.7f, 16.f, 14.f, 13.5f, 12.9f, 7.17f, 4.95f};

/* ------------------------------ Familles ------------------------------ */

enum
{
    CAT_ALCALIN,
    CAT_ALCALINOTERREUX,
    CAT_TRANSITION,
    CAT_POST,
    CAT_METALLOIDE,
    CAT_NONMETAL,
    CAT_HALOGENE,
    CAT_GAZ_NOBLE,
    CAT_LANTHANIDE,
    CAT_ACTINIDE,
    NB_CAT
};

static const char *const cat_noms[NB_CAT] = {
    "Alcalin", "Alcalino-terreux", "Metal de transition", "Metal pauvre", "Metalloide",
    "Non-metal", "Halogene", "Gaz noble", "Lanthanide", "Actinide"};

static const uint16_t cat_couleurs[NB_CAT] = {
    0xFD34, /* alcalin */
    0xFED0, /* alcalino-terreux */
    0xB5FF, /* transition */
    0xD6BA, /* metal pauvre */
    0xFFF3, /* metalloide */
    0x9FF3, /* non-metal */
    0xFDDF, /* halogene */
    0xDD7F, /* gaz noble */
    0xFE59, /* lanthanide */
    0xFCB9  /* actinide */
};

static int categorie(int z)
{
    switch (z)
    {
    case 1:
    case 6:
    case 7:
    case 8:
    case 15:
    case 16:
    case 34:
        return CAT_NONMETAL;
    case 3:
    case 11:
    case 19:
    case 37:
    case 55:
    case 87:
        return CAT_ALCALIN;
    case 4:
    case 12:
    case 20:
    case 38:
    case 56:
    case 88:
        return CAT_ALCALINOTERREUX;
    case 2:
    case 10:
    case 18:
    case 36:
    case 54:
    case 86:
    case 118:
        return CAT_GAZ_NOBLE;
    case 9:
    case 17:
    case 35:
    case 53:
    case 85:
    case 117:
        return CAT_HALOGENE;
    case 5:
    case 14:
    case 32:
    case 33:
    case 51:
    case 52:
        return CAT_METALLOIDE;
    case 13:
    case 31:
    case 49:
    case 50:
    case 81:
    case 82:
    case 83:
    case 84:
        return CAT_POST;
    default:
        break;
    }
    if (z >= 57 && z <= 71)
        return CAT_LANTHANIDE;
    if (z >= 89 && z <= 103)
        return CAT_ACTINIDE;
    if (z >= 113 && z <= 116)
        return CAT_POST;
    return CAT_TRANSITION;
}

/* ------------------- Correspondance (colonne, ligne) <-> Z ------------------- */

static int zmap[NB_COLS][7];
static int zcol[NB_ELEM + 1];
static int zrow[NB_ELEM + 1];

static void build_map()
{
    for (int c = 0; c < NB_COLS; c++)
    {
        for (int l = 0; l < hauteur[c]; l++)
        {
            int pos = 0;
            for (int a = 0; a < c; a++)
                if (hauteur[a] > l)
                    pos++;
            for (int a = 0; a < NB_COLS; a++)
                pos += max(0, hauteur[a] - l - 1);
            zmap[c][l] = pos + 1;
            zcol[pos + 1] = c;
            zrow[pos + 1] = l;
        }
    }
}

/* ------------------------------- Affichage ------------------------------- */

/* Texte dont le haut est a la ligne `top` et le bord gauche a la colonne `left`. */
static void txt(int top, int left, const char *s, int size)
{
    display_text(top - 25, left - 5, (char *)s, size, (int)strlen(s));
}

static void draw_cell(text_box *tb, int col, int row, int first_col, bool sel)
{
    int z = zmap[col][row];
    tb->x = TABLE_X0 + CELL * (col - first_col);
    tb->y = TABLE_Y0 + CELL * row;
    tb->col = cat_couleurs[categorie(z)];
    tb->text = (char *)symboles[z - 1]; /* litteral : free_text_box ne le libere pas */
    tb->t_size = (int)strlen(symboles[z - 1]);
    display_text_box(tb, 0, 0, sel);
}

/* Petit panneau d'infos, place dans la zone vide du tableau (au-dessus des
 * colonnes 2 a 25, qui ne depassent pas la ligne 134). */
static void draw_panel(text_box *panel, int z, int first_col)
{
    panel->x = first_col >= 2 ? TABLE_X0 : TABLE_X0 + CELL * (2 - first_col);
    display_text_box(panel, 0, 0, false);
    int left = panel->x + 6;
    char buf[40];
    const char *nom = noms[z - 1];
    int len = (int)strlen(nom);
    txt(222, left, nom, len * 11 <= 112 ? 2 : 1);

    snprintf(buf, sizeof(buf), "Z=%d  %s  P%d", z, symboles[z - 1], 7 - zrow[z]);
    txt(200, left, buf, 1);
    snprintf(buf, sizeof(buf), "Masse:%.5g", masse[z - 1]);
    txt(188, left, buf, 1);
    if (densite[z - 1] < 0)
        snprintf(buf, sizeof(buf), "Dens.:?");
    else
        snprintf(buf, sizeof(buf), "Dens.:%.5g", densite[z - 1]);
    txt(176, left, buf, 1);
    if (ebullition[z - 1] < 0)
        snprintf(buf, sizeof(buf), "Ebull.:?");
    else
        snprintf(buf, sizeof(buf), "Ebull.:%.5gK", ebullition[z - 1]);
    txt(164, left, buf, 1);
    txt(152, left, cat_noms[categorie(z)], 1);
}

/* Fiche complete d'un element. */
static void draw_detail(int z)
{
    int cat = categorie(z);
    const char *sym = symboles[z - 1];

    text_box *tile = create_text_box(10, 100, 100, 100, 2, false);
    tile->col = cat_couleurs[cat];
    tile->text = (char *)sym;
    tile->t_size = (int)strlen(sym);
    tile->display_text_size = 4;
    display_text_box(tile, 0, -26, false);
    free_text_box(tile);

    char buf[48];
    snprintf(buf, sizeof(buf), "%d", z);
    txt(194, 16, buf, 2);

    txt(200, 122, noms[z - 1], 2);
    snprintf(buf, sizeof(buf), "Numero atomique : %d", z);
    txt(176, 122, buf, 1);
    snprintf(buf, sizeof(buf), "Periode : %d", 7 - zrow[z]);
    txt(162, 122, buf, 1);
    snprintf(buf, sizeof(buf), "Famille : %s", cat_noms[cat]);
    txt(148, 122, buf, 1);
    snprintf(buf, sizeof(buf), "Masse : %.5g u", masse[z - 1]);
    txt(134, 122, buf, 1);
    if (densite[z - 1] < 0)
        snprintf(buf, sizeof(buf), "Densite : inconnue");
    else
        snprintf(buf, sizeof(buf), "Densite : %.5g g/cm3", densite[z - 1]);
    txt(120, 122, buf, 1);
    if (ebullition[z - 1] < 0)
        snprintf(buf, sizeof(buf), "Ebullition : inconnue");
    else
        snprintf(buf, sizeof(buf), "Ebullition : %.5g K (%.5g C)", ebullition[z - 1], ebullition[z - 1] - 273.15f);
    txt(106, 122, buf, 1);

    txt(30, 10, "</> element   OK/BACK retour", 1);
}

/* Fait defiler le tableau pour garder la colonne C visible (marge d'une colonne). */
static int scroll_for(int first, int C)
{
    if (C < first + 1)
        first = C - 1;
    if (C > first + VIS_COLS - 2)
        first = C - (VIS_COLS - 2);
    if (first > NB_COLS - VIS_COLS)
        first = NB_COLS - VIS_COLS;
    if (first < 0)
        first = 0;
    return first;
}

void display_table()
{
    build_map();

    text_box *cell = create_text_box(0, 0, 30, 30, 0, false);
    text_box *panel = create_text_box(72, 134, 93, 124, 2, true);

    int C = zcol[1], L = zrow[1]; /* on demarre sur l'hydrogene */
    int first_col = scroll_for(0, C);
    bool detail = false;

    scan_keypad_nowait(); /* pas bloquant : l'ecran s'affiche tout de suite */

    while (true)
    {
        fill_screen(BACKGROUND_COLOR);
        int z = zmap[C][L];

        if (detail)
        {
            draw_detail(z);
        }
        else
        {
            draw_panel(panel, z, first_col);
            int last = min(first_col + VIS_COLS, NB_COLS);
            for (int c = first_col; c < last; c++)
                for (int l = 0; l < hauteur[c]; l++)
                    draw_cell(cell, c, l, first_col, c == C && l == L);
        }
        flush_display();

        int key = scan_keypad();
        while (key == -1)
            key = scan_keypad();

        switch (key)
        {
        case BACK:
            if (detail)
            {
                detail = false;
                break;
            }
            free_text_box(cell);
            free_text_box(panel);
            return;
        case OK:
        case ENTER:
            detail = !detail;
            break;
        case RIGHT:
            if (detail)
            {
                if (z < NB_ELEM)
                {
                    z++;
                    C = zcol[z];
                    L = zrow[z];
                }
            }
            else
            {
                C = min(C + 1, NB_COLS - 1);
                L = min(hauteur[C] - 1, L);
            }
            break;
        case LEFT:
            if (detail)
            {
                if (z > 1)
                {
                    z--;
                    C = zcol[z];
                    L = zrow[z];
                }
            }
            else
            {
                C = max(0, C - 1);
                L = min(hauteur[C] - 1, L);
            }
            break;
        case UP:
            if (!detail)
                L = min(L + 1, hauteur[C] - 1);
            break;
        case DOWN:
            if (!detail)
                L = max(L - 1, 0);
            break;
        default:
            break;
        }

        first_col = scroll_for(first_col, C);
    }
}