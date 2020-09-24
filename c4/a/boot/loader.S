%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR

jmp loader_start


; 加载GDT表
; 一个dd为4字节，段描述符为8字节，上面为低4字节
; 第一行的 0描述符不可用 因此直接置为 全 0
GDT_BASE:   dd 0x00000000
            dd 0x00000000
            
; GDT 中 先安放三个有用描述符
; 代码段、数据段和栈段、显存段
CODE_DESC:  dd 0x0000FFFF       ; 低32位表示 2字节base + 2字节段限界 
            dd DESC_CODE_HIGH4

; 栈段和数据段共用，向上扩展 type的e为0
DATA_STACK_DESC:    dd 0x0000FFFF
                    dd DESC_DATA_HIGH4

; 显卡段，非平坦，从 0xb8000开始
VIDEO_DESC: dd 0x80000007   ; 段界限为 (bffff - b8000 ) / 4K = 7
            dd DESC_VIDEO_HIGH4

GDT_SIZE equ $ - GDT_BASE   ;$ 表示本行标号, 通过地址差获得size
GDT_LIMIT equ GDT_SIZE - 1

; 填充空白描述符 60 个
times 120 dd 0
; 构建选择子 
SELECTOR_CODE equ (0x0001 << 3) + TI_GDT + RPL0 
SELECTOR_DATA equ (0x0002 << 3) + TI_GDT + RPL0 
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0 

; GDTR 寄存器
gdt_ptr dw GDT_LIMIT
        dd GDT_BASE

loadermsg db '2 loader in real.'    ; 打印输出 长度为 17

;------------------------------------------------------------
;INT 0x10    功能号:0x13    功能描述:打印字符串
;------------------------------------------------------------
;输入:
;AH 子功能号=13H
;BH = 页码
;BL = 属性(若AL=00H或01H)
;CX＝字符串长度
;(DH、DL)＝坐标(行、列)
;ES:BP＝字符串地址 
;AL＝显示输出方式
;   0——字符串中只含显示字符，其显示属性在BL中。显示后，光标位置不变
;   1——字符串中只含显示字符，其显示属性在BL中。显示后，光标位置改变
;   2——字符串中含显示字符和显示属性。显示后，光标位置不变
;   3——字符串中含显示字符和显示属性。显示后，光标位置改变
;无返回值
loader_start:
    ; 调用10号中断
    mov sp, LOADER_BASE_ADDR
    mov bp, loadermsg       ; ES:BP = 字符串地址
    ; 字符串长度
    mov cx, 17
    ; 子功能号以及 显示状态
    mov	 ax, 0x1301		 ; AH = 13,  AL = 01h
    mov	 bx, 0x001f		 ; 页号为0(BH = 0) 蓝底粉红字(BL = 1fh)
    mov dx, 0x1800       ; 0x18 = 24 即最后一行 
    int 0x10        

;----------------------------------------   准备进入保护模式   ------------------------------------------
									;1 打开A20
									;2 加载gdt
									;3 将cr0的pe位置1

    ;-----------------  打开A20  ----------------
    in al, 0x92
    or al, 0000_0010b   ; 将 0x92 的第一位 置 1
    out 0x92, al

    ;-----------------  加载GDT  ----------------
    lgdt [gdt_ptr]

    ;-----------------  cr0第0位置1  ----------------
    mov eax, cr0
    or eax, 0000_0001
    mov cr0, eax

; 构建选择子 
; SELECTOR_CODE equ (0x0001 << 3) + TI_GDT + RPL0 
    ;jmp dword SELECTOR_CODE:p_mode_start	     ; 刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，
    jmp dword SELECTOR_CODE:p_mode_start	     ; 刷新流水线，避免分支预测的影响

; 初始化各个寄存器
[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp,LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax    

    mov byte [gs:160], 'P'

    jmp $