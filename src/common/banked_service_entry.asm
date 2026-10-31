; Bank-1 gateway dispatch. The mirrored FF10h entry switches banks first.
; E=0..5 enter the extent allocator, 6 the BDOS proxy used by the bank-0
; shim, 7 queries capabilities, 8 submits a library request and 9 returns
; the last service status. No service re-enters the gateway while its
; bank-0 caller is suspended.
        org 0e640h
bank_service_entry:
        ld a,e
        cp 9
        jr z,last_status
        xor a
        ld (0dfa2h),a
        ld a,e
        cp 6
        jp c,0e800h
        jr z,bdos_proxy
        cp 7
        jp z,0ec00h
        cp 8
        jr z,library_control
        jp 0e800h               ; allocator rejects unknown operations
last_status:
        ld a,(0dfa2h)
        ld l,a
        ld h,0
        jp 0ff13h
bdos_proxy:
        ld de,0de00h
        jr private_stack
library_control:
        ld de,0ec30h            ; HL still carries the request address
private_stack:
        ld (0e622h),sp
        ld sp,0e5c0h
        push ix
        push iy
        ld ix,service_return
        push ix
        push de
        ret                     ; call DE; it returns to service_return
service_return:
        pop iy
        pop ix
        ld sp,(0e622h)
        jp 0ff13h
