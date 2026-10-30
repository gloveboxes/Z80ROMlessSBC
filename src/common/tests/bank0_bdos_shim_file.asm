; Execute in bank 0 after installing the BDOS shim. Bank 1 owns the file read.
        org 1000h
        ld c,13
        call 0005h
        ld de,file_control_block
        ld c,15
        call 0005h
        cp 0ffh
        jp z,failed
        ld de,0180h
        ld c,26
        call 0005h
        ld de,file_control_block
        ld c,20
        call 0005h
        ld l,a
        ld h,0
        jp 1100h
failed:
        ld hl,0ffffh
        jp 1100h
file_control_block:
        db 1,"AUTORUN COM"
        defs 24,0
