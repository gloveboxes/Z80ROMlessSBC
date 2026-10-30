CCP_BASE:       equ 0e600h
BDOS_ENTRY:     equ 0ee06h
BIOS_BASE:      equ 0fc00h

TERM_DATA:      equ 00h
TERM_STATUS:    equ 01h
TERM_RX_READY:  equ 01h
TERM_TX_ROOM:   equ 02h
BOOT_REPORT:    equ 05h

DISK_COMMAND:   equ 10h
DISK_DRIVE_PORT: equ 11h
DISK_LBA_LOW:   equ 12h
DISK_LBA_HIGH:  equ 13h
DISK_DATA:      equ 14h
DISK_READ:      equ 01h
DISK_WRITE_NORMAL: equ 02h
DISK_WRITE_DIRECTORY: equ 03h
DISK_WRITE_UNALLOCATED: equ 04h
DISK_FLUSH:     equ 05h
DISK_READY:     equ 01h
DISK_DATA_READY: equ 02h
DISK_DATA_ROOM: equ 04h
DISK_ERROR:     equ 80h

                org BIOS_BASE

                jp cold_boot
warm_boot_entry:
                jp warm_boot
                jp console_status
                jp console_input
                jp console_output
list_output_entry:
                jp unused_output
punch_output_entry:
                jp unused_output
reader_input_entry:
                jp unused_input
                jp home
                jp select_disk
                jp set_track
                jp set_sector
                jp set_dma
                jp read_record
                jp write_record
                jp list_status
                jp translate_sector
                db "ZAR1"

cold_boot:
                ld a,0b0h
                out (BOOT_REPORT),a
                ld sp,0080h
                ld hl,0
                ld (0003h),hl
                ld hl,banner
                call print_string
                ld a,1
                jr enter_cpm

warm_boot:
                ; This BIOS is mirrored in both banks. A BDOS exit can enter
                ; here with bank 1 selected, but the CCP lives only in bank 0.
                xor a
                out (7),a
                ld sp,0080h
                call flush_disk
                or a
                jr nz,warm_boot
                ; Successful flush already leaves A=0.
                ld (disk_drive),a
                ld h,a
                ld l,a
                ld (disk_track),hl
                ld (disk_sector),hl
                ld hl,CCP_BASE
                ld (dma_address),hl
                ld de,0080h
                ld b,SYSTEM_RECORDS
warm_boot_loop:
                call read_record
                or a
                jr nz,warm_boot
                ld hl,(dma_address)
                add hl,de
                ld (dma_address),hl
                ; Internal reload uses track 0 plus a linear record index.
                ; prepare_disk_io accepts indices across the track boundary;
                ; SYSTEM_RECORDS is below 256, so only the low byte changes.
                ld hl,disk_sector
                inc (hl)
                djnz warm_boot_loop
                ld a,2

enter_cpm:
                push af
                ld a,0c3h
                ld (0000h),a
                ld (0005h),a
                ld hl,warm_boot_entry
                ld (0001h),hl
                ld hl,BDOS_ENTRY
                ld (0006h),hl
                pop af
                call load_autorun
                ld hl,0080h
                ld (dma_address),hl
                ld a,(0004h)
                ld c,a
                ld a,0c0h
                out (BOOT_REPORT),a
                jp CCP_BASE

load_autorun:
                ld e,a
                xor a
                ld (CCP_BASE+7),a
                ld (disk_drive),a
autorun_read:
                ld hl,AUTORUN_TRACK
                ld (disk_track),hl
                ld hl,AUTORUN_SECTOR
                ld (disk_sector),hl
                ld hl,directory_buffer
                ld (dma_address),hl
                call read_record
                or a
                jr nz,autorun_error
                ld a,(directory_buffer)
                cp 0e5h
                ret z
                ld hl,directory_buffer
                ld b,128
                xor a
autorun_checksum:
                add a,(hl)
                inc hl
                djnz autorun_checksum
                or a
                jr nz,autorun_error
                ld hl,(directory_buffer)
                ld bc,AUTORUN_MAGIC_LOW
                or a
                sbc hl,bc
                jr nz,autorun_error
                ld hl,(directory_buffer+2)
                ld bc,AUTORUN_MAGIC_HIGH
                or a
                sbc hl,bc
                jr nz,autorun_error
                ld a,(directory_buffer+4)
                cp 4
                jr nc,autorun_error
                ld c,a
                ld a,(directory_buffer+5)
                cp AUTORUN_MAX_COMMAND+1
                jr nc,autorun_error
                ld a,c
                and e
                ret z
                call console_status
                or a
                jr z,autorun_command
                call console_input
                ret
autorun_command:
                ld a,(directory_buffer+5)
                or a
                jr z,autorun_error
                ld (CCP_BASE+7),a
                ld c,a
                ld b,0
                ld hl,directory_buffer+6
                ld de,CCP_BASE+8
                ldir
                ld a,b
                ld (de),a
                ret
autorun_error:
                ld hl,autorun_message
print_string:
                ld a,(hl)
                or a
                ret z
                ld c,a
                call console_output
                inc hl
                jr print_string
autorun_message:
                db 13,10,"AUTORUN config error",13,10,0

console_status:
                in a,(TERM_STATUS)
                rrca
                sbc a,a
                ret

console_input:
                in a,(TERM_STATUS)
                rrca
                jr nc,console_input
                in a,(TERM_DATA)
                and 7fh
                ret

console_output:
                in a,(TERM_STATUS)
                and TERM_TX_ROOM
                jr z,console_output
                ld a,c
                out (TERM_DATA),a
                ret

unused_output:
                ret

unused_input:
                ld a,1ah
                ret

list_status:
                xor a
                ret

home:
                ld bc,0

set_track:
                ld (disk_track),bc
                ret

set_sector:
                ld (disk_sector),bc
                ret

set_dma:
                ld (dma_address),bc
                ret

select_disk:
                ld hl,0
                ld a,c
                cp DRIVE_COUNT
                ret nc
                ld (disk_drive),a
                rlca
                rlca
                rlca
                rlca
                ld e,a
                ld d,0
                ld hl,disk_parameter_headers
                add hl,de
                ret

translate_sector:
                ld h,b
                ld l,c
                ret

prepare_disk_io:
disk_ready_wait:
                in a,(DISK_COMMAND)
                rrca
                jr nc,disk_ready_wait
                ld a,(disk_drive)
                out (DISK_DRIVE_PORT),a
                ld hl,(disk_track)
                add hl,hl
                add hl,hl
                add hl,hl
                add hl,hl
                add hl,hl
                ld de,(disk_sector)
                add hl,de
                ld a,l
                out (DISK_LBA_LOW),a
                ld a,h
                out (DISK_LBA_HIGH),a
                ret

read_record:
                ; Interrupts are never enabled anywhere in this system (BIOS
                ; or CP/M), so the alternate register set is never touched by
                ; anything else. Banking BC/DE/HL out with EXX preserves the
                ; caller's values across this routine in one instruction each
                ; way, replacing three PUSHes and three POPs.
                exx
                call prepare_disk_io
                ld a,DISK_READ
                out (DISK_COMMAND),a
read_wait:
                in a,(DISK_COMMAND)
                rlca
                jr c,disk_io_error
                bit 2,a
                jr z,read_wait
                ld hl,(dma_address)
                ld bc,128*256+DISK_DATA
                inir
                xor a
                jr disk_io_done

write_record:
                ; C selects DISK_WRITE_NORMAL/DIRECTORY/UNALLOCATED and must
                ; be read from the caller's BC before EXX banks it away;
                ; stash the computed command byte in memory since A is not
                ; preserved across the prepare_disk_io call that follows.
                ld a,c
                add a,DISK_WRITE_NORMAL
                ld (write_command),a
                exx
                call prepare_disk_io
                ld a,(write_command)
                out (DISK_COMMAND),a
write_room_wait:
                in a,(DISK_COMMAND)
                rlca
                jr c,disk_io_error
                bit 3,a
                jr z,write_room_wait
                ld hl,(dma_address)
                ld bc,128*256+DISK_DATA
                otir
write_done_wait:
                in a,(DISK_COMMAND)
                rlca
                jr c,disk_io_error
                bit 1,a
                jr z,write_done_wait
                xor a
                jr disk_io_done

flush_disk:
                ld a,DISK_FLUSH
                out (DISK_COMMAND),a
flush_wait:
                in a,(DISK_COMMAND)
                rlca
                jr c,flush_error
                bit 1,a
                jr z,flush_wait
                xor a
                ret
flush_error:
                ld a,1
                ret

disk_io_error:
                ld a,1
disk_io_done:
                exx
                ret

banner:
                db 13,10,10,"64K CP/M 2.2",13,10,0

disk_parameter_headers:
                dw 0,0,0,0,directory_buffer,disk_parameter_block,0,allocation_0
                dw 0,0,0,0,directory_buffer,disk_parameter_block,0,allocation_1
                dw 0,0,0,0,directory_buffer,disk_parameter_block,0,allocation_2
                dw 0,0,0,0,directory_buffer,disk_parameter_block,0,allocation_3

disk_parameter_block:
                dw DPB_SPT
                db DPB_BSH,DPB_BLM,DPB_EXM
                dw DPB_DSM
                dw DPB_DRM
                db DPB_AL0,DPB_AL1
                dw DPB_CKS
                dw DPB_OFF

disk_drive:
                db 0
disk_track:
                dw 0
disk_sector:
                dw 0
dma_address:
                dw 0080h
write_command:
                db 0

directory_buffer: equ BIOS_DIRECTORY_BUFFER
allocation_0:
                defs DPB_ALLOCATION_BYTES
allocation_1:
                defs DPB_ALLOCATION_BYTES
allocation_2:
                defs DPB_ALLOCATION_BYTES
allocation_3:
                defs DPB_ALLOCATION_BYTES

BIOS_CORE_END: equ $
                defs 0ff00h-$,0
                include "banked_transfer_v2.asm"
                defs 10000h-$,0
BIOS_END:      equ $
