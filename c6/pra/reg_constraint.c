#include <stdio.h>
void main() {
    int in_a = 1, in_b = 2, out_sum;
    asm("addl %%ebx, %%eax" : "=a"(out_sum):"a"(in_a), "b"(in_b));
    printf("sum is %d\n", out_sum);
}