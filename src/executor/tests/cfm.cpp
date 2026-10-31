#include "gtest/gtest.h"

#include <CodeFragments.h>
#include <MemoryMgr.h>
#include <rsys/cfm.h>
#include <PEFBinaryFormat.h>

#include <cstring>
#include <vector>

using namespace Executor;

namespace
{
/* A PEF container put together by hand (the Mac OS Runtime Architectures'
   layout): a 4-byte code section, a data section and a loader section
   with one relocation for the data section and no imports or exports. */
struct PEFBuilder
{
    std::vector<uint8_t> b;

    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { u8(v >> 8); u8(v); }
    void u32(uint32_t v) { u16(v >> 16); u16(v); }
    void put32(size_t at, uint32_t v)
    {
        b[at] = v >> 24; b[at + 1] = v >> 16; b[at + 2] = v >> 8; b[at + 3] = v;
    }
};

/* Data: two words, the second a pointer to itself as the linker saw it,
   at data_default + 4; RelocBySectDWithSkip (skip 1, relocate 1) adds the
   data section's place. */
std::vector<uint8_t> make_pef(uint32_t data_default)
{
    PEFBuilder p;
    p.u32("Joy!"_4); p.u32("peff"_4); p.u32("pwpc"_4);
    p.u32(1); p.u32(0); p.u32(0); p.u32(0); p.u32(0);
    p.u16(3); p.u16(2); p.u32(0);

    const size_t headers = p.b.size();
    for(int i = 0; i < 3; i++)
        for(int j = 0; j < 7; j++)
            p.u32(0);

    auto section = [&](int i, uint32_t def, uint32_t size, uint8_t kind, uint8_t share,
                       uint8_t align, size_t offset) {
        size_t h = headers + 28 * i;
        p.put32(h, (uint32_t)-1);
        p.put32(h + 4, def);
        p.put32(h + 8, size);
        p.put32(h + 12, size);
        p.put32(h + 16, size);
        p.put32(h + 20, offset);
        p.b[h + 24] = kind; p.b[h + 25] = share; p.b[h + 26] = align;
    };

    size_t code = p.b.size();
    p.u32(0x4e800020); /* blr */
    section(0, 0, 4, kPEFCodeSection, kPEFProcessShare, 4, code);

    size_t data = p.b.size();
    p.u32(0); p.u32(data_default + 4);
    section(1, data_default, 8, kPEFUnpackedDataSection, kPEFProcessShare, 4, data);

    size_t loader = p.b.size();
    p.u32(1); p.u32(0);                 /* main: the data section's start */
    p.u32((uint32_t)-1); p.u32(0);      /* no init */
    p.u32((uint32_t)-1); p.u32(0);      /* no term */
    p.u32(0); p.u32(0);                 /* no imports */
    p.u32(1);                           /* one section relocated */
    size_t fields = p.b.size();
    p.u32(0); p.u32(0); p.u32(0);       /* reloc, strings, hash offsets */
    p.u32(0); p.u32(0);                 /* hash power, no exports */
    p.u16(1); p.u16(0); p.u32(1); p.u32(0); /* data: 1 instruction at 0 */
    p.put32(fields, p.b.size() - loader);
    p.u16(0x0041);                      /* RelocBySectDWithSkip 1, 1 */
    p.u16(0);
    p.put32(fields + 4, p.b.size() - loader);
    p.put32(fields + 8, p.b.size() - loader);
    p.u32(0);                           /* one empty hash chain */
    section(2, 0, p.b.size() - loader, kPEFLoaderSection, kPEFGlobalShare, 4, loader);
    return p.b;
}

uint32_t relocated_word(uint32_t data_default, uint32_t *data_start)
{
    std::vector<uint8_t> pef = make_pef(data_default);
    Ptr image = NewPtr(pef.size());
    memcpy(image, pef.data(), pef.size());
    GUEST<ConnectionID> cid;
    GUEST<Ptr> main;
    Str255 err;
    OSErr e = GetMemFragment(image, pef.size(), (StringPtr) "\0", kLoadLib, &cid, &main, err);
    EXPECT_EQ(noErr, e);
    if(e != noErr)
        return 0;
    *data_start = guest_cast<uint32_t>(main);
    return ((GUEST<uint32_t> *)(Ptr)main)[1];
}
}

/* A section's pointers are relative to its default address: a relocation
   adds where the instance is less that (MPW Shell 3.5's PowerShell has its
   data at $6750). */
TEST(CFM, RelocationIsRelativeToDefaultAddress)
{
    uint32_t start = 0;
    uint32_t word = relocated_word(0, &start);
    EXPECT_EQ(start + 4, word);
    word = relocated_word(0x6750, &start);
    EXPECT_EQ(start + 4, word);
}
