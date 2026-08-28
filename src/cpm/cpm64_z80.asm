; Z80 port generated from the immutable Burcon CP/M 2.2 image.
; Internal references are symbolic so each fixed-base section can be compacted.
BIOS_BASE: equ 0xfd00
        org 0xe700

CCP_COLD_ENTRY:
        jp L_E65C
CCP_WARM_ENTRY:
        jp L_E658
A_E306:
        db 0x7f
A_E307:
        db 0x00
A_E308:
        db 0x43
        db 0x6f
        db 0x70
        db 0x79
        db 0x72
        db 0x69
        db 0x67
        db 0x68
        db 0x74
        db 0x20
        db 0x31
        db 0x39
        db 0x37
        db 0x39
        db 0x20
        db 0x28
        db 0x63
        db 0x29
        db 0x20
        db 0x62
        db 0x79
        db 0x20
        db 0x44
        db 0x69
        db 0x67
        db 0x69
        db 0x74
        db 0x61
        db 0x6c
        db 0x20
        db 0x52
        db 0x65
        db 0x73
        db 0x65
        db 0x61
        db 0x72
        db 0x63
        db 0x68
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
A_E388:
        dw A_E308
A_E38A:
        db 0x00
        db 0x00
L_E38C:
        ld e,a
        ld c,0x02
        jp 0x0005
L_E392:
        push bc
        call L_E38C
        pop bc
        ret
L_E398:
        ld a,0x0d
        call L_E392
        ld a,0x0a
        jr L_E392
L_E3A2:
        ld a,0x20
        jr L_E392
L_E3A7:
        push bc
        call L_E398
        pop hl
L_E3AC:
        ld a,(hl)
        or a
        ret z
        inc hl
        push hl
        call L_E38C
        pop hl
        jr L_E3AC
L_E3B8:
        ld c,0x0d
        jp 0x0005
L_E3BD:
        ld e,a
        ld c,0x0e
        jp 0x0005
L_E3C3:
        call 0x0005
        ld (A_EAEE),a
        inc a
        ret
L_E3CB:
        ld c,0x0f
        jr L_E3C3
L_E3D0:
        xor a
        ld (A_EAED),a
        ld de,A_EACD
        jr L_E3CB
L_E3DA:
        ld c,0x10
        jr L_E3C3
L_E3DF:
        ld c,0x11
        jr L_E3C3
L_E3E4:
        ld c,0x12
        jr L_E3C3
L_E3E9:
        ld de,A_EACD
        jr L_E3DF
L_E3EF:
        ld c,0x13
        jp 0x0005
L_E3F4:
        call 0x0005
        or a
        ret
L_E3F9:
        ld c,0x14
        jr L_E3F4
L_E3FE:
        ld de,A_EACD
        jr L_E3F9
L_E404:
        ld c,0x15
        jr L_E3F4
L_E409:
        ld c,0x16
        jr L_E3C3
L_E40E:
        ld c,0x17
        jp 0x0005
L_E413:
        ld e,0xff
L_E415:
        ld c,0x20
        jp 0x0005
L_E41A:
        call L_E413
        add a,a
        add a,a
        add a,a
        add a,a
        ld hl,A_EAEF
        or (hl)
        ld (0x0004),a
        ret
L_E429:
        ld a,(A_EAEF)
        ld (0x0004),a
        ret
L_E430:
        cp 0x61
        ret c
        cp 0x7b
        ret nc
        and 0x5f
        ret
L_E439:
        ld a,(A_EAAB)
        or a
        jr z,L_E496
        ld a,(A_EAEF)
        or a
        ld a,0x00
        call nz,L_E3BD
        ld de,A_EAAC
        call L_E3CB
        jr z,L_E496
        ld a,(A_EABB)
        dec a
        ld (A_EACC),a
        ld de,A_EAAC
        call L_E3F9
        jr nz,L_E496
        ld de,A_E307
        ld hl,0x0080
        ld b,0x80
        call L_E742
        ld hl,A_EABA
        ld (hl),0x00
        inc hl
        dec (hl)
        ld de,A_EAAC
        call L_E3DA
        jr z,L_E496
        ld a,(A_EAEF)
        or a
        call nz,L_E3BD
        ld hl,A_E308
        call L_E3AC
        call L_E4C2
        jr z,L_E4A7
        call L_E4DD
        jp L_E682
L_E496:
        call L_E4DD
        call L_E41A
        ld c,0x0a
        ld de,A_E306
        call 0x0005
        call L_E429
L_E4A7:
        ld hl,A_E307
        ld b,(hl)
        inc hl
        ld a,b
        or a
        jr z,L_E4BA
L_E4AB:
        ld a,(hl)
        call L_E430
        ld (hl),a
        inc hl
        djnz L_E4AB
L_E4BA:
        ld (hl),b
        ld hl,A_E308
        ld (A_E388),hl
        ret
L_E4C2:
        ld c,0x0b
        call 0x0005
        or a
        ret z
        ld c,0x01
        call 0x0005
        or a
        ret
L_E4D0:
        ld c,0x19
        jp 0x0005
L_E4D5:
        ld de,0x0080
L_E4D8:
        ld c,0x1a
        jp 0x0005
L_E4DD:
        ld hl,A_EAAB
        ld a,(hl)
        or a
        ret z
        ld (hl),0x00
        xor a
        call L_E3BD
        ld de,A_EAAC
        call L_E3EF
        ld a,(A_EAEF)
        jp L_E3BD
L_E4F5:
        ld de,A_E628
        ld hl,A_EB00
        ld b,0x06
L_E4FD:
        ld a,(de)
        cp (hl)
        jp nz,L_E6CF
        inc de
        inc hl
        djnz L_E4FD
        ret
L_E509:
        call L_E398
        ld hl,(A_E38A)
L_E50F:
        ld a,(hl)
        cp 0x20
        jr z,L_E522
        or a
        jr z,L_E522
        push hl
        call L_E38C
        pop hl
        inc hl
        jr L_E50F
L_E522:
        ld a,0x3f
        call L_E38C
        call L_E398
        call L_E4DD
        jp L_E682
L_E530:
        ld a,(de)
        or a
        ret z
        cp 0x20
        jr c,L_E509
        ret z
        cp 0x3d
        ret z
        cp 0x5f
        ret z
        cp 0x2e
        ret z
        cp 0x3a
        ret z
        cp 0x3b
        ret z
        cp 0x3c
        ret z
        cp 0x3e
        ret z
        ret
L_E54F:
        ld a,(de)
        or a
        ret z
        cp 0x20
        ret nz
        inc de
        jr L_E54F
L_E559:
        add a,l
        ld l,a
        ret nc
        inc h
        ret
L_E55E:
        xor a
L_E560:
        ld hl,A_EACD
        call L_E559
        push hl
        push hl
        xor a
        ld (A_EAF0),a
        ld hl,(A_E388)
        ex de,hl
        call L_E54F
        ex de,hl
        ld (A_E38A),hl
        ex de,hl
        pop hl
        ld a,(de)
        or a
        jr z,L_E589
        sbc a,0x40
        ld b,a
        inc de
        ld a,(de)
        cp 0x3a
        jr z,L_E590
        dec de
L_E589:
        ld a,(A_EAEF)
        ld (hl),a
        jr L_E596
L_E590:
        ld a,b
        ld (A_EAF0),a
        ld (hl),b
        inc de
L_E596:
        ld b,0x08
L_E598:
        call L_E530
        jr z,L_E5B9
        inc hl
        cp 0x2a
        jr nz,L_E5A9
        ld (hl),0x3f
        jr L_E5AB
L_E5A9:
        ld (hl),a
        inc de
L_E5AB:
        djnz L_E598
L_E5AF:
        call L_E530
        jr z,L_E5C0
        inc de
        jr L_E5AF
L_E5B9:
        inc hl
        ld (hl),0x20
        djnz L_E5B9
L_E5C0:
        ld b,0x03
        cp 0x2e
        jr nz,L_E5E9
        inc de
L_E5C8:
        call L_E530
        jr z,L_E5E9
        inc hl
        cp 0x2a
        jr nz,L_E5D9
        ld (hl),0x3f
        jr L_E5DB
L_E5D9:
        ld (hl),a
        inc de
L_E5DB:
        djnz L_E5C8
L_E5DF:
        call L_E530
        jr z,L_E5F0
        inc de
        jr L_E5DF
L_E5E9:
        inc hl
        ld (hl),0x20
        djnz L_E5E9
L_E5F0:
        ld b,0x03
L_E5F2:
        inc hl
        ld (hl),0x00
        djnz L_E5F2
        ex de,hl
        ld (A_E388),hl
        pop hl
        ld bc,0x000b
L_E601:
        inc hl
        ld a,(hl)
        cp 0x3f
        jr nz,L_E609
        inc b
L_E609:
        dec c
        jr nz,L_E601
        ld a,b
        or a
        ret
A_E610:
        db 0x44
        db 0x49
        db 0x52
        db 0x20
        db 0x45
        db 0x52
        db 0x41
        db 0x20
        db 0x54
        db 0x59
        db 0x50
        db 0x45
        db 0x53
        db 0x41
        db 0x56
        db 0x45
        db 0x52
        db 0x45
        db 0x4e
        db 0x20
        db 0x55
        db 0x53
        db 0x45
        db 0x52
A_E628:
        db 0x00
        db 0x16
        db 0x00
        db 0x00
        db 0x00
        db 0x00
L_E62E:
        ld hl,A_E610
        ld c,0x00
L_E633:
        ld a,c
        cp 0x06
        ret nc
        ld de,A_EACE
        ld b,0x04
L_E63C:
        ld a,(de)
        cp (hl)
        jr nz,L_E64F
        inc de
        inc hl
        djnz L_E63C
        ld a,(de)
        cp 0x20
        jr nz,L_E654
        ld a,c
        ret
L_E64F:
        inc hl
        djnz L_E64F
L_E654:
        inc c
        jr L_E633
L_E658:
        xor a
        ld (A_E307),a
L_E65C:
        ld sp,A_EAAB
        push bc
        ld a,c
        rra
        rra
        rra
        rra
        and 0x0f
        ld e,a
        call L_E415
        call L_E3B8
        ld (A_EAAB),a
        pop bc
        ld a,c
        and 0x0f
        ld (A_EAEF),a
        call L_E3BD
        ld a,(A_E307)
        or a
        jr nz,L_E698
L_E682:
        ld sp,A_EAAB
        call L_E398
        call L_E4D0
        add a,0x41
        call L_E38C
        ld a,0x3e
        call L_E38C
        call L_E439
L_E698:
        ld de,0x0080
        call L_E4D8
        call L_E4D0
        ld (A_EAEF),a
        call L_E55E
        call nz,L_E509
        ld a,(A_EAF0)
        or a
        jp nz,CCP_COMMAND_6
        call L_E62E
        ld hl,CCP_COMMAND_POINTERS
        ld e,a
        ld d,0x00
        add hl,de
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        jp (hl)
CCP_COMMAND_POINTERS:
        dw CCP_COMMAND_0
        dw CCP_COMMAND_1
        dw CCP_COMMAND_2
        dw CCP_COMMAND_3
        dw CCP_COMMAND_4
        dw CCP_COMMAND_5
        dw CCP_COMMAND_6
L_E6CF:
        ld hl,0x76f3
        ld (CCP_COLD_ENTRY),hl
        ld hl,CCP_COLD_ENTRY
        jp (hl)
L_E6D9:
        ld bc,A_E6DF
        jp L_E3A7
A_E6DF:
        db 0x52
        db 0x65
        db 0x61
        db 0x64
        db 0x20
        db 0x65
        db 0x72
        db 0x72
        db 0x6f
        db 0x72
        db 0x00
L_E6EA:
        ld bc,A_E6F0
        jp L_E3A7
A_E6F0:
        db 0x4e
        db 0x6f
        db 0x20
        db 0x66
        db 0x69
        db 0x6c
        db 0x65
        db 0x00
L_E6F8:
        call L_E55E
        ld a,(A_EAF0)
        or a
        jp nz,L_E509
        ld hl,A_EACE
        ld bc,0x000b
L_E708:
        ld a,(hl)
        cp 0x20
        jr z,L_E733
        inc hl
        sub 0x30
        cp 0x0a
        jp nc,L_E509
        ld d,a
        ld a,b
        and 0xe0
        jp nz,L_E509
        ld a,b
        rlca
        rlca
        rlca
        add a,b
        jp c,L_E509
        add a,b
        jp c,L_E509
        add a,d
        jp c,L_E509
        ld b,a
        dec c
        jr nz,L_E708
        ret
L_E733:
        ld a,(hl)
        cp 0x20
        jp nz,L_E509
        inc hl
        dec c
        jr nz,L_E733
        ld a,b
        ret
L_E740:
        ld b,0x03
L_E742:
        ld c,b
        ld b,0
        ldir
        ret
L_E74B:
        ld hl,0x0080
        add a,c
        call L_E559
        ld a,(hl)
        ret
L_E754:
        xor a
        ld (A_EACD),a
        ld a,(A_EAF0)
        or a
        ret z
        dec a
        ld hl,A_EAEF
        cp (hl)
        ret z
        jp L_E3BD
L_E766:
        ld a,(A_EAF0)
        or a
        ret z
        dec a
        ld hl,A_EAEF
        cp (hl)
        ret z
        ld a,(A_EAEF)
        jp L_E3BD
CCP_COMMAND_0:
        call L_E55E
        call L_E754
        ld hl,A_EACE
        ld a,(hl)
        cp 0x20
        jr nz,L_E78F
        ld b,0x0b
L_E788:
        ld (hl),0x3f
        inc hl
        djnz L_E788
L_E78F:
        ld e,0x00
        push de
        call L_E3E9
        call z,L_E6EA
L_E798:
        jp z,L_E81B
        ld a,(A_EAEE)
        rrca
        rrca
        rrca
        and 0x60
        ld c,a
        ld a,0x0a
        call L_E74B
        rla
        jr c,L_E80F
        pop de
        ld a,e
        inc e
        push de
        and 0x03
        push af
        jr nz,L_E7CC
        call L_E398
        push bc
        call L_E4D0
        pop bc
        add a,0x41
        call L_E392
        ld a,0x3a
        call L_E392
        jr L_E7D4
L_E7CC:
        call L_E3A2
        ld a,0x3a
        call L_E392
L_E7D4:
        call L_E3A2
        ld b,0x01
L_E7D9:
        ld a,b
        call L_E74B
        and 0x7f
        cp 0x20
        jr nz,L_E7F9
        pop af
        push af
        cp 0x03
        jr nz,L_E7F7
        ld a,0x09
        call L_E74B
        and 0x7f
        cp 0x20
        jr z,L_E80E
L_E7F7:
        ld a,0x20
L_E7F9:
        call L_E392
        inc b
        ld a,b
        cp 0x0c
        jr nc,L_E80E
        cp 0x09
        jr nz,L_E7D9
        call L_E3A2
        jr L_E7D9
L_E80E:
        pop af
L_E80F:
        call L_E4C2
        jr nz,L_E81B
        call L_E3E4
        jp L_E798
L_E81B:
        pop de
        jp L_EA86
CCP_COMMAND_1:
        call L_E55E
        cp 0x0b
        jr nz,L_E842
        ld bc,A_E852
        call L_E3A7
        call L_E439
        ld hl,A_E307
        dec (hl)
        jp nz,L_E682
        inc hl
        ld a,(hl)
        cp 0x59
        jp nz,L_E682
        inc hl
        ld (A_E388),hl
L_E842:
        call L_E754
        ld de,A_EACD
        call L_E3EF
        inc a
        call z,L_E6EA
        jp L_EA86
A_E852:
        db 0x41
        db 0x6c
        db 0x6c
        db 0x20
        db 0x28
        db 0x79
        db 0x2f
        db 0x6e
        db 0x29
        db 0x3f
        db 0x00
CCP_COMMAND_2:
        call L_E55E
        jp nz,L_E509
        call L_E754
        call L_E3D0
        jr z,L_E8A7
        call L_E398
        ld hl,A_EAF1
        ld (hl),0xff
L_E874:
        ld hl,A_EAF1
        ld a,(hl)
        cp 0x80
        jr c,L_E887
        push hl
        call L_E3FE
        pop hl
        jr nz,L_E8A0
        xor a
        ld (hl),a
L_E887:
        inc (hl)
        ld hl,0x0080
        call L_E559
        ld a,(hl)
        cp 0x1a
        jp z,L_EA86
        call L_E38C
        call L_E4C2
        jp nz,L_EA86
        jr L_E874
L_E8A0:
        dec a
        jp z,L_EA86
        call L_E6D9
L_E8A7:
        call L_E766
        jp L_E509
CCP_COMMAND_3:
        call L_E6F8
        push af
        call L_E55E
        jp nz,L_E509
        call L_E754
        ld de,A_EACD
        push de
        call L_E3EF
        pop de
        call L_E409
        jr z,L_E8FB
        xor a
        ld (A_EAED),a
        pop af
        ld l,a
        ld h,0x00
        add hl,hl
        ld de,0x0100
L_E8D4:
        ld a,h
        or l
        jr z,L_E8F1
        dec hl
        push hl
        ld hl,0x0080
        add hl,de
        push hl
        call L_E4D8
        ld de,A_EACD
        call L_E404
        pop de
        pop hl
        jr nz,L_E8FB
        jr L_E8D4
L_E8F1:
        ld de,A_EACD
        call L_E3DA
        inc a
        jr nz,L_E901
L_E8FB:
        ld bc,A_E907
        call L_E3A7
L_E901:
        call L_E4D5
        jp L_EA86
A_E907:
        db 0x4e
        db 0x6f
        db 0x20
        db 0x73
        db 0x70
        db 0x61
        db 0x63
        db 0x65
        db 0x00
CCP_COMMAND_4:
        call L_E55E
        jp nz,L_E509
        ld a,(A_EAF0)
        push af
        call L_E754
        call L_E3E9
        jr nz,L_E979
        ld hl,A_EACD
        ld de,A_EADD
        ld b,0x10
        call L_E742
        ld hl,(A_E388)
        ex de,hl
        call L_E54F
        cp 0x3d
        jr z,L_E93F
        cp 0x5f
        jr nz,L_E973
L_E93F:
        ex de,hl
        inc hl
        ld (A_E388),hl
        call L_E55E
        jr nz,L_E973
        pop af
        ld b,a
        ld hl,A_EAF0
        ld a,(hl)
        or a
        jr z,L_E959
        cp b
        ld (hl),b
        jr nz,L_E973
L_E959:
        ld (hl),b
        xor a
        ld (A_EACD),a
        call L_E3E9
        jr z,L_E96D
        ld de,A_EACD
        call L_E40E
        jp L_EA86
L_E96D:
        call L_E6EA
        jp L_EA86
L_E973:
        call L_E766
        jp L_E509
L_E979:
        ld bc,A_E982
        call L_E3A7
        jp L_EA86
A_E982:
        db 0x46
        db 0x69
        db 0x6c
        db 0x65
        db 0x20
        db 0x65
        db 0x78
        db 0x69
        db 0x73
        db 0x74
        db 0x73
        db 0x00
CCP_COMMAND_5:
        call L_E6F8
        cp 0x10
        jp nc,L_E509
        ld e,a
        ld a,(A_EACE)
        cp 0x20
        jp z,L_E509
        call L_E415
        jp L_EA89
CCP_COMMAND_6:
        call L_E4F5
        ld a,(A_EACE)
        cp 0x20
        jr nz,L_E9C4
        ld a,(A_EAF0)
        or a
        jp z,L_EA89
        dec a
        ld (A_EAEF),a
        call L_E429
        call L_E3BD
        jp L_EA89
L_E9C4:
        ld de,A_EAD6
        ld a,(de)
        cp 0x20
        jp nz,L_E509
        push de
        call L_E754
        pop de
        ld hl,A_EA83
        call L_E740
        call L_E3D0
        jp z,L_EA6B
        ld hl,0x0100
L_E9E1:
        push hl
        ex de,hl
        call L_E4D8
        ld de,A_EACD
        call L_E3F9
        jr nz,L_EA01
        pop hl
        ld de,0x0080
        add hl,de
        ld de,CCP_COLD_ENTRY
        ld a,l
        sub e
        ld a,h
        sbc a,d
        jr nc,L_EA71
        jr L_E9E1
L_EA01:
        pop hl
        dec a
        jr nz,L_EA71
        call L_E766
        call L_E55E
        ld hl,A_EAF0
        push hl
        ld a,(hl)
        ld (A_EACD),a
        ld a,0x10
        call L_E560
        pop hl
        ld a,(hl)
        ld (A_EADD),a
        xor a
        ld (A_EAED),a
        ld de,0x005c
        ld hl,A_EACD
        ld b,0x21
        call L_E742
        ld hl,A_E308
L_EA30:
        ld a,(hl)
        or a
        jr z,L_EA3E
        cp 0x20
        jr z,L_EA3E
        inc hl
        jr L_EA30
L_EA3E:
        ld b,0x00
        ld de,0x0081
L_EA43:
        ld a,(hl)
        ld (de),a
        or a
        jr z,L_EA4F
        inc b
        inc hl
        inc de
        jr L_EA43
L_EA4F:
        ld a,b
        ld (0x0080),a
        call L_E398
        call L_E4D5
        call L_E41A
        call 0x0100
        ld sp,A_EAAB
        call L_E429
        call L_E3BD
        jp L_E682
L_EA6B:
        call L_E766
        jp L_E509
L_EA71:
        ld bc,A_EA7A
        call L_E3A7
        jr L_EA86
A_EA7A:
        db 0x42
        db 0x61
        db 0x64
        db 0x20
        db 0x6c
        db 0x6f
        db 0x61
        db 0x64
        db 0x00
A_EA83:
        db 0x43
        db 0x4f
        db 0x4d
L_EA86:
        call L_E766
L_EA89:
        call L_E55E
        ld a,(A_EACE)
        sub 0x20
        ld hl,A_EAF0
        or (hl)
        jp nz,L_E509
        jp L_E682
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
A_EAAB:
        db 0x00
A_EAAC:
        db 0x00
        db 0x24
        db 0x24
        db 0x24
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x53
        db 0x55
        db 0x42
        db 0x00
        db 0x00
A_EABA:
        db 0x00
A_EABB:
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
A_EACC:
        db 0x00
A_EACD:
        db 0x00
A_EACE:
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
A_EAD6:
        db 0x20
        db 0x20
        db 0x20
        db 0x00
        db 0x00
        db 0x00
        db 0x00
A_EADD:
        db 0x00
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x20
        db 0x00
        db 0x00
        db 0x00
        db 0x00
A_EAED:
        db 0x00
A_EAEE:
        db 0x00
A_EAEF:
        db 0x00
A_EAF0:
        db 0x00
A_EAF1:
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00

        defs 0xef00-$,0
        org 0xef00

A_EB00:
        db 0x00
        db 0x16
        db 0x00
        db 0x00
        db 0x00
        db 0x00
BDOS_ENTRY:
        jr L_EB11
BDOS_ERROR_POINTERS:
        dw BDOS_ERROR_0
A_EB0B:
        dw BDOS_ERROR_1
A_EB0D:
        dw BDOS_ERROR_2
A_EB0F:
        dw BDOS_ERROR_3
L_EB11:
        ex de,hl
        ld (A_EE43),hl
        ex de,hl
        ld a,e
        ld (A_F8D6),a
        ld hl,0x0000
        ld (A_EE45),hl
        add hl,sp
        ld (A_EE0F),hl
        ld sp,A_EE41
        xor a
        ld (A_F8E0),a
        ld (A_F8DE),a
        ld hl,BDOS_RETURN_CLEANUP
        push hl
        ld a,c
        cp 0x29
        ret nc
        ld c,e
        ld hl,BDOS_FUNCTION_POINTERS
        ld e,a
        ld d,0x00
        add hl,de
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld hl,(A_EE43)
        ex de,hl
        jp (hl)
BDOS_FUNCTION_POINTERS:
        dw BIOS_BASE+0x03
        dw BDOS_FUNCTION_1
        dw BDOS_FUNCTION_2
        dw BDOS_FUNCTION_3
        dw BIOS_BASE+0x12
        dw BIOS_BASE+0x0f
        dw BDOS_FUNCTION_6
        dw BDOS_FUNCTION_7
        dw BDOS_FUNCTION_8
        dw BDOS_FUNCTION_9
        dw BDOS_FUNCTION_10
        dw BDOS_FUNCTION_11
        dw BDOS_FUNCTION_12
        dw BDOS_FUNCTION_13
        dw BDOS_FUNCTION_14
        dw BDOS_FUNCTION_15
        dw BDOS_FUNCTION_16
        dw BDOS_FUNCTION_17
        dw BDOS_FUNCTION_18
        dw BDOS_FUNCTION_19
        dw BDOS_FUNCTION_20
        dw BDOS_FUNCTION_21
        dw BDOS_FUNCTION_22
        dw BDOS_FUNCTION_23
        dw BDOS_FUNCTION_24
        dw BDOS_FUNCTION_25
        dw BDOS_FUNCTION_26
        dw BDOS_FUNCTION_27
        dw BDOS_FUNCTION_28
        dw BDOS_FUNCTION_29
        dw BDOS_FUNCTION_30
        dw BDOS_FUNCTION_31
        dw BDOS_FUNCTION_32
        dw BDOS_FUNCTION_33
        dw BDOS_FUNCTION_34
        dw BDOS_FUNCTION_35
        dw BDOS_FUNCTION_36
        dw BDOS_FUNCTION_37
        dw BDOS_FUNCTION_39
        dw BDOS_FUNCTION_39
        dw BDOS_FUNCTION_40
BDOS_ERROR_0:
        ld hl,A_EBCA
        call L_EBE5
        cp 0x03
        jp z,0x0000
        ret
BDOS_ERROR_1:
        ld hl,A_EBD5
        jr L_EBB4
BDOS_ERROR_2:
        ld hl,A_EBE1
        jr L_EBB4
BDOS_ERROR_3:
        ld hl,A_EBDC
L_EBB4:
        call L_EBE5
        jp 0x0000
A_EBBA:
        db 0x42
        db 0x64
        db 0x6f
        db 0x73
        db 0x20
        db 0x45
        db 0x72
        db 0x72
        db 0x20
        db 0x4f
        db 0x6e
        db 0x20
A_EBC6:
        db 0x20
        db 0x3a
        db 0x20
        db 0x24
A_EBCA:
        db 0x42
        db 0x61
        db 0x64
        db 0x20
        db 0x53
        db 0x65
        db 0x63
        db 0x74
        db 0x6f
        db 0x72
        db 0x24
A_EBD5:
        db 0x53
        db 0x65
        db 0x6c
        db 0x65
        db 0x63
        db 0x74
        db 0x24
A_EBDC:
        db 0x46
        db 0x69
        db 0x6c
        db 0x65
        db 0x20
A_EBE1:
        db 0x52
        db 0x2f
        db 0x4f
        db 0x24
L_EBE5:
        push hl
        call L_ECC9
        ld a,(A_EE42)
        add a,0x41
        ld (A_EBC6),a
        ld bc,A_EBBA
        call L_ECD3
        pop bc
        call L_ECD3
L_EBFB:
        ld hl,A_EE0E
        ld a,(hl)
        ld (hl),0x00
        or a
        ret nz
        jp BIOS_BASE+0x09
L_EC06:
        call L_EBFB
        call L_EC14
        ret c
        push af
        ld c,a
        call BDOS_FUNCTION_2
        pop af
        ret
L_EC14:
        cp 0x0d
        ret z
        cp 0x0a
        ret z
        cp 0x09
        ret z
        cp 0x08
        ret z
        cp 0x20
        ret
L_EC23:
        ld a,(A_EE0E)
        or a
        jr nz,L_EC45
        call BIOS_BASE+0x06
        and 0x01
        ret z
        call BIOS_BASE+0x09
        cp 0x13
        jr nz,L_EC42
        call BIOS_BASE+0x09
        cp 0x03
        jp z,0x0000
        xor a
        ret
L_EC42:
        ld (A_EE0E),a
L_EC45:
        ld a,0x01
        ret
L_EC48:
        ld a,(A_EE0A)
        or a
        jr nz,L_EC62
        push bc
        call L_EC23
        pop bc
        push bc
        call BIOS_BASE+0x0c
        pop bc
        push bc
        ld a,(A_EE0D)
        or a
        call nz,BIOS_BASE+0x0f
        pop bc
L_EC62:
        ld a,c
        ld hl,A_EE0C
        cp 0x7f
        ret z
        inc (hl)
        cp 0x20
        ret nc
        dec (hl)
        ld a,(hl)
        or a
        ret z
        ld a,c
        cp 0x08
        jr nz,L_EC79
        dec (hl)
        ret
L_EC79:
        cp 0x0a
        ret nz
        ld (hl),0x00
        ret
L_EC7F:
        ld a,c
        call L_EC14
        jr nc,BDOS_FUNCTION_2
        push af
        ld c,0x5e
        call L_EC48
        pop af
        or 0x40
        ld c,a
BDOS_FUNCTION_2:
        ld a,c
        cp 0x09
        jr nz,L_EC48
L_EC96:
        ld c,0x20
        call L_EC48
        ld a,(A_EE0C)
        and 0x07
        jr nz,L_EC96
        ret
L_ECA4:
        call L_ECAC
        ld c,0x20
        call BIOS_BASE+0x0c
L_ECAC:
        ld c,0x08
        jp BIOS_BASE+0x0c
L_ECB1:
        ld c,0x23
        call L_EC48
        call L_ECC9
L_ECB9:
        ld a,(A_EE0C)
        ld hl,A_EE0B
        cp (hl)
        ret nc
        ld c,0x20
        call L_EC48
        jr L_ECB9
L_ECC9:
        ld c,0x0d
        call L_EC48
        ld c,0x0a
        jp L_EC48
L_ECD3:
        ld a,(bc)
        cp 0x24
        ret z
        inc bc
        push bc
        ld c,a
        call BDOS_FUNCTION_2
        pop bc
        jr L_ECD3
BDOS_FUNCTION_10:
        ld a,(A_EE0C)
        ld (A_EE0B),a
        ld hl,(A_EE43)
        ld c,(hl)
        inc hl
        push hl
        ld b,0x00
L_ECEF:
        push bc
        push hl
L_ECF1:
        call L_EBFB
        and 0x7f
        pop hl
        pop bc
        cp 0x0d
        jp z,L_EDC1
        cp 0x0a
        jp z,L_EDC1
        cp 0x08
        jr nz,L_ED16
        ld a,b
        or a
        jr z,L_ECEF
        dec b
        ld a,(A_EE0C)
        ld (A_EE0A),a
        jr L_ED70
L_ED16:
        cp 0x7f
        jr nz,L_ED26
        ld a,b
        or a
        jr z,L_ECEF
        ld a,(hl)
        dec b
        dec hl
        jp L_EDA9
L_ED26:
        cp 0x05
        jr nz,L_ED37
        push bc
        push hl
        call L_ECC9
        xor a
        ld (A_EE0B),a
        jr L_ECF1
L_ED37:
        cp 0x10
        jr nz,L_ED48
        push hl
        ld hl,A_EE0D
        ld a,0x01
        sub (hl)
        ld (hl),a
        pop hl
        jr L_ECEF
L_ED48:
        cp 0x18
        jr nz,L_ED5F
        pop hl
L_ED4E:
        ld a,(A_EE0B)
        ld hl,A_EE0C
        cp (hl)
        jr nc,BDOS_FUNCTION_10
        dec (hl)
        call L_ECA4
        jr L_ED4E
L_ED5F:
        cp 0x15
        jr nz,L_ED6B
        call L_ECB1
        pop hl
        jp BDOS_FUNCTION_10
L_ED6B:
        cp 0x12
        jr nz,L_EDA6
L_ED70:
        push bc
        call L_ECB1
        pop bc
        pop hl
        push hl
        push bc
L_ED78:
        ld a,b
        or a
        jr z,L_ED8A
        inc hl
        ld c,(hl)
        dec b
        push bc
        push hl
        call L_EC7F
        pop hl
        pop bc
        jr L_ED78
L_ED8A:
        push hl
        ld a,(A_EE0A)
        or a
        jp z,L_ECF1
        ld hl,A_EE0C
        sub (hl)
        ld (A_EE0A),a
L_ED99:
        call L_ECA4
        ld hl,A_EE0A
        dec (hl)
        jr nz,L_ED99
        jp L_ECF1
L_EDA6:
        inc hl
        ld (hl),a
        inc b
L_EDA9:
        push bc
        push hl
        ld c,a
        call L_EC7F
        pop hl
        pop bc
        ld a,(hl)
        cp 0x03
        ld a,b
        jr nz,L_EDBD
        cp 0x01
        jp z,0x0000
L_EDBD:
        cp c
        jp c,L_ECEF
L_EDC1:
        pop hl
        ld (hl),b
        ld c,0x0d
        jp L_EC48
BDOS_FUNCTION_1:
        call L_EC06
        jr L_EE01
BDOS_FUNCTION_3:
        call BIOS_BASE+0x15
        jr L_EE01
BDOS_FUNCTION_6:
        ld a,c
        inc a
        jr z,L_EDE0
        inc a
        jp z,BIOS_BASE+0x06
        jp BIOS_BASE+0x0c
L_EDE0:
        call BIOS_BASE+0x06
        or a
        jp z,L_F891
        call BIOS_BASE+0x09
        jr L_EE01
BDOS_FUNCTION_7:
        ld a,(0x0003)
        jr L_EE01
BDOS_FUNCTION_8:
        ld hl,0x0003
        ld (hl),c
        ret
BDOS_FUNCTION_9:
        ex de,hl
        ld c,l
        ld b,h
        jp L_ECD3
BDOS_FUNCTION_11:
        call L_EC23
L_EE01:
        ld (A_EE45),a
BDOS_FUNCTION_39:
        ret
L_EE05:
        ld a,0x01
        jr L_EE01
A_EE0A:
        db 0x00
A_EE0B:
        db 0x02
A_EE0C:
        db 0x00
A_EE0D:
        db 0x00
A_EE0E:
        db 0x00
A_EE0F:
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
A_EE41:
        db 0x00
A_EE42:
        db 0x00
A_EE43:
        db 0x00
        db 0x00
A_EE45:
        db 0x00
        db 0x00
L_EE47:
        ld hl,A_EB0B
L_EE4A:
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        jp (hl)
L_EE4F:
        inc c
L_EE50:
        dec c
        ret z
        ld a,(de)
        ld (hl),a
        inc de
        inc hl
        jr L_EE50
L_EE59:
        ld a,(A_EE42)
        ld c,a
        call BIOS_BASE+0x1b
        ld a,h
        or l
        ret z
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld (A_F8B3),hl
        inc hl
        inc hl
        ld (A_F8B5),hl
        inc hl
        inc hl
        ld (A_F8B7),hl
        inc hl
        inc hl
        ex de,hl
        ld (A_F8D0),hl
        ld hl,A_F8B9
        ld c,0x08
        call L_EE4F
        ld hl,(A_F8BB)
        ex de,hl
        ld hl,A_F8C1
        ld c,0x0f
        call L_EE4F
        ld hl,(A_F8C6)
        ld a,h
        ld hl,A_F8DD
        ld (hl),0xff
        or a
        jr z,L_EE9D
        ld (hl),0x00
L_EE9D:
        ld a,0xff
        or a
        ret
L_EEA1:
        call BIOS_BASE+0x18
        xor a
        ld hl,(A_F8B5)
        ld (hl),a
        inc hl
        ld (hl),a
        ld hl,(A_F8B7)
        ld (hl),a
        inc hl
        ld (hl),a
        ret
L_EEB2:
        call BIOS_BASE+0x27
        jr L_EEBB
L_EEB8:
        call BIOS_BASE+0x2a
L_EEBB:
        or a
        ret z
        ld hl,BDOS_ERROR_POINTERS
        jr L_EE4A
L_EEC3:
        ld hl,(A_F8EA)
        ld c,0x02
        call L_EFEA
        ld (A_F8E5),hl
        ld (A_F8EC),hl
L_EED1:
        ld hl,A_F8E5
        ld c,(hl)
        inc hl
        ld b,(hl)
        ld hl,(A_F8B7)
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld hl,(A_F8B5)
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
L_EEE4:
        ld a,c
        sub e
        ld a,b
        sbc a,d
        jr nc,L_EEFA
        push hl
        ld hl,(A_F8C1)
        or a
        ex de,hl
        sbc hl,de
        ex de,hl
        pop hl
        dec hl
        jr L_EEE4
L_EEFA:
        push hl
        ld hl,(A_F8C1)
        add hl,de
        jr c,L_EF0F
        ld a,c
        sub l
        ld a,b
        sbc a,h
        jr c,L_EF0F
        ex de,hl
        pop hl
        inc hl
        jr L_EEFA
L_EF0F:
        pop hl
        push bc
        push de
        push hl
        ex de,hl
        ld hl,(A_F8CE)
        add hl,de
        ld b,h
        ld c,l
        call BIOS_BASE+0x1e
        pop de
        ld hl,(A_F8B5)
        ld (hl),e
        inc hl
        ld (hl),d
        pop de
        ld hl,(A_F8B7)
        ld (hl),e
        inc hl
        ld (hl),d
        pop bc
        ld a,c
        sub e
        ld c,a
        ld a,b
        sbc a,d
        ld b,a
        ld hl,(A_F8D0)
        ex de,hl
        call BIOS_BASE+0x30
        ld c,l
        ld b,h
        jp BIOS_BASE+0x21
L_EF3E:
        ld hl,A_F8C3
        ld b,(hl)
        ld a,(A_F8E3)
L_EF45:
        or a
        rra
        djnz L_EF45
        ld b,a
        ld a,0x08
        sub (hl)
        ld c,a
        ld a,(A_F8E2)
L_EF53:
        dec c
        jr z,L_EF5C
        or a
        rla
        jr L_EF53
L_EF5C:
        add a,b
        ret
L_EF5E:
        ld hl,(A_EE43)
        ld de,0x0010
        add hl,de
        add hl,bc
        ld a,(A_F8DD)
        or a
        jr z,L_EF71
        ld l,(hl)
        ld h,0x00
        ret
L_EF71:
        add hl,bc
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        ret
L_EF77:
        call L_EF3E
        ld c,a
        ld b,0x00
        call L_EF5E
        ld (A_F8E5),hl
        ret
L_EF84:
        ld hl,(A_F8E5)
        ld a,l
        or h
        ret
L_EF8A:
        ld a,(A_F8C3)
        ld hl,(A_F8E5)
L_EF90:
        add hl,hl
        dec a
        jr nz,L_EF90
        ld (A_F8E7),hl
        ld a,(A_F8C4)
        ld c,a
        ld a,(A_F8E3)
        and c
        or l
        ld l,a
        ld (A_F8E5),hl
        ret
L_EFA6:
        ld hl,(A_EE43)
        ld de,0x000c
        add hl,de
        ret
L_EFAE:
        ld hl,(A_EE43)
        ld de,0x000f
        add hl,de
        ex de,hl
        ld hl,0x0011
        add hl,de
        ret
L_EFBB:
        call L_EFAE
        ld a,(hl)
        ld (A_F8E3),a
        ex de,hl
        ld a,(hl)
        ld (A_F8E1),a
        call L_EFA6
        ld a,(A_F8C5)
        and (hl)
        ld (A_F8E2),a
        ret
L_EFD2:
        call L_EFAE
        ld a,(A_F8D5)
        cp 0x02
        jr nz,L_EFDE
        xor a
L_EFDE:
        ld c,a
        ld a,(A_F8E3)
        add a,c
        ld (hl),a
        ex de,hl
        ld a,(A_F8E1)
        ld (hl),a
        ret
L_EFEA:
        inc c
L_EFEB:
        dec c
        ret z
        ld a,h
        or a
        rra
        ld h,a
        ld a,l
        rra
        ld l,a
        jr L_EFEB
L_EFF7:
        ld c,0x80
        ld hl,(A_F8B9)
        xor a
L_EFFD:
        add a,(hl)
        inc hl
        dec c
        jr nz,L_EFFD
        ret
L_F004:
        inc c
L_F005:
        dec c
        ret z
        add hl,hl
        jr L_F005
L_F00B:
        push bc
        ld a,(A_EE42)
        ld c,a
        ld hl,0x0001
        call L_F004
        pop bc
        ld a,c
        or l
        ld l,a
        ld a,b
        or h
        ld h,a
        ret
L_F01E:
        ld hl,(A_F8AD)
        ld a,(A_EE42)
        ld c,a
        call L_EFEA
        ld a,l
        and 0x01
        ret
BDOS_FUNCTION_28:
        ld hl,A_F8AD
        ld c,(hl)
        inc hl
        ld b,(hl)
        call L_F00B
        ld (A_F8AD),hl
        ld hl,(A_F8C8)
        inc hl
        ex de,hl
        ld hl,(A_F8B3)
        ld (hl),e
        inc hl
        ld (hl),d
        ret
L_F044:
        call L_F05E
L_F047:
        ld de,0x0009
        add hl,de
        ld a,(hl)
        rla
        ret nc
        ld hl,A_EB0F
        jp L_EE4A
L_F054:
        call L_F01E
        ret z
        ld hl,A_EB0D
        jp L_EE4A
L_F05E:
        ld hl,(A_F8B9)
        ld a,(A_F8E9)
L_F064:
        add a,l
        ld l,a
        ret nc
        inc h
        ret
L_F069:
        ld hl,(A_EE43)
        ld de,0x000e
        add hl,de
        ld a,(hl)
        ret
L_F072:
        call L_F069
        ld (hl),0x00
        ret
L_F078:
        call L_F069
        or 0x80
        ld (hl),a
        ret
L_F07F:
        ld hl,(A_F8EA)
        ex de,hl
        ld hl,(A_F8B3)
        ld a,e
        sub (hl)
        inc hl
        ld a,d
        sbc a,(hl)
        ret
L_F08C:
        call L_F07F
        ret c
        inc de
        ld (hl),d
        dec hl
        ld (hl),e
        ret
L_F095:
        ld a,e
        sub l
        ld l,a
        ld a,d
        sbc a,h
        ld h,a
        ret
L_F09C:
        ld c,0xff
L_F09E:
        ld hl,(A_F8EC)
        ex de,hl
        ld hl,(A_F8CC)
        call L_F095
        ret nc
        push bc
        call L_EFF7
        ld hl,(A_F8BD)
        ex de,hl
        ld hl,(A_F8EC)
        add hl,de
        pop bc
        inc c
        jr z,L_F0C4
        cp (hl)
        ret z
        call L_F07F
        ret nc
        jp BDOS_FUNCTION_28
L_F0C4:
        ld (hl),a
        ret
L_F0C6:
        call L_F09C
        call L_F0E0
        ld c,0x01
        call L_EEB8
        jr L_F0DA
L_F0D4:
        call L_F0E0
        call L_EEB2
L_F0DA:
        ld hl,A_F8B1
        jr L_F0E3
L_F0E0:
        ld hl,A_F8B9
L_F0E3:
        ld c,(hl)
        inc hl
        ld b,(hl)
        jp BIOS_BASE+0x24
L_F0E9:
        ld hl,(A_F8B9)
        ex de,hl
        ld hl,(A_F8B1)
        ld c,0x80
        jp L_EE4F
L_F0F5:
        ld hl,A_F8EA
        ld a,(hl)
        inc hl
        cp (hl)
        ret nz
        inc a
        ret
L_F0FE:
        ld hl,0xffff
        ld (A_F8EA),hl
        ret
L_F105:
        ld hl,(A_F8C8)
        ex de,hl
        ld hl,(A_F8EA)
        inc hl
        ld (A_F8EA),hl
        call L_F095
        jr c,L_F0FE
L_F119:
        ld a,(A_F8EA)
        and 0x03
        ld b,0x05
L_F120:
        add a,a
        djnz L_F120
        ld (A_F8E9),a
        or a
        ret nz
        push bc
        call L_EEC3
        call L_F0D4
        pop bc
        jp L_F09E
L_F135:
        ld a,c
        and 0x07
        inc a
        ld e,a
        ld d,a
        ld a,c
        rrca
        rrca
        rrca
        and 0x1f
        ld c,a
        ld a,b
        add a,a
        add a,a
        add a,a
        add a,a
        add a,a
        or c
        ld c,a
        ld a,b
        rrca
        rrca
        rrca
        and 0x1f
        ld b,a
        ld hl,(A_F8BF)
        add hl,bc
        ld a,(hl)
L_F156:
        rlca
        dec e
        jr nz,L_F156
        ret
L_F15C:
        push de
        call L_F135
        and 0xfe
        pop bc
        or c
L_F164:
        rrca
        dec d
        jr nz,L_F164
        ld (hl),a
        ret
L_F16B:
        call L_F05E
        ld de,0x0010
        add hl,de
        push bc
        ld c,0x11
L_F175:
        pop de
        dec c
        ret z
        push de
        ld a,(A_F8DD)
        or a
        jr z,L_F188
        push bc
        push hl
        ld c,(hl)
        ld b,0x00
        jr L_F18E
L_F188:
        dec c
        push bc
        ld c,(hl)
        inc hl
        ld b,(hl)
        push hl
L_F18E:
        ld a,c
        or b
        jr z,L_F19D
        ld hl,(A_F8C6)
        ld a,l
        sub c
        ld a,h
        sbc a,b
        call nc,L_F15C
L_F19D:
        pop hl
        inc hl
        pop bc
        jr L_F175
L_F1A3:
        ld hl,(A_F8C6)
        ld c,0x03
        call L_EFEA
        inc hl
        ld b,h
        ld c,l
        ld hl,(A_F8BF)
L_F1B1:
        ld (hl),0x00
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,L_F1B1
        ld hl,(A_F8CA)
        ex de,hl
        ld hl,(A_F8BF)
        ld (hl),e
        inc hl
        ld (hl),d
        call L_EEA1
        ld hl,(A_F8B3)
        ld (hl),0x03
        inc hl
        ld (hl),0x00
        call L_F0FE
L_F1D2:
        ld c,0xff
        call L_F105
        call L_F0F5
        ret z
        call L_F05E
        ld a,0xe5
        cp (hl)
        jr z,L_F1D2
        ld a,(A_EE41)
        cp (hl)
        jr nz,L_F1F6
        inc hl
        ld a,(hl)
        sub 0x24
        jr nz,L_F1F6
        dec a
        ld (A_EE45),a
L_F1F6:
        ld c,0x01
        call L_F16B
        call L_F08C
        jr L_F1D2
L_F201:
        ld a,(A_F8D4)
        jp L_EE01
L_F207:
        push bc
        push af
        ld a,(A_F8C5)
        cpl
        ld b,a
        ld a,c
        and b
        ld c,a
        pop af
        and b
        sub c
        and 0x1f
        pop bc
        ret
L_F218:
        ld a,0xff
        ld (A_F8D4),a
        ld hl,A_F8D8
        ld (hl),c
        ld hl,(A_EE43)
        ld (A_F8D9),hl
        call L_F0FE
        call L_EEA1
L_F22D:
        ld c,0x00
        call L_F105
        call L_F0F5
        jr z,L_F294
        ld hl,(A_F8D9)
        ex de,hl
        ld a,(de)
        cp 0xe5
        jr z,L_F24A
        push de
        call L_F07F
        pop de
        jr nc,L_F294
L_F24A:
        call L_F05E
        ld a,(A_F8D8)
        ld c,a
        ld b,0x00
L_F253:
        ld a,c
        or a
        jr z,L_F283
        ld a,(de)
        cp 0x3f
        jr z,L_F27C
        ld a,b
        cp 0x0d
        jr z,L_F27C
        cp 0x0c
        ld a,(de)
        jr z,L_F273
        sub (hl)
        and 0x7f
        jr nz,L_F22D
        jr L_F27C
L_F273:
        push bc
        ld c,(hl)
        call L_F207
        pop bc
        jr nz,L_F22D
L_F27C:
        inc de
        inc hl
        inc b
        dec c
        jr L_F253
L_F283:
        ld a,(A_F8EA)
        and 0x03
        ld (A_EE45),a
        ld hl,A_F8D4
        ld a,(hl)
        rla
        ret nc
        xor a
        ld (hl),a
        ret
L_F294:
        call L_F0FE
        ld a,0xff
        jp L_EE01
L_F29C:
        call L_F054
        ld c,0x0c
        call L_F218
L_F2A4:
        call L_F0F5
        ret z
        call L_F044
        call L_F05E
        ld (hl),0xe5
        ld c,0x00
        call L_F16B
        call L_F0C6
        call L_F22D
        jr L_F2A4
L_F2BE:
        ld d,b
        ld e,c
L_F2C0:
        ld a,c
        or b
        jr z,L_F2D1
        dec bc
        push de
        push bc
        call L_F135
        rra
        jr nc,L_F2EC
        pop bc
        pop de
L_F2D1:
        ld hl,(A_F8C6)
        ld a,e
        sub l
        ld a,d
        sbc a,h
        jr nc,L_F2F4
        inc de
        push bc
        push de
        ld b,d
        ld c,e
        call L_F135
        rra
        jr nc,L_F2EC
        pop de
        pop bc
        jr L_F2C0
L_F2EC:
        rla
        inc a
        call L_F164
        pop hl
        pop de
        ret
L_F2F4:
        ld a,c
        or b
        jr nz,L_F2C0
        ld hl,0x0000
        ret
L_F2FD:
        ld c,0x00
        ld e,0x20
L_F301:
        push de
        ld b,0x00
        ld hl,(A_EE43)
        add hl,bc
        ex de,hl
        call L_F05E
        pop bc
        call L_EE4F
L_F310:
        call L_EEC3
        jp L_F0C6
L_F316:
        call L_F054
        ld c,0x0c
        call L_F218
        ld hl,(A_EE43)
        ld a,(hl)
        ld de,0x0010
        add hl,de
        ld (hl),a
L_F327:
        call L_F0F5
        ret z
        call L_F044
        ld c,0x10
        ld e,0x0c
        call L_F301
        call L_F22D
        jr L_F327
L_F33B:
        ld c,0x0c
        call L_F218
L_F340:
        call L_F0F5
        ret z
        ld c,0x00
        ld e,0x0c
        call L_F301
        call L_F22D
        jr L_F340
L_F351:
        ld c,0x0f
        call L_F218
        call L_F0F5
        ret z
L_F35A:
        call L_EFA6
        ld a,(hl)
        push af
        push hl
        call L_F05E
        ex de,hl
        ld hl,(A_EE43)
        ld c,0x20
        push de
        call L_EE4F
        call L_F078
        pop de
        ld hl,0x000c
        add hl,de
        ld c,(hl)
        ld hl,0x000f
        add hl,de
        ld b,(hl)
        pop hl
        pop af
        ld (hl),a
        ld a,c
        cp (hl)
        ld a,b
        jr z,L_F38B
        ld a,0x00
        jr c,L_F38B
        ld a,0x80
L_F38B:
        ld hl,(A_EE43)
        ld de,0x000f
        add hl,de
        ld (hl),a
        ret
L_F394:
        ld a,(hl)
        inc hl
        or (hl)
        dec hl
        ret nz
        ld a,(de)
        ld (hl),a
        inc de
        inc hl
        ld a,(de)
        ld (hl),a
        dec de
        dec hl
        ret
L_F3A2:
        xor a
        ld (A_EE45),a
        ld (A_F8EA),a
        ld (A_F8EB),a
        call L_F01E
        ret nz
        call L_F069
        and 0x80
        ret nz
        ld c,0x0f
        call L_F218
        call L_F0F5
        ret z
        ld bc,0x0010
        call L_F05E
        add hl,bc
        ex de,hl
        ld hl,(A_EE43)
        add hl,bc
        ld c,0x10
L_F3CD:
        ld a,(A_F8DD)
        or a
        jr z,L_F3E8
        ld a,(hl)
        or a
        ld a,(de)
        jr nz,L_F3DB
        ld (hl),a
L_F3DB:
        or a
        jr nz,L_F3E1
        ld a,(hl)
        ld (de),a
L_F3E1:
        cp (hl)
        jr nz,L_F41F
        jr L_F3FD
L_F3E8:
        call L_F394
        ex de,hl
        call L_F394
        ex de,hl
        ld a,(de)
        cp (hl)
        jr nz,L_F41F
        inc de
        inc hl
        ld a,(de)
        cp (hl)
        jr nz,L_F41F
        dec c
L_F3FD:
        inc de
        inc hl
        dec c
        jr nz,L_F3CD
        ld bc,0xffec
        add hl,bc
        ex de,hl
        add hl,bc
        ld a,(de)
        cp (hl)
        jr c,L_F417
        ld (hl),a
        ld bc,0x0003
        add hl,bc
        ex de,hl
        add hl,bc
        ld a,(hl)
        ld (de),a
L_F417:
        ld a,0xff
        ld (A_F8D2),a
        jp L_F310
L_F41F:
        ld hl,A_EE45
        dec (hl)
        ret
L_F424:
        call L_F054
        ld hl,(A_EE43)
        push hl
        ld hl,A_F8AC
        ld (A_EE43),hl
        ld c,0x01
        call L_F218
        call L_F0F5
        pop hl
        ld (A_EE43),hl
        ret z
        ex de,hl
        ld hl,0x000f
        add hl,de
        ld c,0x11
        xor a
L_F446:
        ld (hl),a
        inc hl
        dec c
        jr nz,L_F446
        ld hl,0x000d
        add hl,de
        ld (hl),a
        call L_F08C
        call L_F2FD
        jp L_F078
L_F45A:
        xor a
        ld (A_F8D2),a
        call L_F3A2
        call L_F0F5
        ret z
        ld hl,(A_EE43)
        ld bc,0x000c
        add hl,bc
        ld a,(hl)
        inc a
        and 0x1f
        ld (hl),a
        jr z,L_F483
        ld b,a
        ld a,(A_F8C5)
        and b
        ld hl,A_F8D2
        and (hl)
        jr z,L_F48E
        jr L_F4AC
L_F483:
        ld bc,0x0002
        add hl,bc
        inc (hl)
        ld a,(hl)
        and 0x0f
        jr z,L_F4B6
L_F48E:
        ld c,0x0f
        call L_F218
        call L_F0F5
        jr nz,L_F4AC
        ld a,(A_F8D3)
        inc a
        jr z,L_F4B6
        call L_F424
        call L_F0F5
        jr z,L_F4B6
        jr L_F4AF
L_F4AC:
        call L_F35A
L_F4AF:
        call L_EFBB
        xor a
        jp L_EE01
L_F4B6:
        call L_EE05
        jp L_F078
L_F4BC:
        ld a,0x01
        ld (A_F8D5),a
L_F4C1:
        ld a,0xff
        ld (A_F8D3),a
        call L_EFBB
        ld a,(A_F8E3)
        ld hl,A_F8E1
        cp (hl)
        jr c,L_F4E6
        cp 0x80
        jp nz,L_EE05
        call L_F45A
        xor a
        ld (A_F8E3),a
        ld a,(A_EE45)
        or a
        jp nz,L_EE05
L_F4E6:
        call L_EF77
        call L_EF84
        jp z,L_EE05
        call L_EF8A
        call L_EED1
        call L_EEB2
        jp L_EFD2
L_F4FB:
L_F4FE:
        ld a,0x01
        ld (A_F8D5),a
L_F503:
        xor a
        ld (A_F8D3),a
        call L_F054
        ld hl,(A_EE43)
        call L_F047
        call L_EFBB
        ld a,(A_F8E3)
        cp 0x80
        jp nc,L_EE05
        call L_EF77
        call L_EF84
        ld c,0x00
        jr nz,L_F56E
        call L_EF3E
        ld (A_F8D7),a
        ld bc,0x0000
        or a
        jr z,L_F53B
        ld c,a
        dec bc
        call L_EF5E
        ld b,h
        ld c,l
L_F53B:
        call L_F2BE
        ld a,l
        or h
        jr nz,L_F548
        ld a,0x02
        jp L_EE01
L_F548:
        ld (A_F8E5),hl
        ex de,hl
        ld hl,(A_EE43)
        ld bc,0x0010
        add hl,bc
        ld a,(A_F8DD)
        or a
        ld a,(A_F8D7)
        jr z,L_F564
        call L_F064
        ld (hl),e
        jr L_F56C
L_F564:
        ld c,a
        ld b,0x00
        add hl,bc
        add hl,bc
        ld (hl),e
        inc hl
        ld (hl),d
L_F56C:
        ld c,0x02
L_F56E:
        ld a,(A_EE45)
        or a
        ret nz
        push bc
        call L_EF8A
        ld a,(A_F8D5)
        sub 2
        jr nz,L_F5BB
        pop bc
        push bc
        ld a,c
        sub 2
        jr nz,L_F5BB
        push hl
        ld hl,(A_F8B9)
        ld d,a
L_F58C:
        ld (hl),a
        inc hl
        inc d
        jp p,L_F58C
        call L_F0E0
        ld hl,(A_F8E7)
        ld c,0x02
L_F59A:
        ld (A_F8E5),hl
        push bc
        call L_EED1
        pop bc
        call L_EEB8
        ld hl,(A_F8E5)
        ld c,0x00
        ld a,(A_F8C4)
        ld b,a
        and l
        cp b
        inc hl
        jr nz,L_F59A
        pop hl
        ld (A_F8E5),hl
        call L_F0DA
L_F5BB:
        call L_EED1
        pop bc
        push bc
        call L_EEB8
        pop bc
        ld a,(A_F8E3)
        ld hl,A_F8E1
        cp (hl)
        jr c,L_F5D2
        ld (hl),a
        inc (hl)
        ld c,0x02
L_F5D2:
        nop
        nop
        ld hl,0x0000
        push af
        call L_F069
        and 0x7f
        ld (hl),a
        pop af
        cp 0x7f
        jr nz,L_F600
        ld a,(A_F8D5)
        cp 0x01
        jr nz,L_F600
        call L_EFD2
        call L_F45A
        ld hl,A_EE45
        ld a,(hl)
        or a
        jr nz,L_F5FE
        dec a
        ld (A_F8E3),a
L_F5FE:
        ld (hl),0x00
L_F600:
        jp L_EFD2
L_F603:
        xor a
        ld (A_F8D5),a
L_F607:
        push bc
        ld hl,(A_EE43)
        ex de,hl
        ld hl,0x0021
        add hl,de
        ld a,(hl)
        and 0x7f
        push af
        ld a,(hl)
        rla
        inc hl
        ld a,(hl)
        rla
        and 0x1f
        ld c,a
        ld a,(hl)
        rra
        rra
        rra
        rra
        and 0x0f
        ld b,a
        pop af
        inc hl
        ld l,(hl)
        inc l
        dec l
        ld l,0x06
        jr nz,L_F68B
        ld hl,0x0020
        add hl,de
        ld (hl),a
        ld hl,0x000c
        add hl,de
        ld a,c
        sub (hl)
        jr nz,L_F647
        ld hl,0x000e
        add hl,de
        ld a,b
        sub (hl)
        and 0x7f
        jr z,L_F67F
L_F647:
        push bc
        push de
        call L_F3A2
        pop de
        pop bc
        ld l,0x03
        ld a,(A_EE45)
        inc a
        jr z,L_F684
        ld hl,0x000c
        add hl,de
        ld (hl),c
        ld hl,0x000e
        add hl,de
        ld (hl),b
        call L_F351
        ld a,(A_EE45)
        inc a
        jr nz,L_F67F
        pop bc
        push bc
        ld l,0x04
        inc c
        jr z,L_F684
        call L_F424
        ld l,0x05
        ld a,(A_EE45)
        inc a
        jr z,L_F684
L_F67F:
        pop bc
        xor a
        jp L_EE01
L_F684:
        push hl
        call L_F069
        ld (hl),0xc0
        pop hl
L_F68B:
        pop bc
        ld a,l
        ld (A_EE45),a
        jp L_F078
L_F693:
        ld c,0xff
        call L_F603
        ret nz
        jp L_F4C1
L_F69C:
        ld c,0x00
        call L_F603
        ret nz
        jp L_F503
L_F6A5:
        ex de,hl
        add hl,de
        ld c,(hl)
        ld b,0x00
        ld hl,0x000c
        add hl,de
        ld a,(hl)
        rrca
        and 0x80
        add a,c
        ld c,a
        ld a,0x00
        adc a,b
        ld b,a
        ld a,(hl)
        rrca
        and 0x0f
        add a,b
        ld b,a
        ld hl,0x000e
        add hl,de
        ld a,(hl)
        add a,a
        add a,a
        add a,a
        add a,a
        push af
        add a,b
        ld b,a
        push af
        pop hl
        ld a,l
        pop hl
        or l
        and 0x01
        ret
L_F6D2:
        ld c,0x0c
        call L_F218
        ld hl,(A_EE43)
        ld de,0x0021
        add hl,de
        push hl
        ld (hl),d
        inc hl
        ld (hl),d
        inc hl
        ld (hl),d
L_F6E4:
        call L_F0F5
        jr z,L_F70C
        call L_F05E
        ld de,0x000f
        call L_F6A5
        pop hl
        push hl
        ld e,a
        ld a,c
        sub (hl)
        inc hl
        ld a,b
        sbc a,(hl)
        inc hl
        ld a,e
        sbc a,(hl)
        jr c,L_F706
        ld (hl),e
        dec hl
        ld (hl),b
        dec hl
        ld (hl),c
L_F706:
        call L_F22D
        jr L_F6E4
L_F70C:
        pop hl
        ret
BDOS_FUNCTION_36:
        ld hl,(A_EE43)
        ld de,0x0020
        call L_F6A5
        ld hl,0x0021
        add hl,de
        ld (hl),c
        inc hl
        ld (hl),b
        inc hl
        ld (hl),a
        ret
L_F721:
        ld hl,(A_F8AF)
        ld a,(A_EE42)
        ld c,a
        call L_EFEA
        push hl
        ex de,hl
        call L_EE59
        pop hl
        call z,L_EE47
        ld a,l
        rra
        ret c
        ld hl,(A_F8AF)
        ld c,l
        ld b,h
        call L_F00B
        ld (A_F8AF),hl
        jp L_F1A3
BDOS_FUNCTION_14:
        ld a,(A_F8D6)
        ld hl,A_EE42
        cp (hl)
        ret z
        ld (hl),a
        jr L_F721
L_F751:
        ld a,0xff
        ld (A_F8DE),a
        ld hl,(A_EE43)
        ld a,(hl)
        and 0x1f
        dec a
        ld (A_F8D6),a
        cp 0x1e
        jr nc,L_F775
        ld a,(A_EE42)
        ld (A_F8DF),a
        ld a,(hl)
        ld (A_F8E0),a
        and 0xe0
        ld (hl),a
        call BDOS_FUNCTION_14
L_F775:
        ld a,(A_EE41)
        ld hl,(A_EE43)
        or (hl)
        ld (hl),a
        ret
BDOS_FUNCTION_12:
        ld a,0x22
        jp L_EE01
BDOS_FUNCTION_13:
        ld hl,0x0000
        ld (A_F8AD),hl
        ld (A_F8AF),hl
        xor a
        ld (A_EE42),a
        ld hl,0x0080
        ld (A_F8B1),hl
        call L_F0DA
        jr L_F721
BDOS_FUNCTION_15:
        call L_F072
        call L_F751
        jp L_F351
BDOS_FUNCTION_16:
        call L_F751
        jp L_F3A2
BDOS_FUNCTION_17:
        ld c,0x00
        ex de,hl
        ld a,(hl)
        cp 0x3f
        jr z,L_F7C2
        call L_EFA6
        ld a,(hl)
        cp 0x3f
        call nz,L_F072
        call L_F751
        ld c,0x0f
L_F7C2:
        call L_F218
        jp L_F0E9
BDOS_FUNCTION_18:
        ld hl,(A_F8D9)
        ld (A_EE43),hl
        call L_F751
        call L_F22D
        jp L_F0E9
BDOS_FUNCTION_19:
        call L_F751
        call L_F29C
        jp L_F201
BDOS_FUNCTION_20:
        call L_F751
        jp L_F4BC
BDOS_FUNCTION_21:
        call L_F751
        jp L_F4FE
BDOS_FUNCTION_22:
        call L_F072
        call L_F751
        jp L_F424
BDOS_FUNCTION_23:
        call L_F751
        call L_F316
        jp L_F201
BDOS_FUNCTION_24:
        ld hl,(A_F8AF)
        jr L_F829
BDOS_FUNCTION_25:
        ld a,(A_EE42)
        jp L_EE01
BDOS_FUNCTION_26:
        ex de,hl
        ld (A_F8B1),hl
        jp L_F0DA
BDOS_FUNCTION_27:
        ld hl,(A_F8BF)
        jr L_F829
BDOS_FUNCTION_29:
        ld hl,(A_F8AD)
        jr L_F829
BDOS_FUNCTION_30:
        call L_F751
        call L_F33B
        jp L_F201
BDOS_FUNCTION_31:
        ld hl,(A_F8BB)
L_F829:
        ld (A_EE45),hl
        ret
BDOS_FUNCTION_32:
        ld a,(A_F8D6)
        cp 0xff
        jr nz,L_F83B
        ld a,(A_EE41)
        jp L_EE01
L_F83B:
        and 0x1f
        ld (A_EE41),a
        ret
BDOS_FUNCTION_33:
        call L_F751
        jp L_F693
BDOS_FUNCTION_34:
        call L_F751
        jp L_F69C
BDOS_FUNCTION_35:
        call L_F751
        jp L_F6D2
BDOS_FUNCTION_37:
        ld hl,(A_EE43)
        ld a,l
        cpl
        ld e,a
        ld a,h
        cpl
        ld hl,(A_F8AF)
        and h
        ld d,a
        ld a,l
        and e
        ld e,a
        ld hl,(A_F8AD)
        ex de,hl
        ld (A_F8AF),hl
        ld a,l
        and e
        ld l,a
        ld a,h
        and d
        ld h,a
        ld (A_F8AD),hl
        ret
BDOS_RETURN_CLEANUP:
        ld a,(A_F8DE)
        or a
        jr z,L_F891
        ld hl,(A_EE43)
        ld (hl),0x00
        ld a,(A_F8E0)
        or a
        jr z,L_F891
        ld (hl),a
        ld a,(A_F8DF)
        ld (A_F8D6),a
        call BDOS_FUNCTION_14
L_F891:
        ld hl,(A_EE0F)
        ld sp,hl
        ld hl,(A_EE45)
        ld a,l
        ld b,h
        ret
BDOS_FUNCTION_40:
        call L_F751
        ld a,0x02
        ld (A_F8D5),a
        ld c,0x00
        call L_F607
        ret nz
        jp L_F503
A_F8AC:
        db 0xe5
A_F8AD:
        db 0x00
        db 0x00
A_F8AF:
        db 0x00
        db 0x00
A_F8B1:
        db 0x80
        db 0x00
A_F8B3:
        db 0x00
        db 0x00
A_F8B5:
        db 0x00
        db 0x00
A_F8B7:
        db 0x00
        db 0x00
A_F8B9:
        db 0x00
        db 0x00
A_F8BB:
        db 0x00
        db 0x00
A_F8BD:
        db 0x00
        db 0x00
A_F8BF:
        db 0x00
        db 0x00
A_F8C1:
        db 0x00
        db 0x00
A_F8C3:
        db 0x00
A_F8C4:
        db 0x00
A_F8C5:
        db 0x00
A_F8C6:
        db 0x00
        db 0x00
A_F8C8:
        db 0x00
        db 0x00
A_F8CA:
        db 0x00
        db 0x00
A_F8CC:
        db 0x00
        db 0x00
A_F8CE:
        db 0x00
        db 0x00
A_F8D0:
        db 0x00
        db 0x00
A_F8D2:
        db 0x00
A_F8D3:
        db 0x00
A_F8D4:
        db 0x00
A_F8D5:
        db 0x00
A_F8D6:
        db 0x00
A_F8D7:
        db 0x00
A_F8D8:
        db 0x00
A_F8D9:
        db 0x00
        db 0x00
        db 0x00
        db 0x00
A_F8DD:
        db 0x00
A_F8DE:
        db 0x00
A_F8DF:
        db 0x00
A_F8E0:
        db 0x00
A_F8E1:
        db 0x00
A_F8E2:
        db 0x00
A_F8E3:
        db 0x00
        db 0x00
A_F8E5:
        db 0x00
        db 0x00
A_F8E7:
        db 0x00
        db 0x00
A_F8E9:
        db 0x00
A_F8EA:
        db 0x00
A_F8EB:
        db 0x00
A_F8EC:
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00
        db 0x00

        defs 0xfd00-$,0
