; BIOS bank extension ABI 2.4: identical page at FF00h in both banks.
; Public READ=FF00h / WRITE=FF03h: HL=base, DE=bank offset, BC=byte count.
; Zero count succeeds. Positive counts must be multiples of 16, base >=0100h,
; and both ranges must end <=FF00h without wrapping. INFO=FF06h must succeed
; first; data calls trust that capability and avoid a trapped status read.
; A=0/carry clear succeeds, A=2/carry set rejects invalid data arguments.
; INFO: HL=0204h version, DE=0800h first allocation, BC=F700h capacity;
; A=0/carry clear ready, A=1/HL=0/carry set unavailable.
; PRIVATE base-only workspace (not part of the public ABI):
; FFF0h = initial base address, then advancing source address,
; FFF2h = initial bank address,
; FFF4h = byte count (multiple of 16), FFF6h = direction (0 read, 1 write).
; Pointers are ordinary starting addresses. IX carries the destination end
; across bursts; the routine advances the source and consumes count.
; FFF8h holds the saved base-bank SP. One call handles the
; entire run with no C calls between bursts.
; Payload: BC/DE/HL + alternate BC/DE/HL + alternate AF + IY = 16 bytes.
; Preserves IX, IY, SP and maskable interrupt enable; clobbers both register
; sets AF/BC/DE/HL. Transfers permit no NMI or access outside this routine while bank 1
; is selected. SP temporarily points at payload, never call/push a return then.
        org 0ff00h
; BIOS extension ABI 2: entry slots and descriptor never move.
        jp bank_read
        jp bank_write
        jp bank_info
        db "BMX2",2,4,16
        jp bank_malloc          ; ABI 2.4 HEAP at FF10h, E=operation, HL=input
heap_return:                    ; Bank-1 allocator jumps here, never CALLs.
        xor a
        out (7),a
        jp restore_interrupts
bank_read:
        xor a
        jr transfer
bank_write:
        ld a,1
transfer:
        ld (0fff6h),a
        ld a,b
        or c
        ret z
        ld a,c
        and 15
        jr nz,invalid
        ld a,h
        or a
        jr z,invalid
        ld (0fff0h),hl
        ld (0fff2h),de
        ld (0fff4h),bc
        add hl,bc
        jr c,invalid
        ; Validate both end addresses in registers. With wrap excluded,
        ; high byte FF is valid only when the low byte is zero (FF00h).
        ex de,hl
        add hl,bc
        jr c,invalid
        inc d
        jr nz,base_end_valid
        ld a,e
        or a
        jr nz,invalid
base_end_valid:
        inc h
        jr nz,validated
        ld a,l
        or a
        jr z,validated
invalid:
        ld a,2
        scf
        ret
validated:
        ld a,i
        di
        push af
        push ix
        push iy
        ld (0fff8h),sp
        ; Normalize direction once, then keep the destination end in IX.
        ; Workspace is only accessible in bank 0; IX survives every switch.
        ld hl,(0fff0h)
        ld de,(0fff2h)
        ld a,(0fff6h)
        or a
        jr nz,source_ready
        ex de,hl
source_ready:
        ld (0fff0h),hl
        ld sp,hl
        ld ix,16
        add ix,de
next_burst:
        ld hl,(0fff4h)
        ; Count is a multiple of 16. Adding -16 sets carry exactly when a
        ; full burst remains, avoiding OR A / SBC HL,DE on every iteration.
        ld de,0fff0h
        add hl,de
        jr nc,finish
        ld (0fff4h),hl
        ld a,(0fff6h)
        ; A still holds direction: WRITE loads bank 0 directly; READ must
        ; select bank 1 first. Leave A holding the destination bank.
        or a
        jr nz,load_payload
        inc a
        out (7),a
        xor a
load_payload:
        pop bc
        pop de
        pop hl
        exx
        pop bc
        pop de
        pop hl
        ex af,af'
        pop af
        ex af,af'
        pop iy
        out (7),a
        ld sp,ix
        push iy
        ex af,af'
        push af
        ex af,af'
        push hl
        push de
        push bc
        exx
        push hl
        push de
        push bc
        ; WRITE finishes in bank 1: decrement selects bank 0 directly.
        ; READ already finishes in bank 0 and needs no further OUT.
        dec a
        jr nz,advance
        out (7),a
advance:
        ld de,16
        add ix,de
        ld hl,(0fff0h)
        add hl,de
        ld (0fff0h),hl
        ; No return addresses are needed inside the loop. Prepare the next
        ; source stack directly; restore the caller's SP only on exit.
        ld sp,hl
        jr next_burst
finish:
        ; Zero length returned before saving registers. Interrupts remain
        ; disabled until the caller's real stack and registers are restored.
        ld sp,(0fff8h)
        pop iy
        pop ix
restore_interrupts:
        pop af
        jp po,interrupts_off
        xor a
        ei
        ret
interrupts_off:
        xor a
        ret
bank_info:
        in a,(7)
        cp 0b4h
        jr nz,unavailable
        ld hl,0204h             ; ABI 2.4
        ld de,0800h
        ld bc,0f700h
        xor a
        ret
unavailable:
        ld hl,0
        ld a,1
        scf
        ret

; Bank-1 HEAP gateway preserves SP/IX/IY. Only this mirrored page
; executes across OUT. The saved AF retains the caller's interrupt state.
bank_malloc:
        ld a,i
        di
        push af
        ld a,1
        out (7),a
        jp 0500h
