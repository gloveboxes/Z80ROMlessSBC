/* dcc / CP/M Z80 banked heap. All persistent allocation metadata is in bank 1.
 * First 256 bytes: header and 64-byte two-bit map; resident code at FF00h.
 * Blocks are 256 bytes; 254 blocks (65,024 bytes) are available to applications.
 * No BDOS, interrupt handler, or C stack access runs with bank 1 selected.
 */
#include <string.h>
#include "BANKMEM.H"
#include "BIOSBANK.H"

/* Capability is stable for this program's lifetime; avoid a trapped IN for
 * every chunk. Heap metadata itself remains entirely in bank 1. */
static unsigned char bank_ready;

/* Private stack-ABI helper below; the live allocation map stays in bank 1. */
unsigned int bmcap(const unsigned char *map, bhandle handle);

/* Calls only public BIOS services; workspace layout belongs to the BIOS. */
static int bursts(unsigned int bank, void *base, unsigned int bytes, int writing)
{
    if (writing)
        return bmwrit(bank, base, bytes);
    return bmread(bank, base, bytes);
}

static int copy_bank(unsigned int address, void *buffer, unsigned int bytes,
                     int writing)
{
    unsigned char tail[BMBIOS_BURST_BYTES];
    unsigned char *cursor = buffer;
    unsigned int bulk, start, skip;
    unsigned int pointer = (unsigned int)buffer;
    if (!bank_ready && !binit())
        return 0;
    if (address > BMBIOS_HEAP_END ||
        bytes > BMBIOS_HEAP_END - address ||
        (bytes && (pointer < 256 || pointer >= BMBIOS_HEAP_END ||
                   bytes > BMBIOS_HEAP_END - pointer)))
        return 0;
    if (!bytes)
        return 1;
    if (!(bytes & (BMBIOS_BURST_BYTES - 1U)))
        return bursts(address, buffer, bytes, writing);
    bulk = bytes & ~(BMBIOS_BURST_BYTES - 1U);
    if (bulk)
    {
        if (!bursts(address, cursor, bulk, writing))
            return 0;
        address = address + bulk;
        cursor = cursor + bulk;
        bytes = bytes - bulk;
    }
    if (bytes)
    {
        start = address;
        if (start > BMBIOS_HEAP_END - BMBIOS_BURST_BYTES)
            start = BMBIOS_HEAP_END - BMBIOS_BURST_BYTES;
        skip = address - start;
        if (!bursts(start, tail, BMBIOS_BURST_BYTES, 0))
            return 0;
        if (writing)
        {
            memcpy(tail + skip, cursor, bytes);
            if (!bursts(start, tail, BMBIOS_BURST_BYTES, 1))
                return 0;
        }
        else
            memcpy(cursor, tail + skip, bytes);
    }
    return 1;
}

static unsigned int tag(const unsigned char *map, unsigned int block)
{
    return (map[block >> 2] >> ((block & 3) * 2)) & 3;
}

static void set_tag(unsigned char *map, unsigned int block, unsigned int value)
{
    unsigned int shift = (block & 3) * 2;
    map[block >> 2] = (map[block >> 2] & ~(3 << shift)) | (value << shift);
}

int binit(void)
{
    unsigned char header[4];
    bank_ready = 0;
    if (memcmp((void *)BMBIOS_SIGNATURE, "BMX1", 4) ||
        *(unsigned char *)BMBIOS_VERSION != BMBIOS_ABI_MAJOR ||
        (bmstat() >> 8) != BMBIOS_ABI_MAJOR)
        return 0;
    bank_ready = 1;
    if (!copy_bank(0, header, sizeof(header), 0) || memcmp(header, "BKM2", 4))
    {
        bank_ready = 0;
        return 0;
    }
    return 1;
}

int breset(void)
{
    unsigned char map[64];
    if (!binit())
        return 0;
    memset(map, 0, sizeof(map));
    map[0] = 3;
    map[63] = 0xC0;
    return copy_bank(0x20, map, sizeof(map), 1);
}

bhandle bmalloc(unsigned int bytes)
{
    unsigned char map[64];
    unsigned int needed, run, block, first, i;
    if (!bytes || bytes > BMBIOS_HEAP_BYTES || !copy_bank(0x20, map, sizeof(map), 0))
        return 0;
    needed = (bytes - 1) / 256 + 1;
    run = 0;
    first = 0;
    for (block = 1; block < 256; ++block)
    {
        if (tag(map, block))
            run = 0;
        else
        {
            if (!run)
                first = block;
            if (++run == needed)
            {
                set_tag(map, first, 1);
                for (i = first + 1; i <= block; ++i)
                    set_tag(map, i, 2);
                if (!copy_bank(0x20, map, sizeof(map), 1))
                    return 0;
                return first * 256;
            }
        }
    }
    return 0;
}

unsigned int bsize(bhandle handle)
{
    unsigned char map[64];
    if (!handle || (handle & 255) || (!bank_ready && !binit()) ||
        !bmread(0x20, map, sizeof(map)))
        return 0;
    return bmcap(map, handle);
}

unsigned int bavail(void)
{
    unsigned char map[64];
    unsigned int block, count = 0;
    if (!copy_bank(0x20, map, sizeof(map), 0))
        return 0;
    for (block = 1; block < 256; ++block)
        if (!tag(map, block))
            ++count;
    return count * 256;
}

int bfree(bhandle handle)
{
    unsigned char map[64];
    unsigned int block, count, i;
    if (!handle)
        return 1;
    if (!copy_bank(0x20, map, sizeof(map), 0))
        return 0;
    count = bmcap(map, handle) / 256;
    if (!count)
        return 0;
    block = handle >> 8;
    for (i = 0; i < count; ++i)
        set_tag(map, block + i, 0);
    return copy_bank(0x20, map, sizeof(map), 1);
}

static int transfer(bhandle handle, unsigned int offset, void *buffer,
                    unsigned int bytes, int writing)
{
    unsigned int capacity = bsize(handle);
    if (!capacity || offset > capacity || bytes > capacity - offset)
        return 0;
    /* The public BIOS checks base/bank ranges and alignment itself. Avoid
     * the C tail-buffer path when the complete request is already aligned. */
    if (!(bytes & (BMBIOS_BURST_BYTES - 1U)))
    {
        if (writing)
            return bmwrit(handle + offset, buffer, bytes);
        return bmread(handle + offset, buffer, bytes);
    }
    return copy_bank(handle + offset, buffer, bytes, writing);
}

int bread(bhandle handle, unsigned int offset, void *buffer, unsigned int bytes)
{
    return transfer(handle, offset, buffer, bytes, 0);
}

int bwrite(bhandle handle, unsigned int offset, const void *buffer, unsigned int bytes)
{
    return transfer(handle, offset, (void *)buffer, bytes, 1);
}

#asm
; Decode a live 64-byte two-bit map. No bank switch, cache or workspace.
; D=end block (zero represents 256), C=tag position, E=rotated map byte.
; Aligned AAh bytes cover four continuation blocks at once.
; IX/IY and the stack ABI survive; HL returns capacity or zero.
;@dcc.lto begin _bmcap
	public _bmcap
_bmcap:
	push ix
	ld ix,0
	add ix,sp
	ld a,(ix+6)
	or a
	jr nz,BMC_BAD
	ld a,(ix+7)
	or a
	jr z,BMC_BAD
	and 3
	ld b,a
	ld c,a
	ld a,(ix+7)
	srl a
	srl a
	ld e,a
	ld d,0
	ld l,(ix+4)
	ld h,(ix+5)
	add hl,de
	ld a,(hl)
	inc b
	dec b
	jr z,BMC_HEAD
BMC_ROT:
	rrca
	rrca
	djnz BMC_ROT
BMC_HEAD:
	ld e,a
	and 3
	cp 1
	jr nz,BMC_BAD
	ld d,(ix+7)
	inc d
	jr z,BMC_END
	inc c
	bit 2,c
	jr nz,BMC_NEXT
	ld a,e
	rrca
	rrca
	jr BMC_TAG
BMC_NEXT:
	inc hl
BMC_BYTE:
	ld a,(hl)
	cp 0aah
	jr nz,BMC_MIX
	ld a,d
	add a,4
	ld d,a
	jr z,BMC_END
	inc hl
	jr BMC_BYTE
BMC_MIX:
	ld c,0
BMC_TAG:
	ld e,a
	and 3
	cp 2
	jr nz,BMC_END
	inc d
	jr z,BMC_END
	inc c
	bit 2,c
	jr nz,BMC_NEXT
	ld a,e
	rrca
	rrca
	jr BMC_TAG
BMC_END:
	ld a,d
	sub (ix+7)
	ld h,a
	ld l,0
	pop ix
	ret
BMC_BAD:
	ld hl,0
	pop ix
	ret
;@dcc.lto end _bmcap
#endasm
