; 
;------------------------------------------------------------
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

; 
;0x06?????
; -----------------------------------------------------------
;INT 0x10   ?:0x06	   :?
;------------------------------------------------------
;?
;AH ?= 0x06
;AL = ?(?0,??)
;BH = ?
;(CL,CH) = ??(X,Y)?
;(DL,DH) = ½?(X,Y)?
;??
   mov     ax, 0600h
   mov     bx, 0700h
   mov     cx, 0                   ; ?: (0, 0)
   mov     dx, 184fh		   ; ½: (80,25),
				   ; ?VGA??????80?,25?
				   ; ±0?0x18=24,0x4f=79
   int     10h                     ; int 10h

   ; ?:MBR
   mov byte [gs:0x00],'1'
   mov byte [gs:0x01],0xA4

   mov byte [gs:0x02],' '
   mov byte [gs:0x03],0xA4

   mov byte [gs:0x04],'M'
   mov byte [gs:0x05],0xA4	   ;A???4?????

   mov byte [gs:0x06],'B'
   mov byte [gs:0x07],0xA4

   mov byte [gs:0x08],'R'
   mov byte [gs:0x09],0xA4
	 
   mov eax,LOADER_START_SECTOR	 ; ?lba?
   mov bx,LOADER_BASE_ADDR       ; ???
   mov cx,4			 ; 
   call rd_disk_m_16		 ; ¶????
  
   jmp LOADER_BASE_ADDR + 0x300
       
;-------------------------------------------------------------------------------
;:??n
rd_disk_m_16:	   
;-------------------------------------------------------------------------------
				       ; eax=LBA
				       ; ebx=???
				       ; ecx=
      mov esi,eax	  ;eax
      mov di,cx		  ;cx
;??:
;1??
      mov dx,0x1f2
      mov al,cl
      out dx,al            ;?

      mov eax,esi	   ;?ax

;2LBA?0x1f3 ~ 0x1f6

      ;LBA?7~0???0x1f3
      mov dx,0x1f3                       
      out dx,al                          

      ;LBA?15~8???0x1f4
      mov cl,8
      shr eax,cl
      mov dx,0x1f4
      out dx,al

      ;LBA?23~16???0x1f5
      shr eax,cl
      mov dx,0x1f5
      out dx,al

      shr eax,cl
      and al,0x0f	   ;lba24~27?
      or al,0xe0	   ; 74??1110,?lba??
      mov dx,0x1f6
      out dx,al

;30x1f7???0x20 
      mov dx,0x1f7
      mov al,0x20                        
      out dx,al

;4???
  .not_ready:
      ;??????????????
      nop
      in al,dx
      and al,0x88	   ;4??1??????7??1??æ
      cmp al,0x08
      jnz .not_ready	   ;??ã?

;50x1f0??
      mov ax, di
      mov dx, 256
      mul dx
      mov cx, ax	   ; di????512??ÿ???
			   ; di*512/2?di*256
      mov dx, 0x1f0
  .go_on_read:
      in ax,dx
      mov [bx],ax
      add bx,2		  
      loop .go_on_read
      ret

   times 510-($-$$) db 0
   db 0x55,0xaa
