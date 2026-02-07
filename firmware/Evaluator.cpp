#include <cstdbool>
#include <stdlib.h>
#include <cmath>
#include <math.h>
#include <stdio.h>
#include <string>
#include "headers/stack.h"
#include "headers/Evaluator.h"

double fact_r(double in)
{
    if (in == 0)
        return 1;
    return fact_r(in - 1) * in;
}

double fact(double in)
{
    if (in - (int)in > 0.05 || in < 0)
        return 0;
    else
        return (double)fact_r((int)in);
}
int double_to_string_scientific(double in, char *out)
{
    char buffer[256];
    std::sprintf(buffer, "%.15g", in);
    int j = 0;
    int s = 0;
    while (buffer[j] != '\0') {
        j++;
    }
    //free(out);
    //out = (char*) malloc(sizeof(char) * (j + 2));
    for (int i = 0; i < j + 2; i++) {
        (out)[i] = '\0';
    }
    s = j;
    int k = 0;
    for (int i = 0; i < j; i++) {
        if (buffer[i] == 'e') {
            out[i] = '*';
            out[i + 1] = '1';
            out[i + 2] = '0';
            out[i + 3] = '^';
            k = 2;
            i++;
            i++;
            s = j + 2;
        }
        out[i + k] = buffer[i];
    }
    for (int i = j - 1; i >= 0 && out[i] == '0'; i--) {
        out[i] = '\0';
        s--;
    }
    if (out[s - 1] == '.') {
        out[s - 1] = '\0';
        s--;
    }
    return s;
}

bool is_in(char test, char *arr)
{
    int i = 0;
    while (arr[i] != '\0') {
        if (test == arr[i]) {
            return true;
        }
        i++;
    }
    return false;
}

int count_yarded(char *in)
{
    int out = 0;
    for (int i = 0; in[i] != '\0'; i++) {
        if (!is_in(in[i], (char*) "()")) {
            out++;
        }
    }
    return out - 1;
}

int count_yarded(token *in, int n)
{
    int out = 0;
    for (int i = 0; i < n; i++) {
        if (!is_in(in[i].type, (char*) "()")) {
            out++;
        }
    }
    return out;
}

bool higher_priority(char a, char b)
{
    if (a == '^'|| a=='!') {
        return true;
    }
    if (b == '^') {
        return false;
    }
    if (is_in(a, (char*) "+-") && is_in(b, (char*) "*/")) {
        return false;
    }
    if (is_in(a, (char*) "*/") && is_in(b, (char*) "-+")) {
        return true;
    }
    return false;
}

token *shunting_yard(token *input, int n)
{
    char *function = (char*) "lrcstuvwfghijk!";
    char *operato = (char*) "+-*/^";
    char *letters = (char*) "ABCDEFGHIJKLMNOPQRSTUVWYZ";
    token *output = (token*) malloc(sizeof(token) * 20);
    if (output == NULL) {
        printf("YYAA");
    }
    int output_size = 0;
    stack Operator_stack;
    init(&Operator_stack, n);
    for (int i = 0; i < n; i++) {
        if (is_in(input[i].type, letters) || input[i].type == 'n' || input[i].type == 'X' || input[i].type == 'e'|| input[i].type == 'p') {
            output[output_size].value = input[i].value;
            output[output_size].type = input[i].type;
            output_size++;
        } else if (is_in(input[i].type, function)) {
            push(&Operator_stack, input[i].type);
        } else if (is_in(input[i].type, operato)) {
            while (is_in(input[i].type, operato) && higher_priority(peek(&Operator_stack), input[i].type)) {
                output[output_size].type = peek(&Operator_stack);
                output_size++;
                pop(&Operator_stack);
            }
            push(&Operator_stack, input[i].type);
        } else if (input[i].type == '(') {
            push(&Operator_stack, input[i].type);
        } else if (input[i].type == ')') {
            while(peek(&Operator_stack) != '(') {
                output[output_size].type = peek(&Operator_stack);
                pop(&Operator_stack);   
                output_size++;
            }
            pop(&Operator_stack);
            if (is_in(peek(&Operator_stack), function)) {
                output[output_size].type = peek(&Operator_stack);
                pop(&Operator_stack);
                output_size++;
            }
        }
    }
    while (!isEmpty(&Operator_stack)) {
        output[output_size].type = peek(&Operator_stack);
        pop(&Operator_stack);
        output_size++;
    }
    return output;
}

double evaluate_npi(token *in, int n, double x, int letter)
{
    stack_d nb_stack;
    double a;
    double b;
    init(&nb_stack, n);

    for (int i = 0; i < n; i++) {
        if (in[i].type == 'X') {
            if ((int) (in[i].value) + (int) 'a' == letter) {
                push(&nb_stack, x);
            } else {
                push(&nb_stack, (double) 0);
            }
        }
        if (in[i].type == 'n') {
            push(&nb_stack, in[i].value);
        } else if (is_in(in[i].type, (char*) "+-*/^")) { // operator requiring 2 input
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type) {
            case '+' :
                push(&nb_stack, a + b);
                break;
            case '*' :
                push(&nb_stack, a * b);
                break;
            case '-' :
                push(&nb_stack, b - a);
                break;
            case '/' :
                if(a == 0) {
                    return NAN;
                }
                push(&nb_stack , b / a);
                break;
            case '^' :
                push(&nb_stack, pow(b, a));
                break;
            default :
                break;
            }
        } else { //in case of function requiring single argument or no argument
            a = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type) {
            case 'r' :
                a = sqrt(a);
                break;
            case 'l' :
                a = log(a);
                break;
            case 'c' :
                a = cos(a);
                break;
            case 's' :
                a = sin(a);
                break;
            case 't' :
                a = tan(a);
                break;
            case 'u' :
                a = acos(a);
                break;
            case 'v' :
                a = asin(a);
                break;
            case 'w' :
                a = atan(a);
                break;
            case 'f':
                a = cosh(a);
                break;
            case 'g':
                a = sinh(a);
                break;
            case 'h':
                a = tanh(a);
                break;
            case 'i':
                a = acosh(a);
                break;
            case 'j':
                a = asinh(a);
                break;
            case 'k':
                a = atanh(a);
                break;
            case '!':
                a = fact(a);
                break;
            case 'p' :
                push(&nb_stack, a);
                a = 3.14159265358979323846264338327;
                break;
            case 'e' :
                push(&nb_stack, a);
                a = 2.71828182845904523536;   
                break;
            default :
                break;
            }
            push(&nb_stack, a);
        }  
    }
    a = peek(&nb_stack);
    free(&nb_stack);
    return a;
}

double evaluate_npi(token *in, int n)
{
    stack_d nb_stack;
    double a;
    double b;
    init(&nb_stack, n);
    int nb_frac = 0;
    for (int i = 0; i < n; i++) {
        if (in[i].type == 'n') {
            push(&nb_stack, in[i].value);
        } else if (is_in(in[i].type, (char*) "+-*/^")) { // operator requiring 2 input
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type) {
            case '+' :
                push(&nb_stack, a + b);
                break;
            case '*' :
                push(&nb_stack, a * b);
                break;
            case '-' :
                push(&nb_stack, b - a);
                break;
            case '/' :
                push(&nb_stack, b / a);
                break;
            case '^' :
                push(&nb_stack, pow(b, a));
                break;
            default :
                break;
            }
        } else { //in case of function requiring single argument or no argument
            a = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type) {
            case 'r' :
                a = sqrt(a);
                break;
            case 'l' :
                a = log(a);
            break;
            case 'c' :
                a = cos(a);
                break;
            case 's' :
                a = sin(a);
                break;
            case 't' :
                a = tan(a);
                break;
            case 'u' :
                a = acos(a);
                break;
            case 'v' :
                a = asin(a);
                break;
            case 'w' :
                a = atan(a);
                break;
            case 'p' :
                push(&nb_stack, a);
                a = 3.14159265358979323846264338327;
                break;
            case 'e' :
                push(&nb_stack, a);
                a = 2.71828182845904523536;   
                break;
            case 'f':
                a = cosh(a);
                break;
            case 'g':
                a = sinh(a);
                break;
            case 'h':
                a = tanh(a);
                break;
            case 'i':
                a = acosh(a);
                break;
            case 'j':
                a = asinh(a);
                break;
            case 'k':
                a = atanh(a);
                break;
            case '!':
                a = fact(a);
                break;
            default :
                break;
            }
            	push(&nb_stack, a);
        }  
    }
    a = peek(&nb_stack);
    free(&nb_stack);
    return a;
}

char *prompt_input(int max_size)
{
    char *in = (char*) malloc(sizeof(char) * max_size);
    for (int i = 0; i < max_size; i++) {
        in[i] = EOF;
    }
    scanf("%[^\n]s", in);
    return in;
}

token *parse_string_to_token(char *in, int n, int *tokenized_size)
{ // suppose input is correct, i'm too lazy to handle this, random behavior if not
    if (n == 0) {
        token *out = (token*) malloc(sizeof(token));
        out[0].type = 'n';
        out[0].value = 0;
        *tokenized_size = 1;
        return out;
    }
    int OUT_S = n;
    for (int i = 0; i < n; i++)
    {
        if (in[i] == '!')
        {
            OUT_S += 2;
        }
    }
    token *out = (token *)malloc(sizeof(token) * (OUT_S));
    int out_size = 0;
    char *nb = (char*) "0123456789";
    char *funop = (char*) "+-*/rcstuvwfghijkel)pX=^";
    token cur;
    cur.value = 0;
    cur.type = 'n';
    
    for (int i = 0; i < n; i++) {
        if (is_in(in[i], nb)) {
            while (i < n && is_in(in[i], nb)) {
                cur.type = 'n';
                cur.value *= 10;
                cur.value += (int) (in[i] - '0');
                i++;
            }
            if (i == n || (in[i] != ',' && in[i] != '.')) {
                out[out_size].value = cur.value;
                out[out_size].type = 'n';
                cur.value = 0;
                out_size++;
            }
        }
        
        if (in[i] == '!')
        {
            int pronf = 0;
            int k = out_size - 1;
            do
            {
                if (out[k].type == '(')
                    pronf++;
                if (out[k].type == ')')
                    pronf--;
                k--;
            } while (k > 0 && pronf != 0);

            if (k == out_size - 2)
            { // nombre juste avant la factorielle

                token temp;
                temp.h = out[out_size - 1].h;
                temp.type = out[out_size - 1].type;
                temp.value = out[out_size - 1].value;
                out[out_size - 1].type = '!';
                out[out_size - 1].value = (int)'!' - 'a';
                out[out_size].type = '(';
                out[out_size].value = (int)'(' - 'a';

                out[out_size + 1].type = temp.type;
                out[out_size + 1].value = temp.value;

                out[out_size + 2].type = ')';
                out[out_size + 2].value = (int)')' - 'a';
                out_size += 3;
            }
        }
        if (i < n && in[i] == ',' || in[i] == '.') {
            double multiplicator = 0.1;
            i++;
            while (i < n && is_in(in[i], nb)) {
                cur.value += (in[i] - '0') * multiplicator;
                multiplicator *= 0.1;
                i++;
            }
            out[out_size].value = cur.value;
            out[out_size].type = 'n';
            cur.value = 0;
            out_size++;
        }
        if (i < n && is_in(in[i], (char*) "(")) {
            out[out_size].value = (int) in[i] - 'a';
            out[out_size].type = in[i];
            out_size++; 
        } else if (i < n && is_in(in[i], funop)) {
            out[out_size].value = (int) in[i] - 'a';
            out[out_size].type = in[i];
            out_size++;
        } else if (in[i] <= 'Z' && 'A' <= in[i]) {
            out[out_size].value = in[i] - 'a';
            out[out_size].type = 'X';
            out_size++;
        }
    }
    *tokenized_size = out_size;
    return(out);
}
