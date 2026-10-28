; Bank-1 extent heap, ABI 2.0. E=0 alloc,1 free,2 size,3 available,4 reset.
; HL=input/output. Out-of-band byte tables indexed by 256-byte block:
; 0100 length (zero = interior), 0200 physical predecessor,
; 0300 next free (FF = allocated), 0400 previous free.
; Free list is address ordered. Heap 0800..FEFF (247 blocks).
; Header: 0010 free head, 0012 saved SP, 0014 free byte count.
; Scratch 0016..001F. Private stack 07C0..07FF. No base-bank stack accesses.
        org 0500h
entry:
        exx
        ld hl,0
        add hl,sp
        ld (0012h),hl
        ld sp,0800h
        exx
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
bad:
        ld hl,0
return:
        ld sp,(0012h)
        jp 0ff13h
; Validate an allocated extent. HL handle -> A block, carry on invalid.
valid:
        ld a,l
        or a
        jr nz,invalid
        ld a,h
        cp 8
        jr c,invalid
        cp 255
        jr nc,invalid
        ld l,a
        ld h,1
        ld a,(hl)
        or a
        jr z,invalid
        ld h,3
        ld a,(hl)
        cp 255
        jr nz,invalid
        ld a,l
        or a
        ret
invalid:
        scf
        ret
size:
        call valid
        jp c,bad
        ld h,1
        ld h,(hl)
        ld l,0
        jp return
available:
        ld hl,(0014h)
        jp return
reset:
        ld hl,0100h
        ld de,0101h
        ld bc,03ffh
        ld (hl),0
        ldir
        ld a,247
        ld (0108h),a
        ld hl,0f700h
        ld (0014h),hl
        ld a,8
        ld (0010h),a
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
        cp 248
        jp nc,bad
        ld (0016h),a           ; need
        ld b,a                 ; keep request in register during free-list walk
        ld a,(0010h)
find:
        or a
        jp z,bad
        ld l,a
        ld h,1
        ld a,(hl)
        cp b
        jr nc,fit
        ld h,3
        ld a,(hl)
        jr find
fit:
        ld a,l
        ld (0017h),a           ; allocated extent start
        ld a,(hl)
        sub b
        ld (0018h),a           ; remainder
        ld a,(0017h)
        call unlink
        ld a,(0017h)
        ld l,a
        ld h,1
        ld a,(0016h)
        ld (hl),a
        ld b,a
        ld a,(0017h)
        add a,b
        ld (0019h),a           ; successor / split start
        ld a,(0018h)
        or a
        jr z,no_split
        ld b,a
        ld a,(0019h)
        ld l,a
        ld h,1
        ld (hl),b
        ld h,2
        ld a,(0017h)
        ld (hl),a
        ld a,l
        add a,b
        cp 255
        jr z,split_end
        ld l,a
        ld h,2
        ld a,(0019h)
        ld (hl),a
split_end:
        ld a,(0019h)
        call insert
no_split:
        ld a,(0017h)
        ld l,a
        ld h,3
        ld (hl),255
        ld hl,(0014h)
        ld a,(0016h)
        sub h
        neg
        ld h,a
        ld (0014h),hl
        ld a,(0017h)
        ld h,a
        ld l,0
        jp return
release:
        ld a,h
        or l
        jp z,ok
        call valid
        jp c,bad
        ld (0017h),a           ; coalescing anchor
        ld l,a
        ld h,1
        ld a,(hl)
        ld (0018h),a           ; merged length
        ld b,a
        ld hl,(0014h)
        ld a,h
        add a,b
        ld h,a
        ld (0014h),hl
        ld a,(0017h)
        ld l,a
        ld h,2
        ld a,(hl)
        or a
        jr z,merge_right
        ld l,a
        ld h,3
        ld a,(hl)
        cp 255
        jr z,merge_right
        ld a,l
        ld (001ah),a           ; free predecessor
        call unlink
        ld a,(0017h)
        ld l,a
        ld h,1
        ld (hl),0              ; old anchor becomes interior
        ld a,(001ah)
        ld (0017h),a
        ld l,a
        ld a,(0018h)
        add a,(hl)
        ld (0018h),a
merge_right:
        ld a,(0017h)
        ld b,a
        ld a,(0018h)
        add a,b
        cp 255
        jr z,commit
        ld l,a
        ld h,3
        ld a,(hl)
        cp 255
        jr z,commit
        ld a,l
        ld (001ah),a
        call unlink
        ld a,(001ah)
        ld l,a
        ld h,1
        ld a,(0018h)
        add a,(hl)
        ld (0018h),a
        ld (hl),0              ; right extent becomes interior
commit:
        ld a,(0017h)
        ld l,a
        ld h,1
        ld a,(0018h)
        ld (hl),a
        add a,l
        cp 255
        jr z,insert_merged
        ld l,a
        ld h,2
        ld a,(0017h)
        ld (hl),a
insert_merged:
        ld a,(0017h)
        call insert
        jp ok
; Remove free extent A. Does not change its physical extent metadata.
unlink:
        ld l,a
        ld h,3
        ld e,(hl)              ; next
        ld h,4
        ld d,(hl)              ; previous
        ld a,d
        or a
        jr z,unlink_head
        ld l,d
        ld h,3
        ld (hl),e
        jr unlink_next
unlink_head:
        ld a,e
        ld (0010h),a
unlink_next:
        ld a,e
        or a
        ret z
        ld l,e
        ld h,4
        ld (hl),d
        ret
; Insert free extent A in address order. Walks only free extents.
insert:
        ld c,a
        ld d,0
        ld a,(0010h)
insert_walk:
        or a
        jr z,insert_here
        cp c
        jr nc,insert_here
        ld d,a
        ld l,a
        ld h,3
        ld a,(hl)
        jr insert_walk
insert_here:
        ld e,a
        ld l,c
        ld h,3
        ld (hl),e
        ld h,4
        ld (hl),d
        ld a,d
        or a
        jr z,insert_head
        ld l,d
        ld h,3
        ld (hl),c
        jr insert_next
insert_head:
        ld a,c
        ld (0010h),a
insert_next:
        ld a,e
        or a
        ret z
        ld l,e
        ld h,4
        ld (hl),c
        ret
