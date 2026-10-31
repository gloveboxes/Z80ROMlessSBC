; BIOS bank extension ABI 1.0: identical page at FF00h in both banks.
; Public READ=FF00h / WRITE=FF03h: HL=base, DE=bank offset, BC=byte count.
; Zero count succeeds. Positive counts must be multiples of 16, base >=0100h,
; and both ranges must end <=FF00h without wrapping. INFO=FF06h must succeed
; first; data calls trust that capability and avoid a trapped status read.
; A=0/carry clear succeeds, A=2/carry set rejects invalid data arguments.
; INFO: HL=0100h version, DE=C800h first general allocation, BC=1600h capacity;
; A=0/carry clear ready, A=1/HL=0/carry set unavailable.
; PRIVATE workspace (not part of the public ABI):
; FFF0h = advancing source SP in its source bank,
; FFF2h = initial bank address,
; FFF4h = remaining 256-byte groups minus one (bank 0 only),
; FFF6h = direction (0 read, 1 write, bank 0 only).
; Pointers are ordinary starting addresses. IX carries the destination end
; across bursts; the routine advances the source and consumes count.
; FFF8h holds the saved base-bank SP. One call handles the
; entire run with no C calls between bursts.
; Payload: BC/DE/HL + alternate BC/DE/HL + alternate AF + IY = 16 bytes.
; Preserves IX, IY, SP and maskable interrupt enable; clobbers both register
; sets AF/BC/DE/HL. Transfers permit no NMI or access outside this routine while bank 1
; is selected. SP temporarily points at payload, never call/push a return then.
        org 0ff00h
; Entry slots and descriptor have fixed addresses.
        jp bank_read
        jp bank_write
        jp 0fa00h               ; Base-bank INFO helper in the CP/M shim.
        db "BMX1",1,0,16
        jp bank_malloc          ; Service at FF10h, E=operation, HL=input
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
        ; Only the source-SP word is accessed in bank 1.
        ld hl,(0fff0h)
        ld de,(0fff2h)
        ld a,(0fff6h)
        or a
        jr nz,source_ready
        ex de,hl
source_ready:
        ld ix,16
        add ix,de
        ; A holds the low byte of the remaining count plus the direction.
        ; Both are safe OUT values: the PIO selects A16 from D0 only.
        ld a,c
        or a
        jr nz,count_ready
        dec b
count_ready:
        ld a,b
        ld (0fff4h),a
        ld a,(0fff6h)
        add a,c
        bit 0,a
        jr nz,next_burst
        inc a
        out (7),a
        dec a
        ld (0fff0h),hl
        jr load_payload
next_burst:
        rrca
        jr c,source_selected
        inc a
        out (7),a
        dec a
source_selected:
        ; RLCA restores the count and puts destination D0 in carry.
        ; Payload loads/stores and EX AF preserve that main-register flag.
        rlca
load_payload:
        ld sp,(0fff0h)
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
        ld (0fff0h),sp
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
        jr nc,advance
        dec a
        out (7),a
        inc a
advance:
        ld de,16
        add ix,de
        ; Sixteen bursts consume a 256-byte group. No count load/store or
        ; pointer arithmetic is needed in the source-SP workspace per burst.
        sub 16
        cp 16
        jr nc,next_burst
        ld hl,0fff4h
        dec (hl)
        ld a,(hl)
        inc a
        jr z,finish
        ld a,(0fff6h)
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
; Bank-1 HEAP gateway preserves SP/IX/IY. Only this mirrored page
; executes across OUT. The saved AF retains the caller's interrupt state.
bank_malloc:
        ld a,i
        di
        push af
        ld a,1
        out (7),a
        jp 0e640h
