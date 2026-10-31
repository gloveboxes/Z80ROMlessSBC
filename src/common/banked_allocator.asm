; Bank-1 extent heap. E=0 alloc,1 free,2 size,3 available,4 reset,
; 5 call at code+IX on the BC stack with IY as a C argument.
; HL=input/output. Four 32-byte tables at E700/E720/E740/E760:
; length, physical predecessor, next free (FF = allocated), previous free.
; Index 1 is the library, 2 the stack, and 8..29 the C800..DD00 pool.
; 0100..BFFF is pinned library code/heap; C000..C7FF pinned stack.
; Free list is address ordered. General pool C800..DDFF (22 blocks).
; Header: E610 free head, E612 saved SP, E614 free byte count.
; Scratch E616..E620. Private stack E5C0..E5FF. No base-bank stack accesses.
        org 0e800h
entry:
        ld (0e612h),sp
        ld sp,0e600h
        ld a,e
        or a
        jp z,allocate
        dec a
        jp z,release
        dec a
        jp z,size
        dec a
        jp z,available
        dec a
        jp z,reset
        dec a
        jp z,execute_at
bad:
        ld a,2
        jr error
bad_handle:
        ld a,3
        jr error
no_memory:
        ld a,4
        jr error
pinned:
        ld a,5
error:
        ld (0dfa2h),a
        ld hl,0
        call 0e786h
return:
        ld sp,(0e612h)
        ld a,(0dfa5h)
        or a
        jr z,base_return
        xor a
        ld (0dfa5h),a
        ret
base_return:
        jp 0ff13h
; Validate an allocated extent. HL handle -> A block, carry on invalid.
valid:
        ld a,l
        or a
        jr nz,invalid
        ld a,h
        cp 1
        jr z,valid_index
        cp 0c0h
        jr nz,valid_pool
        ld a,2
        jr valid_index
valid_pool:
        cp 0c8h
        jr c,invalid
        cp 0deh
        jr nc,invalid
        sub 0c0h
valid_index:
        ld l,a
        ld h,0e7h
        ld a,(hl)
        or a
        jr z,invalid
        set 6,l
        ld a,(hl)
        cp 255
        jr nz,invalid
        ld a,l
        and 31
        ret
invalid:
        scf
        ret
size:
        call valid
        jp c,bad_handle
        res 6,l
        ld h,(hl)
        ld l,0
        jp return
available:
        ld hl,(0e614h)
        jp return
reset:
        ld hl,(0dfa6h)
        inc hl
        ld a,h
        or l
        jr nz,reset_epoch
        ld a,10
        jp error
reset_epoch:
        ld (0dfa6h),hl
        ld hl,0e700h
        ld de,0e701h
        ld bc,127
        ld (hl),0
        ldir
        ld a,0bfh
        ld (0e701h),a          ; pinned library through BFFFh
        ld a,8
        ld (0e702h),a          ; pinned call stack
        ld a,1
        ld (0e722h),a
        ld a,2
        ld (0e728h),a
        ld a,255
        ld (0e741h),a
        ld (0e742h),a
        ld a,22
        ld (0e708h),a
        ld hl,01600h
        ld (0e614h),hl
        ld a,8
        ld (0e610h),a
ok:
        ld hl,1
        jp return
allocate:
        ld a,h
        or l
        jp z,bad
        dec hl
        inc h
        jp z,bad
        ld a,h
        cp 23
        jp nc,bad
        ld (0e616h),a           ; need
        ld b,a                 ; keep request in register during free-list walk
        ld a,(0e610h)
find:
        or a
        jp z,no_memory
        ld l,a
        call length_ptr
        ld a,(hl)
        cp b
        jr nc,fit
        call next_ptr
        ld a,(hl)
        jr find
fit:
        ld a,l
        and 31
        ld (0e617h),a           ; allocated extent start
        ld a,(hl)
        sub b
        ld (0e618h),a           ; remainder
        ld a,(0e617h)
        call unlink
        ld a,(0e617h)
        ld l,a
        call length_ptr
        ld a,(0e616h)
        ld (hl),a
        ld b,a
        ld a,(0e617h)
        add a,b
        ld (0e619h),a           ; successor / split start
        ld a,(0e618h)
        or a
        jr z,no_split
        ld b,a
        ld a,(0e619h)
        ld l,a
        call length_ptr
        ld (hl),b
        call previous_ptr
        ld a,(0e617h)
        ld (hl),a
        ld a,l
        and 31
        add a,b
        cp 30
        jr z,split_end
        ld l,a
        call previous_ptr
        ld a,(0e619h)
        ld (hl),a
split_end:
        ld a,(0e619h)
        call insert
no_split:
        ld a,(0e617h)
        ld l,a
        call next_ptr
        ld (hl),255
        ld hl,(0e614h)
        ld a,(0e616h)
        sub h
        neg
        ld h,a
        ld (0e614h),hl
        ld a,(0e617h)
        add a,0c0h
        ld h,a
        ld l,0
        jp return
release:
        ld a,h
        or l
        jp z,ok
        call valid
        jp c,bad_handle
        cp 1
        jp z,pinned
        cp 2
        jp z,pinned
        ld (0e617h),a           ; coalescing anchor
        ld l,a
        call length_ptr
        ld a,(hl)
        ld (0e618h),a           ; merged length
        ld b,a
        ld hl,(0e614h)
        ld a,h
        add a,b
        ld h,a
        ld (0e614h),hl
        ld a,(0e617h)
        ld l,a
        call previous_ptr
        ld a,(hl)
        or a
        jr z,merge_right
        ld l,a
        call next_ptr
        ld a,(hl)
        cp 255
        jr z,merge_right
        ld a,l
        and 31
        ld (0e61ah),a           ; free predecessor
        call unlink
        ld a,(0e617h)
        ld l,a
        call length_ptr
        ld (hl),0              ; old anchor becomes interior
        ld a,(0e61ah)
        ld (0e617h),a
        ld l,a
        ld a,(0e618h)
        add a,(hl)
        ld (0e618h),a
merge_right:
        ld a,(0e617h)
        ld b,a
        ld a,(0e618h)
        add a,b
        cp 30
        jr z,commit
        ld l,a
        call next_ptr
        ld a,(hl)
        cp 255
        jr z,commit
        ld a,l
        and 31
        ld (0e61ah),a
        call unlink
        ld a,(0e61ah)
        ld l,a
        call length_ptr
        ld a,(0e618h)
        add a,(hl)
        ld (0e618h),a
        ld (hl),0              ; right extent becomes interior
commit:
        ld a,(0e617h)
        ld l,a
        call length_ptr
        ld a,(0e618h)
        ld (hl),a
        add a,l
        cp 30
        jr z,insert_merged
        ld l,a
        call previous_ptr
        ld a,(0e617h)
        ld (hl),a
insert_merged:
        ld a,(0e617h)
        call insert
        jp ok
; E=5: HL=code handle, BC=distinct nonzero stack handle, IX=byte offset,
; IY=bank-1 context pointer. The entry must be inside the code allocation.
; Push IY onto the selected stack so an ordinary DCC C function sees it at
; SP+2 and returns its 16-bit result in HL. The private stack keeps IX/IY
; and the return path; the call runs at the stack allocation's exclusive end.
execute_at:
        ld (0e61bh),hl
        ld a,b
        or c
        jp z,bad
        ld h,b
        ld l,c
        ld (0e61dh),hl
        ld de,(0e61bh)
        or a
        sbc hl,de
        jp z,bad               ; code and stack may not overlap
        ld hl,(0e61bh)
        call module_ready
        jp z,bad
        call valid
        jp c,bad_handle
        ld l,a
        call length_ptr
        ld a,(hl)             ; allocated size in 256-byte blocks
        ld h,a
        ld l,0
        push ix
        pop de                 ; requested byte offset
        or a
        sbc hl,de
        jp c,bad
        jp z,bad              ; offset must be strictly less than size
        ld hl,(0e61bh)
        add hl,de
        ld (0e61bh),hl         ; validated entry address
        ld hl,(0e61dh)
        call valid
        jp c,bad_handle
        call length_ptr
        ld a,(hl)             ; rounded stack size in 256-byte blocks
        ld hl,(0e61dh)
        add a,h               ; exclusive end block, at most FFh
        ld h,a
        ld l,0
        ld (0e61dh),hl
        push ix
        push iy
        call 0e780h
        ld (0e619h),sp          ; return to the private stack after payload RET
        ld sp,(0e61dh)
        push iy                ; DCC first argument, after return address
        ld de,stack_return
        push de                ; payload's RET target on its own stack
        ld hl,(0e61bh)
        jp (hl)
stack_return:
        ld sp,(0e619h)
        call 0e783h
        call 0e786h
        pop iy
        pop ix
        jp return
; Z means the fixed library slot is not currently callable. Ordinary
; bmalloc code allocations are unaffected.
module_ready:
        ld a,h
        cp 1
        ret nz
        ld a,l
        or a
        ret nz
        ld a,(0de9fh)
        or a
        ret
; Remove free extent A. Does not change its physical extent metadata.
unlink:
        ld l,a
        call next_ptr
        ld e,(hl)              ; next
        call back_ptr
        ld d,(hl)              ; previous
        ld a,d
        or a
        jr z,unlink_head
        ld l,d
        call next_ptr
        ld (hl),e
        jr unlink_next
unlink_head:
        ld a,e
        ld (0e610h),a
unlink_next:
        ld a,e
        or a
        ret z
        ld l,e
        call back_ptr
        ld (hl),d
        ret
; Insert free extent A in address order. Walks only free extents.
insert:
        ld c,a
        ld d,0
        ld a,(0e610h)
insert_walk:
        or a
        jr z,insert_here
        cp c
        jr nc,insert_here
        ld d,a
        ld l,a
        call next_ptr
        ld a,(hl)
        jr insert_walk
insert_here:
        ld e,a
        ld l,c
        call next_ptr
        ld (hl),e
        call back_ptr
        ld (hl),d
        ld a,d
        or a
        jr z,insert_head
        ld l,d
        call next_ptr
        ld (hl),c
        jr insert_next
insert_head:
        ld a,c
        ld (0e610h),a
insert_next:
        ld a,e
        or a
        ret z
        ld l,e
        call back_ptr
        ld (hl),c
        ret

; Bit selectors preserve AF while replacing only the table bits of L.
length_ptr:
        res 7,l
        res 6,l
        res 5,l
        ld h,0e7h
        ret
previous_ptr:
        res 7,l
        res 6,l
        set 5,l
        ld h,0e7h
        ret
next_ptr:
        res 7,l
        set 6,l
        res 5,l
        ld h,0e7h
        ret
back_ptr:
        res 7,l
        set 6,l
        set 5,l
        ld h,0e7h
        ret
