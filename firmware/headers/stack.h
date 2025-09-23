#ifndef STACK_H
#define STACK_H
struct Stack{
    char* arr;  
    int top;        
} ;
struct Stack_d{
    double* arr;  
    int top;        
} ;

typedef struct Stack stack;

typedef struct Stack_d stack_d;


void init(Stack *stack,int size);
bool isEmpty(Stack *stack);
bool isFull(Stack *stack);
void push(Stack *stack, int value);
int pop(Stack *stack);
int peek(Stack *stack);
void free(Stack * stack);


void init(Stack_d *stack,int size);
bool isEmpty(Stack_d *stack);
bool isFull(Stack_d *stack);
void push(Stack_d *stack, double value);
double pop(Stack_d *stack);
double peek(Stack_d *stack);
void free(Stack_d * stack);
#endif