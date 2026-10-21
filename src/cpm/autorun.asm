                org 0100h

                ld (saved_sp),sp
                ld sp,stack_top
                ld hl,(0001h)
                ld de,NATIVE_WBOOT
                or a
                sbc hl,de
                jp nz,wrong_bios
                ld hl,AUTORUN_BIOS_SIGNATURE
                ld de,magic
                ld b,4
check_bios:
                ld a,(de)
                cp (hl)
                jp nz,wrong_bios
                inc hl
                inc de
                djnz check_bios
                ld c,25
                call 5
                ld (saved_drive),a
                ld bc,0
                ld de,24
                call bios_call
                ld a,h
                or l
                jp z,read_error
                ld bc,config
                call position_record
                ld de,36
                call bios_call
                or a
                jp nz,read_error
                call validate_config
                jr z,show_config
                ld de,invalid_message
                call print
                ld hl,config
                ld de,config+1
                ld bc,127
                ld (hl),0
                ldir
                ld hl,magic
                ld de,config
                ld bc,4
                ldir
show_config:
                ld de,title
                call print
                ld a,(config+4)
                or a
                jr nz,show_mode
                ld a,4
show_mode:
                add a,'0'
                ld e,a
                ld c,2
                call 5
                ld de,command_message
                call print
                ld a,(config+5)
                or a
                jr z,show_menu
                ld b,a
                ld hl,config+6
show_command:
                push bc
                push hl
                ld e,(hl)
                ld c,2
                call 5
                pop hl
                pop bc
                inc hl
                djnz show_command
show_menu:
                ld de,menu
                call print
                call input_line
                ld a,(input+1)
                or a
                jr z,keep_mode
                cp 1
                jp nz,no_changes
                ld a,(input+2)
                cp '1'
                jp c,no_changes
                cp '5'
                jp nc,no_changes
                sub '0'
                and 3
                jr set_mode
keep_mode:
                ld a,(config+4)
set_mode:
                ld (config+4),a
                or a
                jr z,save_config
                ld de,new_command_message
                call print
                call input_line
                ld a,(input+1)
                or a
                jr z,check_command
                ld (config+5),a
                ld b,a
                ld hl,input+2
                ld de,config+6
copy_command:
                ld a,(hl)
                cp 20h
                jp c,bad_command
                cp 7fh
                jp nc,bad_command
                cp 'a'
                jr c,store_character
                cp 'z'+1
                jr nc,store_character
                and 5fh
store_character:
                ld (de),a
                inc hl
                inc de
                djnz copy_command
check_command:
                ld a,(config+5)
                or a
                jp z,bad_command
save_config:
                ld hl,config
                ld b,127
                xor a
sum_config:
                add a,(hl)
                inc hl
                djnz sum_config
                neg
                ld (hl),a
                ld bc,config
                call position_record
                ld c,1
                ld de,39
                call bios_call
                or a
                jp nz,write_error
                ld bc,verify_buffer
                call position_record
                ld de,36
                call bios_call
                or a
                jp nz,write_error
                ld hl,config
                ld de,verify_buffer
                ld b,128
verify_config:
                ld a,(de)
                cp (hl)
                jp nz,write_error
                inc hl
                inc de
                djnz verify_config
                ld de,saved_message
                jp finish

position_record:
                ld de,33
                call bios_call
                ld bc,AUTORUN_TRACK
                ld de,27
                call bios_call
                ld bc,AUTORUN_SECTOR
                ld de,30
                jp bios_call

bios_call:
                ld hl,(0001h)
                add hl,de
                jp (hl)

validate_config:
                ld hl,config
                ld b,128
                xor a
validate_sum:
                add a,(hl)
                inc hl
                djnz validate_sum
                or a
                ret nz
                ld hl,config
                ld de,magic
                ld b,4
validate_magic:
                ld a,(de)
                cp (hl)
                ret nz
                inc hl
                inc de
                djnz validate_magic
                ld a,(hl)
                cp 4
                jr nc,invalid_config
                ld c,a
                inc hl
                ld a,(hl)
                cp AUTORUN_MAX_COMMAND+1
                jr nc,invalid_config
                or a
                jr nz,validate_length
                ld a,c
                or a
                ret z
                jr invalid_config
validate_length:
                ld b,a
validate_text:
                inc hl
                ld a,(hl)
                cp 20h
                jr c,invalid_config
                cp 7fh
                jr nc,invalid_config
                djnz validate_text
                xor a
                ret
invalid_config:
                ld a,1
                or a
                ret

input_line:
                ld a,AUTORUN_MAX_COMMAND
                ld (input),a
                ld de,input
                ld c,10
                jp 5
print:
                ld c,9
                jp 5
wrong_bios:
                ld de,bios_message
                call print
                jr return_to_cpm
no_changes:
                ld de,no_changes_message
                jr finish
bad_command:
                ld de,bad_command_message
                jr finish
read_error:
                ld de,read_message
                jr finish
write_error:
                ld de,write_message
finish:
                call print
                ld a,(saved_drive)
                ld c,a
                ld b,0
                ld de,24
                call bios_call
                ld bc,0080h
                ld de,33
                call bios_call
return_to_cpm:
                ld sp,(saved_sp)
                ret

magic:          db "ZAR1"
title:          db 13,10,"*** Native CP/M Auto-Run (drive A) ***",13,10
                db "Current option: $"
command_message:
                db 13,10,"Current command: $"
menu:           db 13,10,10
                db "1) Cold boot only",13,10
                db "2) Warm boot only",13,10
                db "3) Cold and warm boot",13,10
                db "4) Disable",13,10
                db "Q) Quit without saving",13,10
                db "New option (RETURN keeps current): $"
new_command_message:
                db 13,10,"New command (RETURN keeps current): $"
saved_message:
                db 13,10,"Configuration saved and verified.",13,10
                db "Press a key during boot to bypass auto-run.",13,10,"$"
no_changes_message:
                db 13,10,"No changes saved.",13,10,"$"
bad_command_message:
                db 13,10,"A printable command of 1-80 characters is required."
                db 13,10,"No changes saved.",13,10,"$"
invalid_message:
                db 13,10,"Invalid configuration; choose an option to replace it."
                db 13,10,"$"
bios_message:
                db 13,10,"This AUTORUN requires the native ZAR1 BIOS.",13,10,"$"
read_message:
                db 13,10,"Unable to read drive A configuration.",13,10,"$"
write_message:
                db 13,10,"Configuration write or verification failed.",13,10,"$"
saved_sp:       dw 0
saved_drive:    db 0
config:         defs 128
verify_buffer:  defs 128
input:          defs AUTORUN_MAX_COMMAND+2
                defs 128
stack_top:      equ $
