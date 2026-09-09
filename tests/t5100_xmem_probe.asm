; Copyright 2026 rtzor, Project BluMach.
; GPL-2.0-or-later. Project-authored bounded BIOS INT 15h/87h memory probe.
; Saves/restores 16 words at 64 KiB boundaries from 100000h to 1F0000h.
; Distinct markers detect aliasing between sampled locations, not every cell.
bits 16
org 100h
    push cs
    pop ds
    mov ah,88h
    int 15h
    jnc .size_ok
    xor ax,ax
.size_ok:
    mov [reported],ax
    cmp ax,1024
    jb report
    xor eax,eax
    mov ax,cs
    shl eax,4
    mov [localbase],eax
    xor ebx,ebx
.save:
    call addresses
    add edx,saved
    call move_word
    jc abort_save
    inc bx
    cmp bx,16
    jb .save
    xor ebx,ebx
.write:
    mov ax,bx
    xor ax,0a55ah
    mov [marker],ax
    call addresses
    mov edx,[localbase]
    add edx,marker
    xchg eax,edx
    call move_word
    jnc .write_next
    inc word [errors]
.write_next:
    inc bx
    cmp bx,16
    jb .write
    xor ebx,ebx
.read:
    call addresses
    mov edx,[localbase]
    add edx,result
    call move_word
    jc .fail
    mov ax,bx
    xor ax,0a55ah
    cmp ax,[result]
    jne .fail
    inc word [passed]
    jmp .next
.fail:
    inc word [errors]
.next:
    inc bx
    cmp bx,16
    jb .read
    xor ebx,ebx
.restore:
    call addresses
    add edx,saved
    xchg eax,edx
    call move_word
    jnc .restore_next
    inc word [restore_errors]
.restore_next:
    inc bx
    cmp bx,16
    jb .restore
    jmp report
abort_save:
    inc word [errors]
report:
    mov dx,msg
    call puts
    mov ax,[reported]
    call hex
    mov dx,passmsg
    call puts
    mov ax,[passed]
    call hex
    mov dx,errormsg
    call puts
    mov ax,[errors]
    call hex
    mov dx,restoremsg
    call puts
    mov ax,[restore_errors]
    call hex
    mov dx,endmsg
    call puts
    mov ax,4c00h
    int 21h
addresses:
    mov eax,ebx
    shl eax,16
    add eax,100000h
    mov edx,ebx
    shl edx,1
    add edx,[localbase]
    ret
move_word:
    pushad
    push es
    mov [gdt+18],ax
    shr eax,16
    mov [gdt+20],al
    mov [gdt+26],dx
    shr edx,16
    mov [gdt+28],dl
    push cs
    pop es
    mov si,gdt
    mov cx,1
    mov ah,87h
    int 15h
    pop es
    popad
    ret
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
align 8
gdt:
    times 16 db 0
    dw 0ffffh,0
    db 0,93h,0,0
    dw 0ffffh,0
    db 0,93h,0,0
    times 16 db 0
localbase dd 0
reported dw 0
passed dw 0
errors dw 0
restore_errors dw 0
marker dw 0
result dw 0
saved times 16 dw 0
msg db 13,10,'BIOS XMEM probe (hex): reported KiB = $'
passmsg db 13,10,'16 boundary words passing = $'
errormsg db 13,10,'Transfer/compare errors = $'
restoremsg db 13,10,'Restore errors = $'
endmsg db 13,10,'Sampled test only; skipped if reported RAM < 1024 KiB.',13,10,'$'
