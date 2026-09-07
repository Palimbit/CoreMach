; BluMach PC5286 memory-map probe, 80286 compatible.
; NASM -f bin pc5286-memory-floppy.asm -o pc5286-memory-probe.img
; Standalone read/write/restore probe: no DOS and no disk writes.
; It enters 286 protected mode and deliberately stays there until reset.
bits 16
cpu 286
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
    mov [drive], dl
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
    jz .halt
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt
drive db 0
boot_error db 'Memory probe: floppy read error.',0
times 510-($-$$) db 0
dw 0xaa55

; Payload lives at 0800:0000 in real mode, physical 8000h.
section payload start=512 vstart=0
real_start:
    cld
    push cs
    pop ds
    mov ax, 3
    int 0x10
    mov si, real_intro
    call real_puts
    xor ah, ah
    int 0x16
    cli
    lgdt [cs:gdtr]
    mov ax, 1
    lmsw ax
    db 0xea
    dw pm_start
    dw 0x08

real_puts:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp real_puts
.done:
    ret

; 08/10 code and data at the payload's physical base.  Keeping this conventional
; 286 layout means all program labels remain ordinary near offsets.  18 is the
; variable probe window and 20 is text video at B8000.
gdt:
    dq 0
    dw 0xffff, 0x8000
    db 0x00, 0x9a, 0x00, 0x00
    dw 0xffff, 0x8000
    db 0x00, 0x92, 0x00, 0x00
probe_desc:
    dw 0xffff, 0x0000
    db 0x10, 0x92, 0x00, 0x00
    dw 0xffff, 0x8000
    db 0x0b, 0x92, 0x00, 0x00
gdt_end:
gdtr:
    dw gdt_end-gdt-1
    dd PAYLOAD_PHYS+gdt

pm_start:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov sp, 0xfffe
    call pm_clear
    mov si, pm_intro
    call pm_puts
    call probe_extended
    mov si, blocks_label
    call pm_puts
    mov ax, [cs:blocks]
    call pm_hex16
    mov si, kb_label
    call pm_puts
    mov ax, [cs:blocks]
    mov cl, 6
    shl ax, cl
    call pm_hex16
    mov si, end_label
    call pm_puts
.halt:
    hlt
    jmp .halt

; Probe one word at the beginning of each 64KiB window from 1MiB to 4MiB.
; Each candidate is written, read and restored. The fixed 1MiB anchor detects
; a wrapped/mirrored window. This is a bank-presence probe, not a full RAM test.
probe_extended:
    mov byte [cs:probe_desc+4], 0x10
    mov ax, 0x18
    mov es, ax
    mov ax, [es:0]
    mov [cs:anchor_original], ax
    mov word [es:0], 0xa55a
    cmp word [es:0], 0xa55a
    jne .none
    mov word [cs:blocks], 1
    mov bl, 0x11
.next:
    cmp bl, 0x40
    je .done
    mov byte [cs:probe_desc+4], bl
    mov ax, 0x18
    mov es, ax
    mov dx, [es:0]
    mov word [cs:candidate_original], dx
    mov word [es:0], 0x5aa5
    cmp word [es:0], 0x5aa5
    jne .finish
    mov byte [cs:probe_desc+4], 0x10
    mov ax, 0x18
    mov es, ax
    cmp word [es:0], 0xa55a
    jne .finish
    mov byte [cs:probe_desc+4], bl
    mov ax, 0x18
    mov es, ax
    mov dx, [cs:candidate_original]
    mov [es:0], dx
    inc word [cs:blocks]
    inc bl
    jmp .next
.none:
    mov word [cs:blocks], 0
.finish:
    mov byte [cs:probe_desc+4], 0x10
    mov ax, 0x18
    mov es, ax
    mov dx, [cs:anchor_original]
    mov [es:0], dx
.done:
    ret

pm_clear:
    mov ax, 0x20
    mov es, ax
    xor di, di
    mov ax, 0x0720
    mov cx, 2000
    rep stosw
    mov word [cs:cursor], 0
    ret

pm_puts:
    lodsb
    or al, al
    jz .done
    call pm_char
    jmp pm_puts
.done:
    ret

pm_char:
    cmp al, 13
    je .cr
    cmp al, 10
    je .lf
    mov ax, 0x20
    mov es, ax
    mov di, [cs:cursor]
    mov ah, 0x07
    stosw
    mov [cs:cursor], di
    ret
.cr:
    mov ax, [cs:cursor]
    and ax, 0xffb0
    mov [cs:cursor], ax
    ret
.lf:
    add word [cs:cursor], 160
    ret

pm_hex16:
    mov bp, ax
    mov di, 4
.next:
    mov cl, 4
    rol bp, cl
    mov ax, bp
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    call pm_char
    dec di
    jnz .next
    ret

cursor dw 0
blocks dw 0
anchor_original dw 0
candidate_original dw 0
real_intro db 'BluMach PC5286 memory probe v1',13,10
           db '80286 protected-mode bank probe; restores sampled words.',13,10
           db 'It does not test EMS, shadow or every RAM byte.',13,10
           db 'Press a key to enter probe mode; reset to return.',13,10,0
pm_intro db 'PC5286 extended-memory bank probe',13,10,13,10,0
blocks_label db '64KiB windows from 1MiB: 0x',0
kb_label db 13,10,'Contiguous KiB from 1MiB: 0x',0
end_label db 13,10,13,10,'Sampled words restored. Reset to exit protected mode.',0
times 8192-($-$$) db 0
section padding start=8704
times 1474560-8704 db 0
