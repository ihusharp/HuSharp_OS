char* str = "hello, world!\n";

int count = 0;
void main() {
asm("pusha; \
    movl $4, %eax;  \
    movl $1, %ebx;  \
    movl str, %ecx; \
    movl $15, %edx; \
    int $0x80;      \
    mov %eax, count;    \
    popa             \
    ");
}