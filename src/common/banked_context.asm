; Context hooks. DMA/search are per domain; drive/user stay shared.
; Private snapshots pack DMA (2 bytes) and actual search state (1+3+3).
        org 0e780h
        jp enter_context
        jp leave_context
        jp 0edc0h
        jp restore_search
        jp save_search
enter_context:
        push hl
        push bc
        push de
        ld de,0df60h
        call save_context
        ld hl,0df80h
        call restore_context
        pop de
        pop bc
        pop hl
        ret
leave_context:
        push hl
        push bc
        push de
        ld de,0df80h
        call save_context
        ld hl,0df60h
        call restore_context
        pop de
        pop bc
        pop hl
        ret
save_context:
        ld hl,0faf1h
        ldi
        ldi
save_search:
        ld hl,0fb14h
        ldi
        ld hl,0fb18h
        ld bc,3
        ldir
        ld hl,0fb29h
        ld c,3
        ldir
        ret
restore_context:
        ld c,(hl)
        inc hl
        ld b,(hl)
        inc hl
        push hl
        ld (0faf1h),bc
        call 0fc24h
        pop hl
        jp restore_search
restore_search:
        ld de,0fb14h
        ldi
        ld de,0fb18h
        ld bc,3
        ldir
        ld de,0fb29h
        ld c,3
        ldir
        ret
