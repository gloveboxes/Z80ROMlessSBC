; Resident program: code <1000h, reference vectors 1000h-19ffh,
; stack 1a00h-1fffh, destructive work area 2000h-ffffh.
        org 0
        di
        ld sp,02000h
        if scope_only
        ld a,1
        ld (00f04h),a
        endif
cycle:
        xor a
        call phase
        ld a,1
        call phase
        ld a,2
        call phase
        ld a,3
        call phase
        jp cycle

phase:
        ld (pattern),a
        ld ix,01000h
        ld b,128
        call stack_test
        ld iy,0
        ld a,16
        ld (repeats),a
math_repeat:
        call vectors
        inc iy
        ld a,(repeats)
        dec a
        ld (repeats),a
        jr nz,math_repeat
        push iy
        pop hl
        ld de,16
        ld c,18
        call check_word

        ld a,(pattern)
        ld c,a
        ld hl,02000h
fill:
        call pattern_byte
        ld (hl),a
        inc hl
        ld a,h
        or l
        jr nz,fill
        ld hl,02000h
verify:
        call pattern_byte
        ld d,a
        ld a,(hl)
        cp d
        jp nz,memory_failure
        inc hl
        ld a,h
        or l
        jr nz,verify
        if scope_only
        ld a,1
        ld (00f05h),a
        ld hl,(00f06h)
        inc hl
        ld (00f06h),hl
        ld a,h
        or l
        jr nz,scope_counted
        ld hl,(00f08h)
        inc hl
        ld (00f08h),hl
scope_counted:
        ld a,(pattern)
        ld (00f0ah),a
        xor a
        ld (00f05h),a
        else
        ld a,(pattern)
        add a,040h
        out (6),a
        endif
        ret

pattern_byte:
        bit 0,c
        ld a,l
        jr z,pattern_complement
        ld a,h
pattern_complement:
        bit 1,c
        ret z
        cpl
        ret

vectors:
        ld ix,01000h
        ld b,128
vector:
        ld l,(ix+0)
        ld h,(ix+1)
        ld e,(ix+2)
        ld d,(ix+3)
        or a
        add hl,de
        push af
        ld e,(ix+4)
        ld d,(ix+5)
        ld c,1
        call check_word
        pop hl
        ld a,l
        and 013h
        ld l,a
        ld h,0
        ld e,(ix+6)
        ld d,0
        inc c
        call check_word

        ld l,(ix+0)
        ld h,(ix+1)
        ld e,(ix+2)
        ld d,(ix+3)
        scf
        adc hl,de
        push af
        ld e,(ix+7)
        ld d,(ix+8)
        ld c,3
        call check_word
        pop hl
        ld a,l
        and 0d7h
        ld l,a
        ld h,0
        ld e,(ix+9)
        ld d,0
        inc c
        call check_word

        ld l,(ix+0)
        ld h,(ix+1)
        ld e,(ix+2)
        ld d,(ix+3)
        or a
        sbc hl,de
        push af
        ld e,(ix+10)
        ld d,(ix+11)
        ld c,5
        call check_word
        pop hl
        ld a,l
        and 0d7h
        ld l,a
        ld h,0
        ld e,(ix+12)
        ld d,0
        inc c
        call check_word

        ld l,(ix+0)
        ld h,(ix+1)
        ld e,(ix+2)
        ld d,(ix+3)
        scf
        sbc hl,de
        push af
        ld e,(ix+13)
        ld d,(ix+14)
        ld c,7
        call check_word
        pop hl
        ld a,l
        and 0d7h
        ld l,a
        ld h,0
        ld e,(ix+15)
        ld d,0
        inc c
        call check_word

        push bc
        ld e,(ix+0)
        ld d,(ix+1)
        ld c,(ix+2)
        ld b,(ix+3)
        call multiply
        pop bc
        ld e,(ix+16)
        ld d,(ix+17)
        ld c,9
        call check_word

        ld l,(ix+0)
        ld h,(ix+1)
        ld e,(ix+2)
        ld d,(ix+3)
        or a
        sbc hl,de
        ld a,0
        jr z,unsigned_done
        jr c,unsigned_less
        inc a
        jr unsigned_done
unsigned_less:
        dec a
unsigned_done:
        ld l,a
        ld h,0
        ld e,(ix+18)
        ld d,0
        ld c,10
        call check_word

        ld l,(ix+0)
        ld h,(ix+1)
        ld e,(ix+2)
        ld d,(ix+3)
        or a
        sbc hl,de
        ld a,0
        jr z,signed_done
        jp pe,signed_overflow
        jp m,signed_less
signed_greater:
        inc a
        jr signed_done
signed_overflow:
        jp m,signed_greater
signed_less:
        dec a
signed_done:
        ld l,a
        ld h,0
        ld e,(ix+19)
        ld d,0
        ld c,11
        call check_word

        ld de,20
        add ix,de
        dec b
        jp nz,vector
        push ix
        pop hl
        ld de,01a00h
        ld c,17
        call check_word
        ret

; 16-bit shift/add multiplication, modulo 65536.
multiply:
        ld hl,0
        ld a,16
multiply_loop:
        srl b
        rr c
        jr nc,multiply_shift
        add hl,de
multiply_shift:
        ex de,hl
        add hl,hl
        ex de,hl
        dec a
        jr nz,multiply_loop
        ret

stack_test:
        ld hl,01357h
        ld de,02468h
        ld bc,05faah
        call nested
        push bc
        push de
        ld de,01357h
        ld c,12
        call check_word
        pop hl
        ld de,02468h
        ld c,13
        call check_word
        pop hl
        ld de,05faah
        ld c,14
        call check_word
        ld hl,0
        add hl,sp
        ld de,01ffch
        ld c,15
        call check_word
        ld a,(01a00h)
        ld l,a
        ld h,0
        ld de,0a5h
        ld c,16
        call check_word
        ret

nested:
        push bc
        push de
        push hl
        ld a,b
        and 00fh
        jr z,nested_return
        dec b
        call nested
nested_return:
        pop hl
        pop de
        pop bc
        ret

check_word:
        ld a,l
        cp e
        jp nz,failure
        ld a,h
        cp d
        jp nz,failure
        ret

memory_failure:
        ld e,d
        ld d,0
        push hl
        pop ix
        ld l,a
        ld h,0
        ld a,(pattern)
        or 080h
        ld c,a
        ld b,129
failure:
        if scope_only
        ld a,1
        ld (00f05h),a
        ld a,c
        ld (00f0ch),a
        ld a,128
        sub b
        ld (00f0dh),a
        ld (00f0eh),ix
        ld (00f10h),hl
        ld (00f12h),de
        ld a,0e1h
        ld (00f04h),a
        xor a
        ld (00f05h),a
scope_halted:
        halt
        jp scope_halted
        else
        ld a,0e1h
        out (6),a
        ld a,c
        out (6),a
        ld a,128
        sub b
        out (6),a
        push hl
        push ix
        pop hl
        ld a,l
        out (6),a
        ld a,h
        out (6),a
        pop hl
        ld a,l
        out (6),a
        ld a,h
        out (6),a
        ld a,e
        out (6),a
        ld a,d
        out (6),a
        halt
        jp failure
        endif

pattern: db 0
repeats: db 0
