section .data
str_c_lib: db "c library says : hello world!", 0xa  ;0xa 为 LF ASCII码
str_c_lib_len equ $-str_c_lib

str_syscall: db "syscall says : hello world!", 0xa
str_syscall_len equ $-str_syscall

section .text
global _start
_start:
;;;;;模拟 方式一 利用 c 语言进行调用
; write 总共三个参数 从右至左压入
    push str_c_lib_len
    push str_c_lib
    push 1

    call simu_write
    add esp, 12     ; 回收栈空间

;;;;;; 模拟方式二 跨过库函数，直接进行系统调用
    mov eax, 4          ; 4 号功能是write的系统调用
    mov ebx, 1
    mov ecx, str_syscall
    mov edx, str_syscall_len
    int 0x80        ;发起中断

;;;;;  退出程序
    mov eax, 1      ; 第 1 号子功能为 exit
    int 0x80

;;;;;;; 自定义 simu_write 函数 用来模拟系统调用的 write 
simu_write:
    push ebp
    mov ebp, esp
    mov eax, 4
    mov ebx, [ebp + 8]
    mov ecx, [ebp + 12]
    mov edx, [ebp + 16]
    int 0x80
    pop ebp     ; 恢复ebp
    ret



