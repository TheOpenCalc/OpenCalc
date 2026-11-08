#ifndef UI_KIT_H
#define UI_KIT_H
#include <stdint.h>

/* Petite boite a outils d'interface partagee par les apps Statistiques et
 * Probabilites (Core/ui_kit.cpp).
 *
 * Coordonnees "naturelles" : X de 0 (gauche) a 319 (droite), Y de 0 (haut) a
 * 239 (bas). Elles sont converties vers la convention de fill_rect/draw_char
 * (x = ligne verticale comptee depuis le bas, y = colonne). */

#define UK_HEADER_H 18
#define UK_TABS_H 18
#define UK_FOOTER_H 12
#define UK_HEADER_BG 0x311f
#define UK_SEL_BG 0xfff0
#define UK_TAB_BG 0xce79
#define UK_TAB_SEL 0xffff
#define UK_TAB_FOCUS 0xfff0

void uk_rect(int X, int Y, int W, int H, uint16_t color);
void uk_frame(int X, int Y, int W, int H, uint16_t color);
void uk_line(int X0, int Y0, int X1, int Y1, uint16_t color);
void uk_pixel(int X, int Y, uint16_t color);

/* Texte sans fond, coin haut gauche en (X, Y). Taille 1 : 5x8 px par lettre. */
void uk_text(int X, int Y, const char *s, uint16_t fg, int size);
int uk_text_w(const char *s, int size);
void uk_text_center(int X, int Y, int W, const char *s, uint16_t fg, int size);
void uk_text_right(int X_right, int Y, const char *s, uint16_t fg, int size);

/* Bandeau de titre, barre d'onglets (focus = la barre a le focus clavier) et
 * ligne d'aide en bas d'ecran. */
void uk_header(const char *title);
void uk_tabs(const char **names, int n, int sel, bool focus);
void uk_footer(const char *hint);

/* Nombre compact sur au plus maxch caracteres (notation scientifique au
 * besoin). Retourne la longueur. */
int uk_fmt(double v, char *buf, int maxch);

/* Evalue une expression saisie (syntaxe interne de l'editeur). */
bool uk_eval(const char *text, int len, double *out);

/* Attend une touche (fait clignoter le curseur des zones de saisie). Les
 * caracteres tapes au clavier du PC (simulateur) sont renvoyes en negatif :
 * -c pour le caractere c. */
int uk_wait_key(void);

/* Liste plein ecran ; renvoie l'indice choisi ou -1 (BACK). */
int uk_menu(const char *title, const char **labels, int n, int sel);

/* Saisie d'un nombre (expression acceptee : 1/3, r(2), 2p...) dans une
 * fenetre. first_key : touche deja pressee a inserer (-1 sinon). Renvoie
 * false si l'utilisateur annule. */
bool uk_input(const char *title, const char *prompt, double *val, int first_key);

/* Message bloquant (OK/BACK pour fermer). */
void uk_message(const char *title, const char *line1, const char *line2);

/* Icones 79x79 du menu principal, dessinees a la volee. */
enum
{
    UK_ICON_STATS = 0,
    UK_ICON_PROBA,
    UK_ICON_TAMAGOTCHI
};
void uk_draw_icon(int id, int X, int Y);

/* True si la touche est un caractere de saisie de nombre (chiffre, -, ., ...). */
bool uk_is_entry_key(int key);

#endif
