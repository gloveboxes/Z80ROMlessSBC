CCP_BASE:       equ 0e300h
BDOS_ENTRY:     equ 0eb06h
BIOS_BASE:      equ 0f900h

TERM_DATA:      equ 00h
TERM_STATUS:    equ 01h
TERM_RX_READY:  equ 01h
TERM_TX_ROOM:   equ 02h

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
                jp console_output
punch_output_entry:
                jp console_output
reader_input_entry:
                jp console_input
                jp home
                jp select_disk
                jp set_track
                jp set_sector
                jp set_dma
                jp read_record
                jp write_record
                jp list_status
                jp translate_sector

cold_boot:
                ld sp,0080h
                xor a
                ld (0004h),a
                ld hl,banner
cold_boot_message:
                ld a,(hl)
                or a
                jr z,enter_cpm
                ld c,a
                call console_output
                inc hl
                jr cold_boot_message

warm_boot:
                ld sp,0080h
                call flush_disk
                or a
                jr nz,warm_boot
                xor a
                ld (disk_drive),a
                ld (disk_track),a
                ld (disk_track+1),a
                ld (disk_sector),a
                ld (disk_sector+1),a
                ld hl,CCP_BASE
                ld (dma_address),hl
                ld b,SYSTEM_RECORDS
warm_boot_loop:
                push bc
                call read_record
                pop bc
                or a
                jr nz,warm_boot
                ld hl,(dma_address)
                ld de,0080h
                add hl,de
                ld (dma_address),hl
                ld hl,(disk_sector)
                inc hl
                ld a,l
                cp 32
                jr c,warm_boot_next
                ld hl,0
                ld de,(disk_track)
                inc de
                ld (disk_track),de
warm_boot_next:
                ld (disk_sector),hl
                djnz warm_boot_loop

enter_cpm:
                ld a,0c3h
                ld (0000h),a
                ld (0005h),a
                ld hl,warm_boot_entry
                ld (0001h),hl
                ld hl,BDOS_ENTRY
                ld (0006h),hl
                ld bc,0080h
                call set_dma
                ld a,(0004h)
                ld c,a
                jp CCP_BASE+3

console_status:
                in a,(TERM_STATUS)
                and TERM_RX_READY
                ret z
                ld a,0ffh
                ret

console_input:
                call console_status
                jr z,console_input
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

list_status:
                in a,(TERM_STATUS)
                and TERM_TX_ROOM
                ret z
                ld a,0ffh
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
                bit 0,a
                jr z,disk_ready_wait
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
                push bc
                push de
                push hl
                call prepare_disk_io
                ld a,DISK_READ
                out (DISK_COMMAND),a
read_wait:
                in a,(DISK_COMMAND)
                bit 7,a
                jr nz,disk_io_error
                bit 1,a
                jr z,read_wait
                ld hl,(dma_address)
                ld b,128
                ld c,DISK_DATA
                inir
                xor a
                jr disk_io_done

write_record:
                push bc
                push de
                push hl
                call prepare_disk_io
                ld a,c
                cp 3
                jr c,write_command_valid
                xor a
write_command_valid:
                add a,DISK_WRITE_NORMAL
                out (DISK_COMMAND),a
write_room_wait:
                in a,(DISK_COMMAND)
                bit 7,a
                jr nz,disk_io_error
                bit 2,a
                jr z,write_room_wait
                ld hl,(dma_address)
                ld b,128
                ld c,DISK_DATA
                otir
write_done_wait:
                in a,(DISK_COMMAND)
                bit 7,a
                jr nz,disk_io_error
                bit 0,a
                jr z,write_done_wait
                xor a
                jr disk_io_done

flush_disk:
                ld a,DISK_FLUSH
                out (DISK_COMMAND),a
flush_wait:
                in a,(DISK_COMMAND)
                bit 7,a
                jr nz,flush_error
                bit 0,a
                jr z,flush_wait
                xor a
                ret
flush_error:
                ld a,1
                ret

disk_io_error:
                ld a,1
disk_io_done:
                pop hl
                pop de
                pop bc
                ret

banner:
                db 13,10,10,"64K CP/M 2.2 - Z80 ROMless SBC",13,10,0

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

directory_buffer:
                defs 128
allocation_0:
                defs 20
allocation_1:
                defs 20
allocation_2:
                defs 20
allocation_3:
                defs 20

BIOS_END:      equ $