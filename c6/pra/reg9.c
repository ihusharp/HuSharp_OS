#include <stdio.h>
void main() {
    int in_a = 0, sum = 0;
    asm("addl %1, %0;"
        : "=a"(sum)
        : "%I"(2), "0"(in_a));
    printf("sum is %d\n", sum);
}