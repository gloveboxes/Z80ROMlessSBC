; E=7 service at DE00h. The bank-0 shim stages request at E4B0h, a 36-byte
; FCB at E480h and DMA at E400h. BDOS and allocation vectors live only in
; bank 1. This proxy preserves the caller's DMA setting across each request.
        org 0de00h
bank1_bdos_proxy:
        ld hl,(0faf1h)         ; translated CP/M A_F8B1 DMA pointer
        ld (0e4c0h),hl
        ld hl,0e400h
        ld (0faf1h),hl         ; avoid BDOS 26 changing search continuation
        ld bc,0e400h
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
        ld (0e4b6h),a
        ld a,b
        ld (0e4b7h),a
        ld (0e4b8h),hl
        ld a,(0e4b0h)
        cp 17
        jr z,proxy_save_search
        cp 18
        jr nz,proxy_after_search
proxy_save_search:
        ld de,0df00h
        call 0e78ch
        ld a,(0e4b6h)
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
