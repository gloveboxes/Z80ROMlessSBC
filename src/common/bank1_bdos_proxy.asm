; E=6 service at DE00h, entered from the bank-0 shim through the gateway with
; C=function, HL=argument and IY=DMA. B is nonzero when IY is the caller's
; bank-0 DMA: the BIOS then moves data records directly to or from bank 0.
; Otherwise IY is E400h, where a search returns its directory record. FCB
; arguments are staged at E480h. HL returns the BDOS result (A=L, B=H).
; BDOS and allocation vectors live only in bank 1. This proxy preserves the
; bank-1 DMA setting across each request.
BANK0_RECORDS: equ 0fc37h      ; BIOS flag in bank 1's copy of the BIOS
        org 0de00h
bank1_bdos_proxy:
        ld a,c
        ld (0e4b0h),a
        ld (0e4b2h),hl
        ld a,b
        ld (BANK0_RECORDS),a
        ld hl,(0faf1h)         ; translated CP/M A_F8B1 DMA pointer
        ld (0e4c0h),hl
        ld (0faf1h),iy         ; avoid BDOS 26 changing search continuation
        push iy
        pop bc
        call 0fc24h            ; BIOS SETDMA
        ld a,(0e4b0h)
        cp 18
        jr nz,proxy_call
        ld a,(0deaah)
        or a
        jr z,proxy_call
        ld hl,0df00h
        call 0e789h             ; restore search, not shared drive geometry
proxy_call:
        ld a,(0e4b0h)
        ld c,a
        ld de,(0e4b2h)
        call 0005h
        ld (0e4b8h),hl
        xor a
        ld (BANK0_RECORDS),a
        ld a,(0e4b0h)
        cp 17
        jr z,proxy_save_search
        cp 18
        jr nz,proxy_after_search
proxy_save_search:
        ld de,0df00h
        call 0e78ch
        ld a,(0e4b8h)
        inc a
        ld a,0
        jr z,proxy_search_flag
        inc a
proxy_search_flag:
        ld (0deaah),a
proxy_after_search:
        ld a,(0e4b0h)
        cp 13
        jr nz,proxy_restore_dma
        ld a,1
        ld (0dea9h),a
        xor a
        ld (0deaah),a
proxy_restore_dma:
        ld hl,(0e4c0h)
        ld (0faf1h),hl
        ld b,h
        ld c,l
        call 0fc24h
        ld hl,(0e4b8h)
        ret
