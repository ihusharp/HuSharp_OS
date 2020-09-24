// 名称占位符
#include <stdio.h>
void main() {
    int in_a = 18, in_b = 3, out_sum;
    asm("divb %[divisor]; movb %%al, %[result]"     \
            :[result]"=m"(out)                      \
            :"a"(in_a), [divisor]"m"(in_b)          \
            );
    printf("The result is %d\n", out_sum);
}