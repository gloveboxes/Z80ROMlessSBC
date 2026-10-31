; Bank-0 CP/M 2.2 entry. Bank 1 owns the BDOS disk state.
; The bank-0 shim keeps console calls local and copies FCB/DMA arguments
; through the mirrored bank transfer API before invoking HEAP operation 7.
; It occupies the old EE00h..FB3Fh BDOS slot; FB80h..FBFFh stays reserved
; for the BIOS directory buffer.
; CCP's stack starts at ED3Eh, just above executable CCP bytes. Use a private
; bank-0 stack for every shim call before any PUSH or nested bank transfer.
        org 0ee00h
        db 0,22,0,0,0,0     ; CCP validates this BDOS signature after DIR
bdos_entry:
        ld (caller_sp),sp
        ld sp,0f800h
        push ix
        push iy
        ld a,c
        ld (function_number),a
        ld (original_arg),de
        or a
        jp z,bdos_warm_boot
        cp 13
        jp c,console_dispatch
        cp 26
        jr z,set_dma
        call forward_request
        jp c,forward_error
        ld a,(function_number)
        cp 13
        jr nz,forward_result_ready
        ld hl,0080h
        ld (base_dma),hl
forward_result_ready:
        cp 27
        jp z,return_alloc_vector
        cp 31
        jp z,return_disk_params
        call fcb_copy_back_if_needed
        jp c,forward_error
        call dma_copy_back_if_needed
        jp c,forward_error
        ld a,(response+7)
        ld b,a
        ld hl,(response+8)
        ld a,(response+6)
        pop iy
        pop ix
        ld sp,(caller_sp)
        or a
        ret
forward_error:
        ld hl,0ffffh
        ld a,0ffh
        pop iy
        pop ix
        ld sp,(caller_sp)
        or a
        ret
bdos_warm_boot:
        jp 0000h
set_dma:
        ld (base_dma),de
        xor a
        ld h,a
        ld l,a
        pop iy
        pop ix
        ld sp,(caller_sp)
        ret

; Carry indicates a failed bank transfer. Request and response are 16 bytes.
forward_request:
        ld a,(function_number)
        ld (response),a
        ld de,(original_arg)
        ld (response+2),de
        call fcb_copy_in_if_needed
        ret c
        call dma_copy_in_if_needed
        ret c
        ld hl,response
        ld de,0e4b0h
        ld bc,16
        call 0ff03h
        ret c
        ld e,6
        ld hl,0e4b0h
        call 0ff10h
        ld hl,response
        ld de,0e4b0h
        ld bc,16
        call 0ff00h
        ret

; The FCB lives in the caller's bank. Stage 36 bytes into a padded 48-byte
; bank-0 buffer, then copy that complete buffer to the stable bank-1 FCB.
fcb_copy_in_if_needed:
        call is_fcb_function
        ret nc
        ld hl,(original_arg)
        ld de,fcb_buffer
        ld bc,36
        ldir
        xor a
        ld hl,fcb_buffer+36
        ld (hl),a
        ld de,fcb_buffer+37
        ld bc,11
        ldir
        ld hl,fcb_buffer
        ld de,0e480h
        ld bc,48
        call 0ff03h
        ret c
        ld hl,0e480h
        ld (response+2),hl
        or a
        ret
fcb_copy_back_if_needed:
        call is_fcb_function
        ret nc
        ld hl,fcb_buffer
        ld de,0e480h
        ld bc,48
        call 0ff00h
        ret c
        ld hl,fcb_buffer
        ld de,(original_arg)
        ld bc,36
        ldir
        or a
        ret

; Function numbers with DE pointing to a 36-byte FCB. Search-next uses
; the previous stable bank-1 FCB and does not take a new pointer.
is_fcb_function:
        ld a,(function_number)
        cp 15
        jr c,no_fcb
        cp 18
        jr c,yes_fcb
        cp 19
        jr z,yes_fcb
        cp 20
        jr c,no_fcb
        cp 24
        jr c,yes_fcb
        cp 30
        jr z,yes_fcb
        cp 33
        jr c,no_fcb
        cp 37
        jr c,yes_fcb
        cp 40
        jr z,yes_fcb
no_fcb:
        or a
        ret
yes_fcb:
        scf
        ret

dma_copy_in_if_needed:
        ld a,(function_number)
        cp 21
        jr z,copy_dma_in
        cp 34
        jr z,copy_dma_in
        cp 40
        jr nz,no_dma_in
copy_dma_in:
        ld hl,(base_dma)
        ld de,dma_buffer
        ld bc,128
        ldir
        ld hl,dma_buffer
        ld de,0e400h
        ld bc,128
        call 0ff03h
        ret
no_dma_in:
        or a
        ret
dma_copy_back_if_needed:
        ld a,(function_number)
        cp 17
        jr z,copy_dma_out
        cp 18
        jr z,copy_dma_out
        cp 20
        jr z,copy_dma_out
        cp 33
        jr nz,no_dma_out
copy_dma_out:
        ld hl,dma_buffer
        ld de,0e400h
        ld bc,128
        call 0ff00h
        ret c
        ld hl,dma_buffer
        ld de,(base_dma)
        ld bc,128
        ldir
        or a
        ret
no_dma_out:
        or a
        ret

; BDOS functions 27 and 31 return pointers into bank-1 resident data.
; Mirror their public byte ranges into stable bank-0 storage.
return_alloc_vector:
        ld hl,alloc_vector_mirror
        ld de,(response+8)
        ld bc,32
        call 0ff00h
        jp c,forward_error
        ld hl,alloc_vector_mirror
        ld b,h
        ld a,l
        pop iy
        pop ix
        ld sp,(caller_sp)
        ret
return_disk_params:
        ld hl,disk_params_mirror
        ld de,(response+8)
        ld bc,16
        call 0ff00h
        jp c,forward_error
        ld hl,disk_params_mirror
        ld b,h
        ld a,l
        pop iy
        pop ix
        ld sp,(caller_sp)
        ret

console_dispatch:
        ld a,(function_number)
        cp 1
        jr z,console_read
        cp 2
        jr z,console_write
        cp 3
        jr z,reader_input
        cp 6
        jr z,console_direct
        cp 7
        jr z,read_io_byte
        cp 8
        jr z,write_io_byte
        cp 9
        jr z,print_string
        cp 10
        jr z,read_line
        cp 11
        jp z,console_poll
        cp 12
        jp z,version
        xor a
        jp console_return
console_read:
        call 0fc09h
        ld c,a
        call 0fc0ch
        ld a,c
        jp console_return
console_write:
        ld c,e
        call 0fc0ch
        ld a,e
        jp console_return
reader_input:
        call 0fc15h
        jp console_return
console_direct:
        ld a,e
        cp 0ffh
        jr z,console_direct_read
        jr console_write
console_direct_read:
        call 0fc06h
        or a
        jp z,console_return
        call 0fc09h
        jp console_return
read_io_byte:
        ld a,(0003h)
        jp console_return
write_io_byte:
        ld a,e
        ld (0003h),a
        xor a
        jp console_return
print_string:
        ld hl,(original_arg)
print_next:
        ld a,(hl)
        cp '$'
        jr z,print_done
        ld c,a
        push hl
        call 0fc0ch
        pop hl
        inc hl
        jr print_next
print_done:
        xor a
        jp console_return
read_line:
        ld hl,(original_arg)
        ld b,(hl)             ; maximum characters
        inc hl
        ld (line_count_address),hl
        inc hl
        ld (line_cursor),hl
        ld c,0
read_line_next:
        call 0fc09h
        cp 0dh
        jr z,read_line_done
        cp 08h
        jr z,read_line_backspace
        cp 07fh
        jr z,read_line_backspace
        ld d,a
        ld a,c
        cp b
        jr nc,read_line_next
        ld hl,(line_cursor)
        ld (hl),d
        inc hl
        ld (line_cursor),hl
        inc c
        push bc
        ld c,d
        call 0fc0ch
        pop bc
        jr read_line_next
read_line_backspace:
        ld a,c
        or a
        jr z,read_line_next
        dec c
        ld hl,(line_cursor)
        dec hl
        ld (line_cursor),hl
        push bc
        ld c,08h
        call 0fc0ch
        ld c,' '
        call 0fc0ch
        ld c,08h
        call 0fc0ch
        pop bc
        jr read_line_next
read_line_done:
        ld hl,(line_count_address)
        ld (hl),c
        ld c,0dh
        call 0fc0ch
        xor a
        jp console_return
console_poll:
        call 0fc06h
        jp console_return
version:
        ld a,022h
console_return:
        ld l,a
        ld h,0
        ld b,0
        pop iy
        pop ix
        ld sp,(caller_sp)
        or a
        ret

function_number: db 0
original_arg: dw 0
base_dma: dw 0080h
line_count_address: dw 0
line_cursor: dw 0
caller_sp: dw 0
        defs 0f400h-$,0
dma_buffer: defs 128,0
fcb_buffer: defs 48,0
response: defs 16,0
        defs 0f500h-$,0
alloc_vector_mirror: defs 32,0
disk_params_mirror: defs 16,0
        defs 0f600h-$,0
private_stack: defs 512,0
; Public ABI 1.0 byte transfers. Keep the 16-byte gateway as the bulk path;
; a stack-local tail buffer preserves neighbouring bytes at either boundary.
byte_read_entry:
        jp byte_read
byte_write_entry:
        jp byte_write
        db "BXT1"
byte_read:
        ld a,c
        and 15
        jp z,0ff00h
        xor a
        jr byte_transfer
byte_write:
        ld a,c
        and 15
        jp z,0ff03h
        ld a,1
byte_transfer:
        push ix
        push iy
        push af
        ld a,i
        di
        push af
        ld ix,0
        add ix,sp
        push hl
        push de
        push bc
        push bc
        ld hl,-16
        add hl,sp
        ld sp,hl
        ld a,b
        or c
        jp z,byte_success
        ld l,(ix-2)
        ld h,(ix-1)
        ld a,h
        or a
        jp z,byte_invalid
        add hl,bc
        jp c,byte_invalid
        inc h
        jr nz,byte_base_valid
        ld a,l
        or a
        jp nz,byte_invalid
byte_base_valid:
        ex de,hl
        add hl,bc
        jp c,byte_invalid
        inc h
        jr nz,byte_ranges_valid
        ld a,l
        or a
        jp nz,byte_invalid
byte_ranges_valid:
        ld a,c
        and 0f0h
        ld c,a
        or b
        jr z,byte_tail
        ld l,(ix-2)
        ld h,(ix-1)
        ld e,(ix-4)
        ld d,(ix-3)
        ld a,(ix+3)
        or a
        jr nz,byte_bulk_write
        call 0ff00h
        jr byte_bulk_done
byte_bulk_write:
        call 0ff03h
byte_bulk_done:
        jp c,byte_invalid
        ld e,(ix-6)
        ld d,(ix-5)
        ld a,e
        and 0f0h
        ld e,a
        ld l,(ix-2)
        ld h,(ix-1)
        add hl,de
        ld (ix-2),l
        ld (ix-1),h
        ld l,(ix-4)
        ld h,(ix-3)
        add hl,de
        ld (ix-4),l
        ld (ix-3),h
byte_tail:
        ld a,(ix-6)
        and 15
        jp z,byte_success
        ld (ix-7),a
        xor a
        ld (ix-8),a
        ld e,(ix-4)
        ld d,(ix-3)
        ld a,d
        cp 0feh
        jr nz,byte_tail_address
        ld a,e
        sub 0f0h
        jr c,byte_tail_address
        ld (ix-8),a
        ld e,0f0h
        ld (ix-4),e
byte_tail_address:
        push ix
        pop hl
        ld bc,-24
        add hl,bc
        push hl
        pop iy
        ld bc,16
        call 0ff00h
        jp c,byte_invalid
        push iy
        pop hl
        ld e,(ix-8)
        ld d,0
        add hl,de
        ld e,(ix-2)
        ld d,(ix-1)
        ld c,(ix-7)
        ld b,0
        ld a,(ix+3)
        or a
        jr nz,byte_tail_write
        ldir
        jr byte_success
byte_tail_write:
        ex de,hl
        ldir
        push iy
        pop hl
        ld e,(ix-4)
        ld d,(ix-3)
        ld bc,16
        call 0ff03h
        jr c,byte_invalid
byte_success:
        xor a
        jr byte_finish
byte_invalid:
        ld a,2
byte_finish:
        ld e,a
        ld sp,ix
        pop af
        pop bc
        pop iy
        pop ix
        jp po,byte_interrupts_off
        ld a,e
        or a
        jr z,byte_enable
        scf
byte_enable:
        ei
        ret
byte_interrupts_off:
        ld a,e
        or a
        ret z
        scf
        ret
        defs 0fa00h-$,0
; FF06h delegates here so the mirrored page can spend its budget on copying.
; INFO never switches banks.
bank_info:
        in a,(7)
        cp 0b4h
        jr nz,bank_unavailable
        ld hl,0100h
        ld de,0c800h
        ld bc,01600h
        xor a
        ret
bank_unavailable:
        ld hl,0
        ld a,1
        scf
        ret
        defs 0fb40h-$,0
