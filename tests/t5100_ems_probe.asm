; Copyright 2026 rtzor, Project BluMach.
; Project-authored bounded T5100 register probe, GPL-2.0-or-later.
; Tests unique first/last words of bank-0 pages 0..87 through 0208/D000.
; Saves/restores every touched word and the page register; no disk writes.
bits 16
org 100h
start:
    push cs
    pop ds
    mov dx,0208h
    in al,dx
    mov [oldreg],al
    mov ax,0d000h
    mov es,ax
    pushf
    cli
    xor bx,bx
    mov si,saved
.save:
    mov al,bl
    or al,80h
    out dx,al
    mov ax,[es:0]
    mov [si],ax
    mov ax,[es:3ffeh]
    mov [si+2],ax
    add si,4
    inc bx
    cmp bx,88
    jb .save
    xor bx,bx
.write:
    mov al,bl
    or al,80h
    out dx,al
    mov ax,bx
    xor ax,0a55ah
    mov [es:0],ax
    not ax
    mov [es:3ffeh],ax
    inc bx
    cmp bx,88
    jb .write
    xor bx,bx
.check:
    mov al,bl
    or al,80h
    out dx,al
    mov ax,bx
    xor ax,0a55ah
    cmp ax,[es:0]
    jne .fail
    not ax
    cmp ax,[es:3ffeh]
    jne .fail
    cmp bx,24
    jae .upper
    inc word [basepass]
    jmp .next
.upper:
    inc word [upperpass]
    jmp .next
.fail:
    cmp word [firstfail],0ffffh
    jne .next
    mov [firstfail],bx
.next:
    inc bx
    cmp bx,88
    jb .check
    xor bx,bx
    mov si,saved
.restore:
    mov al,bl
    or al,80h
    out dx,al
    mov ax,[si]
    mov [es:0],ax
    mov ax,[si+2]
    mov [es:3ffeh],ax
    add si,4
    inc bx
    cmp bx,88
    jb .restore
    mov al,[oldreg]
    out dx,al
    popf
    mov dx,title
    call puts
    mov ax,[basepass]
    call hex
    mov dx,uppermsg
    call puts
    mov ax,[upperpass]
    call hex
    mov dx,failmsg
    call puts
    mov ax,[firstfail]
    call hex
    mov dx,done
    call puts
    mov ax,4c00h
    int 21h
puts:
    mov ah,9
    int 21h
    ret
hex:
    mov bx,ax
    mov cx,4
.digit:
    push cx
    mov cl,4
    rol bx,cl
    mov dl,bl
    and dl,0fh
    add dl,'0'
    cmp dl,'9'
    jbe .print
    add dl,7
.print:
    mov ah,2
    int 21h
    pop cx
    loop .digit
    ret
title db 13,10,'T5100 EMS register probe; counts are hexadecimal.',13,10,'BASE 24 passing = $'
uppermsg db 13,10,'UPPER 64 passing = $'
failmsg db 13,10,'FIRST FAIL page (FFFF=none) = $'
done db 13,10,'Touched words and page register restored.',13,10,'$'
oldreg db 0
basepass dw 0
upperpass dw 0
firstfail dw 0ffffh
saved times 88*4 db 0
