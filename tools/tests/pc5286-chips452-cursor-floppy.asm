; BluMach PC5286 C&T 82C452 guest-visible graphics cursor demonstration.
; NASM -f bin pc5286-chips452-cursor-floppy.asm -o pc5286-chips452-cursor.img
; Programs only documented VGA mode 13h and XR30-XR3A cursor registers.
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
    mov ax, 0x0208
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
    mov ax, 0x0013
    int 0x10

    ; Fill 320x200 with sixteen vertical VGA palette bands.
    mov ax, 0xa000
    mov es, ax
    xor di, di
    mov dx, 200
.row:
    xor ax, ax
    mov bl, 16
.band:
    mov cx, 20
    rep stosb
    inc al
    dec bl
    jnz .band
    dec dx
    jnz .row

    ; Temporarily select the extended packed map at physical 30000h.  F000h
    ; in the A0000h aperture then names physical 3F000h, beyond mode 13h's
    ; visible 64000-byte framebuffer.
    mov dx, 0x3d6
    mov al, 0x10
    out dx, al
    inc dx
    mov al, 0x0c
    out dx, al
    dec dx
    mov al, 0x0b
    out dx, al
    inc dx
    mov al, 0x05
    out dx, al
    dec dx

    ; Place a 32x32 cursor pattern at physical VRAM 3F000h. The first and
    ; last rows are cursor colour 1; the interior has a colour-1 outline.
    ; The middle two rows use pattern 1 to invert the complete background.
    mov di, 0xf000
    mov dx, 32
.cursor_row:
    cmp dx, 32
    je .solid
    cmp dx, 1
    je .solid
    cmp dx, 17
    je .invert
    cmp dx, 16
    je .invert

    ; ABCD, four skipped bytes, EFGH, four skipped bytes.
    mov byte [es:di+0], 0x80
    mov byte [es:di+1], 0x00
    mov byte [es:di+2], 0x80
    mov byte [es:di+3], 0x00
    mov word [es:di+4], 0
    mov word [es:di+6], 0
    mov byte [es:di+8], 0x00
    mov byte [es:di+9], 0x01
    mov byte [es:di+10], 0x00
    mov byte [es:di+11], 0x01
    mov word [es:di+12], 0
    mov word [es:di+14], 0
    jmp .next_cursor_row
.solid:
    mov byte [es:di+0], 0xff
    mov byte [es:di+1], 0xff
    mov byte [es:di+2], 0xff
    mov byte [es:di+3], 0xff
    mov word [es:di+4], 0
    mov word [es:di+6], 0
    mov byte [es:di+8], 0xff
    mov byte [es:di+9], 0xff
    mov byte [es:di+10], 0xff
    mov byte [es:di+11], 0xff
    mov word [es:di+12], 0
    mov word [es:di+14], 0
    jmp .next_cursor_row
.invert:
    mov byte [es:di+0], 0xff
    mov byte [es:di+1], 0xff
    mov byte [es:di+2], 0x00
    mov byte [es:di+3], 0x00
    mov word [es:di+4], 0
    mov word [es:di+6], 0
    mov byte [es:di+8], 0xff
    mov byte [es:di+9], 0xff
    mov byte [es:di+10], 0x00
    mov byte [es:di+11], 0x00
    mov word [es:di+12], 0
    mov word [es:di+14], 0
.next_cursor_row:
    add di, 16
    dec dx
    jnz .cursor_row

    ; Restore the normal VGA-compatible CPU address translation.
    mov dx, 0x3d6
    mov al, 0x0b
    out dx, al
    inc dx
    xor al, al
    out dx, al
    dec dx
    mov al, 0x10
    out dx, al
    inc dx
    xor al, al
    out dx, al

    ; Program the documented 82C452 cursor fields through 3D6h/3D7h.
    mov dx, 0x3d6
    mov si, cursor_registers
    mov cx, cursor_registers_end-cursor_registers
.write_xr:
    lodsw
    out dx, al
    inc dx
    mov al, ah
    out dx, al
    dec dx
    sub cx, 2
    jnz .write_xr

.display:
    sti
    hlt
    jmp .display

; index,value pairs: start 3F00h in plane-address units, 32 rows, position
; (144,84), enabled, full mask, red/white replacement colours.
cursor_registers:
    db 0x30,0x3f, 0x31,0x00, 0x32,0x0f
    db 0x33,0x00, 0x34,0x90, 0x35,0x00, 0x36,0x54
    db 0x38,0xff, 0x39,0x04, 0x3a,0x0f
    db 0x37,0x01
cursor_registers_end:

times 4096-($-$$) db 0
section padding start=4608
times 1474560-4608 db 0
