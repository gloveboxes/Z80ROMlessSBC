#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../../examples/BANKSDK/BANKLIB.C"

#define TEST_FRAME 0xC800
#define TEST_EXPORTS 0xE300
#define TEST_ORIGIN 0x0100

static unsigned char bank[65536];
static unsigned int extent_live, calls, cleanup_calls, allocations, frees;
static unsigned int signature_reads, module_loaded, mock_generation, mock_epoch = 1;
static unsigned int mock_status, load_status;
static int fail_read, fail_write, fail_free, fail_load_reply, fail_query;

static void set_word(unsigned char *p, unsigned int offset, unsigned int value)
{
    p[offset] = (unsigned char)value;
    p[offset + 1] = (unsigned char)(value >> 8);
}

unsigned int bmstat(void) { return BMBIOS_ABI; }
unsigned int bmerr(void) { return mock_status; }

int bmqury(bank_bios_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->bytes = 48;
    info->revision = 1;
    info->abi = BMBIOS_ABI;
    info->features = BANK_BIOS_FEATURES;
    info->allocator_revision = BMBIOS_CALL_REVISION;
    info->library_request = 0xDF20;
    info->library_request_bytes = 32;
    info->image_origin = TEST_ORIGIN;
    info->frame_bytes = BL_FRAME_MAX;
    info->signature_bytes = BL_SIGNATURE_MAX;
    info->export_count = 8;
    mock_status = 0;
    return 1;
}

unsigned int bmaloc(unsigned int bytes)
{
    assert(!extent_live && bytes == BL_FRAME_MAX + 16);
    ++allocations;
    extent_live = 1;
    mock_status = 0;
    return TEST_FRAME;
}

int bmfree(unsigned int handle)
{
    assert(handle == TEST_FRAME && extent_live);
    if (fail_free)
    {
        mock_status = BANK_BIOS_IO;
        return 0;
    }
    ++frees;
    extent_live = 0;
    mock_status = 0;
    return 1;
}

int bmread(unsigned int address, void *target, unsigned int bytes)
{
    assert(address + bytes <= sizeof(bank) && !(bytes & 15));
    if (address < TEST_ORIGIN + 64)
        ++signature_reads;
    if (fail_read && address == TEST_FRAME)
        return 0;
    memcpy(target, bank + address, bytes);
    return 1;
}

int bmwrit(unsigned int address, const void *source, unsigned int bytes)
{
    assert(address + bytes <= sizeof(bank) && !(bytes & 15));
    if (fail_write && address == TEST_FRAME)
        return 0;
    if (address == TEST_FRAME && module_loaded)
    {
        const unsigned char *frame = source;
        unsigned int i, length = word_at(frame);
        assert(bytes == ((length + 17U) & 0xFFF0U));
        for (i = length + 2; i < bytes; ++i)
            assert(frame[i] == 0);
    }
    memcpy(bank + address, source, bytes);
    return 1;
}

static void descriptor(unsigned char *p)
{
    set_word(p, 8, mock_generation);
    set_word(p, 16, module_loaded);
    set_word(p, 18, 2);
    set_word(p, 20, 12);
    set_word(p, 22, 64);
    set_word(p, 24, TEST_EXPORTS);
    set_word(p, 26, TEST_ORIGIN);
    set_word(p, 28, 0xC000);
    set_word(p, 30, mock_epoch);
}

int bmctrl(unsigned int address, void *buffer)
{
    unsigned char *p = buffer, *frame = bank + TEST_FRAME;
    unsigned int action = word_at(p + 4), index, length;
    assert(address == 0xDF20 && word_at(p) == 32 && word_at(p + 2) == 1);
    mock_status = 0;
    if (action == BANK_LIBRARY_LOAD)
    {
        assert(word_at(p + 12) == TEST_FRAME && extent_live);
        module_loaded = 1;
        ++mock_generation;
        memset(bank + TEST_EXPORTS, 0, 96);
        memcpy(bank + TEST_EXPORTS, "ADD", 3);
        bank[TEST_EXPORTS + 10] = 8;
        memcpy(bank + TEST_EXPORTS + 12, "ONUNLOAD", 8);
        bank[TEST_EXPORTS + 20] = 16;
        bank[TEST_EXPORTS + 22] = 32;
        memcpy(bank + TEST_ORIGIN + 8, "u16(u16)", 9);
        memcpy(bank + TEST_ORIGIN + 32, "u16()", 6);
        mock_status = fail_load_reply ? BANK_BIOS_IO : load_status;
        if (load_status)
            module_loaded = 0;
        descriptor(p);
    }
    else if (action == BANK_LIBRARY_INVALIDATE)
    {
        module_loaded = 0;
        ++mock_generation;
        descriptor(p);
    }
    else if (action == BANK_LIBRARY_QUERY)
    {
        if (fail_query)
            mock_status = BANK_BIOS_IO;
        else
            descriptor(p);
    }
    else
    {
        assert(action == BANK_LIBRARY_INVOKE && module_loaded && extent_live);
        assert(word_at(p + 8) == mock_generation &&
               word_at(p + 30) == mock_epoch &&
               word_at(p + 12) == TEST_FRAME);
        set_word(bank, BL_TOKEN_ADDRESS, word_at(p + 14));
        ++calls;
        index = word_at(p + 10);
        length = word_at(frame);
        if (index == 1)
        {
            assert(length == 2);
            ++cleanup_calls;
            set_word(frame, 2, 1);
        }
        else if (length != 4)
            mock_status = BANK_BIOS_DISPATCH;
        else
        {
            assert(index == 0);
            set_word(frame, 2, frame[4] + 7);
        }
    }
    set_word(p, 6, mock_status);
    return mock_status == 0;
}

int main(void)
{
    unsigned char frame[4] = {0xA5, 0xA5, 5, 0}, maximum_frame[256];
    char name[9], signature[256];
    unsigned int before, before_reads, before_allocations, before_frees, i;
    bank_library_binding_t binding = {0, 0};
    unsigned int statuses[] = {BANK_BIOS_FORMAT, BANK_BIOS_IO};
    unsigned int errors[] = {BL_FORMAT, BL_IO};

    fail_load_reply = 1;
    assert(!blopen("DATA.LIB") && blerr() == BL_IO &&
           !module_loaded && !extent_live);
    fail_load_reply = 0;
    for (i = 0; i < 2; ++i)
    {
        load_status = statuses[i];
        assert(!blopen("DATA.LIB") && blerr() == errors[i] &&
               !module_loaded && !extent_live);
    }
    load_status = 0;
    fail_write = 1;
    assert(!blopen("DATA.LIB") && blerr() == BL_IO && !extent_live);
    fail_write = 0;
    assert(!blopen("../BAD.LIB") && blerr() == BL_ARGUMENT);
    assert(blopen("B:DATA.LIB") && blerr() == BL_OK && extent_live);
    assert(blcnt() == 2 && blname(0, name) && !strcmp(name, "ADD"));
    assert(blsig(0, signature, sizeof(signature)) && !strcmp(signature, "u16(u16)"));
    memset(maximum_frame, 0xA5, sizeof(maximum_frame));
    assert(!blcall("ADD", "u16(u16)", maximum_frame, sizeof(maximum_frame)) &&
           blerr() == BL_DISPATCH && extent_live);
    for (i = 0; i < sizeof(maximum_frame); ++i)
        assert(maximum_frame[i] == 0xA5);
    assert(!blsig(0, signature, 3) && blerr() == BL_BOUNDS);
    before = calls;
    assert(!blcall("ADD", "i16(i16)", frame, sizeof(frame)) &&
           blerr() == BL_SIGNATURE && calls == before);
    assert(!blcall("UNKNOWN", "u16(u16)", frame, sizeof(frame)) &&
           blerr() == BL_ARGUMENT && calls == before);
    assert(!blcall("ADD", "u16(u16)", frame, 257) &&
           blerr() == BL_BOUNDS && calls == before);
    assert(!blcall("ADD", "u16(u16)", frame, 1) &&
           blerr() == BL_DISPATCH && frame[0] == 0xA5);
    fail_write = 1;
    assert(!blcall("ADD", "u16(u16)", frame, sizeof(frame)) &&
           blerr() == BL_IO && frame[0] == 0xA5);
    fail_write = 0;
    fail_read = 1;
    assert(!blcall("ADD", "u16(u16)", frame, sizeof(frame)) &&
           blerr() == BL_IO && frame[0] == 0xA5);
    fail_read = 0;
    assert(bank_library_call_bound(&binding, "ADD", "u16(u16)", frame, sizeof(frame)));
    before_reads = signature_reads;
    before_allocations = allocations;
    before_frees = frees;
    for (i = 0; i < 20; ++i)
        assert(bank_library_call_bound(&binding, "ADD", "u16(u16)", frame, sizeof(frame)) &&
               blerr() == BL_OK && frame[0] == 12);
    assert(signature_reads == before_reads && allocations == before_allocations &&
           frees == before_frees);
    assert(word_at(bank + BL_TOKEN_ADDRESS) == serial);
    fail_query = 1;
    before = calls;
    assert(!blcall("ADD", "u16(u16)", frame, sizeof(frame)) &&
           blerr() == BL_IO && calls == before && extent_live);
    fail_query = 0;
    fail_free = 1;
    assert(!blclos() && blerr() == BL_CLEANUP && extent_live && !module_loaded);
    fail_free = 0;
    assert(blclos() && blerr() == BL_OK && !extent_live);
    assert(blopen("DATA"));
    before_reads = signature_reads;
    assert(bank_library_call_bound(&binding, "ADD", "u16(u16)", frame, sizeof(frame)));
    assert(signature_reads == before_reads + 1 && binding.generation == mock_generation);
    before_frees = frees;
    ++mock_epoch;
    memset(bank + TEST_FRAME, 0x5A, 512);
    assert(!blcall("ADD", "u16(u16)", frame, sizeof(frame)) &&
           blerr() == BL_STALE && !workspace);
    assert(!blclos() && blerr() == BL_STALE && frees == before_frees);
    for (i = 0; i < 512; ++i)
        assert(bank[TEST_FRAME + i] == 0x5A);
    extent_live = 0;
    assert(blopen("DATA"));
    ++mock_generation;
    assert(!blcall("ADD", "u16(u16)", frame, sizeof(frame)) && blerr() == BL_STALE);
    assert(!blclos() && blerr() == BL_STALE && !extent_live && module_loaded);
    module_loaded = 0;
    assert(blopen("DATA"));
    serial = 65534;
    before = calls;
    assert(!blcall("ADD", "u16(u16)", frame, sizeof(frame)) &&
           blerr() == BL_EXHAUSTED && calls == before);
    assert(blclos() && blerr() == BL_OK && serial == 65535 && !extent_live && !blcnt());
    assert(cleanup_calls > 0);
    assert(!blcall("ADD", "u16(u16)", frame, sizeof(frame)) && blerr() == BL_ARGUMENT);
    assert(blopen("DATA") &&
           !blcall("ADD", "u16(u16)", frame, sizeof(frame)) &&
           blerr() == BL_EXHAUSTED && serial == 65535);
    assert(!blclos() && blerr() == BL_EXHAUSTED && !extent_live);
    puts("PASS: typed transport, reusable frames, cached bindings, generations and cleanup");
    return 0;
}
