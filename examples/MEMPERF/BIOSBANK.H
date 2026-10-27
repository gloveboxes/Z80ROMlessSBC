#ifndef BIOSBANK_H
#define BIOSBANK_H
/* Public, platform-independent Z80 BIOS bank extension ABI 1.
 * Register calls: HL=base pointer, DE=bank offset, BC=byte count (multiple of
 * 16). Carry clear/A=0 succeeds; carry set/A=2 rejects invalid bounds/length.
 * INFO must succeed before READ/WRITE. Backend is responsible for banking.
 * The assembly adapter below translates dcc's ordinary stack convention.
 */
#define BMBIOS_READ 0xFF00
#define BMBIOS_WRITE 0xFF03
#define BMBIOS_INFO 0xFF06
#define BMBIOS_SIGNATURE 0xFF09
#define BMBIOS_VERSION 0xFF0D
#define BMBIOS_ABI_MAJOR 1
#define BMBIOS_BURST_BYTES 16
#define BMBIOS_HEAP_START 0x0100
#define BMBIOS_HEAP_END 0xFF00
#define BMBIOS_HEAP_BYTES 65024U
/* INFO: version word (major<<8 | minor), or zero if unavailable. */
unsigned int bmstat(void);
/* These raw calls require successful INFO first; return 1 success / 0 error. */
int bmread(unsigned int bank, void *base, unsigned int bytes);
int bmwrit(unsigned int bank, const void *base, unsigned int bytes);
#endif
