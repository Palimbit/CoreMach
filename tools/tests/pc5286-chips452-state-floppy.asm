; BluMach PC5286 C&T 82C452 post-BIOS extension-register snapshot.
; NASM -f bin pc5286-chips452-state-floppy.asm -o pc5286-chips452-state.img
; Reads XR00-XR3F before setting a video mode. No disk or register writes other
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

title db 'BluMach PC5286 C&T 82C452 post-BIOS state v1',13,10
      db 'XR00-XR3F captured before INT 10h mode set.',13,10,0
port_label db 'Extension index port: ',0
legend db 13,10,'       XR index=value (hex)',13,10,0
unavailable db 'FAIL: neither 3D6h nor 3B6h exposes extension registers.',13,10,0
done_text db 13,10,'Snapshot complete. Reset or close BluMach.',13,10,0
port_ok db 0
saved_index db 0
index_port dw 0
xr_values times 64 db 0

times 4096-($-$$) db 0
section padding start=4608
times 1474560-4608 db 0
