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
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <rsys/cfm.h>

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
   with_init. */
Bytes pef(const std::vector<std::string>& imports, bool with_init, int16_t init_result = 0)
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
    for(uint32_t at : name_at)
    {
        b.u32(at); b.u32(0); b.u32(0); b.u32(0); b.u32(0);
        b.u8(0); b.u8(0); b.u16(0);
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
