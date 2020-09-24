;���������� 
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

; ����
;����0x06�Ź��ܣ��Ͼ�ȫ���У����������
; -----------------------------------------------------------
;INT 0x10   ���ܺ�:0x06	   ��������:�Ͼ�����
;------------------------------------------------------
;���룺
;AH ���ܺ�= 0x06
;AL = �Ͼ�������(���Ϊ0,��ʾȫ��)
;BH = �Ͼ�������
;(CL,CH) = �������Ͻǵ�(X,Y)λ��
;(DL,DH) = �������½ǵ�(X,Y)λ��
;�޷���ֵ��
   mov     ax, 0600h
   mov     bx, 0700h
   mov     cx, 0                   ; ���Ͻ�: (0, 0)
   mov     dx, 184fh		   ; ���½�: (80,25),
				   ; ��ΪVGA�ı�ģʽ�У�һ��ֻ������80���ַ�,��25�С�
				   ; �±��0��ʼ������0x18=24,0x4f=79
   int     10h                     ; int 10h

   ; ����ַ���:MBR
   mov byte [gs:0x00],'1'
   mov byte [gs:0x01],0xA4

   mov byte [gs:0x02],' '
   mov byte [gs:0x03],0xA4

   mov byte [gs:0x04],'M'
   mov byte [gs:0x05],0xA4	   ;A��ʾ��ɫ������˸��4��ʾǰ��ɫΪ��ɫ

   mov byte [gs:0x06],'B'
   mov byte [gs:0x07],0xA4

   mov byte [gs:0x08],'R'
   mov byte [gs:0x09],0xA4
	 
   mov eax,LOADER_START_SECTOR	 ; ��ʼ����lba��ַ
   mov bx,LOADER_BASE_ADDR       ; д��ĵ�ַ
   mov cx,4			 ; �������������
   call rd_disk_m_16		 ; ���¶�ȡ�������ʼ���֣�һ��������
  
   jmp LOADER_BASE_ADDR + 0x300
       
;-------------------------------------------------------------------------------
;����:��ȡӲ��n������
rd_disk_m_16:	   
;-------------------------------------------------------------------------------
				       ; eax=LBA������
				       ; ebx=������д����ڴ��ַ
				       ; ecx=�����������
      mov esi,eax	  ;����eax
      mov di,cx		  ;����cx
;��дӲ��:
;��1��������Ҫ��ȡ��������
      mov dx,0x1f2
      mov al,cl
      out dx,al            ;��ȡ��������

      mov eax,esi	   ;�ָ�ax

;��2������LBA��ַ����0x1f3 ~ 0x1f6

      ;LBA��ַ7~0λд��˿�0x1f3
      mov dx,0x1f3                       
      out dx,al                          

      ;LBA��ַ15~8λд��˿�0x1f4
      mov cl,8
      shr eax,cl
      mov dx,0x1f4
      out dx,al

      ;LBA��ַ23~16λд��˿�0x1f5
      shr eax,cl
      mov dx,0x1f5
      out dx,al

      shr eax,cl
      and al,0x0f	   ;lba��24~27λ
      or al,0xe0	   ; ����7��4λΪ1110,��ʾlbaģʽ
      mov dx,0x1f6
      out dx,al

;��3������0x1f7�˿�д������0x20 
      mov dx,0x1f7
      mov al,0x20                        
      out dx,al

;��4�������Ӳ��״̬
  .not_ready:
      ;ͬһ�˿ڣ�дʱ��ʾд�������֣���ʱ��ʾ����Ӳ��״̬
      nop
      in al,dx
      and al,0x88	   ;��4λΪ1��ʾӲ�̿�������׼�������ݴ��䣬��7λΪ1��ʾӲ��æ
      cmp al,0x08
      jnz .not_ready	   ;��δ׼���ã������ȡ�

;��5������0x1f0�˿ڶ�����
      mov ax, di
      mov dx, 256
      mul dx
      mov cx, ax	   ; diΪҪ��ȡ����������һ��������512�ֽڣ�ÿ�ζ���һ���֣�
			   ; ����di*512/2�Σ�����di*256
      mov dx, 0x1f0
  .go_on_read:
      in ax,dx
      mov [bx],ax
      add bx,2		  
      loop .go_on_read
      ret

   times 510-($-$$) db 0
   db 0x55,0xaa
