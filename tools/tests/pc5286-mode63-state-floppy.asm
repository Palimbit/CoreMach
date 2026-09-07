; BluMach PC5286 C&T 82C452 mode 63h extension-register snapshot.
; NASM -f bin pc5286-chips452-state-floppy.asm -o pc5286-chips452-state.img
; Sets BIOS mode 63h, then reads XR00-XR3F. No disk or data-register writes other
; than selecting the extension index; the original index is restored.
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
    jc disk_error
    jmp 0x0800:0

disk_error:
    mov si, boot_error
.print:
    lodsb
    or al, al
    jz boot_halt
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp .print
boot_halt:
    sti
    hlt
    jmp boot_halt

boot_error db 'C&T state probe: floppy read error.',0
times 510-($-$$) db 0
dw 0xaa55

section payload start=512 vstart=0
payload_start:
    cld
    push cs
    pop ds

    mov ax, 0x0063
    int 0x10
    push cs
    pop ds

    ; Prefer the colour extension pair. If it is disabled, the 82C452 returns
    ; FFh and the monochrome pair is tested instead.
    mov dx, 0x3d6
    in al, dx
    cmp al, 0xff
    jne .port_found
    mov dx, 0x3b6
    in al, dx
    cmp al, 0xff
    jne .port_found
    mov byte [port_ok], 0
    jmp .show
.port_found:
    mov byte [port_ok], 1
    mov [saved_index], al
    mov [index_port], dx
    xor bx, bx
.read_xr:
    mov al, bl
    out dx, al
    inc dx
    in al, dx
    dec dx
    mov [xr_values + bx], al
    inc bl
    cmp bl, 0x40
    jne .read_xr
    mov al, [saved_index]
    out dx, al

    ; Focused standard-VGA/BDA state needed to explain a blank accepted mode.
    mov dx, 0x3cc
    in al, dx
    mov [misc_value], al
    mov dx, 0x3c4
    mov al, 1
    out dx, al
    inc dx
    in al, dx
    mov [seq1_value], al
    mov dx, 0x3ce
    mov al, 6
    out dx, al
    inc dx
    in al, dx
    mov [gfx6_value], al
    mov dx, 0x3d4
    mov al, 1
    out dx, al
    inc dx
    in al, dx
    mov [crtc1_value], al
    dec dx
    mov al, 9
    out dx, al
    inc dx
    in al, dx
    mov [crtc9_value], al
    dec dx
    mov al, 0x12
    out dx, al
    inc dx
    in al, dx
    mov [crtc12_value], al
    dec dx
    mov al, 0x17
    out dx, al
    inc dx
    in al, dx
    mov [crtc17_value], al
    dec dx
    mov al, 0x24
    out dx, al
    inc dx
    in al, dx
    mov [crtc24_value], al
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x49]
    mov [bda_mode], al
    mov ax, [es:0x4a]
    mov [bda_cols], ax
    mov al, [es:0x84]
    inc al
    mov [bda_rows], al

.show:
    mov ax, 3
    int 0x10
    mov si, title
    call puts
    cmp byte [port_ok], 0
    jne .show_port
    mov si, unavailable
    call puts
    jmp halt
.show_port:
    mov si, port_label
    call puts
    mov ax, [index_port]
    call hex16
    call newline
    mov si, bda_label
    call puts
    mov al, [bda_mode]
    call hex8
    mov al, ' '
    call putc
    mov ax, [bda_cols]
    call hex16
    mov al, ' '
    call putc
    mov al, [bda_rows]
    call hex8
    call newline
    mov si, vga_label
    call puts
    mov al, [misc_value]
    call hex8
    mov al, ' '
    call putc
    mov al, [seq1_value]
    call hex8
    mov al, ' '
    call putc
    mov al, [gfx6_value]
    call hex8
    mov al, ' '
    call putc
    mov al, [crtc1_value]
    call hex8
    mov al, ' '
    call putc
    mov al, [crtc9_value]
    call hex8
    mov al, ' '
    call putc
    mov al, [crtc12_value]
    call hex8
    mov al, ' '
    call putc
    mov al, [crtc17_value]
    call hex8
    mov al, ' '
    call putc
    mov al, [crtc24_value]
    call hex8
    call newline
    mov si, legend
    call puts

    xor bx, bx
.show_xr:
    mov al, bl
    call hex8
    mov al, '='
    call putc
    mov al, [xr_values + bx]
    call hex8
    mov al, ' '
    call putc
    inc bl
    test bl, 7
    jnz .same_line
    call newline
.same_line:
    cmp bl, 0x40
    jne .show_xr
    mov si, done_text
    call puts

halt:
    sti
    hlt
    jmp halt

puts:
    lodsb
    or al, al
    jz .done
    call putc
    jmp puts
.done:
    ret

putc:
    push ax
    push bx
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    pop bx
    pop ax
    ret

newline:
    push ax
    mov al, 13
    call putc
    mov al, 10
    call putc
    pop ax
    ret

hex8:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call hex_nibble
    mov al, ah
    and al, 0x0f
    call hex_nibble
    pop cx
    pop ax
    ret

hex16:
    push ax
    xchg al, ah
    call hex8
    xchg al, ah
    call hex8
    pop ax
    ret

hex_nibble:
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    call putc
    ret

title db 'BluMach PC5286 C&T 82C452 BIOS mode 63h state v1',13,10
      db 'XR00-XR3F captured after INT 10h mode 63h.',13,10,0
port_label db 'Extension index port: ',0
bda_label db 'BDA mode cols rows: ',0
vga_label db 'MISC SR01 GR06 CR01 CR09 CR12 CR17 CR24: ',0
legend db 13,10,'       XR index=value (hex)',13,10,0
unavailable db 'FAIL: neither 3D6h nor 3B6h exposes extension registers.',13,10,0
done_text db 13,10,'Snapshot complete. Reset or close BluMach.',13,10,0
port_ok db 0
saved_index db 0
index_port dw 0
misc_value db 0
seq1_value db 0
gfx6_value db 0
crtc1_value db 0
crtc9_value db 0
crtc12_value db 0
crtc17_value db 0
crtc24_value db 0
bda_mode db 0
bda_cols dw 0
bda_rows db 0
xr_values times 64 db 0

times 4096-($-$$) db 0
section padding start=4608
times 1474560-4608 db 0
