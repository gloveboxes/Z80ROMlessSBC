#ifndef Z80SBC_RAM_FUNC_H
#define Z80SBC_RAM_FUNC_H

/* Core-0 I/O trap handlers run with CLK stopped; keep them out of XIP so a
 * flash cache miss never extends a Z80 wait state. Host builds map to plain
 * functions. */
#if defined(__has_include)
#if __has_include("pico.h")
#include "pico.h"
#endif
#endif

#ifndef __not_in_flash_func
#define __not_in_flash_func(name) name
#endif

#endif
