; BluMach PC5286 BIOS memory report, 8086/80286 compatible.
; NASM -f bin pc5286-memory-report-floppy.asm -o pc5286-memory-report.img
; Read-only guest diagnostic: no DOS, no disk writes and no protected mode.
bits 16
cpu 8086

section boot start=0 vstart=0x7c00
    jmp 0:start
start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7c00
    mov ds, ax
    sti
    mov ax, 0x0800
    mov es, ax
    xor bx, bx
    mov ax, 0x0210
    mov cx, 0x0002
    xor dh, dh
    int 0x13
    jc disk_error
    jmp 0x0800:0

disk_error:
    mov si, boot_error
.print:
    lodsb
    or al, al
    jz halt
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp .print

halt:
    sti
    hlt
    jmp halt

boot_error db 'Memory report: floppy read error.',0
times 510-($-$$) db 0
dw 0xaa55

section payload start=512 vstart=0
payload_start:
    cld
    push cs
    pop ds
    mov ax, 3
    int 0x10

    mov si, title
    call puts

    int 0x12
    push ax
    mov si, conventional
    call puts
    pop ax
    call hex16
    call newline

    mov ah, 0x88
    int 0x15
    jc extended_error
    push ax
    mov si, extended
    call puts
    pop ax
    call hex16
    call newline
    jmp report_done

extended_error:
    mov si, unavailable
    call puts
    xor ax, ax
    mov al, ah
    call hex16
    call newline

report_done:
    mov si, explanation
    call puts
payload_halt:
    sti
    hlt
    jmp payload_halt

puts:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp puts
.done:
    ret

newline:
    push ax
    mov al, 13
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    mov al, 10
    mov ah, 0x0e
    int 0x10
    pop ax
    ret

hex16:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax
    mov cx, 4
.digit:
    rol dx, 1
    rol dx, 1
    rol dx, 1
    rol dx, 1
    mov al, dl
    and al, 0x0f
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    loop .digit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

title db 'BluMach PC5286 BIOS memory report v3',13,10,13,10,0
conventional db 'INT 12h conventional KiB: 0x',0
extended db 'INT 15h/88h extended KiB above 1 MiB: 0x',0
unavailable db 'INT 15h/88h unavailable, BIOS status: 0x',0
explanation db 13,10,'Read-only BIOS report. No protected mode or high-memory writes.',13,10
            db 'Reset or close BluMach when recorded.',13,10,0

times 8192-($-$$) db 0
section padding start=8704
times 1474560-8704 db 0
