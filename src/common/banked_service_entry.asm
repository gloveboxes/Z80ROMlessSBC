; Bank-1 gateway dispatch. The mirrored FF10h entry switches banks first.
; E=0..6 retains the extent allocator ABI. E=7 enters the sole BDOS proxy;
; E=8 enters the fixed-address library loader; 9 queries capabilities,
; 10 uses the versioned library request, 11 returns the last service status.
; Neither service re-enters
; the allocator while its bank-0 caller is suspended.
        org 0e640h
bank_service_entry:
        ld a,e
        cp 11
        jr z,last_status
        xor a
        ld (0dfa2h),a
        ld a,e
        cp 7
        jp c,0e800h
        jr z,bdos_proxy
        cp 8
        jr z,library_loader
        cp 9
        jp z,0ec00h
        cp 10
        jr z,library_control
        jp 0e800h
last_status:
        ld a,(0dfa2h)
        ld l,a
        ld h,0
        jp 0ff13h
bdos_proxy:
        ld (0e622h),sp
        ld sp,0e5c0h
        push ix
        push iy
        call 0de00h
        jr service_return
library_loader:
        ld (0e622h),sp
        ld sp,0e5c0h
        push ix
        push iy
        call 0ebb0h
        jr service_return
library_control:
        ld (0e622h),sp
        ld sp,0e5c0h
        push ix
        push iy
        call 0ec30h
service_return:
        pop iy
        pop ix
        ld sp,(0e622h)
        jp 0ff13h
