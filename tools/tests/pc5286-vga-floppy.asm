; BluMach, a preservation-focused fork of 86Box.
; Author: rtzor
; Project: BluMach
; NASM -f bin pc5286-vga-floppy.asm -o pc5286-vga-test.img
; Standalone 1.44 MB diagnostic; no DOS, no disk writes, 8086 instructions.
bits 16
cpu 8086
section boot start=0 vstart=0x7c00
    jmp 0:start
start:
    cli
    xor ax,ax
    mov ss,ax
    mov sp,0x7c00
    mov ds,ax
    sti
    mov [drive],dl
    mov ax,0x0800
    mov es,ax
    xor bx,bx
    mov ax,0x0210
    mov cx,0x0002
    xor dh,dh
    int 0x13
    jc disk_error
    jmp 0x0800:0
disk_error:
    mov si,boot_error
.print:
    lodsb
    test al,al
    jz .halt
    mov ah,0x0e
    xor bx,bx
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt
drive db 0
boot_error db 'VGA test: floppy read error. Reset and retry.',0
times 510-($-$$) db 0
dw 0xaa55

; Payload loaded from floppy sectors 2 through 17.
section payload start=512 vstart=0
main:
    cld
    push cs
    pop ds
    mov ax,3
    int 0x10
    mov si,intro
    call puts
    call key
    ; Mode 12 provides unchained planar access to all four 64K planes.
    mov ax,0x12
    int 0x10
    mov dx,0x3c4
    mov ax,0x0604
    out dx,ax
    mov dx,0x3ce
    mov ax,0x0000
    out dx,ax
    mov ax,0x0001
    out dx,ax
    mov ax,0x0003
    out dx,ax
    mov ax,0x0005
    out dx,ax
    mov ax,0x0506
    out dx,ax
    mov ax,0xff08
    out dx,ax
    mov ax,0xa000
    mov es,ax
    mov word [seed],0x5a3c
    mov byte [passes],2
.pass:
    xor bp,bp
.fillplane:
    mov dx,0x3c4
    mov ax,0x0102
    mov cx,bp
    shl ah,cl
    out dx,ax
    xor di,di
.fill:
    mov ax,di
    xor ax,[seed]
    xor ax,bp
    stosw
    test di,di
    jnz .fill
    inc bp
    cmp bp,4
    jb .fillplane
    xor bp,bp
.readplane:
    mov dx,0x3ce
    mov ax,bp
    mov ah,al
    mov al,4
    out dx,ax
    xor di,di
.verify:
    mov ax,di
    xor ax,[seed]
    xor ax,bp
    cmp ax,[es:di]
    jne failed
    add di,2
    jnz .verify
    inc bp
    cmp bp,4
    jb .readplane
    not word [seed]
    dec byte [passes]
    jnz .pass
    mov ax,3
    int 0x10
    mov si,passed
    call puts
    call key
    ; Sixteen vertical colour bars in BIOS mode 12h, 640x480.
    mov ax,0x12
    int 0x10
    mov ax,0xa000
    mov es,ax
    xor bp,bp
.barplane:
    mov dx,0x3c4
    mov ax,0x0102
    mov cx,bp
    shl ah,cl
    out dx,ax
    xor di,di
    mov bx,480
.barrow:
    xor si,si
.bar:
    mov ax,si
    mov cx,bp
    shr ax,cl
    and al,1
    neg al
    mov cx,5
    rep stosb
    inc si
    cmp si,16
    jb .bar
    dec bx
    jnz .barrow
    inc bp
    cmp bp,4
    jb .barplane
    call key
    mov ax,3
    int 0x10
    mov si,mode13msg
    call puts
    call key
    mov ax,0x13
    int 0x10
    mov ax,0xa000
    mov es,ax
    xor di,di
    mov bx,200
.ramp_row:
    xor ax,ax
    mov cx,32
    rep stosb
    mov cx,256
.ramp:
    stosb
    inc al
    loop .ramp
    mov cx,32
    rep stosb
    dec bx
    jnz .ramp_row
    call key
    mov ax,3
    int 0x10
    mov si,done
    call puts
.stop:
    sti
    hlt
    jmp .stop
failed:
    mov [badplane],bp
    mov [badoffset],di
    mov ax,3
    int 0x10
    mov si,failmsg
    call puts
    mov ax,[badplane]
    call hexword
    mov si,offsetmsg
    call puts
    mov ax,[badoffset]
    call hexword
    mov si,resetmsg
    call puts
    jmp main.stop
puts:
    lodsb
    test al,al
    jz .ret
    mov ah,0x0e
    mov bx,7
    int 0x10
    jmp puts
.ret:
    ret
key:
    xor ah,ah
    int 0x16
    ret
hexword:
    mov bp,ax
    mov di,4
.next:
    mov cx,4
    rol bp,cl
    mov ax,bp
    and al,15
    add al,'0'
    cmp al,'9'
    jbe .emit
    add al,7
.emit:
    mov ah,0x0e
    mov bx,7
    int 0x10
    dec di
    jnz .next
    ret
seed dw 0
passes db 0
badplane dw 0
badoffset dw 0
intro db 'BluMach PC5286 VGA test v2',13,10
      db 'Tests standard VGA; does not certify the complete 82C452.',13,10
      db 'No DOS required. No disk writes.',13,10,13,10
      db 'Press a key: test 4 x 64 KiB with two complementary patterns.',13,10,0
passed db 'PASS: four VGA planes, 256 KiB, two patterns.',13,10,13,10
       db 'Press a key for 640x480 / 16-colour bars.',13,10
       db 'Check 16 clean vertical bars; press a key to continue.',13,10,0
mode13msg db 'Next: 320x200 / 256 palette indices (BIOS default palette).',13,10
          db 'Expected: central vertical colour bands, black side margins.',13,10
          db 'Press a key to display; another key returns to text.',13,10,0
done db 'Returned to text mode 03h.',13,10
     db 'VRAM test passed; record visual results separately.',13,10
     db 'Try Ctrl+Alt+Del and a hard reset to repeat.',13,10,0
failmsg db 'FAIL: VGA memory mismatch. Plane (0-3): ',0
offsetmsg db ' offset: ',0
resetmsg db 13,10,'Take a screenshot and reset. Do not treat this as PASS.',0
times 8192-($-$$) db 0
section padding start=8704
times 1474560-8704 db 0
