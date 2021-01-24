#include <stdio.h> 
#include "memoryalloc.c"

int main() {
    
    int *mem_test = NULL; 

    mem_test = malloc(15);  

    print_memory(); 

    return 0; 
}