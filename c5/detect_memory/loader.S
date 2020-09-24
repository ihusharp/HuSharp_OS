%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR

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


    ; total_mem_bytes用于保存内存容量,以字节为单位,此位置比较好记。
    ; 当前偏移loader.bin文件头0x200字节,loader.bin的加载地址是0x900,
    ; 故total_mem_bytes内存中的地址是0xb00.将来在内核中咱们会引用此地址
    total_mem_bytes dd 0

; GDTR 寄存器
gdt_ptr dw GDT_LIMIT
        dd GDT_BASE


    ards_buf times 244 db 0 ; 一个 ards 为 20字节，因此可以存放12个 
    ards_nr dw 0    ; 记录 ards 结构体的数量

;-------  int 15h eax = 0000E820h ,edx = 534D4150h ('SMAP') 获取内存布局  -------

loader_start:
    xor ebx, ebx    ; xor 表示清 0 
    mov edx, 0x534d4150
    ; es 在mbr中赋值
    mov di, ards_buf    ;缓冲区
    ; 由于每次 eax ecx ebx 都会更新
.e820_mem_get_loop:
    mov eax, 0x0000e820
    mov ecx, 20 ; ards 大小为 20字节
    int 0x15
    jc .e801_failed_so_try801   ; 通过 cf 位来进行判断
    add di, cx  ;使 di 向后20字节
    inc word [ards_nr]
    cmp ebx, 0
    jnz .e820_mem_get_loop

;在所有ards结构中，找出(base_add_low + length_low)的最大值，即内存的容量。
    mov cx, [ards_nr]
    mov ebx, ards_buf
    xor edx, edx

.find_max_mem_area:	      ;无须判断type是否为1,最大的内存块一定是可被使用
    mov eax, [ebx]  ; 取得 base 
    add eax, [ebx+8]    ; len
    add ebx, 20 ; 取得下一个
    cmp edx, eax    ; edx 存放最大值
    jge .next_ards  ; edx > eax 进入循环 ，大于就先赋值后 再循环
    mov edx, eax

.next_ards:
    loop .find_max_mem_area
    jmp .mem_get_ok


;------  int 15h ax = E801h 获取内存大小,最大支持4G  ------
.e801_failed_so_try801: 
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try88

    ; ax * 1024 单位为 1Kb
    ; 计算 15 Mb 以下的
    mov cx, 0x400
    mul cx
    ; 积 的 高16位 在dx中， 低16位在ax中
    shl edx, 16 ; 将 edx 低 16 位 置为 0 
    and eax, 0x0000FFFF ; 将 eax 高 16 置为0
    or edx, eax
    add edx, 0x100000   ; 加上 1Mb
    mov esi, edx    ; 存放在 esi 中

    ; bx * 1024 *64 单位为64kb
    ; 16 Mb 以上的
    xor eax, eax
    mov ax, bx  ; bx 存放
    mov ecx, 0x10000
    mul ecx
    add esi, eax    ; 大小最大为 4G 因此32位的 eax 便足够存放乘积了
    mov edx, esi
    jmp .mem_get_ok

;-----------------  int 15h ah = 0x88 获取内存大小,只能获取64M之内  ----------
.e801_failed_so_try88: 
    mov ah, 0x88
    int 0x15
    jc .error_hlt   ; 最终出错挂起
    and eax, 0x0000FFFF

    mov cx, 0x400
    mul cx
    shl edx, 16
    or edx, eax
    add edx, 0x100000   ; 加上 1Mb

.mem_get_ok:
    mov [total_mem_bytes], edx  ;全部都由 edx 存储
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


.error_hlt:		      ;出错则挂起
    hlt

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