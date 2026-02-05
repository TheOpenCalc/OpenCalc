#include "headers/stack.h"
#include <stdlib.h>
#include <stdio.h>

void init(Stack *stack, int size)
{
    stack->arr = (char*) malloc(sizeof(char) * size);
    stack->top = -1;
}

bool isEmpty(Stack *stack)
{
    return stack->top == -1;  
}

bool isFull(Stack *stack)
{
    return false;
    //return stack->top == MAX_SIZE - 1;  
}

void push(Stack *stack, int value)
{
    if (isFull(stack)) {
        printf("Stack Overflow\n");
        return;
    }
    stack->arr[++stack->top] = value;
}

int pop(Stack *stack)
{
    if (isEmpty(stack)) {
        printf("Stack Underflow\n");
        return -1;
    }
    int popped = stack->arr[stack->top];
    stack->top--;
    return popped;
}

int peek(Stack *stack)
{
    if (isEmpty(stack)) {
        return -1;
    }
    return stack->arr[stack->top];
}

void free(Stack *stack)
{
    free(stack->arr);
    return;
}


///////////////////////


void init(Stack_d *stack, int size)
{
    stack->arr = (double*) malloc(sizeof(double) * size);
    stack->top = -1;
    for(int i = 0 ; i < size; i++) {
        stack->arr[i] = 0;
    }
}

bool isEmpty(Stack_d *stack)
{
    return stack->top == -1;  
}

bool isFull(Stack_d *stack)
{
    return false;
    //return stack->top == MAX_SIZE - 1;  
}

void push(Stack_d *stack, double value)
{
    if (isFull(stack)) {
        return;
    }
    stack->arr[++stack->top] = value;
}

double pop(Stack_d *stack)
{
    if (isEmpty(stack)) {
        return -1;
    }
    double popped = stack->arr[stack->top];
    stack->top = stack->top - 1;
    return popped;
}

double peek(Stack_d *stack)
{
    if (isEmpty(stack)) {
        return -1;
    }
    return stack->arr[stack->top];
}

void free(Stack_d * stack)
{
    free(stack->arr);
    return;
}
