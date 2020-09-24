; 内核打印功能的实现
; 定义视频段的选择子
TI_GDT equ 0
RPL0 equ 0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

section .data
put_int_buffer  dq  0    ; 定义8 字节缓冲区用于

[bits 32]
section .text
;--------------------------------------------
;put_str 通过put_char来打印以0字符结尾的字符串
global put_str
put_str:
; 由于本函数  只用到 ecx ebx 因此只备份这两个
    push ebx
    push ecx
    xor ecx, ecx    ;清空ecx
    mov ebx, [esp + 12] ;得到 str 的内存地址

.goon:
    mov cl, [ebx]      ; cl 为当前字符的 ASCII 码
    cmp cl, 0       ; 判断是否到达末尾
    jz .str_over
    push ecx    ; 向push_char 传入参数
    call put_char
    add esp, 4      ; 回收参数所占栈空间
    inc ebx         ; ebx 指向下一个字符
    jmp .goon
.str_over:
    pop ecx
    pop ebx
    ret


; put_char，将栈中的一个字符写入光标所在处
global put_char 
put_char:
    pushad  ;压入 所有双字长寄存器  push all double
    mov ax, SELECTOR_VIDEO      
    mov gs, ax

;;;;;;;   获取当前光标值    ;;;;;;;;;;;
    ; 先获取高 8 位
    mov dx, 0x03d4      ; 向端口 为 0x03d4 写入索引
    mov al, 0x0e        ; 索引值 high
    out dx, al
    mov dx, 0x03d5      ; 向该端口的  data寄存器 读写数据
    in al, dx           ; in 8位时 必须为 al 因此不能用 ah
    mov ah, al          ; 将 al 即高 8 位放入 ah 中

    mov dx, 0x03d4      ; 向端口 为 0x03d4 写入索引
    mov al, 0x0f        ; 索引值 low
    out dx, al
    mov dx, 0x03d5      ; 向该端口的  data寄存器 读写数据
    in al, dx           ; in 8位时 必须为 al 因此不能用 ah
; 至此 ax 中存放的是光标完整的 16 位地址

    ; 将光标存入 bx
    mov bx, ax
    ; 从栈中获取待打印的字符
    ; 由于 pushad 压入 8个 double，因此4 * 8
    mov ecx, [esp + 36]

    ; 开始判断打印字符是什么字符
    ; 若为 回车符CR 0xd ，换行符 LF 0xa， 退隔符 backspace 0x8，则分别进入相对应流程
    cmp cl, 0xd
    jz .is_carriage_return
    cmp cl, 0xa
    jz .is_line_feed

    cmp cl, 0x8
    jz .is_backspace
    jmp .put_other	  

; bx 已经为下一个光标位置
.is_backspace:
;;;;;;;;;   backspace 的说明   ;;;;; 
    dec bx
    shl bx, 1   ;<<1

    mov byte [gs:bx], 0x20  ;补为空格
    inc bx
    mov byte [gs:bx], 0x7   ; 显存颜色 黑屏白字
    shr bx, 1
    jmp .set_cursor


.put_other:
    shl bx, 1   ;<<1

    mov byte [gs:bx], cl  ;补为空格
    inc bx
    mov byte [gs:bx], 0x7   ; 显存颜色 黑屏白字
    shr bx, 1   
    inc bx
    ; 判断是否需要滚屏
    cmp bx, 2000
    jl .set_cursor
    
    ; 滚屏也可以是 CR LF
.is_line_feed:              ;   是 LF 换行符
.is_carriage_return:        ;   是 CR 回车符
;
    xor dx, dx      ;dx 是被除数的高 16 位 因此清 0 
    mov ax, bx  ;被除数
    mov si, 80

    div si

    ; 对 bx 取整，移至本行首个字符处 以实现回车
    sub bx, dx     ; 光标值减去 除了80 的余数 便是取整

    ; 接下来处理换行符
.is_carriage_return_end:
    add bx, 80
    cmp bx, 2000
.is_line_feed_end:      ; 若是 LF 将光标 +80 即可
    jl .set_cursor

; 现在由于是最后一行的滚屏，因此需要将 1-24 行 搬到第 0 -23 行
; 再将 24 行用空格填充
.roll_screen:
    cld
    mov ecx, 960

    mov esi, 0xc00b80a0 ; 第一行行首
    mov edi, 0xc00b8000 ; 第二行行首
    rep movsd              ; 循环复制

;;;; 将最后一行填充为空白
    mov ebx, 3840   ; 最后一行首字符的第一个字符偏移量
    mov ecx, 80     ; 一共80字符，每次清理一个字符

.cls:
    mov word [gs:ebx], 0x0720
    add ebx, 2
    loop .cls
    mov bx, 1920

.set_cursor:
    mov dx, 0x03d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x03d5
    mov al, bh
    out dx, al

    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    mov al, bl
    out dx, al

.put_char_done:
    popad
    ret    

;--------------------   将小端字节序的数字变成对应的ascii后，倒置   -----------------------
;输入：栈中参数为待打印的数字
;输出：在屏幕上打印16进制数字,并不会打印前缀0x,如打印10进制15时，只会直接打印f，不会是0xf
;------------------------------------------------------------------------------------------
global put_int
put_int:
    pushad
    mov ebp, esp    ; 通过 ebp 来获取参数
    mov eax, [ebp + 4*9]        ; pushad 占 8 个 4 字节， call的返回地址占 4 个字节 
    mov edx, eax
    mov edi, 7      ; 缓冲区中的偏移量，指向缓冲区中的最后 1 个字节
    mov ecx, 8      ; 32 位数中，16 进制数字的位数为 8 个
    mov ebx, put_int_buffer

; 将 32 位 数字按照 16 进制的形式从低位到高位逐个处理
.16based_4bits:
    and edx, 0x0000000F     ; 解析 16 进制的每一位，edx只有 4 位
    cmp edx, 9      ; 0-9 和 A-Z 需要分别处理
    jg .is_A2F
    add edx, '0'
    jmp .store

.is_A2F:
    sub edx, 10 ; 和 10 的差
    add edx, 'A'

;将每一位数字转换成对应的字符后,按照类似“大端”的顺序存储到缓冲区put_int_buffer
;高位字符放在低地址,低位字符要放在高地址,这样和大端字节序类似,只不过咱们这里是字符序.
.store:
; 此时 dl 中应该是数字对应的字符 ASCII 码
    mov [ebx + edi], dl
    dec edi
    shr eax, 4  ; 右移得到 下一个数位值
    mov edx, eax
    loop .16based_4bits

;现在put_int_buffer中已全是字符,打印之前,
;把高位连续的字符去掉,比如把字符000123变成123
.ready_to_print:
    inc edi         ; 此时edi退减为-1(0xffffffff),加1使其为0
.skip_prefix_0:
    cmp edi, 8      ; 跳过前面的 0 字符，若已经是第 9 个字符了，说明前面全部为 0
    je .full0       ; 将其处理为 字符 '0'

;找出连续的0字符, edi做为非0的最高位字符的偏移
    mov cl, [put_int_buffer + edi]
    inc edi
    cmp cl, '0'
    je .skip_prefix_0
    ;edi在上面的inc操作中指向了下一个字符,若当前字符不为'0',要恢复edi指向当前字符		  
    dec edi
    jmp .put_each_num

.full0:
    mov cl, '0'     ; 只打印 0 
.put_each_num:
    push ecx        ; 此时 cl 中为 可打印的字符
    call put_char
    add esp, 4
    inc edi         ; 使 edi 指向下一个字符
    mov cl, [put_int_buffer + edi]
    cmp edi, 8
    jl .put_each_num        ; 逐个打印 cl 中的字符
    popad
    ret

