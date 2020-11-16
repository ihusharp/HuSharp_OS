[bits 32]
section .text
global switch_to
; switch_to 函数接收两个参数
; 一个是当前线程 cur， 一个是下一个处理器线程 
; 因此该函数的作用是 保存当前线程，将下一个线程放到处理器中
; 
; struct thread_stack {
;     uint32_t ebp;
;     uint32_t ebx;
;     uint32_t edi;
;     uint32_t esi; 
; 
; 易知反向压栈
    ; 之前完成第一部分  kernel.S/intr%1entry 
    ; 第一部分：保存中断前的上下文环境
    ; 压入到内核栈，因此之后将内核栈储存
; switch_to 完成第二部分，用于任务切换后恢复执行中断处理程序中的后续代码
switch_to:
    push esi
    push edi
    push ebx
    push ebp

; struct task_struct {
;     uint32_t* self_kstack;  // 各内核线程都用自己的内核栈

    mov eax, [esp + 20]     ; 得到栈中参数 4*4 + 4 即 4 个压栈 和 1 个返回地址
    mov [eax], esp          ; eax 指的是 PCB 的首地址，而第一个参数是 每个线程的内核栈

    ; 往处理器上装载调度的新线程上下文
    mov eax, [esp + 24]     ; 同理
    mov esp, [eax]

    pop ebp     ; 此时恢复的是：新线程内核栈内部的上下文
    pop ebx
    pop edi
    pop esi
    ret     ; ret 改变 eip 指向，使 next 线程恢复执行