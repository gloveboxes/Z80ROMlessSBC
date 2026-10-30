; ABI 2.6. E=9 returns a size/version-tagged descriptor; E=10 accepts the
; discovered 32-byte request. Status is independent of function results.
        org 0ec00h
capabilities:
        ld hl,0e600h
        ld de,signature
        ld b,9
check_signature:
        ld a,(de)
        cp (hl)
        jr nz,unavailable
        inc hl
        inc de
        djnz check_signature
        ld hl,0e360h
        jp 0ff13h
unavailable:
        ld a,1
        ld (0dfa2h),a
        ld hl,0
        jp 0ff13h
signature:
        db "BKM3BEX1",6
        defs 0ec30h-$,0
control:
        ld de,0df20h
        or a
        sbc hl,de
        jp nz,bad_argument
        ld hl,(0df20h)
        ld de,32
        or a
        sbc hl,de
        jp nz,bad_argument
        ld hl,(0df22h)
        dec hl
        ld a,h
        or l
        jp nz,bad_argument
        ld a,(0df25h)
        or a
        jp nz,bad_argument
        ld a,(0df24h)
        or a
        jr z,load
        dec a
        jr z,invalidate
        dec a
        jr z,query
        dec a
        jp z,invoke
bad_argument:
        ld a,2
        jp finish
invalidate:
        xor a
        ld (0de9fh),a
        ld (0e4d2h),a
        call 0e393h
        call 0e396h
query:
        call 0e390h
        xor a
        jp finish
load:
        ld hl,(0df2ch)
        call pool_size
        ld a,h
        or a
        jp z,bad_handle
        call 0e393h
        jp c,exhausted
        xor a
        ld (0de9fh),a
        ld (0e4d0h),a
        ld (0e4d2h),a
        call 0e396h
        ld hl,(0df2ch)
        ld de,0e500h
        ld bc,48
        ldir
        call 0e780h
        call 0e000h
        push hl
        call 0e783h
        call 0e390h
        pop hl
        ld a,l
        or a
        jp z,finish
        cp 2
        ld a,7
        jp z,finish
        ld a,l
        cp 3
        ld a,8
        jp z,finish
        ld a,9
        jp finish
invoke:
        ld a,(0de9fh)
        or a
        jp z,stale
        ld hl,(0df28h)
        ld de,(0dfa0h)
        or a
        sbc hl,de
        jp nz,stale
        ld hl,(0df3eh)
        ld de,(0dfa6h)
        or a
        sbc hl,de
        jp nz,stale
        ld hl,(0df2ch)
        call pool_size
        ld a,h
        cp 2
        jp c,bad_handle
        ld hl,(0df2ch)
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld a,d
        or e
        jp z,bad_argument
        ld hl,256
        or a
        sbc hl,de
        jp c,bad_argument
        ld a,(0e4d3h)
        cp 12
        jr nz,bad_format
        ld hl,(0df2ah)
        ld a,h
        or a
        jp nz,bad_argument
        ld a,(0e4d2h)
        cp l
        jp c,bad_argument
        jp z,bad_argument
        add hl,hl
        add hl,hl
        ld d,h
        ld e,l
        add hl,hl
        add hl,de
        ld de,0e308h
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        push de
        pop ix
        ld iy,(0df2ch)
        ld hl,(0df2eh)
        ld (0e4d6h),hl
        ld a,1
        ld (0dfa4h),a
        ld (0dfa5h),a
        ld hl,0100h
        ld bc,0c000h
        ld e,6
        call 0e800h
        ld a,(0df26h)
        ld l,a
        ld h,0
        ret
bad_format:
        ld a,7
        jr finish
bad_handle:
        ld a,3
        jr finish
stale:
        ld a,10
        jr finish
exhausted:
        ld a,11
finish:
        ld (0dfa2h),a
        ld (0df26h),a
        ld l,a
        xor a
        ld h,a
        ld (0df27h),a
        ret
; Only ordinary general-pool extents can stage requests and call frames.
pool_size:
        ld a,l
        or a
        jr nz,invalid_pool
        ld a,h
        cp 0c8h
        jr c,invalid_pool
        cp 0deh
        jr nc,invalid_pool
        sub 0c0h
        ld l,a
        ld h,0e7h
        ld a,(hl)
        ld d,a
        set 6,l
        ld a,(hl)
        cp 255
        jr nz,invalid_pool
        ld h,d
        ld l,0
        ret
invalid_pool:
        ld hl,0
        ret
        defs 0edc0h-$,0
complete_control:
        push hl
        ld a,(0dfa4h)
        or a
        jr z,control_return
        xor a
        ld (0dfa4h),a
        ld a,(0dfa2h)
        or a
        jr nz,control_status
        pop hl
        push hl
        dec hl
        ld a,h
        or l
        jr z,control_status
        ld a,6
control_status:
        ld (0dfa2h),a
        ld (0df26h),a
        xor a
        ld (0df27h),a
control_return:
        pop hl
        ret
