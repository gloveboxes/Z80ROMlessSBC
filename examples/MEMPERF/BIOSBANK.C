/* dcc stack ABI -> public BIOS register ABI. No port or private workspace
 * addresses belong here. IX/IY survive; return HL=1 success or 0 failure.
 */
#include "BIOSBANK.H"

#asm
;@dcc.lto begin _bmstat
	public _bmstat
_bmstat:
	call 0ff06h
	ret
;@dcc.lto end _bmstat
;@dcc.lto begin _bmread
	public _bmread
_bmread:
	push ix
	ld ix,0
	add ix,sp
	ld e,(ix+4)
	ld d,(ix+5)
	ld l,(ix+6)
	ld h,(ix+7)
	ld c,(ix+8)
	ld b,(ix+9)
	call 0ff00h
	ld hl,0
	jr c,BMR_END
	inc l
BMR_END:
	pop ix
	ret
;@dcc.lto end _bmread
;@dcc.lto begin _bmwrit
	public _bmwrit
_bmwrit:
	push ix
	ld ix,0
	add ix,sp
	ld e,(ix+4)
	ld d,(ix+5)
	ld l,(ix+6)
	ld h,(ix+7)
	ld c,(ix+8)
	ld b,(ix+9)
	call 0ff03h
	ld hl,0
	jr c,BMW_END
	inc l
BMW_END:
	pop ix
	ret
;@dcc.lto end _bmwrit
#endasm
