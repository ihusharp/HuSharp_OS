[bits 32]
extern	 main   ; 即用户程序中的主函数
section .text
global _start      ; 链接器的默认入口
_start:
; 此时压入的是用户栈， 可见参考 execv 函数中的注释
;下面这两个要和execv中load之后指定的寄存器一致
    push ebx
    push ecx
    call main