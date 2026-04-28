#include <vector>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cmath>
#include <cstdlib>
#include <string.h>
#include <string>

#include "headers/ui.h"
#include "headers/menu.h"
#include "headers/Evaluator.h"
#include "headers/widgets.h"


#define MAX_NODES 128

static ASTNode node_pool[MAX_NODES];
static int     pool_top = 0;

static ASTNode *alloc_node() {
    if (pool_top >= MAX_NODES) return &node_pool[MAX_NODES - 1];
    ASTNode *n  = &node_pool[pool_top++];
    n->left     = nullptr;
    n->right    = nullptr;
    n->number   = 0;
    n->op       = 0;
    n->src_pos  = -1;
    return n;
}
static void reset_pool() { pool_top = 0; }

static ASTNode *make_placeholder() {
    ASTNode *nd = alloc_node();
    nd->type    = N_PLACEHOLDER;
    return nd;
}

struct coord_s {
    int x;
    int y;
};


typedef struct coord_s coord;

extern int x_cursor;
extern int y_cursor;



int min(int a, int b)
{
    if (a < b) {
        return a;
    }
    return b;
}


int max(int a, int b)
{
    if (a > b) {
        return a;
    }
    return b;
}

void axis()
{
    fill_rect(0, 159, 220, 2, 0x0000);
    fill_rect(120, 0, 2, 340, 0x0000);
}


void display_text(int x, int y, char * t,int SIZE, int t_size){
    int pos =0;
    x+=25;
    for(int i =0;i<t_size;i++){
    draw_char(x, y + 5 + pos, &t[i], 0X0000, 0X0000, SIZE);
            pos += 5.5 * SIZE;
    }
}

void draw_buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buffer)
{
    if ((x >= SCREEN_HEIGHT) || (y >= SCREEN_WIDTH))
        return;
    if ((x + w - 1) >= SCREEN_HEIGHT)
        w = SCREEN_HEIGHT - x;
    if ((y + h - 1) >= SCREEN_WIDTH)
        h = SCREEN_WIDTH - y;

    // Définir la zone de dessin
    ili_cmd(0x2A); // Set column address
    ili_data(x >> 8);
    ili_data(x & 0xFF);
    ili_data((x + w - 1) >> 8);
    ili_data((x + w - 1) & 0xFF);

    ili_cmd(0x2B); // Set row address
    ili_data(y >> 8);
    ili_data(y & 0xFF);
    ili_data((y + h - 1) >> 8);
    ili_data((y + h - 1) & 0xFF);

    ili_cmd(0x2C); // Memory write

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

// Envoyer le buffer en batch pour ne pas saturer la mémoire
#define BUF_PIXELS 1024
    static uint8_t spi_buf[BUF_PIXELS * 2];

    int total_pixels = w * h;
    int idx = 0;

    while (total_pixels > 0)
    {
        int batch = (total_pixels > BUF_PIXELS) ? BUF_PIXELS : total_pixels;

        // Convertir les pixels du buffer 16 bits en tableau d'octets pour SPI
        for (int i = 0; i < batch; ++i)
        {
            uint16_t color = buffer[idx++];
            spi_buf[2 * i] = color >> 8;
            spi_buf[2 * i + 1] = color & 0xFF;
        }

        spi_write_blocking(spi0, spi_buf, batch * 2);
        total_pixels -= batch;
    }

    gpio_put(PIN_CS, 1);
}

int * get_depth(char * in, int input_size){
    int * depth = (int*)malloc(sizeof(int)*input_size);
    for(int i = 0 ; i < input_size;i++){
        depth[i]=0;
    }
    int m=0;
    int cur_depth =0;   
    for(int searched_depth = 10 ; searched_depth>=0;searched_depth--){      //A optimiser, inneficient
    for(int i = 0 ; i < input_size;i++){
        if(in[i]=='(')
            cur_depth++;
        if(in[i]==')')
            cur_depth--;
        if(cur_depth==searched_depth && in[i]=='/'){
            int a = 0;
            int loc_max = 0;
            int j=i+1;
            for(j = i+1 ; (j==i+1 || a>0    ) &&  j<input_size;j++){
                if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                if(depth[j]>loc_max)
                    loc_max=depth[j];


               
            }
            a = 0;
            int loc_min=0;
            for(j = i-1 ; (j==i-1 ||a<0 )&&  j>=0;j--){
                if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                if(depth[j]<loc_min)
                    loc_min=depth[j];

            }
            

      for(j = i-1 ; (j==i-1 ||a<0 )&&  j>=0;j--){
                if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                    depth[j]+=-loc_min+1;
            }
            for(j = i+1 ; (j==i+1 || a>0    ) &&  j<input_size;j++){
                if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                depth[j]+=-loc_max-1;
            }
         
        }
    }
}
    for(int i = 0 ; i < input_size;i++){
        
        
                        if(depth[i]<m)
                    m=depth[i];
    }
    for(int i = 0 ; i < input_size;i++){
        

        depth[i]-=m;
       //depth[i]=0;
    }
    return depth;
}

float* get_length(char* in, int input_size, int* depth) {
    float* length = (float*)malloc(sizeof(float) * input_size);
    length[0]=0;
     if(is_in(in[0],"uvwijk"))
            length[0]+=3;
        if(is_in(in[0],"fghcst"))
            length[0]+=2;
    for(int i = 1; i < input_size; i++) {
        length[i] = length[i-1]+1;
        if(is_in(in[i],"uvwijk"))
            length[i]+=3;
        if(is_in(in[i],"fghcst"))
            length[i]+=2;
    }
    for(int searched_depth = 10; searched_depth >= 0; searched_depth--) {
        int cur_depth=0;
        for(int i = 0; i < input_size;i++){
            if(in[i]=='('){
                cur_depth++;
            }
            if(in[i]==')'){
                cur_depth--;
            }
            if(in[i]=='/' && cur_depth==searched_depth){
                int a =0;
                int l1=0;
                int j;
                for(j = i-1 ; (j==i-1 ||a<0 )&&  j>0;j--){
                 if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                }

                int d = j;
                l1=length[i]-length[max(j,0)];

                int l2=0;
                a=0;
                for(j = i+1 ; (j==i+1 || a>0    ) &&  j<input_size;j++){
                    if(in[j]=='(')
                        a++;
                    if(in[j]==')')
                        a--;
                }
                l2=length[j]-length[i];
                if(l2>l1){
                    for(int j = d+1 ; j<i;j++){
                        length[j]+=(l2-l1)/2.0;
                    }
                    for(int j = i ; j<input_size;j++){
                        length[j]-=l1;
                    }
                }else{

                }
            
            }

            }

        }
            return length;

    }


static token *p_peek(Parser *p) {
    return p->pos < p->n ? &p->toks[p->pos] : nullptr;
}
static token *p_consume(Parser *p) {
    return p->pos < p->n ? &p->toks[p->pos++] : nullptr;
}

static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_term(Parser *p);
static ASTNode *parse_factor(Parser *p);
static ASTNode *parse_base(Parser *p);
static ASTNode *parse_base(Parser *p) {
    token *t = p_peek(p);
    if (!t) return make_placeholder();

    if (t->type == '(') {
     //   bool virt = t->is_virtual;
        p_consume(p);
        ASTNode *inner  = parse_expr(p);
        ASTNode *paren  = alloc_node();
        paren->type     = N_PARENTHESIS;  
        paren->op       = '(';     
        paren->left     = inner;
        paren->src_pos  = t->src_pos;
        if (p_peek(p) && p_peek(p)->type == ')') {
            paren->right = alloc_node();   
            paren->right->type = N_PLACEHOLDER;
            p_consume(p);
        }
        return paren;
    }

    if (t->type == ')') {
        p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type    = N_PARENTHESIS;
        nd->op      = ')';
        nd->src_pos = t->src_pos;
        return nd;
    }
    if (t->type == 'n' ) {
        
        p_consume(p);
        ASTNode *nd   = alloc_node();
        nd->type      = N_NUMBER;
        nd->number    = t->value;
        nd->src_pos   = t->src_pos;
        return nd;
    }
    if (t->type == 'X') {
        p_consume(p);
        ASTNode *nd   = alloc_node();
        nd->type      = N_VARIABLE;
        nd->variable  = (char)('a' + (int)t->value);
        nd->src_pos   = t->src_pos;
        return nd;
    }
    if (t->type == 'p' || t->type == 'e') {
        p_consume(p);
        ASTNode *nd  = alloc_node();
        nd->type     = N_CONST;
        nd->op       = t->type;
        nd->src_pos  = t->src_pos;
        return nd;
    }
    if (t->type == '(') {
        p_consume(p);
        ASTNode *inner = parse_expr(p);
        if (p_peek(p) && p_peek(p)->type == ')') p_consume(p);
        return inner;
    }

    char *funcs = (char *)"lrcstuvwfghijk!";
    if (is_in(t->type, funcs)) {
        token *ft = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type    = N_FUNC;
        nd->op      = ft->type;
        nd->src_pos = ft->src_pos;
        if (p_peek(p) && p_peek(p)->type == '(') p_consume(p);
        token *next = p_peek(p);
        if (!next || next->type == ')')
            nd->left = make_placeholder();
        else
            nd->left = parse_expr(p);
        if (p_peek(p) && p_peek(p)->type == ')') p_consume(p);
        return nd;
    }

    return make_placeholder();
}

static ASTNode *parse_factor(Parser *p) {
    ASTNode *base = parse_base(p);
    token   *t    = p_peek(p);
    if (t && t->type == '^') {
        token *ot = p_consume(p);
        ASTNode *nd  = alloc_node();
        nd->type     = N_BINOP;
        nd->op       = '^';
        nd->src_pos  = ot->src_pos;
        nd->left     = base;
        nd->right    = parse_factor(p);
        return nd;
    }
    return base;
}

static ASTNode *parse_term(Parser *p) {
    ASTNode *left = parse_factor(p);
    while (true) {
        token *t = p_peek(p);
        if (!t || (t->type != '*' && t->type != '/')) break;
        token *ot   = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type    = N_BINOP;
        nd->op      = ot->type;
        nd->src_pos = ot->src_pos;
        nd->left    = left;
        nd->right   = parse_factor(p);
        left        = nd;
    }
    return left;
}

static ASTNode *parse_expr(Parser *p) {
    ASTNode *left = parse_term(p);
    while (true) {
        token *t = p_peek(p);
        if (!t || (t->type != '+' && t->type != '-')) break;
        token *ot   = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type    = N_BINOP;
        nd->op      = ot->type;
        nd->src_pos = ot->src_pos;
        nd->left    = left;
        nd->right   = parse_term(p);
        left        = nd;
    }
    return left;
}
 ASTNode *parse_equation(Parser *p) {
    ASTNode *left = parse_expr(p);
    token *t = p_peek(p);
    if (t && t->type == '=') {
        token *ot = p_consume(p);
        ASTNode *nd  = alloc_node();
        nd->type     = N_BINOP;
        nd->op       = '=';
        nd->src_pos  = ot->src_pos;
        nd->left     = left;
        nd->right    = parse_expr(p);
        return nd;
    }
    return left;
}


static inline int CW(int S) { return 6 * S; }
static inline int CH(int S) { return 8 * S; }

int fmt_number(double v, char *buf) {
    if (v == (int)v && v >= 0 && v < 10000000) {
        return sprintf(buf, "%d", (int)v);
    }
    long int    int_part = (long int)v;
    double dec_part = v - int_part;
    if (dec_part < 0) dec_part = -dec_part;

    long int decimals = (long int)(dec_part * 1000000 + 0.5);
    long int len = sprintf(buf, "%d.", int_part);

    char dec_buf[8];
    long int dec_len = sprintf(dec_buf, "%06d", decimals);  

    while (dec_len > 1 && dec_buf[dec_len-1] == '0') dec_len--;
    dec_buf[dec_len] = '\0';

    for (int i = 0; i < dec_len; i++) buf[len++] = dec_buf[i];
    buf[len] = '\0';
    return len;
}

static int func_name_width(char op, int SIZE) {
    switch (op) {
        case 'c': case 's': case 't':               return 3*CW(SIZE);
        case 'u': case 'v': case 'w':
        case 'f': case 'g': case 'h':               return 4*CW(SIZE);
        case 'i': case 'j': case 'k':               return 5*CW(SIZE);
        case 'l':                                   return 2*CW(SIZE);
        default:                                    return   CW(SIZE);
    }
}

Dims measure(ASTNode *nd, int SIZE) {
    if (!nd) return {CW(SIZE), CH(SIZE), CH(SIZE)/2};
    switch (nd->type) {
      
        
        case N_PLACEHOLDER:
        case N_VARIABLE:
        case N_CONST:
            return {CW(SIZE), CH(SIZE), CH(SIZE)/2};
        case N_NUMBER: {
            char buf[32];
            int nc = fmt_number(nd->number, buf);
            return {nc*CW(SIZE), CH(SIZE), CH(SIZE)/2};
        }
         case N_PARENTHESIS: {
            if (nd->op == ')') {
                return {CW(SIZE), CH(SIZE), CH(SIZE)/2};
            }
            
            Dims c   = measure(nd->left, SIZE);
            int  pw  = CW(SIZE) + (nd->right ? CW(SIZE) : 0);
            return {c.w + pw, c.h, c.baseline};
        }
        case N_BINOP: {
            if (nd->op == '/') {
                Dims num = measure(nd->left,  SIZE);
                Dims den = measure(nd->right, SIZE);
                int  w   = (num.w > den.w ? num.w : den.w) + 4;
                return {w, num.h + den.h + 6, den.h + 3};
            } else if (nd->op == '^') {
                int eS = SIZE > 1 ? SIZE-1 : 1;
                Dims b = measure(nd->left,  SIZE);
                Dims e = measure(nd->right, eS);
                return {b.w + e.w, b.h + e.h/2, b.baseline + e.h/2};
            } else {
                Dims l       = measure(nd->left,  SIZE);
                Dims r       = measure(nd->right, SIZE);
                int ow       = nd->op == '*' ? 0 : CW(SIZE);
                int baseline = l.baseline > r.baseline ? l.baseline : r.baseline;
                int below_l  = l.h - l.baseline;
                int below_r  = r.h - r.baseline;
                int h        = baseline + (below_l > below_r ? below_l : below_r);
                return {l.w + ow + r.w, h, baseline};
            }
        }
        case N_FUNC: {
            if (nd->op == 'r') {
                Dims c = measure(nd->left, SIZE);
                return {CW(SIZE)+2+c.w, c.h+3, c.baseline+3};
            }
            Dims c = measure(nd->left, SIZE);
            int fw = func_name_width(nd->op, SIZE);
            int h  = c.h > CH(SIZE) ? c.h : CH(SIZE);
            return {fw + CW(SIZE) + c.w + CW(SIZE), h, h/2};
        }
    }
    return {CW(SIZE), CH(SIZE), CH(SIZE)/2};
}

extern int x_cursor;
extern int y_cursor;

static int draw_str(int x, int y, const char *s, int SIZE) {
    char buf[2] = {0,0};
    while (*s) { buf[0] = *s++; draw_char(x, y, buf, 0x0000, 0x0000, SIZE); y += CW(SIZE); }
    return y;
}
static int draw_func_name(char op, int x, int y, int SIZE) {
    switch (op) {
        case '!': return draw_str(x,y,"!",SIZE);
        case 'c': return draw_str(x,y,"cos",SIZE);
        case 's': return draw_str(x,y,"sin",SIZE);
        case 't': return draw_str(x,y,"tan",SIZE);
        case 'u': return draw_str(x,y,"acos",SIZE);
        case 'v': return draw_str(x,y,"asin",SIZE);
        case 'w': return draw_str(x,y,"atan",SIZE);
        case 'f': return draw_str(x,y,"cosh",SIZE);
        case 'g': return draw_str(x,y,"sinh",SIZE);
        case 'h': return draw_str(x,y,"tanh",SIZE);
        case 'i': return draw_str(x,y,"acosh",SIZE);
        case 'j': return draw_str(x,y,"asinh",SIZE);
        case 'k': return draw_str(x,y,"atanh",SIZE);
        case 'l': return draw_str(x,y,"ln",SIZE);
        default:  return y;
    }
}
static void update_cursor(ASTNode *nd, int x, int y, int cursor_pos) {
    if (nd->src_pos < 0) return;
    if (cursor_pos - 1 == nd->src_pos) {
        x_cursor = x;
        y_cursor = y + 7;
    }
}

static int render_node(ASTNode *nd, int x, int y, int SIZE, int cursor_pos) {
    if (!nd) return y;
    Dims d = measure(nd, SIZE);
    char buf[2] = {0,0}; char numbuf[32];

    switch (nd->type) {

        case N_PLACEHOLDER: {
            int w = CW(SIZE), h = CH(SIZE);
            fill_rect(x-13,         y,         1, w, 0x0000);
            fill_rect(x + h - 14, y,         1, w, 0x0000);
            fill_rect(x-13,         y,         h, 1, 0x0000);
            fill_rect(x-13,         y + w - 1, h, 1, 0x0000);
            return y + w;
        }

        case N_NUMBER: {
            int nc = fmt_number(nd->number, numbuf);
            // src_pos is the last digit's index; src_start is the first digit's index.
            int src_start = nd->src_pos - nc + 1;
            if (src_start >= 0 && cursor_pos >= src_start + 1 && cursor_pos <= src_start + nc) {
                x_cursor = x;
                y_cursor = y -6+ (cursor_pos - src_start) * CW(SIZE);
            }
            return draw_str(x, y, numbuf, SIZE);
        }
        case N_PARENTHESIS: {
            update_cursor(nd, x, y, cursor_pos);
    if (nd->op == ')') {
        buf[0] = ')';
        draw_char(x, y, buf, 0x0000, 0x0000, SIZE);
        return y + CW(SIZE);
    }
    Dims c  = measure(nd->left, SIZE);
    int  xc = x + (c.h - CH(SIZE)) / 2;
    buf[0] = '(';
    draw_char(xc, y, buf, 0x0000, 0x0000, SIZE);
    int cy = render_node(nd->left, x, y + CW(SIZE), SIZE, cursor_pos);
    if (nd->right) {
        buf[0] = ')';
        draw_char(xc, cy, buf, 0x0000, 0x0000, SIZE);
        cy += CW(SIZE);
    }
    return cy;
        }

        case N_VARIABLE: {
            update_cursor(nd, x, y, cursor_pos);
            buf[0] = nd->variable;
            draw_char(x, y, buf, 0x0000, 0x0000, SIZE);
            return y + CW(SIZE);
        }

        case N_CONST: {
            update_cursor(nd, x, y, cursor_pos);
            buf[0] = nd->op;
            draw_char(x, y, buf, 0x0000, 0x0000, SIZE);
            return y + CW(SIZE);
        }
        
        case N_BINOP: {

            if (nd->op == '/') {
                Dims num = measure(nd->left,  SIZE);
                Dims den = measure(nd->right, SIZE);
                int bw   = d.w;
                update_cursor(nd, x + den.h + 2, y + bw/2, cursor_pos);
                render_node(nd->right, x,              y + (bw - den.w)/2, SIZE, cursor_pos);
                fill_rect(x + den.h -SIZE*7+3, y, 2, bw, 0x0000);
                render_node(nd->left,  x + den.h + 5, y + (bw - num.w)/2, SIZE, cursor_pos);
                return y + bw;

            } else if (nd->op == '^') {
                int eS = SIZE > 1 ? SIZE-1 : 1;
                Dims b = measure(nd->left,  SIZE);
                Dims e = measure(nd->right, eS);
                update_cursor(nd, x + e.h/2, y + b.w, cursor_pos);
                render_node(nd->left,  x + e.h/2, y,        SIZE, cursor_pos);
                render_node(nd->right, x+10,          y + b.w,  eS,   cursor_pos);
                return y + d.w;

            } else {
                Dims l = measure(nd->left,  SIZE);
                Dims r = measure(nd->right, SIZE);
                int  h        = d.h;
                int  baseline = d.baseline;
                int cy = render_node(nd->left,  x + (baseline - l.baseline), y,  SIZE, cursor_pos);
                update_cursor(nd, x + (baseline - CH(SIZE)/2), cy, cursor_pos);
                buf[0] = nd->op;
                draw_char(x + (baseline - CH(SIZE)/2), cy, buf, 0x0000, 0x0000, SIZE);
                cy += CW(SIZE);
                return render_node(nd->right, x + (baseline - r.baseline), cy, SIZE, cursor_pos);
            }
        }

        case N_FUNC: {
            update_cursor(nd, x, y, cursor_pos);
            if (nd->op == 'r') {
                Dims c = measure(nd->left, SIZE);
                buf[0] = 'R'; draw_char(x+3, y, buf, 0x0000, 0x0000, SIZE);
                int cy = y + CW(SIZE) + 2;
                fill_rect(x, cy, 1, c.w, 0x0000);
                render_node(nd->left, x+3, cy, SIZE, cursor_pos);
                return cy + c.w;
            } else if(nd->op=='!') {
                   int cy = render_node(nd->left, x, y, SIZE, cursor_pos);
                buf[0] = '!'; draw_char(x, cy, buf, 0x0000, 0x0000, SIZE);
                return cy + CW(SIZE);

            }else {
                Dims c = measure(nd->left, SIZE);
                int  h = d.h;
                int cy = draw_func_name(nd->op, x + (h-CH(SIZE))/2, y, SIZE);
                int xc = x + (h-c.h)/2;
                buf[0] = '('; draw_char(xc, cy, buf, 0x0000, 0x0000, SIZE); cy += CW(SIZE);
                cy = render_node(nd->left, xc, cy, SIZE, cursor_pos);
                buf[0] = ')'; draw_char(xc, cy, buf, 0x0000, 0x0000, SIZE);
                return cy + CW(SIZE);
            }
        }
    }
    return y + d.w;
}


void display_equation(char *in, int input_size, int x, int y, int SIZE, int cursor_pos)
{
    x += 10;
    reset_pool();
    if (!in || input_size == 0) return;

    int    tok_n = 0;
    token *toks  = parse_string_to_token(in, input_size, &tok_n);
    Parser p     = { toks, tok_n, 0 };
    int    cy    = y + 5;

    while (p.pos < p.n) {
        int before   = p.pos;
        ASTNode *node = parse_equation(&p);
        if (node) cy = render_node(node, x + 14, cy, SIZE, cursor_pos);
        if (p.pos == before) p.pos++; 
    }

    free(toks);
}

void blink_cursor()
{
    char *temp = (char*) malloc(sizeof(char) * 2);

    temp[0] = '|';
    temp[1] = '\0';

    if (to_ms_since_boot(get_absolute_time()) % 1500 < 750) {
        draw_char(x_cursor, y_cursor, temp, 0X0000, 0X0000, 2);
    } else {
        draw_char(x_cursor, y_cursor, temp, BACKGROUND_COLOR, BACKGROUND_COLOR, 2);
    };
}