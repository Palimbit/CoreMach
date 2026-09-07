; BluMach PC5286 VGA BIOS extended-mode matrix, 8086/80286 compatible.
; Tests BIOS INT10h/AH=00h mode selection and reports BDA/XR0E state.
; No disk writes and no direct video-register writes except XR index selection.
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
    jc boot_halt
    jmp 0x0800:0
boot_halt:
    sti
    hlt
    jmp boot_halt

times 510-($-$$) db 0
dw 0xaa55

section payload start=512 vstart=0
payload_start:
    cld
    push cs
    pop ds
    mov ax, 0x0040
    mov es, ax

    ; Capture the C&T controller-information extension result.
    mov ax, 0x5f00
    int 0x10
    push cs
    pop ds
    mov [info_ax], ax
    mov [info_bx], bx
    mov [info_cx], cx
    mov [info_dx], dx

    mov si, modes
    mov di, results
    mov cx, mode_count
.next_mode:
    push cx
    lodsb
    mov [requested_mode], al
    mov [di], al
    push si
    push di
    mov ax, 0x0003
    int 0x10
    pop di
    pop si
    push cs
    pop ds
    mov al, [requested_mode]
    xor ah, ah
    push si
    push di
    int 0x10
    pop di
    pop si
    push cs
    pop ds
    mov ax, 0x0040
    mov es, ax

    ; Current mode, columns and rows are the BIOS's own BDA report.
    mov al, [es:0x49]
    mov [di+1], al
    mov ax, [es:0x4a]
    mov [di+2], ax
    mov al, [es:0x84]
    inc al
    mov [di+4], al

    ; Observe the extended-text selector chosen by the mode routine.
    mov dx, 0x3d6
    in al, dx
    mov [saved_index], al
    mov al, 0x0e
    out dx, al
    inc dx
    in al, dx
    mov [di+5], al
    dec dx
    mov al, [saved_index]
    out dx, al

    add di, result_size
    pop cx
    loop .next_mode

    mov ax, 0x0003
    int 0x10
    push cs
    pop ds
    mov si, title
    call puts
    mov si, info_label
    call puts
    mov ax, [info_ax]
    call hex16
    mov al, ' '
    call putc
    mov ax, [info_bx]
    call hex16
    mov al, ' '
    call putc
    mov ax, [info_cx]
    call hex16
    mov al, ' '
    call putc
    mov ax, [info_dx]
    call hex16
    call newline
    mov si, heading
    call puts

    mov di, results
    mov cx, mode_count
.show:
    push cx
    mov al, [di]
    call hex8
    mov al, ' '
    call putc
    mov al, [di+1]
    call hex8
    mov al, ' '
    call putc
    mov ax, [di+2]
    call hex16
    mov al, ' '
    call putc
    mov al, [di+4]
    call hex8
    mov al, ' '
    call putc
    mov al, [di+5]
    call hex8
    cmp byte [di], 0x65
    jbe .text_note
    mov si, graphics_note
    jmp .note
.text_note:
    mov si, text_note
.note:
    call puts
    add di, result_size
    pop cx
    loop .show
    mov si, done
    call puts
.halt:
    sti
    hlt
    jmp .halt

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
    call nibble
    mov al, ah
    and al, 0x0f
    call nibble
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
nibble:
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    call putc
    ret

title db 'BluMach PC5286 VGA BIOS mode matrix v1',13,10,0
info_label db 'INT10 5F00 AX BX CX DX: ',0
heading db 13,10,'REQ CUR COLS ROWS XR0E RESULT',13,10,0
text_note db ' text candidate',13,10,0
graphics_note db ' VESA452 graphics candidate',13,10,0
done db 13,10,'CUR=REQ means BIOS accepted the mode. Reset or close.',13,10,0
modes db 0x60,0x62,0x63,0x64,0x65,0x6e,0x6f,0x70,0x71,0x72,0x78,0x79,0x7a
mode_count equ $-modes
result_size equ 6
results times mode_count*result_size db 0
info_ax dw 0
info_bx dw 0
info_cx dw 0
info_dx dw 0
saved_index db 0
requested_mode db 0

times 4096-($-$$) db 0
section padding start=4608
times 1474560-4608 db 0
