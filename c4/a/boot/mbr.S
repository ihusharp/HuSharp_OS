;主引导程序 
;
;LOADER_BASE_ADDR equ 0xA000 
;LOADER_START_SECTOR equ 0x2
;------------------------------------------------------------
# %include 为nasm自带的预处理
%include "boot.inc"
SECTION MBR vstart=0x7c00         
   mov ax,cs      
   mov ds,ax
   mov es,ax
   mov ss,ax
   mov fs,ax
   mov sp,0x7c00
   mov ax,0xb800
   mov gs,ax

; 清屏
;利用0x06号功能，上卷全部行，则可清屏。
; -----------------------------------------------------------
;INT 0x10   功能号:0x06	   功能描述:上卷窗口
;------------------------------------------------------
;输入：
;AH 功能号= 0x06
;AL = 上卷的行数(如果为0,表示全部)
;BH = 上卷行属性
;(CL,CH) = 窗口左上角的(X,Y)位置
;(DL,DH) = 窗口右下角的(X,Y)位置
;无返回值：
   mov     ax, 0600h
   mov     bx, 0700h
   mov     cx, 0               ; 左上角: (0, 0)
   mov     dx, 184fh	       ; 右下角: (80,25),
			       ; 因为VGA文本模式中，一行只能容纳80个字符,共25行。
			       ; 下标从0开始，所以0x18=24,0x4f=79
   int     10h                 ; int 10h

   ; 输出背景色绿色，前景色红色，并且跳动的字符串"1 MBR"
   mov byte [gs:0x00],'1'
   mov byte [gs:0x01],0xA4     ; A表示绿色背景闪烁，4表示前景色为红色

   mov byte [gs:0x02],' '
   mov byte [gs:0x03],0xA4

   mov byte [gs:0x04],'M'
   mov byte [gs:0x05],0xA4   

   mov byte [gs:0x06],'B'
   mov byte [gs:0x07],0xA4

   mov byte [gs:0x08],'R'
   mov byte [gs:0x09],0xA4

   mov eax, LOADER_START_SECTOR     ; 起始扇区的lba地址
   mov bx, LOADER_BASE_ADDR         ; 写入的地址
   mov cx, 4                        ; 等待读入的扇区数

   call rd_disk_m_16                ; 读取程序的起始部分

   jmp LOADER_BASE_ADDR

;-------------------------------------------------------------------------------
;功能:读取硬盘n个扇区
; 函数名即为 在 16 位下读硬盘
rd_disk_m_16:	   
;-------------------------------------------------------------------------------
   ; eax= LBA扇区号
	; ebx= 将数据写入的内存地址
	; ecx= 读入的扇区数
   mov esi, eax   ;备份 eax
   mov di, cx     ; 备份 cx

; 读写硬盘
;  第 1 步： 设置要读取的扇区数
   mov dx, 0x1f2
   mov al, cl     ; out 指令从 端口输入一个字节或字到AL或AX中
   out dx, al     ;读取扇区数

   mov eax, esi   ;恢复备份

;第2步：将LBA地址存入0x1f3 ~ 0x1f6
   ;LBA地址7~0位写入端口0x1f3
   mov dx, 0x1f3
   out dx, al

   ;LBA地址15~8位写入端口0x1f4
   mov cl, 8
   shr eax, cl ;右移8位
   mov dx, 0x1f4
   out dx, al

   ;LBA地址23~16位写入端口0x1f5
   shr eax, cl
   mov dx, 0x1f5
   out dx, al

   ; 写入 device 寄存器的 低四位
   shr eax, cl
   and al, 0x0f   ;第 24~27位
   ; 现需要更改device寄存器的其他位置，设7~4位为1110
   or al, 0xe0
   mov dx, 0x1f6
   out dx, al

;第3步：向0x1f7端口写入读命令，0x20 
   mov dx, 0x1f7
   mov al, 0x20
   out dx, al

;第4步：检测硬盘状态
.not_ready:
   ;同一端口，写时表示写入命令字，读时表示读入硬盘状态
   nop   ;增加延迟 减少打扰硬盘
   in al, dx
   and al,0x88	   ;第3位为1表示硬盘控制器已准备好数据传输，第7位为1表示硬盘忙
   cmp al, 0x08
   jnz .not_ready ;循环等待

;第5步：从0x1f0端口读数据   
   mov ax, di     ;di 为之前备份的扇区数
   mov dx, 256 
   mul dx         ;一个扇区有512字节，每次读入一个字，
                  ; 共需di*512/2次，所以di*256 
   mov cx, ax     ; 将乘法结果次数写入 cx 为下面的写入做好准备
                  ; 且我们知道 该 16 位乘法的结果不会大于16位寄存器
   mov dx, 0x1f0  ; data 寄存器读入数据
.go_on_read:
   in ax, dx      ; 写入到 bx 指向的内存，由于 bx = 1~65535，因此写入数据不能超过 64kb 
   mov [bx], ax
   add bx, 2   ;每次传输2个字节
   loop .go_on_read
   ret         ; 返回到 58 行的 call 指令


   times 510-($-$$) db 0
   db 0x55,0xaa