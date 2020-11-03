#include "stdio.h"

int main()
{
    char *a = "1";
    while(*(a++) != 0) {
        printf("Null!\n");
    }
}