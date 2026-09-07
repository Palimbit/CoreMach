; BluMach PC5286 SCAT shadow-state probe, 8086/80286 compatible.
; NASM -f bin pc5286-shadow-floppy.asm -o pc5286-shadow.img
; Reads SCAT registers and tests/restores one byte per 16KiB C0000h-FFFFFh block.
; No DOS and no disk writes.
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
    jz boot_halt
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp .print
boot_halt:
    sti
    hlt
    jmp boot_halt

boot_error db 'Shadow probe: floppy read error.',0
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

    mov si, regs_label
    call puts
    xor bx, bx
    mov bl, 0x40
.read_reg:
    xor ax, ax
    mov al, bl
    call hex8
    mov al, '='
    call putc
    mov al, bl
    out 0x22, al
    in al, 0x23
    mov [reg_values + bx - 0x40], al
    call hex8
    mov al, ' '
    call putc
    inc bl
    test bl, 3
    jnz .same_line
    call newline
.same_line:
    cmp bl, 0x50
    jne .read_reg

    mov si, map_label
    call puts
    mov ax, 0xc000
    mov cx, 16
.next_block:
    push ax
    call hex16
    mov al, ':'
    call putc
    pop ax
    push ax
    call test_block
    or al, al
    jz .read_only
    mov si, writable
    jmp .state
.read_only:
    mov si, read_only
.state:
    call puts
    pop ax
    add ax, 0x0400
    loop .next_block

    mov si, done_text
    call puts
halt:
    sti
    hlt
    jmp halt

; AX is the segment. Return AL=1 if a changed byte reads back, otherwise zero.
; Interrupts remain disabled only between the test write and its restoration.
test_block:
    push bx
    push dx
    push di
    push es
    mov es, ax
    mov di, 0x3ffe
    cli
    mov al, [es:di]
    mov dl, al
    xor al, 0xa5
    mov dh, al
    mov [es:di], al
    mov al, [es:di]
    mov [es:di], dl
    sti
    cmp al, dh
    jne .no
    mov al, 1
    jmp .done
.no:
    xor al, al
.done:
    pop es
    pop di
    pop dx
    pop bx
    ret

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

title db 'BluMach PC5286 SCAT shadow probe v1',13,10
      db 'Read registers; test and restore one byte per 16KiB block.',13,10,13,10,0
regs_label db 'SCAT configuration registers:',13,10,0
map_label db 13,10,'Post-BIOS C0000h-FFFFFh write state:',13,10,0
writable db 'RW ',0
read_only db 'RO ',0
done_text db 13,10,13,10,'All samples restored. Reset or close BluMach.',13,10,0
reg_values times 16 db 0

times 8192-($-$$) db 0
section padding start=8704
times 1474560-8704 db 0
