; BluMach PC5286 SCAT EMS two-window alias probe.
; Standalone 8086 test; no disk writes. Restores the sampled EMS word,
; both page registers, the selector and SCAT register 4Fh.
bits 16
cpu 8086
%define PAGE_0 0x18               ; D0000h
%define PAGE_1 0x19               ; D4000h
%define TARGET_PAGE 0x60          ; 0x60 * 16 KiB = 0x180000

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
boot_error db 'EMS probe: floppy read error.',0
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

    mov al, 0x4f
    call scat_read
    mov [old_4f], al
    mov word [ems_base], 0x0208
    test al, 1
    jz .base_ready
    mov word [ems_base], 0x0218
.base_ready:
    mov ah, al
    and ah, 0x7f
    or ah, 0x40
    mov [idle_4f], ah
    mov al, 0x4f
    call scat_write

    mov dx, [ems_base]
    add dx, 2
    in al, dx
    mov [old_selector], al
    mov al, PAGE_0
    out dx, al
    mov dx, [ems_base]
    in al, dx
    mov [old_page0_low], al
    inc dx
    in al, dx
    mov [old_page0_high], al
    mov dx, [ems_base]
    add dx, 2
    mov al, PAGE_1
    out dx, al
    mov dx, [ems_base]
    in al, dx
    mov [old_page1_low], al
    inc dx
    in al, dx
    mov [old_page1_high], al

    call program_test_pages
    mov ah, [idle_4f]
    or ah, 0x80
    mov al, 0x4f
    call scat_write

    mov ax, 0xd000
    mov es, ax
    mov ax, [es:0]
    mov [original_word], ax
    mov word [es:0], 0xa55a
    mov ax, 0xd400
    mov es, ax
    mov ax, [es:0]
    mov [alias_from_d000], ax
    cmp ax, 0xa55a
    jne .restore
    mov word [es:0], 0x5aa5
    mov ax, 0xd000
    mov es, ax
    mov ax, [es:0]
    mov [alias_from_d400], ax
    cmp ax, 0x5aa5
    jne .restore
    mov byte [alias_ok], 1

.restore:
    mov ax, [original_word]
    mov [es:0], ax
    mov ax, 0xd400
    mov es, ax
    mov ax, [es:0]
    mov [restored_word], ax
    cmp ax, [original_word]
    jne .restore_registers
    mov byte [memory_restored], 1

.restore_registers:
    mov al, 0x4f
    mov ah, [idle_4f]
    call scat_write
    mov dx, [ems_base]
    add dx, 2
    mov al, PAGE_0
    out dx, al
    mov dx, [ems_base]
    mov al, [old_page0_low]
    out dx, al
    inc dx
    mov al, [old_page0_high]
    out dx, al
    mov dx, [ems_base]
    add dx, 2
    mov al, PAGE_1
    out dx, al
    mov dx, [ems_base]
    mov al, [old_page1_low]
    out dx, al
    inc dx
    mov al, [old_page1_high]
    out dx, al
    mov dx, [ems_base]
    add dx, 2
    mov al, [old_selector]
    out dx, al
    mov al, 0x4f
    mov ah, [old_4f]
    call scat_write
    mov byte [registers_restored], 1

    mov si, control_text
    call puts
    xor ax, ax
    mov al, [old_4f]
    call hex16
    mov si, base_text
    call puts
    mov ax, [ems_base]
    call hex16
    call newline
    mov si, saved0_text
    call puts
    mov al, [old_page0_high]
    call hex8
    mov al, [old_page0_low]
    call hex8
    mov si, saved1_text
    call puts
    mov al, [old_page1_high]
    call hex8
    mov al, [old_page1_low]
    call hex8
    call newline
    mov si, alias0_text
    call puts
    mov ax, [alias_from_d000]
    call hex16
    mov si, alias1_text
    call puts
    mov ax, [alias_from_d400]
    call hex16
    call newline
    mov si, restore_text
    call puts
    mov ax, [restored_word]
    call hex16
    call newline
    cmp byte [alias_ok], 1
    jne .failed
    cmp byte [memory_restored], 1
    jne .failed
    cmp byte [registers_restored], 1
    jne .failed
    mov si, pass_text
    call puts
    jmp payload_halt
.failed:
    mov si, fail_text
    call puts
payload_halt:
    mov si, done_text
    call puts
.halt:
    sti
    hlt
    jmp .halt

program_test_pages:
    mov dx, [ems_base]
    add dx, 2
    mov al, PAGE_0
    out dx, al
    mov dx, [ems_base]
    mov al, TARGET_PAGE
    out dx, al
    inc dx
    mov al, 0x80
    out dx, al
    mov dx, [ems_base]
    add dx, 2
    mov al, PAGE_1
    out dx, al
    mov dx, [ems_base]
    mov al, TARGET_PAGE
    out dx, al
    inc dx
    mov al, 0x80
    out dx, al
    ret

scat_read:
    out 0x22, al
    in al, 0x23
    ret
scat_write:
    out 0x22, al
    mov al, ah
    out 0x23, al
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
    mov al, ah
    call hex8
    pop ax
    call hex8
    ret
hex8:
    push ax
    push bx
    mov bl, al
    mov al, bl
    mov cl, 4
    shr al, cl
    call hex_digit
    mov al, bl
    and al, 0x0f
    call hex_digit
    pop bx
    pop ax
    ret
hex_digit:
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    mov ah, 0x0e
    xor bh, bh
    int 0x10
    ret

old_4f db 0
idle_4f db 0
ems_base dw 0x0208
old_selector db 0
old_page0_low db 0
old_page0_high db 0
old_page1_low db 0
old_page1_high db 0
original_word dw 0
alias_from_d000 dw 0
alias_from_d400 dw 0
restored_word dw 0
alias_ok db 0
memory_restored db 0
registers_restored db 0
title db 'BluMach PC5286 SCAT EMS two-window probe v2',13,10
      db 'D0000h and D4000h map target page 60h.',13,10,13,10,0
control_text db 'Initial SCAT 4Fh: 0x',0
base_text db ' page ports: 0x',0
saved0_text db 'Saved page 18h: 0x',0
saved1_text db ' page 19h: 0x',0
alias0_text db 'D400 sees D000 write: 0x',0
alias1_text db ' D000 sees D400 write: 0x',0
restore_text db 'Restored target word: 0x',0
pass_text db 13,10,'PASS: two-way EMS alias verified; memory and registers restored.',13,10,0
fail_text db 13,10,'FAIL: EMS alias or restoration did not verify.',13,10,0
done_text db 'Reset or close BluMach.',13,10,0

times 8192-($-$$) db 0
section padding start=8704
times 1474560-8704 db 0
