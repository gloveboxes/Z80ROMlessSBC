                org 0000h
                ld sp,0200h
                xor a
                ld (drive),a
next_drive:
                ld a,(drive)
                out (11h),a
                ld de,0
next_record:
                ld a,e
                out (12h),a
                ld a,d
                out (13h),a
                ld a,1
                out (10h),a
wait_record:
                in a,(10h)
                bit 7,a
                jr nz,failed
                bit 1,a
                jr z,wait_record
                ld hl,0100h
                ld bc,8014h
                inir
                ld hl,0100h
                ld bc,8006h
                otir
                inc de
                ld a,d
                cp 10h
                jr nz,next_record
                ld a,(drive)
                inc a
                ld (drive),a
                cp 4
                jr nz,next_drive
                ld a,05ah
                out (05h),a
                halt
failed:
                ld a,0e1h
                out (05h),a
                halt
drive:          db 0
