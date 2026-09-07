; BluMach PC5286 BIOS mode 64h planar graphics demonstration.
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
    mov ax, 0x0204
    mov cx, 0x0002
    xor dh, dh
    int 0x13
    jc halt
    jmp 0x0800:0
halt:
    sti
    hlt
    jmp halt
times 510-($-$$) db 0
dw 0xaa55

section payload start=512 vstart=0
payload_start:
    cld
    push cs
    pop ds
    mov ax, 0x0064
    int 0x10
    mov ax, 0xa000
    mov es, ax
    xor bp, bp
.plane:
    mov dx, 0x3c4
    mov ax, 0x0102
    mov cx, bp
    shl ah, cl
    out dx, ax
    xor di, di
    mov bx, 600
.row:
    xor si, si
.band:
    mov ax, si
    mov cx, bp
    shr ax, cl
    and al, 1
    neg al
    mov cx, 5
    rep stosb
    inc si
    cmp si, 20
    jb .band
    dec bx
    jnz .row
    inc bp
    cmp bp, 4
    jb .plane
.stop:
    sti
    hlt
    jmp .stop

times 2048-($-$$) db 0
section padding start=2560
times 1474560-2560 db 0
