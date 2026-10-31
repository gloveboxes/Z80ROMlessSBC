; Resident descriptor helpers, outside exports and the capability record.
        org 0e390h
        jp describe_library
        jp advance_generation
        jp reset_library_context
describe_library:
        xor a
        ld (0df31h),a
        ld (0df33h),a
        ld (0df35h),a
        ld hl,(0dfa0h)
        ld (0df28h),hl
        ld a,(0de9fh)
        ld (0df30h),a
        ld a,(0e4d2h)
        ld (0df32h),a
        ld a,12                ; export entry bytes
        ld (0df34h),a
        ld hl,(0e4d4h)
        ld (0df36h),hl
        ld hl,0e300h
        ld (0df38h),hl
        ld hl,0100h
        ld (0df3ah),hl
        ld hl,0c000h
        ld (0df3ch),hl
        ld hl,(0dfa6h)
        ld (0df3eh),hl
        ret
advance_generation:
        ld hl,(0dfa0h)
        inc hl
        ld a,h
        or l
        scf
        ret z
        ld (0dfa0h),hl
        or a
        ret
reset_library_context:
        xor a
        ld hl,0df80h
        ld de,0df81h
        ld bc,29
        ld (hl),a
        ldir
        ld a,080h
        ld (0df80h),a
        ret
