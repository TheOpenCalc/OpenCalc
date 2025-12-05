#include "headers/Solver.h"
#include <stdlib.h>
#include "time.h"
#include <stdio.h>

void simplify_first(double * a,double * b, int n,int first_nb){
    double K = b[first_nb]/a[first_nb];
    for (int i = first_nb; i < n; i++)
    {
        b[i]-=a[i]*K;
    }    
}
void normalize(double * a , int size, int n){
    double K = 1/a[n];
    for(int i = 0 ; i < size;i++){
        a[i]*=K;
    }
}
bool comp(double * a,double * b,int n){
    double c = b[0]-a[0];
    int i =0;
    while(abs(c)<0.001 && i<n){
        c+=b[i]-a[i];
        i++;
    }
    return c>0;
}
void insertion_sort(double ** input,int nb_element, int size_element){
    int start =0;
    while(start!=nb_element){
        int k = start;
        for(int i =start+1;i<nb_element;i++){
            printf("%i\n",i);
            if(comp(input[k],input[i],size_element)){
                double * temp= input[k];
                input[k]=input[i];
                input[i]=temp;
            }
        }
        start++;
    }

}
double * solve (double ** input, int nb_var,int nb_eq){
    insertion_sort(input,nb_eq,nb_var);
    for(int i = 0 ; i < nb_eq-1;i++){
        for(int k = i+1;k<nb_eq;k++){
            simplify_first((input[i]),(input[k]),nb_var+1,i);
            
        }
        
    }
        printdouble2D(input,5,6);
    printf("\n\n");
    for(int i = 0 ; i < nb_eq;i++){
        normalize(input[i],nb_var+1,i);
    }

    printdouble2D(input,5,6);
        printf("\n\n");
    double * solution = (double *) malloc(sizeof(double)*nb_var);
    for(int i = 0 ; i <nb_var;i++)
        solution[i]=input[i][nb_var];

        for(int i = 0 ; i < nb_var;i++){
        printf("%f \n",solution[i]);
    }
    

        for(int i = nb_eq-1;i>=0;i--){
        for(int k = 0; k < nb_var;k++){
            if(k!= i ){
                solution[i]-=solution[k]*input[i][k];
            }
        }
    }
        printf("\n\n");

    for(int i = 0 ; i < nb_var;i++){
        printf("%f \n",solution[i]);
    }
    

    return input[0];
    
}
void printdouble2D(double ** input,int L, int H){
    for(int i = 0 ; i < L;i++){
        for(int j = 0 ; j < H; j++){
            printf("%f\t\t",input[i][j]);
        }
        printf("\n");
    }
}
int main(){

        srand( time( NULL ) );
        double ** c =(double **) malloc(sizeof(double*)*5);
    for(int i=0;i<5;i++){
        c[i]=(double *)malloc(sizeof(double)*6);
        for(int j = 0 ; j <6;j++){
            c[i][j]=(rand()%20000)/500.0;
        }
    }
    printdouble2D(c,5,6);
    printf("\n\n");
    solve(c,5,5);
    //////////////////////////////////////////
    //////////////////////////////////////////
    //////////////////////////////////////////


    
}