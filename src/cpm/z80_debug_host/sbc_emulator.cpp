// Shared-library front end of the project's Z80 emulator (x80 engine plus the
// two-bank SRAM model) for host tests. The engine keeps its CPU state in the
// global `reg`, so one machine exists per process.

#include "sbc_emulator.h"

#include <cstring>

#include "sbc_bank.h"
#include "x80.hxx"
#include <djltrace.hxx>

CDJLTrace tracer;

static sbc_in_fn g_in;
static sbc_out_fn g_out;
static void *g_context;
static bool g_halted;
static int g_breakpoint_count;
static uint16_t g_instruction_pc;
static sbc_write_fn g_write;
static void *g_write_context;
static bool g_watched[65536];
static int g_watch_count;

uint8_t x80_invoke_hook(void)
{
    return OPCODE_NOP;
}

void x80_invoke_halt(void)
{
    g_halted = true;
}

// The Z80 drives A15-A8 from B for ED-prefixed I/O and from A otherwise. DD and
// FD prefixes before an ED are ignored.
static uint16_t io_address(uint8_t port)
{
    uint16_t pc = g_instruction_pc;
    while (memory[pc] == 0xDD || memory[pc] == 0xFD)
        pc++;
    const uint8_t high = memory[pc] == 0xED ? reg.b : reg.a;
    return (uint16_t)(high << 8 | port);
}

void x80_invoke_in(uint8_t port)
{
    reg.a = g_in ? g_in(io_address(port), g_context) : 0xFF;
}

void x80_invoke_out(uint8_t port)
{
    const uint16_t address = io_address(port); // decoded before a bank switch replaces the instruction
    if (sbc_bank_port_matches(port))
        sbc_bank_select(reg.a & 1);
    if (g_out)
        g_out(address, reg.a, g_context);
}

void x80_hard_exit(const char *, uint8_t, uint8_t)
{
    g_halted = true;
}

extern "C" void sbc_reset(void)
{
    reg = registers();
    std::memset(memory, 0, SBC_BANK_SIZE);
    sbc_bank_reset();
    g_halted = false;
    g_breakpoint_count = 0;
    g_write = nullptr;
    std::memset(g_watched, 0, sizeof g_watched);
    g_watch_count = 0;
}

extern "C" uint8_t *sbc_memory(void)
{
    return memory;
}

extern "C" void sbc_set_io(sbc_in_fn in, sbc_out_fn out, void *context)
{
    g_in = in;
    g_out = out;
    g_context = context;
}

static uint16_t pair(uint8_t high, uint8_t low);

extern "C" void sbc_set_write_hook(sbc_write_fn write, void *context)
{
    g_write = write;
    g_write_context = context;
}

extern "C" void sbc_watch(uint16_t address, uint32_t count, int watched)
{
    for (uint32_t i = 0; i < count && address + i < 65536; ++i)
    {
        g_watch_count += (watched != 0) - g_watched[address + i];
        g_watched[address + i] = watched != 0;
    }
}

static uint16_t operand16(uint16_t at)
{
    return (uint16_t)(memory[at] | memory[(uint16_t)(at + 1)] << 8);
}

// Addresses the instruction at g_instruction_pc stores to, decoded before it runs.
static int store_targets(uint16_t (&targets)[2])
{
    uint16_t at = g_instruction_pc;
    uint8_t op = memory[at];
    uint16_t index = 0;
    bool indexed = false;
    if (op == 0xDD || op == 0xFD)
    {
        index = op == 0xDD ? reg.ix : reg.iy;
        indexed = true;
        op = memory[++at];
    }
    else if (op == 0xED)
    {
        op = memory[++at];
        if (op == 0x43 || op == 0x53 || op == 0x63 || op == 0x73)
        {
            targets[0] = operand16(at + 1);
            targets[1] = targets[0] + 1;
            return 2;
        }
        return 0;
    }

    const uint16_t hl = pair(reg.h, reg.l);
    if (op == 0x22)
    {
        targets[0] = operand16(at + 1);
        targets[1] = targets[0] + 1;
        return 2;
    }
    if (!indexed && op == 0x02) { targets[0] = pair(reg.b, reg.c); return 1; }
    if (!indexed && op == 0x12) { targets[0] = pair(reg.d, reg.e); return 1; }
    if (!indexed && op == 0x32) { targets[0] = operand16(at + 1); return 1; }
    if (op == 0x34 || op == 0x35 || op == 0x36 || ((op & 0xF8) == 0x70 && op != 0x76))
    {
        targets[0] = indexed ? (uint16_t)(index + (int8_t)memory[(uint16_t)(at + 1)]) : hl;
        return 1;
    }
    return 0;
}

static uint16_t pair(uint8_t high, uint8_t low)
{
    return (uint16_t)((high << 8) | low);
}

extern "C" uint16_t sbc_get_reg(sbc_reg_t which)
{
    switch (which)
    {
    case SBC_REG_AF: return pair(reg.a, reg.materializeFlags());
    case SBC_REG_BC: return pair(reg.b, reg.c);
    case SBC_REG_DE: return pair(reg.d, reg.e);
    case SBC_REG_HL: return pair(reg.h, reg.l);
    case SBC_REG_IX: return reg.ix;
    case SBC_REG_IY: return reg.iy;
    case SBC_REG_SP: return reg.sp;
    case SBC_REG_PC: return reg.pc;
    case SBC_REG_AF2: return pair(reg.ap, reg.fp);
    case SBC_REG_BC2: return pair(reg.bp, reg.cp);
    case SBC_REG_DE2: return pair(reg.dp, reg.ep);
    case SBC_REG_HL2: return pair(reg.hp, reg.lp);
    case SBC_REG_I: return reg.i;
    case SBC_REG_R: return reg.r;
    case SBC_REG_IFF1: return reg.fINTE;
    case SBC_REG_IFF2: return reg.fINTE2;
    case SBC_REG_IM: return reg.interruptMode;
    default: return 0;
    }
}

extern "C" void sbc_set_reg(sbc_reg_t which, uint16_t value)
{
    const uint8_t high = (uint8_t)(value >> 8);
    const uint8_t low = (uint8_t)value;
    switch (which)
    {
    case SBC_REG_AF: reg.a = high; reg.f = low; reg.unmaterializeFlags(); break;
    case SBC_REG_BC: reg.b = high; reg.c = low; break;
    case SBC_REG_DE: reg.d = high; reg.e = low; break;
    case SBC_REG_HL: reg.h = high; reg.l = low; break;
    case SBC_REG_IX: reg.ix = value; break;
    case SBC_REG_IY: reg.iy = value; break;
    case SBC_REG_SP: reg.sp = value; break;
    case SBC_REG_PC: reg.pc = value; break;
    case SBC_REG_AF2: reg.ap = high; reg.fp = low; break;
    case SBC_REG_BC2: reg.bp = high; reg.cp = low; break;
    case SBC_REG_DE2: reg.dp = high; reg.ep = low; break;
    case SBC_REG_HL2: reg.hp = high; reg.lp = low; break;
    case SBC_REG_I: reg.i = low; break;
    case SBC_REG_R: reg.r = low; break;
    case SBC_REG_IFF1: reg.fINTE = value != 0; break;
    case SBC_REG_IFF2: reg.fINTE2 = value != 0; break;
    case SBC_REG_IM: reg.interruptMode = low; break;
    default: break;
    }
}

extern "C" int sbc_halted(void)
{
    return g_halted;
}

extern "C" void sbc_set_halted(int halted)
{
    g_halted = halted != 0;
}

static const int kMaxBreakpoints = 16;
static uint16_t g_breakpoints[kMaxBreakpoints];

extern "C" void sbc_clear_breakpoints(void)
{
    g_breakpoint_count = 0;
}

extern "C" int sbc_add_breakpoint(uint16_t address)
{
    if (g_breakpoint_count == kMaxBreakpoints)
        return 0;
    g_breakpoints[g_breakpoint_count++] = address;
    return 1;
}

extern "C" void sbc_remove_breakpoint(uint16_t address)
{
    int kept = 0;
    for (int i = 0; i < g_breakpoint_count; ++i)
        if (g_breakpoints[i] != address)
            g_breakpoints[kept++] = g_breakpoints[i];
    g_breakpoint_count = kept;
}

static bool at_breakpoint()
{
    for (int i = 0; i < g_breakpoint_count; ++i)
        if (g_breakpoints[i] == reg.pc)
            return true;
    return false;
}

extern "C" uint32_t sbc_run(uint32_t max_ticks, uint32_t *ticks_used)
{
    uint32_t total = 0;
    uint32_t events = 0;
    bool first = true;
    while (true)
    {
        if (g_halted)
        {
            events = SBC_EVENT_HALT;
            break;
        }
        if (total >= max_ticks)
        {
            events = SBC_EVENT_TICKS;
            break;
        }
        if (g_breakpoint_count && !first && at_breakpoint())
        {
            events = SBC_EVENT_BREAKPOINT;
            break;
        }
        first = false;
        // One instruction per call: I/O needs the instruction's PC, and x80_emulate
        // returns a uint16_t that must not overflow.
        g_instruction_pc = reg.pc;
        uint16_t targets[2];
        const int stores = g_write && g_watch_count ? store_targets(targets) : 0;
        const uint16_t spent = x80_emulate(1);
        for (int i = 0; i < stores; ++i)
            if (g_watched[targets[i]])
                g_write(targets[i], memory[targets[i]], g_write_context);
        total += spent ? spent : 1;
    }
    if (ticks_used)
        *ticks_used = total;
    return events;
}

extern "C" int sbc_active_bank(void)
{
    return sbc_bank_active();
}

extern "C" void sbc_select_bank(int bank)
{
    sbc_bank_select(bank);
}

extern "C" uint8_t *sbc_bank_memory(int bank)
{
    return sbc_bank_view(bank);
}
