; BluMach PC5286 physical memory probe through AT BIOS INT 15h/AH=87h.
; NASM -f bin pc5286-memory-move-floppy.asm -o pc5286-memory-move.img
; Standalone read/write/verify/restore probe; no DOS and no disk writes.
bits 16
cpu 8086
%define PAYLOAD_PHYS 0x8000

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

boot_error db 'Memory move probe: floppy read error.',0
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
    call test_service_control
    mov si, control_status_text
    call puts
    xor ax, ax
    mov al, [control_status]
    call hex16
    mov si, control_value_text
    call puts
    mov ax, [control_destination]
    call hex16
    call newline
    cmp byte [control_ok], 1
    je .control_passed
    mov si, control_failed_text
    call puts
    jmp payload_halt
.control_passed:
    call probe_memory

    cmp byte [service_error], 0
    je show_result
    mov si, service_error_text
    call puts
    xor ax, ax
    mov al, [error_status]
    call hex16
    call newline

show_result:
    mov si, blocks_text
    call puts
    mov ax, [blocks]
    call hex16
    call newline
    mov si, kib_text
    call puts
    mov ax, [blocks]
    mov cl, 6
    shl ax, cl
    call hex16
    call newline
    mov si, done_text
    call puts

payload_halt:
    sti
    hlt
    jmp payload_halt

; First prove that this BIOS can complete AH=87h independently of extended
; memory by copying one word between two conventional-memory variables.
test_service_control:
    mov word [control_destination], 0
    mov byte [control_ok], 0
    mov bx, control_source
    mov dx, control_destination
    call move_low_to_low
    mov al, [move_status]
    mov [control_status], al
    jc .done
    cmp word [control_destination], 0x1234
    jne .done
    mov byte [control_ok], 1
.done:
    ret

; Probe 1MiB first as a fixed anchor.  For later 64KiB windows, verify that
; writing the candidate does not alter the anchor, then restore both samples.
probe_memory:
    mov word [blocks], 0
    mov byte [service_error], 0
    mov byte [current_bank], 0x10
    mov bx, anchor_original
    call move_high_to_low
    jc .service_fail
    mov bx, anchor_pattern
    call move_low_to_high
    jc .service_fail
    mov bx, anchor_verify
    call move_high_to_low
    jc .restore_anchor
    cmp word [anchor_verify], 0xa55a
    jne .restore_anchor
    mov word [blocks], 1
    mov byte [current_bank], 0x11

.next_bank:
    cmp byte [current_bank], 0x40
    je .restore_anchor
    mov bx, candidate_original
    call move_high_to_low
    jc .service_fail_restore_anchor
    mov bx, candidate_pattern
    call move_low_to_high
    jc .service_fail_restore_candidate
    mov bx, candidate_verify
    call move_high_to_low
    jc .service_fail_restore_candidate
    mov bx, candidate_original
    call move_low_to_high
    jc .service_fail_restore_anchor
    cmp word [candidate_verify], 0x5aa5
    jne .restore_anchor
    mov byte [current_bank], 0x10
    mov bx, anchor_verify
    call move_high_to_low
    jc .service_fail_restore_anchor
    cmp word [anchor_verify], 0xa55a
    jne .restore_anchor
    inc word [blocks]
    mov al, byte [blocks]
    add al, 0x10
    mov byte [current_bank], al
    jmp .next_bank

.service_fail_restore_candidate:
    mov al, [move_status]
    mov [error_status], al
    mov bx, candidate_original
    call move_low_to_high
    jmp .mark_service_error_restore_anchor
.service_fail_restore_anchor:
    mov al, [move_status]
    mov [error_status], al
.mark_service_error_restore_anchor:
    mov byte [service_error], 1
    jmp .restore_anchor
.service_fail:
    mov al, [move_status]
    mov [error_status], al
    mov byte [service_error], 1
    ret

.restore_anchor:
    mov byte [current_bank], 0x10
    mov bx, anchor_original
    call move_low_to_high
    ret

; Copy one word from current_bank:0000 to low-memory variable DS:BX.
move_high_to_low:
    mov word [move_table+0x12], 0
    mov al, [current_bank]
    mov byte [move_table+0x14], al
    mov ax, bx
    add ax, PAYLOAD_PHYS
    mov word [move_table+0x1a], ax
    mov byte [move_table+0x1c], 0
    jmp bios_move_word

; Copy one word from low-memory variable DS:BX to current_bank:0000.
move_low_to_high:
    mov ax, bx
    add ax, PAYLOAD_PHYS
    mov word [move_table+0x12], ax
    mov byte [move_table+0x14], 0
    mov word [move_table+0x1a], 0
    mov al, [current_bank]
    mov byte [move_table+0x1c], al

    jmp bios_move_word

; Copy one word between two conventional-memory variables, DS:BX to DS:DX.
move_low_to_low:
    mov ax, bx
    add ax, PAYLOAD_PHYS
    mov word [move_table+0x12], ax
    mov byte [move_table+0x14], 0
    mov ax, dx
    add ax, PAYLOAD_PHYS
    mov word [move_table+0x1a], ax
    mov byte [move_table+0x1c], 0

bios_move_word:
    push bx
    push cx
    push dx
    push si
    push di
    ; IBM's documented contract requires callers to initialize the source and
    ; destination descriptor limits and access bytes.  Some later Phoenix
    ; descriptions say the BIOS fills them, but the original AT-compatible
    ; implementation rejects an all-zero descriptor with AH=02h.
    mov word [move_table+0x10], 1
    mov byte [move_table+0x15], 0x93
    mov word [move_table+0x16], 0
    mov word [move_table+0x18], 1
    mov byte [move_table+0x1d], 0x93
    mov word [move_table+0x1e], 0
    push ds
    pop es
    mov si, move_table
    mov cx, 1
    mov ah, 0x87
    int 0x15
    mov [move_status], ah
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

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

align 16
move_table times 0x30 db 0
anchor_original dw 0
anchor_pattern dw 0xa55a
anchor_verify dw 0
candidate_original dw 0
candidate_pattern dw 0x5aa5
candidate_verify dw 0
blocks dw 0
current_bank db 0
service_error db 0
move_status db 0
error_status db 0
control_source dw 0x1234
control_destination dw 0
control_status db 0
control_ok db 0

title db 'BluMach PC5286 BIOS block-move control v6',13,10
      db 'Tests and restores one word per 64KiB window.',13,10,13,10,0
control_status_text db 'Conventional control status: 0x',0
control_value_text db ' copied word: 0x',0
control_failed_text db 'AH=87h control failed; high-memory probe skipped.',13,10,0
service_error_text db 'INT 15h/87h error status: 0x',0
blocks_text db 'Verified 64KiB windows from 1MiB: 0x',0
kib_text db 'Verified KiB from 1MiB: 0x',0
done_text db 13,10,'Samples restored. Reset or close BluMach.',13,10,0

times 8192-($-$$) db 0
section padding start=8704
times 1474560-8704 db 0
