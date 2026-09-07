bits 16
org 0x100
cpu 8086

%macro check_ah 0
    test ah, ah
    jz %%ok
    jmp fail
%%ok:
%endmacro

start:
    push cs
    pop ds

    mov dx, msg_title
    call puts

    mov byte [stage], 0x40
    mov ah, 0x40
    int 0x67
    check_ah

    mov byte [stage], 0x46
    mov ah, 0x46
    int 0x67
    check_ah
    mov [version], al
    cmp al, 0x40
    jae .version_ok
    jmp fail_version
.version_ok:

    mov byte [stage], 0x41
    mov ah, 0x41
    int 0x67
    check_ah
    mov [frame], bx

    mov byte [stage], 0x42
    mov ah, 0x42
    int 0x67
    check_ah
    mov [free_pages], bx
    mov [total_pages], dx
    cmp bx, 2
    jae .pages_ok
    jmp fail_short
.pages_ok:

    mov byte [stage], 0x43
    mov bx, 2
    mov ah, 0x43
    int 0x67
    check_ah
    mov [handle], dx
    mov byte [allocated], 1

    ; Physical page 0 -> logical page 0.
    mov byte [stage], 0x44
    mov dx, [handle]
    xor bx, bx
    mov ax, 0x4400
    int 0x67
    check_ah

    ; Physical page 1 -> logical page 1.
    mov dx, [handle]
    mov bx, 1
    mov ax, 0x4401
    int 0x67
    check_ah

    mov ax, [frame]
    mov es, ax
    mov word [es:0x0000], 0xa55a
    mov word [es:0x4000], 0x5aa5
    cmp word [es:0x0000], 0xa55a
    je .first_page_ok
    jmp fail_alias
.first_page_ok:
    cmp word [es:0x4000], 0x5aa5
    je .second_page_ok
    jmp fail_alias
.second_page_ok:

    mov byte [stage], 0x47
    mov dx, [handle]
    mov ah, 0x47
    int 0x67
    check_ah

    ; Swap the two logical pages.
    mov byte [stage], 0x44
    mov dx, [handle]
    mov bx, 1
    mov ax, 0x4400
    int 0x67
    check_ah
    mov dx, [handle]
    xor bx, bx
    mov ax, 0x4401
    int 0x67
    check_ah
    cmp word [es:0x0000], 0x5aa5
    je .swap_first_ok
    jmp fail_alias
.swap_first_ok:
    cmp word [es:0x4000], 0xa55a
    je .swap_second_ok
    jmp fail_alias
.swap_second_ok:

    mov byte [stage], 0x48
    mov dx, [handle]
    mov ah, 0x48
    int 0x67
    check_ah
    cmp word [es:0x0000], 0xa55a
    je .restore_first_ok
    jmp fail_alias
.restore_first_ok:
    cmp word [es:0x4000], 0x5aa5
    je .restore_second_ok
    jmp fail_alias
.restore_second_ok:

    mov byte [stage], 0x4c
    mov dx, [handle]
    mov ah, 0x4c
    int 0x67
    check_ah
    cmp bx, 2
    je .handle_pages_ok
    jmp fail_pages
.handle_pages_ok:

    call release
    mov dx, msg_pass
    call puts
    mov dx, msg_version
    call puts
    mov al, [version]
    call hex8
    mov dx, msg_frame
    call puts
    mov ax, [frame]
    call hex16
    mov dx, msg_total
    call puts
    mov ax, [total_pages]
    call hex16
    mov dx, msg_free
    call puts
    mov ax, [free_pages]
    call hex16
    mov dx, msg_done
    call puts
    jmp halt

fail_version:
    mov byte [stage], 0x46
    mov ah, [version]
    jmp fail

fail_short:
    mov byte [stage], 0x42
    mov ah, 0xfe
    jmp fail

fail_alias:
    mov byte [stage], 0xa1
    mov ah, 0xfd
    jmp fail

fail_pages:
    mov byte [stage], 0x4c
    mov ah, 0xfc

fail:
    mov [error_code], ah
    call release
    mov dx, msg_fail
    call puts
    mov al, [stage]
    call hex8
    mov dx, msg_error
    call puts
    mov al, [error_code]
    call hex8
    mov dx, msg_done
    call puts

halt:
    cli
.stop:
    hlt
    jmp .stop

release:
    cmp byte [allocated], 0
    je .done
    mov dx, [handle]
    mov ah, 0x45
    int 0x67
    mov byte [allocated], 0
.done:
    ret

puts:
    mov ah, 0x09
    int 0x21
    ret

hex16:
    push ax
    mov al, ah
    call hex8
    pop ax
    call hex8
    ret

hex8:
    push ax
    mov cl, 4
    shr al, cl
    call hex_nibble
    pop ax
    and al, 0x0f
hex_nibble:
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 'A' - '9' - 1
.emit:
    mov dl, al
    mov ah, 0x02
    int 0x21
    ret

msg_title   db 'BluMach PC5286 SCATEMM LIM API probe v1',13,10,'$'
msg_pass    db 'PASS: LIM 4.0 allocation, mapping, save/restore and release.',13,10,'$'
msg_fail    db 'FAIL: stage 0x','$'
msg_error   db ' status 0x','$'
msg_version db 'Version: 0x','$'
msg_frame   db '  frame: 0x','$'
msg_total   db '  total pages: 0x','$'
msg_free    db '  free before allocation: 0x','$'
msg_done    db 13,10,'Reset or close BluMach.',13,10,'$'

stage       db 0
error_code  db 0
version     db 0
allocated   db 0
handle      dw 0
frame       dw 0
free_pages  dw 0
total_pages dw 0
