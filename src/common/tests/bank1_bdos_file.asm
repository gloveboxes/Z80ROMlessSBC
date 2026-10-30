; Executed through the pinned bank-1 library slot to prove BDOS disk access.
        org 0100h
        ld c,13                 ; initialize bank-1 disk system
        call 0005h
        ld de,file_control_block
        ld c,15                 ; open AUTORUN.COM on drive A
        call 0005h
        cp 0ffh
        jr z,failed
        ld de,0200h
        ld c,26                 ; DMA in bank 1
        call 0005h
        ld de,file_control_block
        ld c,20                 ; read first record
        call 0005h
        ld l,a
        ld h,0
        ret
failed:
        ld hl,0ffffh
        ret
file_control_block:
        db 1,"AUTORUN COM"
        defs 24,0
