; Bank-1 .LIB loader at E000h, called by the E=8 library LOAD action.
; The request stages a 36-byte CP/M FCB at E500h and sets action E4D0h
; (0 load, 1 invalidate); status is E4D1h and exports are copied to E300h.
; A successful load installs one image at 0100h. E4D1h: 0 ready, 1 I/O,
; 2 invalid header. E4D2h is export count, E4D4h image length. Like a CP/M
; .COM load, the image is copied verbatim; header bytes 14..15 are reserved.
; The fixed image/heap and stack extents are pinned by the allocator.
        org 0e000h
loader:
        ld a,(0e4d0h)
        or a
        jr z,load_module
        xor a
        ld (0de9fh),a
        ld (0e4d1h),a
        ld h,a
        ld l,a
        ret
load_module:
        xor a
        ld (0de9fh),a
        ld a,1
        ld (0e4d1h),a
        ld a,(0dea9h)
        or a
        jr nz,bdos_ready
        ld c,13                ; initialize a fresh bank-1 BDOS once
        call 0005h
        ld a,1
        ld (0dea9h),a
bdos_ready:
        ld hl,(0faf1h)         ; translated CP/M A_F8B1 DMA pointer
        ld (0de9eh),hl
        ld hl,0e400h
        ld (0faf1h),hl
        ld bc,0e400h
        call 0fc24h
        ld de,0e500h
        ld c,15
        call 0005h
        inc a
        jp z,restore_dma
        ld de,0e500h
        ld c,20
        call 0005h
        or a
        jr nz,close_file
        call validate_header
        jr c,bad_header
        ; Header and at most eight exports fit entirely in first record.
        ld hl,(0de94h)
        ld de,26
        or a
        sbc hl,de
        ld b,h
        ld c,l
        ld hl,0e41ah
        ld de,0e300h
        ldir
        ld hl,(0de94h)
        ld de,0e400h
        add hl,de
        ld (0de9ah),hl
        ld hl,0100h
        ld (0de96h),hl
        call copy_chunk
read_more:
        ld hl,(0de98h)
        ld a,h
        or l
        jr z,image_loaded
        ld de,0e500h
        ld c,20
        call 0005h
        or a
        jr nz,close_file
        ld hl,0e400h
        ld (0de9ah),hl
        call copy_chunk
        jr read_more
image_loaded:
        call initialize_runtime
        jr c,bad_header
        xor a
        ld (0e4d1h),a
        inc a
        ld (0de9fh),a
        jr close_file
bad_header:
        ld a,2
        ld (0e4d1h),a
close_file:
        ld de,0e500h
        ld c,16
        call 0005h
restore_dma:
        ld hl,(0de9eh)
        ld (0faf1h),hl
        ld b,h
        ld c,l
        call 0fc24h
        ld a,(0e4d1h)
        ld l,a
        ld h,0
        ret

reject:
        scf
        ret
; Carry on any invalid metadata. The image must leave at least one
; byte of DCC heap before the configured stack.
validate_header:
        ld hl,0e400h
        ld de,magic
        ld b,10                ; magic, versions and 0100h origin
        call match
        jr nz,reject
        ld hl,(0e40ah)
        ld a,h
        or l
        jr z,reject
        ld de,0c600h          ; excludes wrap; heap/stack check is exact
        or a
        sbc hl,de
        jr nc,reject
        ld hl,(0e40ah)
        ld (0de92h),hl
        ld (0e4d4h),hl
        ld (0de98h),hl
        ld a,(0e40dh)
        or a
        jr nz,reject
        ld a,(0e40ch)
        or a
        jr z,reject
        cp 9
        jr nc,reject
        ld (0e4d2h),a
        add a,a
        add a,a
        ld c,a
        add a,a
        add a,c                ; twelve bytes per export
        add a,26
        ld b,a
        ld hl,(0e412h)
        ld a,h
        or a
        jr nz,header_invalid
        ld a,l
        cp b
        jr nz,header_invalid
        ld (0de94h),hl
        ld hl,(0e414h)
        ld (0dea4h),hl
        ld hl,(0e416h)
        ld (0dea6h),hl
        ld hl,(0e410h)        ; required stack: 256-byte units, max 2 KiB
        ld a,l
        or a
        jr nz,header_invalid
        ld a,h
        or a
        jr z,header_invalid
        cp 9
        jr nc,header_invalid
        ld (0deabh),hl         ; preserve before later records replace E400h
        ld hl,(0de92h)
        ld de,0100h
        add hl,de
        ex de,hl
        ld hl,(0e418h)         ; linked heap base, after image and BSS
        or a
        sbc hl,de
        jr c,header_invalid
        ld hl,(0e418h)
        ld (0deadh),hl
        ld hl,0c800h
        ld de,(0deabh)
        or a
        sbc hl,de
        ld de,(0deadh)
        or a
        sbc hl,de
        jr c,header_invalid
        jr z,header_invalid
        ld a,(0e40ch)
        ld b,a
        ld hl,0e422h          ; first export's two-byte entry offset
validate_exports:
        call validate_offset   ; adapter
        jr c,header_invalid
        call validate_offset   ; canonical signature
        jr c,header_invalid
        ld de,8
        add hl,de
        djnz validate_exports
        or a
        ret
validate_offset:
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        push hl
        ld hl,(0de92h)        ; image length, excluding 0100h origin
        or a
        sbc hl,de
        pop hl
        jr z,header_invalid   ; entry must be inside loaded image
        ret
header_invalid:
        scf
        ret
; Z when B bytes at HL match DE; HL and DE advance past the compared bytes.
match:
        ld a,(de)
        cp (hl)
        ret nz
        inc de
        inc hl
        djnz match
        ret
; Magic, format 1, ABI 1.0, call revision 1 and origin 0100h.
magic:  db 'B1LB',1,1,0,1,0,1

; The optional __brk and __hlimit offsets are supplied by DCC's L80C. An image
; without the DCC runtime uses FFFFh for both. With the runtime, the heap
; starts at the linked heap base and __hlimit is C800h minus the stack size.
initialize_runtime:
        ld hl,(0dea4h)
        ld a,h
        cp 0ffh
        jr nz,runtime_present
        ld a,l
        cp 0ffh
        jr nz,runtime_invalid
        ld hl,(0dea6h)
        ld a,h
        and l
        inc a
        ret z
        jr runtime_invalid
runtime_present:
        call valid_runtime_offset
        jr nc,runtime_invalid
        ld de,0100h
        add hl,de
        ld (0dea0h),hl
        ld hl,(0dea6h)
        call valid_runtime_offset
        jr nc,runtime_invalid
        ld de,0100h
        add hl,de
        ld (0dea2h),hl
        ld hl,(0dea0h)
        ld de,(0deadh)
        ld (hl),e
        inc hl
        ld (hl),d
        ld hl,0c800h
        ld de,(0deabh)
        or a
        sbc hl,de              ; heap ceiling and lowest valid stack address
        ex de,hl
        ld hl,(0dea2h)
        ld (hl),e
        inc hl
        ld (hl),d
        ld hl,(0de92h)
        ld de,0100h
        add hl,de
        ex de,hl
        ld hl,(0deadh)
        or a
        sbc hl,de
        ld b,h
        ld c,l
        ex de,hl
        ld a,b
        or c
        ret z
zero_bss:
        ld (hl),0
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,zero_bss
        or a
        ret
valid_runtime_offset:
        ld a,h
        and l
        inc a
        jr z,offset_wrapped    ; FFFFh would wrap to page zero below
        push hl
        inc hl
        ld de,(0de92h)
        or a
        sbc hl,de
        pop hl
        ret
offset_wrapped:
        or a                  ; valid_runtime_offset reports invalid with NC
        ret
runtime_invalid:
        scf
        ret

copy_chunk:
        ld hl,0e480h
        ld de,(0de9ah)
        or a
        sbc hl,de
        ld b,h
        ld c,l
        ld hl,(0de98h)
        or a
        sbc hl,bc
        jr nc,copy_count_ready
        ld bc,(0de98h)
copy_count_ready:
        ld (0de9ch),bc
        ld hl,(0de9ah)
        ld de,(0de96h)
        ldir
        ld (0de96h),de
        ld hl,(0de98h)
        ld bc,(0de9ch)
        or a
        sbc hl,bc
        ld (0de98h),hl
        ret
