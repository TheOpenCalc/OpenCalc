#ifndef EVALUATOR_H
#define EVALUATOR_H

struct token_s {
    char type;
    double value;
    int h = 0;
};

struct operation_s {
    char operator_type;
    void *el1;
    void *el2;
};

typedef struct operation_s operation;

typedef struct token_s token;

int double_to_string_scientific(double in, char *out);

bool is_in(char test, char *arr);

int count_yarded(char *in);

int count_yarded(token *in, int n);

bool higher_priority(char a, char b);

token *shunting_yard(token *input, int n);

double evaluate_npi(token *in, int n, double x, int letter);

double evaluate_npi(token *in, int n);

char *prompt_input(int max_size);

token *parse_string_to_token(char *in, int n, int *tokenized_size);

#endif
