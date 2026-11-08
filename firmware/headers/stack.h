#ifndef STACK_H
#define STACK_H

struct complex_s
{
    double re;
    double im;
};

typedef struct complex_s complex;

struct Stack
{
    char *arr;
    int top;
};

struct Stack_d
{
    double *arr;
    int top;
};

typedef struct Stack stack;

typedef struct Stack_d stack_d;

struct Stack_c
{
    complex *arr;
    int top;
};

typedef struct Stack_c stack_c;

void init(Stack *stack, int size);

bool isEmpty(Stack *stack);

bool isFull(Stack *stack);

void push(Stack *stack, int value);

int pop(Stack *stack);

int peek(Stack *stack);

void free(Stack *stack);

void init(Stack_d *stack, int size);

bool isEmpty(Stack_d *stack);

bool isFull(Stack_d *stack);

void push(Stack_d *stack, double value);

double pop(Stack_d *stack);

double peek(Stack_d *stack);

void free(Stack_d *stack);

void init(Stack_c *stack, int size);

bool isEmpty(Stack_c *stack);

bool isFull(Stack_c *stack);

void push(Stack_c *stack, complex value);

complex pop(Stack_c *stack);

complex peek(Stack_c *stack);

void free(Stack_c *stack);

#endif
