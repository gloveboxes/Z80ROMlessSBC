; Mirrored FB40h..FB7Fh slot. The CP/M image installs it in bank 0 and the
; firmware copies base-image FB40h+ into bank 1, so code here keeps running
; across bank-select OUTs. Never touch the stack while bank 1 is selected.
        org 0fb40h
; Bank-1 BIOS warm boot (FC03h patched to JP FB40h) re-enters bank 0.
warm_boot_return:
        xor a
        out (7),a
        jp 0fc03h
        defs 0fb48h-$,0
; FB48h: A=extent-table index -> A=allocated length in 256-byte pages, or 0
; when that extent is free or unused. Called from bank 0 with maskable
; interrupts disabled; returns in bank 0. Clobbers F and HL.
extent_pages:
        ld l,a
        ld h,0e7h
        ld a,1
        out (7),a
        set 6,l
        ld a,(hl)               ; next-free link, FFh marks an allocation
        res 6,l
        inc a
        ld a,0
        jr nz,extent_selected
        ld a,(hl)               ; length in pages
extent_selected:
        ld l,a
        xor a
        out (7),a
        ld a,l
        ret
        defs 0fb68h-$,0
; CCP transient launch: discard the previous app's banked runtime before
; entering 0100h. Preserve the CCP's argument registers and return address.
application_start:
        push af
        push bc
        push de
        push hl
        ld e,10
        call 0ff10h
        pop hl
        pop de
        pop bc
        pop af
        jp 0100h
        defs 0fb80h-$,0
