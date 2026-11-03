#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <ToolboxUtil.h>
#include <ScriptMgr.h>
#else
#include <Resources.h>
#include <Files.h>
#include <TextUtils.h>
#include <Script.h>
#endif

#include <string.h>

class ResourceTest : public testing::Test
{
public:
    short ref = -1;
    StringPtr filename;

    ResourceTest()
    {
        filename = PSTR("test-resfile");
        CreateResFile(filename);
        EXPECT_EQ(noErr, ResError());
        open();
    }

    void open()
    {
        ref = OpenResFile(filename);
        EXPECT_EQ(noErr, ResError());
        EXPECT_NE(-1, ref);
    }

    void close()
    {
        if(ref != -1)
            CloseResFile(ref);
        ref = -1;
    }

    void reopen()
    {
        close();
        open();
    }

    ~ResourceTest()
    {
        close();
        FSDelete(filename,0);
    }
};

TEST_F(ResourceTest, CreateDelete)
{
    EXPECT_EQ(0, GetResFileAttrs(ref) & 0xE0);
}

TEST_F(ResourceTest, CreateDuplicate)
{
    CreateResFile(filename);
    EXPECT_EQ(opWrErr, ResError());
}

TEST_F(ResourceTest, GetResourceNotFound)
{
    Handle h;
    h = Get1Resource('QUUX', 128);
    EXPECT_EQ(nullptr, h);
    EXPECT_EQ(noErr, ResError());

    h = GetResource('QUUX', 128);
    EXPECT_EQ(nullptr, h);
    EXPECT_EQ(noErr, ResError());
}

/* What GetResource finds is the resource's data as stored, whatever its
   type and ID (Executor once special-cased some for single programs). */
static void expectStoredAsIs(ResourceTest &t, ResType type, short id)
{
    const unsigned char bytes[4] = { 0x01, 0x01, 0x02, 0x04 };
    Handle h = NewHandle(sizeof bytes);
    ASSERT_NE(nullptr, h);
    memcpy(*h, bytes, sizeof bytes);
    AddResource(h, type, id, PSTR(""));
    ASSERT_EQ(noErr, ResError());
    t.reopen();
    Handle h2 = GetResource(type, id);
    ASSERT_NE(nullptr, h2);
    ASSERT_EQ((long)sizeof bytes, GetHandleSize(h2));
    EXPECT_EQ(0, memcmp(*h2, bytes, sizeof bytes));
}

// Ultima III's preferences (upstream cleared their music byte)
TEST_F(ResourceTest, PrefAsStored)
{
    expectStoredAsIs(*this, 'PREF', 400);
}

// Bob's Eyes Pro's (upstream answered nil for it)
TEST_F(ResourceTest, RAnd128AsStored)
{
    expectStoredAsIs(*this, 'rAnd', 128);
}

TEST_F(ResourceTest, AddResource)
{
    Handle h;
    h = NewHandle(4);
    *(GUEST<uint32_t>*)*h = 0xDEADBEEF;
    ASSERT_NE(nullptr, h);

    AddResource(h, 'QUUX', 128, PSTR("Hello, world."));
    EXPECT_EQ(noErr, ResError());

    EXPECT_EQ(mapChanged, GetResFileAttrs(ref) & mapChanged);
    EXPECT_EQ(0, GetResFileAttrs(ref) & mapReadOnly);
    //EXPECT_EQ(mapCompact, GetResFileAttrs(ref) & mapCompact);

    Handle h2 = GetResource('QUUX', 128);
    EXPECT_EQ(h, h2);
    EXPECT_EQ(noErr, ResError());

        // ReleaseResource is documented to do nothing if resChanged is set
    ReleaseResource(h);
    EXPECT_NE(nullptr, *h);

        // .. so a new handle will not reuse the same address
    h2 = NewHandleClear(4);
    DisposeHandle(h2);
    EXPECT_NE(h, h2);

    h2 = GetResource('QUUX', 128);
    EXPECT_EQ(h, h2);
    
    CloseResFile(ref);

    h = GetResource('QUUX', 128);
    EXPECT_EQ(nullptr, h);

    ref = OpenResFile(filename);
    EXPECT_EQ(0, GetResFileAttrs(ref) & mapChanged);
    EXPECT_EQ(0, GetResFileAttrs(ref) & mapReadOnly);

    h = GetResource('QUUX', 128);
    EXPECT_NE(nullptr, h);
    EXPECT_NE(nullptr, *h);
    EXPECT_EQ(0xDEADBEEF, *(GUEST<uint32_t>*)*h);

    DetachResource(h);

    h2 = GetResource('QUUX', 128);
    EXPECT_NE(nullptr, h2);
    EXPECT_NE(nullptr, *h2);
    EXPECT_NE(h, h2);
    EXPECT_EQ(0xDEADBEEF, *(GUEST<uint32_t>*)*h2);

    ReleaseResource(h2);    // not really a way to check this

        // .. a new handle might reuse the same address
    Handle h3 = NewHandleClear(4);
        // but it's not guaranteed to.

        // but now the next GetResource probably won't reuse the
        // address.
    Handle h4 = GetResource('QUUX', 128);
    EXPECT_NE(nullptr, h4);
    EXPECT_NE(h2, h4);

    DisposeHandle(h3);
    DisposeHandle(h);
}

/* A resource is read into the current zone (the system heap if its
   resSysHeap attribute is set), not the zone of its file's map: INITs
   keep code for after startup this way, SetZone(SystemZone()) and then
   Get1Resource (the Shared Library Manager's 'root' 2, whose file the
   startup application heap holds the map of). */
TEST_F(ResourceTest, LoadsIntoCurrentZone)
{
    Handle h = NewHandleClear(16);
    ASSERT_NE(nullptr, h);
    AddResource(h, 'QUUX', 129, PSTR(""));
    ASSERT_EQ(noErr, ResError());
    reopen();
    THz app = GetZone();
    ASSERT_NE(SystemZone(), app);

    SetZone(SystemZone());
    Handle sys = Get1Resource('QUUX', 129);
    SetZone(app);
    ASSERT_NE(nullptr, sys);
    EXPECT_EQ(SystemZone(), HandleZone(sys));
    ReleaseResource(sys);

    Handle here = Get1Resource('QUUX', 129);
    ASSERT_NE(nullptr, here);
    EXPECT_EQ(app, HandleZone(here));
}

TEST_F(ResourceTest, Modify1)
{
    Handle h1;
    h1 = NewHandle(4);
    *(GUEST<uint32_t>*)*h1 = 0xDEADBEEF;
    ASSERT_NE(nullptr, h1);

    AddResource(h1, 'QUUX', 128, PSTR("Hello, world."));
    EXPECT_EQ(noErr, ResError());

    Handle h2;
    h2 = NewHandle(4);
    *(GUEST<uint32_t>*)*h2 = 0xFEEDFACE;

    AddResource(h2, 'QUUX', 129, PSTR("Hello again."));
    EXPECT_EQ(noErr, ResError());

    reopen();

    h1 = GetResource('QUUX', 128);
    h2 = GetResource('QUUX', 129);

    ASSERT_EQ(0xDEADBEEF, *(GUEST<uint32_t>*)*h1);
    ASSERT_EQ(0xFEEDFACE, *(GUEST<uint32_t>*)*h2);

    SetHandleSize(h1, 1024);
    memset(*h1, 0xAB, 1024);
    ChangedResource(h1);

    reopen();

    h1 = GetResource('QUUX', 128);
    h2 = GetResource('QUUX', 129);

    ASSERT_EQ(0xABABABAB, *(GUEST<uint32_t>*)*h1);
    ASSERT_EQ(0xFEEDFACE, *(GUEST<uint32_t>*)*h2);
}

/* A resource marked changed and then grown (ChangedResource before
   SetHandleSize) is written where it fits, not over the resources after
   it; with a resource removed too, the file is compacted on the way. THINK
   C 6 converting an old project does both (resMod.cpp:398 `lc == size`,
   M1.md). */
static void changedThenGrown(ResourceTest& t, bool remove)
{
    for(short id = 128; id <= 131; id++)
    {
        Handle h = NewHandle(4);
        *(GUEST<uint32_t>*)*h = 0xFEED0000 + id;
        AddResource(h, 'QUUX', id, PSTR(""));
        ASSERT_EQ(noErr, ResError());
    }
    t.reopen();

    if(remove)
    {
        Handle gone = GetResource('QUUX', 128);
        RemoveResource(gone);
        ASSERT_EQ(noErr, ResError());
        DisposeHandle(gone);
    }
    Handle h = GetResource('QUUX', 129);
    ChangedResource(h);
    SetHandleSize(h, 1024);
    memset(*h, 0xAB, 1024);

    t.reopen();
    h = GetResource('QUUX', 128);
    if(remove)
        EXPECT_EQ(nullptr, h);
    else
    {
        ASSERT_NE(nullptr, h);
        EXPECT_EQ(0xFEED0080u, *(GUEST<uint32_t>*)*h);
    }
    h = GetResource('QUUX', 129);
    ASSERT_NE(nullptr, h);
    EXPECT_EQ(1024, GetHandleSize(h));
    EXPECT_EQ(0xABABABAB, *(GUEST<uint32_t>*)(*h + 1020));
    for(short id = 130; id <= 131; id++)
    {
        h = GetResource('QUUX', id);
        ASSERT_NE(nullptr, h);
        EXPECT_EQ(4, GetHandleSize(h));
        EXPECT_EQ(0xFEED0000u + id, *(GUEST<uint32_t>*)*h);
    }
}

TEST_F(ResourceTest, ChangedThenGrown)
{
    changedThenGrown(*this, false);
}

TEST_F(ResourceTest, ChangedThenGrownCompacted)
{
    changedThenGrown(*this, true);
}

/* Added and not written yet, a resource's size on disk is its handle's: 0
   for an empty one (THINK C 6 re-adds an unloaded resource's empty handle
   under another ID and asks), and it is written empty. */
TEST_F(ResourceTest, AddedEmptySizeOnDisk)
{
    Handle h = NewHandle(4);
    *(GUEST<uint32_t>*)*h = 0xFEEDFACE;
    AddResource(h, 'QUUX', 128, PSTR(""));
    reopen();
    RemoveResource(GetResource('QUUX', 128)); /* compacted when written */

    Handle e = NewEmptyHandle();
    AddResource(e, 'QUUX', 1, PSTR(""));
    ASSERT_EQ(noErr, ResError());
    EXPECT_EQ(0, GetResourceSizeOnDisk(e));
    Handle f = NewHandle(8);
    AddResource(f, 'QUUX', 2, PSTR(""));
    EXPECT_EQ(8, GetResourceSizeOnDisk(f));

    reopen();
    e = GetResource('QUUX', 1);
    ASSERT_NE(nullptr, e);
    EXPECT_EQ(0, GetHandleSize(e));
    f = GetResource('QUUX', 2);
    ASSERT_NE(nullptr, f);
    EXPECT_EQ(8, GetHandleSize(f));
    EXPECT_EQ(nullptr, GetResource('QUUX', 128));
}

/* An index outside the list gives the empty string and writes nothing
   more, as on the Macs (tests/guest/strprobe; the count is big-endian: AppleWorks 5 asks for one string past its
   list and passes a buffer smaller than 256 bytes). */
TEST_F(ResourceTest, GetIndStringOutOfRange)
{
    static const unsigned char list[] = { 0, 2, 3, 'o', 'n', 'e', 3, 't', 'w', 'o' };
    Handle h = NewHandle(sizeof list);
    ASSERT_NE(nullptr, h);
    memcpy(*h, list, sizeof list);
    AddResource(h, 'STR#', 128, PSTR(""));
    EXPECT_EQ(noErr, ResError());

    Str255 s;
    memset(s, 0xAA, sizeof s);
    GetIndString(s, 128, 2);
    EXPECT_EQ(3, s[0]);
    EXPECT_EQ(0, memcmp(s + 1, "two", 3));

    for(short index : { 3, 0, -1 })
    {
        memset(s, 0xAA, sizeof s);
        GetIndString(s, 128, index);
        EXPECT_EQ(0, s[0]) << index;
        EXPECT_EQ(0xAA, s[1]) << index;
    }
}

/* The standard color tables are ROM resources: RGetResource finds them
   (PC Exchange's start-up icon asks for 'clut' 8). */
TEST(Resources, RGetResourceROMClut)
{
    for(short id : { 1, 2, 4, 8 })
    {
        Handle h = RGetResource('clut', id);
        ASSERT_NE(nullptr, h) << id;
        CTabHandle ct = (CTabHandle)h;
        EXPECT_EQ((1 << id) - 1, (*ct)->ctSize) << id;
    }
}

/* The packages' 'PACK' resources GetResource finds, as on a 7.6.1 Quadra
   (2026-11-03): its System file's 0, 2, 3, 6, 8, 9 and 11-15; 4, 5 and 7
   live in ROM and 1 and 10 nowhere. Executor's own System has stand-ins
   for those; a missing one is nil (Executor once answered an ALRT). */
TEST(Resources, SystemPacks)
{
    for(short id = 0; id < 16; id++)
    {
        bool there = !(id == 1 || id == 4 || id == 5 || id == 7 || id == 10);
        Handle h = GetResource('PACK', id);
        EXPECT_EQ(there, h != nullptr) << id;
        EXPECT_EQ(noErr, ResError()) << id;
        if(h)
            EXPECT_GE(GetHandleSize(h), 12) << id;
    }
}

/* The word break tables are in the script's 'itl2' (itlbSort): the
   word-selection and line-break tables' 16-bit offsets at 22 and 24 of
   its header, their lengths at 54 and 56, each an NBreakTable (flags1's
   high bit set, its own length at 14). Microsoft Works 4.0b takes a nil
   word-selection table for "not enough memory" when it makes a document.
   On Executor an 'itl2' 0 of the test's own (its System has none) at the
   top of the chain; on a Mac, the System's. */
TEST_F(ResourceTest, IntlWordBreakTables)
{
#ifdef EXECUTOR
    Handle h = NewHandleClear(200);
    char *p = (char *)*h;
    auto put = [&](int at, short v) { p[at] = v >> 8; p[at + 1] = v; };
    put(8, -1);                               /* 6.0.4 and later format */
    put(22, 64), put(54, 40);                 /* word-selection table */
    put(24, 120), put(56, 30);                /* line-break table */
    p[64] = p[120] = (char)0x80;
    put(64 + 14, 40), put(120 + 14, 30);
    AddResource(h, 'itl2', 0, PSTR(""));
    ASSERT_EQ(noErr, ResError());
#endif
    for(short code : { smWordSelectTable, smWordWrapTable })
    {
        Handle table = nullptr;
        long offset = -1, length = -1;
#ifdef EXECUTOR
        GUEST<Handle> th;
        GUEST<LONGINT> to, tl;
        GetIntlResourceTable(smSystemScript, code, &th, &to, &tl);
        table = th, offset = to, length = tl;
#else
        GetIntlResourceTable(smSystemScript, code, &table, &offset, &length);
#endif
        ASSERT_NE(nullptr, table) << code;
        ASSERT_GT(offset, 0) << code;
        ASSERT_GT(length, 16) << code;
        const unsigned char *t = (const unsigned char *)*table + offset;
        EXPECT_EQ(0x80, t[0] & 0x80) << code;
        EXPECT_EQ(length, (t[14] << 8) | t[15]) << code;
#ifdef EXECUTOR
        EXPECT_EQ(code == smWordSelectTable ? 64 : 120, offset);
#else
        /* On a System (the Quadra's, or Apple's under Executor): its own
           'itl2', the one GetIntlResource(2) answers (difftest intl). */
        EXPECT_EQ(GetIntlResource(2), table) << code;
        short id = -1;
        ResType type = 0;
        Str255 name;
        GetResInfo(table, &id, &type, name);
        EXPECT_EQ((ResType)'itl2', type) << code;
#endif
    }
}
