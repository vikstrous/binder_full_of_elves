BITS 64
; org     0x00200000

; exit()
  mov eax, 1
  mov ebx, 43
  int 0x80
