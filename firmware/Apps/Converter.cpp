#include "ui.h"
#include "Evaluator.h"
#include "Converter.h"
#include "widgets.h"
#include "menu.h"
#include <stdlib.h>
#include <math.h>
#include "colors.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"

struct UnitEntry
{
    const char *name;
    const char *symbol;
    double factor;
    double offset;
};

struct Category
{
    const char *name;
    const UnitEntry *units;
    int count;
};

static const UnitEntry lng[] = {
    {"metre", "m", 1.0, 0.0},
    {"kilometre", "km", 1000.0, 0.0},
    {"centimetre", "cm", 0.01, 0.0},
    {"millimetre", "mm", 0.001, 0.0},
    {"micrometre", "um", 1.0e-6, 0.0},
    {"mile", "mi", 1609.344, 0.0},
    {"pied", "ft", 0.3048, 0.0},
    {"pouce", "in", 0.0254, 0.0},
    {"yard", "yd", 0.9144, 0.0},
    {"mille marin", "nmi", 1852.0, 0.0},
};

static const UnitEntry mss[] = {
    {"kilogramme", "kg", 1.0, 0.0},
    {"gramme", "g", 1.0e-3, 0.0},
    {"milligramme", "mg", 1.0e-6, 0.0},
    {"tonne", "t", 1000.0, 0.0},
    {"livre", "lb", 0.4535924, 0.0},
    {"once", "oz", 0.0283495, 0.0},
    {"carat", "ct", 2.0e-4, 0.0},
};

static const UnitEntry tmp[] = {
    {"Celsius", "C", 1.0, 0.0},
    {"Fahrenheit", "F", 5.0 / 9.0, -(32.0 * 5.0 / 9.0)},
    {"Kelvin", "K", 1.0, -273.15},
};

static const UnitEntry srf[] = {
    {"m carre", "m2", 1.0, 0.0},
    {"km carre", "km2", 1.0e6, 0.0},
    {"cm carre", "cm2", 1.0e-4, 0.0},
    {"mm carre", "mm2", 1.0e-6, 0.0},
    {"hectare", "ha", 1.0e4, 0.0},
    {"are", "a", 100.0, 0.0},
    {"pied carre", "ft2", 9.2903e-2, 0.0},
    {"acre", "ac", 4046.856, 0.0},
};

static const UnitEntry vlm[] = {
    {"litre", "L", 1.0, 0.0},
    {"millilitre", "mL", 1.0e-3, 0.0},
    {"centilitre", "cL", 1.0e-2, 0.0},
    {"m cube", "m3", 1000.0, 0.0},
    {"cm cube", "cm3", 1.0e-3, 0.0},
    {"gallon US", "gal", 3.785412, 0.0},
    {"fl oz US", "floz", 0.0295735, 0.0},
    {"pinte US", "pt", 0.4731765, 0.0},
};

static const UnitEntry spd[] = {
    {"m par s", "m/s", 1.0, 0.0},
    {"km par h", "km/h", 1.0 / 3.6, 0.0},
    {"mile par h", "mph", 0.44704, 0.0},
    {"noeud", "kn", 0.514444, 0.0},
    {"pied par s", "ft/s", 0.3048, 0.0},
};

static const UnitEntry tme[] = {
    {"seconde", "s", 1.0, 0.0},
    {"milliseconde", "ms", 1.0e-3, 0.0},
    {"minute", "min", 60.0, 0.0},
    {"heure", "h", 3600.0, 0.0},
    {"jour", "j", 86400.0, 0.0},
    {"semaine", "sem", 604800.0, 0.0},
    {"mois", "mois", 2629800.0, 0.0},
    {"annee", "an", 31557600.0, 0.0},
};

static const UnitEntry agl[] = {
    {"radian", "rad", 1.0, 0.0},
    {"degre", "deg", 3.14159265358979 / 180.0, 0.0},
    {"grade", "gon", 3.14159265358979 / 200.0, 0.0},
    {"tour", "tr", 6.28318530717959, 0.0},
};

static const UnitEntry nrg[] = {
    {"joule", "J", 1.0, 0.0},
    {"kilojoule", "kJ", 1000.0, 0.0},
    {"calorie", "cal", 4.184, 0.0},
    {"kilocalorie", "kcal", 4184.0, 0.0},
    {"kWh", "kWh", 3.6e6, 0.0},
    {"BTU", "BTU", 1055.056, 0.0},
    {"eV", "eV", 1.60218e-19, 0.0},
};

static const UnitEntry prs[] = {
    {"pascal", "Pa", 1.0, 0.0},
    {"hectopascal", "hPa", 100.0, 0.0},
    {"kilopascal", "kPa", 1000.0, 0.0},
    {"bar", "bar", 1.0e5, 0.0},
    {"millibar", "mbar", 100.0, 0.0},
    {"atm", "atm", 101325.0, 0.0},
    {"mmHg", "mmHg", 133.322, 0.0},
    {"psi", "psi", 6894.757, 0.0},
};

#define NB_CATS 10

static const Category cats[NB_CATS] = {
    {"Longueur", lng, 10},
    {"Masse", mss, 7},
    {"Temperature", tmp, 3},
    {"Surface", srf, 8},
    {"Volume", vlm, 8},
    {"Vitesse", spd, 5},
    {"Temps", tme, 8},
    {"Angle", agl, 4},
    {"Energie", nrg, 7},
    {"Pression", prs, 8},
};

static double do_conv(double v, const UnitEntry *fr, const UnitEntry *to)
{
    double base = v * fr->factor + fr->offset;
    return (base - to->offset) / to->factor;
}

static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void tbox(int x, int y, int h, int w,
                 const char *txt, int tsz,
                 uint16_t bg, bool sel, char al, int sz)
{
    text_box *tb = create_text_box(x, y, h, w, 1, false);
    free(tb->text);
    tb->col = bg;
    tb->text = (char *)txt;
    tb->t_size = tsz;
    tb->display_text_size = sz;
    tb->allign = al;
    display_text_box(tb, 0, 0, sel);
    tb->text = nullptr;
    free(tb);
}

static int build_unit_str(const char *prefix,
                          const char *sym,
                          char *buf)
{
    int p = 0;
    while (prefix[p])
    {
        buf[p] = prefix[p];
        p++;
    }
    buf[p++] = '<';
    buf[p++] = ' ';
    for (int i = 0; sym[i]; i++)
        buf[p++] = sym[i];
    buf[p++] = ' ';
    buf[p++] = '>';
    buf[p] = '\0';
    return p;
}

int Converter()
{
    bool snd = false;
    int sel_cat = 0;
    int first_cat = 0;
    int sel_from = 0;
    int sel_to = 1;
    int cursor = 0;
    bool cat_scr = true;

    fill_box *vbox = create_fill_box(0, 97, 36, 320, 2);
    vbox->color = FRONTGROUND_COLOR;

    fill_screen(BACKGROUND_COLOR);

    int key = scan_keypad();

    while (true)
    {
        fill_screen(BACKGROUND_COLOR);

        if (cat_scr)
        {
            tbox(0, 220, 20, 320,
                 "Conversion d'unites", 19,
                 0x311f, false, 'c', 1);

            int vis = min(NB_CATS - first_cat, 8);
            for (int i = 0; i < vis; i++)
            {
                int ci = first_cat + i;
                uint16_t bg = (ci % 2 == 0) ? FRONTGROUND_COLOR
                                            : FRONTGROUND_COLOR_BIS;
                tbox(0, 190 - i * 27, 27, 320,
                     cats[ci].name, slen(cats[ci].name),
                     bg, ci == sel_cat, 'r', 1);
            }
        }

        else
        {
            const Category &cat = cats[sel_cat];
            const UnitEntry *fu = &cat.units[sel_from];
            const UnitEntry *tu = &cat.units[sel_to];

            tbox(0, 220, 20, 320,
                 cat.name, slen(cat.name),
                 0x311f, false, 'c', 1);

            {
                static char dbuf[32];
                int len = build_unit_str("De: ", fu->symbol, dbuf);
                uint16_t bg = (cursor == 0) ? (uint16_t)0xfff0 : FRONTGROUND_COLOR;
                tbox(0, 192, 26, 320, dbuf, len, bg, false, 'c', 2);
            }

            {
                static char ebuf[32];
                int len = build_unit_str("En: ", tu->symbol, ebuf);
                uint16_t bg = (cursor == 1) ? (uint16_t)0xfff0 : FRONTGROUND_COLOR;
                tbox(0, 163, 26, 320, ebuf, len, bg, false, 'c', 2);
            }

            fill_rect(152, 8, 2, 304, 0x0000);

            {

                int tok_n = 0;
                token *toks = parse_string_to_token(
                    vbox->text, vbox->t_size, &tok_n);
                token *npi = shunting_yard(toks, tok_n);
                int npi_n = count_yarded(toks, tok_n);
                double inv = evaluate_npi(npi, npi_n);
                double res = do_conv(inv, fu, tu);
                free(toks);
                free(npi);

                static char rbuf[48];
                if (isnan(res) || isinf(res))
                {
                    rbuf[0] = 'E';
                    rbuf[1] = 'r';
                    rbuf[2] = 'r';
                    rbuf[3] = '\0';
                }
                else
                {
                    double_to_string_scientific(res, rbuf);
                }

                static char full[64];
                full[0] = '=';
                full[1] = ' ';
                int p = 2;
                for (int i = 0; rbuf[i]; i++)
                    full[p++] = rbuf[i];
                full[p++] = ' ';
                for (int i = 0; tu->symbol[i]; i++)
                    full[p++] = tu->symbol[i];
                full[p] = '\0';

                tbox(0, 7, 52, 320, full, p,
                     FRONTGROUND_COLOR_BIS, false, 'c', 2);
            }

            tbox(0, 63, 22, 144, fu->name, slen(fu->name),
                 BACKGROUND_COLOR, false, 'c', 1);
            tbox(146, 63, 22, 20, "->", 2,
                 BACKGROUND_COLOR, false, 'c', 1);
            tbox(168, 63, 22, 144, tu->name, slen(tu->name),
                 BACKGROUND_COLOR, false, 'c', 1);

            display_fill_box(vbox, 0, cursor == 2, -1, ' ',false);
        }

        key = scan_keypad();
        while (key == -1)
        {

            if (!cat_scr && cursor == 0)
                blink_cursor();
            key = scan_keypad();
        }
        sleep_ms(50);

        if (cat_scr)
        {
            switch (key)
            {
            case UP:
                sel_cat = max(0, sel_cat - 1);
                if (sel_cat < first_cat)
                    first_cat = sel_cat;
                break;

            case DOWN:
                sel_cat = min(NB_CATS - 1, sel_cat + 1);
                if (sel_cat >= first_cat + 7)
                    first_cat++;
                break;

            case OK:
            case ENTER:
                cat_scr = false;
                sel_from = 0;
                sel_to = min(1, cats[sel_cat].count - 1);
                cursor = 0;

                for (int i = 0; i < 100; i++)
                    vbox->text[i] = '\0';
                vbox->t_size = 0;
                vbox->curso_pos = 0;
                break;

            case BACK:
                return 0;

            default:
                break;
            }
        }

        else
        {
            switch (key)
            {

            case UP:
                cursor = max(0, cursor - 1);
                break;

            case DOWN:
                cursor = min(2, cursor + 1);
                break;

            case LEFT:
                if (cursor == 0)
                {
                    sel_from = (sel_from - 1) % cats[sel_cat].count;
                }
                else if (cursor == 1)
                {
                    sel_to = (sel_to - 1) % cats[sel_cat].count;
                }
                break;

            case RIGHT:
                if (cursor == 0)
                {
                    sel_from = (sel_from + 1) % cats[sel_cat].count;
                }
                else if (cursor == 1)
                {
                    sel_to = (sel_to + 1) % cats[sel_cat].count;
                }
                break;

            case OK:
            case ENTER:
            {
                int swap = sel_from;
                sel_from = sel_to;
                sel_to = swap;
                break;
            }

            case SECOND:
                toggle(&snd);
                break;

            case BACK:
                if (cursor == 2 && vbox->t_size > 0)
                {
                    update_fill_box(vbox, key, snd);
                }
                else
                {

                    if (cursor > 0)
                        cursor = 0;
                    else
                        cat_scr = true;
                }
                break;

            default:
                if (cursor == 2)
                    update_fill_box(vbox, key, snd);
                break;
            }
        }
    }

    return 0;
}