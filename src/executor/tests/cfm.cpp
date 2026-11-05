/* MacPhoenix: the Code Fragment Manager's closures. A preparation that
   fails leaves nothing behind: the libraries its closure loaded new are
   released, not left prepared with their init routines never run (a later
   load would find them half done: Slithereens on 8.6, InputSprocket's USB
   driver failing for want of USBManagerLib). Small PowerPC containers made
   here, libraries in 'shlb' files in the default folder (the application's
   folder, as CFM searches it). */

#include "gtest/gtest.h"

#include <CodeFragments.h>
#include <FileMgr.h>
#include <LowMem.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <rsys/cfm.h>
#include <PEFBinaryFormat.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace Executor;

StringPtr PSTR(const char *s);

namespace
{
struct Bytes : std::vector<uint8_t>
{
    void u8(uint8_t v) { push_back(v); }
    void u16(uint16_t v) { u8(v >> 8); u8(v); }
    void u32(uint32_t v) { u16(v >> 16); u16(v); }
    void align4() { while(size() % 4) u8(0); }
};

/* A container with a code section (li r3,init_result; blr), a data section
   (that code's transition vector, relocated) and a loader section importing
   the libraries named, no symbols; with an init routine (the vector) if
   with_init; imports named in weak are weak. */
Bytes pef(const std::vector<std::string>& imports, bool with_init, int16_t init_result = 0,
          const std::vector<std::string>& weak = {})
{
    const uint32_t header = 40, section_headers = 3 * 28;
    const uint32_t code_at = header + section_headers, data_at = code_at + 8;
    const uint32_t loader_at = data_at + 8;

    Bytes strings;
    std::vector<uint32_t> name_at;
    for(const std::string& name : imports)
    {
        name_at.push_back(strings.size());
        for(char c : name)
            strings.u8(c);
        strings.u8(0);
    }
    strings.align4();

    const uint32_t info = 56, libs = 24 * imports.size(), reloc_headers = 12;
    const uint32_t reloc_instrs_at = info + libs + reloc_headers;
    const uint32_t strings_at = reloc_instrs_at + 4;
    const uint32_t hash_at = strings_at + strings.size();
    const uint32_t loader_size = hash_at + 4;

    Bytes b;
    b.u32("Joy!"_4); b.u32("peff"_4); b.u32("pwpc"_4); b.u32(1);
    b.u32(0); b.u32(0); b.u32(0); b.u32(0);
    b.u16(3); b.u16(2); b.u32(0);
    auto section = [&](uint32_t at, uint32_t size, uint8_t kind, uint8_t share) {
        b.u32(-1); b.u32(0); b.u32(size); b.u32(size); b.u32(size); b.u32(at);
        b.u8(kind); b.u8(share); b.u8(2); b.u8(0);
    };
    section(code_at, 8, 0, 4);              /* code */
    section(data_at, 8, 1, 1);              /* data */
    section(loader_at, loader_size, 4, 4);  /* loader */

    b.u32(0x38600000 | (uint16_t)init_result); /* li r3,init_result */
    b.u32(0x4e800020);                         /* blr */
    b.u32(0); b.u32(0);                        /* TVector: code + 0, TOC 0 */

    b.u32(-1); b.u32(0);                                /* main */
    b.u32(with_init ? 1 : -1); b.u32(0);                /* init */
    b.u32(-1); b.u32(0);                                /* term */
    b.u32(imports.size()); b.u32(0); b.u32(1);
    b.u32(reloc_instrs_at); b.u32(strings_at); b.u32(hash_at);
    b.u32(0); b.u32(0);
    for(size_t i = 0; i < imports.size(); i++)
    {
        bool is_weak = std::find(weak.begin(), weak.end(), imports[i]) != weak.end();
        b.u32(name_at[i]); b.u32(0); b.u32(0); b.u32(0); b.u32(0);
        b.u8(is_weak ? kPEFWeakImportLibMask : 0); b.u8(0); b.u16(0);
    }
    b.u16(1); b.u16(0); b.u32(1); b.u32(0);  /* data: one instruction */
    b.u16(0x4000); b.u16(0);                 /* RelocBySectC, 1 word */
    b.insert(b.end(), strings.begin(), strings.end());
    b.u32(0);                                /* export hash: one empty slot */
    return b;
}

/* A 'shlb' file named name holding the library name, whole data fork. */
void write_library(const std::string& name, const Bytes& container)
{
    StringPtr fname = PSTR(name.c_str());
    FSDelete(fname, 0);
    ASSERT_EQ(noErr, Create(fname, 0, "Xctr"_4, "shlb"_4));
    GUEST<INTEGER> rn;
    ASSERT_EQ(noErr, FSOpen(fname, 0, &rn));
    GUEST<LONGINT> count = container.size();
    Ptr buf = NewPtr(container.size()); /* guest memory */
    memcpy(buf, container.data(), container.size());
    ASSERT_EQ(noErr, FSWrite(rn, &count, buf));
    DisposePtr(buf);
    FSClose(rn);

    CreateResFile(fname);
    ASSERT_EQ(noErr, ResError());
    INTEGER res = OpenResFile(fname);
    ASSERT_NE(-1, res);
    Bytes cfrg;
    for(int i = 0; i < 7; i++)
        cfrg.u32(i == 2 ? 1 : 0);   /* version 1 */
    cfrg.u32(1);                    /* one member */
    size_t member = cfrg.size();
    cfrg.u32("pwpc"_4); cfrg.u32(0); cfrg.u32(0); cfrg.u32(0); cfrg.u32(0);
    cfrg.u16(0);
    cfrg.u8(kImportLibraryCFrag); cfrg.u8(1 /* data fork */);
    cfrg.u32(0); cfrg.u32(kWholeFork); cfrg.u32(0); cfrg.u32(0);
    size_t length_at = cfrg.size();
    cfrg.u16(0);
    cfrg.u8(name.size());
    for(char c : name)
        cfrg.u8(c);
    cfrg.align4();
    uint16_t length = cfrg.size() - member;
    cfrg[length_at] = length >> 8;
    cfrg[length_at + 1] = length;
    Handle h = NewHandle(cfrg.size());
    memcpy(*h, cfrg.data(), cfrg.size());
    AddResource(h, "cfrg"_4, 0, (StringPtr) "");
    ASSERT_EQ(noErr, ResError());
    CloseResFile(res);
}

OSErr load(const Bytes& container, std::string *err_name = nullptr)
{
    Ptr p = NewPtr(container.size());
    memcpy(p, container.data(), container.size());
    GUEST<ConnectionID> cid;
    GUEST<Ptr> main;
    Str255 errname = { 0 };
    OSErr err = GetMemFragment(p, container.size(), (StringPtr) "\4Root", kReferenceCFrag,
                               &cid, &main, errname);
    if(err != noErr)
        DisposePtr(p);
    if(err_name)
        *err_name = std::string((const char *)errname + 1, errname[0]);
    return err;
}

bool prepared(const std::string& name)
{
    Str63 lib;
    lib[0] = name.size();
    memcpy(lib + 1, name.data(), name.size());
    GUEST<ConnectionID> cid;
    GUEST<Ptr> main;
    Str255 errname;
    return GetSharedLibrary(lib, "pwpc"_4, kFindLib, &cid, &main, errname) == noErr;
}
}

TEST(CFM, ClosureLoadsLibraries)
{
    write_library("CfmTestLoads", pef({}, true));
    EXPECT_EQ(noErr, load(pef({ "CfmTestLoads" }, false)));
    EXPECT_TRUE(prepared("CfmTestLoads"));
}

TEST(CFM, FailedClosureReleasesItsLibraries)
{
    write_library("CfmTestReleased", pef({}, true));
    load(pef({ "CfmTestReleased", "CfmTestMissing" }, false)); /* the File Manager's caches */
    int32_t free_appl = FreeMem(), free_sys = FreeMemSys();
    std::string missing;
    EXPECT_EQ(fragLibNotFound, load(pef({ "CfmTestReleased", "CfmTestMissing" }, false), &missing));
    EXPECT_EQ("CfmTestMissing", missing);
    EXPECT_FALSE(prepared("CfmTestReleased"));
    EXPECT_GE(FreeMem(), free_appl) << "the library's memory is given back";
    EXPECT_GE(FreeMemSys(), free_sys);
    /* the next importer prepares it afresh, init routine and all */
    EXPECT_EQ(noErr, load(pef({ "CfmTestReleased" }, false)));
    EXPECT_TRUE(prepared("CfmTestReleased"));
}

TEST(CFM, FailedInitRoutineReleasesTheClosure)
{
    write_library("CfmTestInitOK", pef({}, true));
    write_library("CfmTestInitFails", pef({}, true, -50));
    EXPECT_NE(noErr, load(pef({ "CfmTestInitOK", "CfmTestInitFails" }, false)));
    EXPECT_FALSE(prepared("CfmTestInitOK"));
    EXPECT_FALSE(prepared("CfmTestInitFails"));
}

/* Executor's own libraries are connected once and stay: a closure that
   fails gives back its connection, not the library. */
TEST(CFM, FailedClosureKeepsExecutorsLibraries)
{
    GUEST<ConnectionID> cid, again;
    GUEST<Ptr> main;
    Str255 errname;
    ASSERT_EQ(noErr, GetSharedLibrary((StringPtr) "\014InterfaceLib", "pwpc"_4,
                                      kReferenceCFrag, &cid, &main, errname));
    EXPECT_EQ(fragLibNotFound, load(pef({ "InterfaceLib", "CfmTestMissing" }, false)));
    ASSERT_EQ(noErr, GetSharedLibrary((StringPtr) "\014InterfaceLib", "pwpc"_4,
                                      kFindLib, &again, &main, errname));
    EXPECT_EQ(cid, again);
}

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
std::vector<uint8_t> make_pef(uint32_t data_default,
                              const std::vector<uint32_t>& words = {},
                              const std::vector<uint16_t>& relocs = {})
{
    /* default: two words, the second relocated */
    std::vector<uint32_t> values = words.empty() ? std::vector<uint32_t>{ 0, data_default + 4 } : words;
    std::vector<uint16_t> program = relocs.empty() ? std::vector<uint16_t>{ 0x0041 } : relocs;
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
    for(uint32_t w : values)
        p.u32(w);
    section(1, data_default, 4 * values.size(), kPEFUnpackedDataSection, kPEFProcessShare, 4, data);

    size_t loader = p.b.size();
    p.u32(1); p.u32(0);                 /* main: the data section's start */
    p.u32((uint32_t)-1); p.u32(0);      /* no init */
    p.u32((uint32_t)-1); p.u32(0);      /* no term */
    p.u32(0); p.u32(0);                 /* no imports */
    p.u32(1);                           /* one section relocated */
    size_t fields = p.b.size();
    p.u32(0); p.u32(0); p.u32(0);       /* reloc, strings, hash offsets */
    p.u32(0); p.u32(0);                 /* hash power, no exports */
    p.u16(1); p.u16(0); p.u32(program.size()); p.u32(0); /* data: its program at 0 */
    p.put32(fields, p.b.size() - loader);
    for(uint16_t r : program)           /* default: RelocBySectDWithSkip 1, 1 */
        p.u16(r);
    if(program.size() & 1)
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

/* The relocation instructions with longer forms: RelocLgRepeat (the
   chunks before it again, repeatCount more times), RelocLgBySection and
   RelocSmBySection were skipped, the words after them left as the linker
   wrote them (Word 98's data: the routine descriptor of its SetCursor
   patch pointed at $F9E0). */
TEST(CFM, LongRelocationInstructions)
{
    std::vector<uint32_t> words;
    for(uint32_t i = 0; i < 8; i++)
        words.push_back(4 * i);
    std::vector<uint8_t> pef = make_pef(0, words,
                                        { 0x4200,           /* RelocBySectD, 1 word */
                                          0xB000, 0x0004,   /* RelocLgRepeat 1 chunk, 4 times */
                                          0xB400, 0x0001,   /* RelocLgBySection 1 */
                                          0x6601 });        /* RelocSmBySection 1 */
    Ptr image = NewPtr(pef.size());
    memcpy(image, pef.data(), pef.size());
    GUEST<ConnectionID> cid;
    GUEST<Ptr> main;
    Str255 err;
    ASSERT_EQ(noErr, GetMemFragment(image, pef.size(), (StringPtr) "\0", kLoadLib, &cid, &main,
                                    err));
    uint32_t start = guest_cast<uint32_t>(main);
    GUEST<uint32_t> *w = (GUEST<uint32_t> *)(Ptr)main;
    for(int i = 0; i < 7; i++)
        EXPECT_EQ(start + 4 * i, (uint32_t)w[i]) << "word " << i;
    EXPECT_EQ(28u, (uint32_t)w[7]);
}

/* PrivateInterfaceLib's low-memory accessors applications import from it
   (8.6's exports them): MPW Shell keeps its globals in HpChk ($316) through
   LMSetHpChk and LMGetHpChk. */
TEST(CFM, PrivateInterfaceLibExportsLowMemAccessors)
{
    GUEST<ConnectionID> cid;
    GUEST<Ptr> main, addr;
    uint8_t cls;
    Str255 errname;
    ASSERT_EQ(noErr, GetSharedLibrary((StringPtr) "\023PrivateInterfaceLib", "pwpc"_4,
                                      kReferenceCFrag, &cid, &main, errname));
    for(const char *name : { "LMGetHpChk", "LMSetHpChk", "LMGetQDExist", "LMSetQDExist",
                             "LMGetIAZNotify", "LMSetIAZNotify", "LMSetJSwapFont" })
        EXPECT_EQ(noErr, FindSymbol(cid, PSTR(name), &addr, &cls)) << name;

    Ptr saved = LMGetHpChk(), p = NewPtr(4);
    LMSetHpChk(p);
    EXPECT_EQ(guest_cast<uint32_t>(p), (uint32_t)*(GUEST<uint32_t> *)SYN68K_TO_US(0x316));
    LMSetHpChk(saved);
    DisposePtr(p);
}

/* InterfaceLib's LMGetHWCfgFlags ($B22; 9.0.4's Iomega Drive Options and
   Tools ask): the G3's $EC00. */
TEST(CFM, InterfaceLibExportsHWCfgFlags)
{
    GUEST<ConnectionID> cid;
    GUEST<Ptr> main, addr;
    uint8_t cls;
    Str255 errname;
    ASSERT_EQ(noErr, GetSharedLibrary((StringPtr) "\014InterfaceLib", "pwpc"_4,
                                      kReferenceCFrag, &cid, &main, errname));
    for(const char *name : { "LMGetHWCfgFlags", "LMSetHWCfgFlags" })
        EXPECT_EQ(noErr, FindSymbol(cid, PSTR(name), &addr, &cls)) << name;
    EXPECT_EQ(0xEC00, (uint16_t)LMGetHWCfgFlags());
}

/* Apple's MPLibrary needs the nanokernel: Executor declines it, file or
   not, and a weak import of it goes nil (REALbasic's plugins, GameBox
   Classic on 8.6). */
TEST(CFM, AppleMPLibraryIsDeclined)
{
    write_library("MPLibrary", pef({}, true));
    write_library("CfmTestNotMP", pef({}, true));
    EXPECT_FALSE(prepared("MPLibrary"));
    GUEST<ConnectionID> cid;
    GUEST<Ptr> main;
    Str255 errname;
    EXPECT_EQ(fragLibNotFound, GetSharedLibrary((StringPtr) "\011MPLibrary", "pwpc"_4,
                                                kReferenceCFrag, &cid, &main, errname));
    EXPECT_EQ(noErr, load(pef({ "CfmTestNotMP", "MPLibrary" }, false, 0, { "MPLibrary" })));
    EXPECT_TRUE(prepared("CfmTestNotMP"));
    std::string missing;
    EXPECT_EQ(fragLibNotFound, load(pef({ "MPLibrary" }, false), &missing));
    EXPECT_EQ("MPLibrary", missing);
}

/* GetMemFragment with no connection or main-address pointer prepares the
   fragment all the same (REALbasic's RBFramework loader asks no main
   address of the runtime). */
TEST(CFM, GetMemFragmentTakesNilOutPointers)
{
    write_library("CfmTestNilOut", pef({}, true));
    Bytes c = pef({ "CfmTestNilOut" }, false);
    Ptr p = NewPtr(c.size());
    memcpy(p, c.data(), c.size());
    GUEST<ConnectionID> cid;
    Str255 errname = { 0 };
    EXPECT_EQ(noErr, GetMemFragment(p, c.size(), (StringPtr) "\4Root", kReferenceCFrag,
                                    &cid, nullptr, errname));
    EXPECT_TRUE(prepared("CfmTestNilOut"));
    Ptr q = NewPtr(c.size());
    memcpy(q, c.data(), c.size());
    EXPECT_EQ(noErr, GetMemFragment(q, c.size(), (StringPtr) "\4Root", kReferenceCFrag,
                                    nullptr, nullptr, errname));
}
