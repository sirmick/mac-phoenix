/* An HFS+ volume through the File Manager (file/hfsplusvolume.cpp): the
   long-name fixture made on Mac OS 9.0.4, mounted as Executor mounts disk
   images (ROMlib_openharddisk), read through the classic calls; what
   9.0.4 answered for the same calls (tests/guest/hfsplus/oracle-9.0.4). In
   the wasm build this runs under node, through emscripten's files.
   MakeLongNames wrote its "\r" with MPW's C, where '\r' is 0x0A. */
#include "gtest/gtest.h"

#include "compat.h"

#include <FileMgr.h>
#include <rsys/filesystem.h>
#include <file/file.h>
#include <hfs/hfs.h>

#include <algorithm>
#include <cstring>
#include <MemoryMgr.h>
#include <string>
#include <vector>

using namespace Executor;

#ifdef EXECUTOR_HFSPLUS_FIXTURE

namespace
{
StringPtr pstr(Str255 buf, const std::string& s)
{
    buf[0] = (uint8_t)s.size();
    memcpy(buf + 1, s.data(), s.size());
    return buf;
}

class HFSPlusVolumeTest : public testing::Test
{
protected:
    static short vref;

    static void SetUpTestSuite()
    {
        GUEST<LONGINT> mess;
        bool was = ROMlib_readonly_images;
        ROMlib_readonly_images = true;          /* a locked volume: what 9.0.4 answered was on one */
        ROMlib_openharddisk(EXECUTOR_HFSPLUS_FIXTURE, &mess);
        ROMlib_readonly_images = was;
        HParamBlockRec pb;
        Str255 name;
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioNamePtr = pstr(name, "LongNames:");
        pb.volumeParam.ioVolIndex = -1;
        if(PBHGetVInfo(&pb, false) == noErr)
            vref = pb.volumeParam.ioVRefNum;
    }

    static void TearDownTestSuite()
    {
        if(!vref)
            return;
        ParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioVRefNum = vref;
        EXPECT_EQ(noErr, PBUnmountVol(&pb));
        vref = 0;
    }

    void SetUp() override { ASSERT_NE(0, vref) << "the fixture didn't mount"; }

    /* PBGetCatInfo by name in dirID (the name in guest memory) */
    OSErr catInfo(long dirID, const std::string& name, CInfoPBRec& c)
    {
        StringPtr buf = (StringPtr)NewPtr(256);
        memset(&c, 0, sizeof c);
        c.hFileInfo.ioNamePtr = pstr(buf, name);
        c.hFileInfo.ioVRefNum = vref;
        c.hFileInfo.ioDirID = dirID;
        OSErr err = PBGetCatInfo(&c, false);
        c.hFileInfo.ioNamePtr = nullptr;
        DisposePtr((Ptr)buf);
        return err;
    }

    std::vector<uint8_t> readFork(long dirID, const std::string& name, bool rsrc)
    {
        Str255 buf;
        GUEST<INTEGER> ref;
        OSErr err = rsrc ? HOpenRF(vref, dirID, pstr(buf, name), fsRdPerm, &ref)
                         : HOpenDF(vref, dirID, pstr(buf, name), fsRdPerm, &ref);
        EXPECT_EQ(noErr, err) << name;
        if(err)
            return {};
        GUEST<LONGINT> eof;
        GetEOF(ref, &eof);
        Ptr p = NewPtr(std::max<LONGINT>(eof, 1));      /* guest memory */
        GUEST<LONGINT> n = eof.get();
        EXPECT_EQ(noErr, FSRead(ref, &n, p));
        EXPECT_EQ((LONGINT)eof, (LONGINT)n);
        FSClose(ref);
        std::vector<uint8_t> out((uint8_t *)p, (uint8_t *)p + (LONGINT)n);
        DisposePtr(p);
        return out;
    }
};
short HFSPlusVolumeTest::vref = 0;
} // namespace

TEST_F(HFSPlusVolumeTest, Volume)
{
    HParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioVRefNum = vref;
    ASSERT_EQ(noErr, PBHGetVInfo(&pb, false));
    EXPECT_EQ(0x4244, (uint16_t)pb.volumeParam.ioVSigWord);    /* 'BD', as 9.0.4 */
    EXPECT_TRUE(pb.volumeParam.ioVAtrb & 0x80);                 /* locked */
    EXPECT_EQ(4096, (LONGINT)pb.volumeParam.ioVAlBlkSiz);
    EXPECT_EQ(32768, (uint16_t)pb.volumeParam.ioVNmAlBlks);     /* 9.0.4's figure for 32 MB */
}

TEST_F(HFSPlusVolumeTest, LongNamesByTheirShortForm)
{
    CInfoPBRec c;
    ASSERT_EQ(noErr, catInfo(2, "This is a file with a ra#16.txt", c));
    EXPECT_EQ(0x16, (LONGINT)c.hFileInfo.ioDirID);
    EXPECT_EQ(48, (LONGINT)c.hFileInfo.ioFlLgLen);
    EXPECT_EQ(0x02, c.hFileInfo.ioFlAttrib & 0x7F);            /* 9.0.4: the record's flags */
    std::vector<uint8_t> data = readFork(2, "This is a file with a ra#16.txt", false);
    EXPECT_EQ(std::string("This is a file with a rather long name.txt data\n"), std::string(data.begin(), data.end()));

    /* the shortened form only as it is */
    EXPECT_EQ(fnfErr, catInfo(2, "THIS IS A FILE WITH A RA#16.TXT", c));
    /* ordinary names in any case */
    ASSERT_EQ(noErr, catInfo(2, "SHORT CAF\x8E.TXT", c));
    EXPECT_EQ(0x1B, (LONGINT)c.hFileInfo.ioDirID);
    /* a decomposed accent, composed */
    ASSERT_EQ(noErr, catInfo(2, "Caf\x8E" " cr\x8F" "me \xD1 accented an#1A.txt", c));
}

TEST_F(HFSPlusVolumeTest, Folders)
{
    CInfoPBRec c;
    ASSERT_EQ(noErr, catInfo(2, "A folder whose name is longe#22", c));
    EXPECT_TRUE(c.dirInfo.ioFlAttrib & ATTRIB_ISADIR);
    EXPECT_EQ(0x22, (LONGINT)c.dirInfo.ioDrDirID);
    EXPECT_EQ(2, (uint16_t)c.dirInfo.ioDrNmFls);
    ASSERT_EQ(noErr, catInfo(0x22, "Another long file name i#24.txt", c));
    EXPECT_EQ(0x22, (LONGINT)c.hFileInfo.ioFlParID);
    /* a full path */
    ASSERT_EQ(noErr, catInfo(0, "LongNames:A folder whose name is longe#22:inner.txt", c));
    EXPECT_EQ(0x23, (LONGINT)c.hFileInfo.ioDirID);
    /* "::" from the folder: the root */
    ASSERT_EQ(noErr, catInfo(0x22, "::", c));
    EXPECT_EQ(2, (LONGINT)c.dirInfo.ioDrDirID);

    /* index order, every item */
    int n = 0;
    for(short i = 1;; i++)
    {
        Str255 name;
        memset(&c, 0, sizeof c);
        c.hFileInfo.ioNamePtr = name;
        c.hFileInfo.ioVRefNum = vref;
        c.hFileInfo.ioDirID = 2;
        c.hFileInfo.ioFDirIndex = i;
        if(PBGetCatInfo(&c, false) != noErr)
            break;
        n++;
    }
    Str255 none;
    memset(&c, 0, sizeof c);
    none[0] = 0;
    c.hFileInfo.ioNamePtr = none;
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = 2;
    c.hFileInfo.ioFDirIndex = -1;
    ASSERT_EQ(noErr, PBGetCatInfo(&c, false));
    EXPECT_EQ(n, (uint16_t)c.dirInfo.ioDrNmFls);
    EXPECT_EQ(std::string("LongNames"), std::string((char *)none + 1, none[0]));
}

TEST_F(HFSPlusVolumeTest, ForkPastItsEightExtents)
{
    /* 22 bytes of text, then 24 chunks of 64 KB written in turn with
       another file's: 25 extents, 6 records in the extents overflow file */
    std::vector<uint8_t> data = readFork(2, "Fragmented A.bin", false);
    ASSERT_EQ(22u + 24 * 65536, data.size());
    EXPECT_EQ(std::string("Fragmented A.bin data\n"), std::string(data.begin(), data.begin() + 22));
    int bad = 0;
    for(int round = 0; round < 24; round++)
        for(long i = 0; i < 65536; i++)
            bad += data[22 + round * 65536 + i] != (uint8_t)(i * 7 + round * 31);
    EXPECT_EQ(0, bad);
}

TEST_F(HFSPlusVolumeTest, ResourceFork)
{
    std::vector<uint8_t> r = readFork(2, "Long named document wit#21.rsrc", true);
    EXPECT_EQ(std::string("Long named document with both forks.rsrc resource fork\n"), std::string(r.begin(), r.end()));
}

TEST_F(HFSPlusVolumeTest, Locked)
{
    Str255 name;
    EXPECT_EQ(wPrErr, HCreate(vref, 2, pstr(name, "New file"), "TEXT"_4, "ttxt"_4));
    EXPECT_EQ(wPrErr, HDelete(vref, 2, pstr(name, "Short Caf\x8E.txt")));
    /* opened for writing: allowed; writing through it, wPrErr (9.0.4) */
    GUEST<INTEGER> ref;
    ASSERT_EQ(noErr, HOpenDF(vref, 2, pstr(name, "Short Caf\x8E.txt"), fsRdWrPerm, &ref));
    Ptr bytes = NewPtrClear(4);
    GUEST<LONGINT> n = 4;
    EXPECT_EQ(wPrErr, FSWrite(ref, &n, bytes));
    FSClose(ref);
    ASSERT_EQ(noErr, HOpenDF(vref, 2, pstr(name, "Short Caf\x8E.txt"), fsRdPerm, &ref));
    n = 4;
    EXPECT_EQ(wrPermErr, FSWrite(ref, &n, bytes));
    FSClose(ref);
    DisposePtr(bytes);
}

/* ---- writing: a blank volume (mkfs.hfsplus), mounted writable ----------- */

#ifdef EXECUTOR_HFSPLUS_BLANK
namespace
{
class HFSPlusWriteTest : public testing::Test
{
protected:
    static short vref;

    static std::string copy;

    static void SetUpTestSuite() { mount(EXECUTOR_HFSPLUS_BLANK); }

    static void mount(const char *image)
    {
        /* a fresh copy each run */
        copy = (fs::temp_directory_path() / fs::unique_path("hfsplus-blank-%%%%-%%%%.img")).string();
        std::error_code ec;
        fs::copy_file(image, copy, ec);
        GUEST<LONGINT> mess;
        ROMlib_openharddisk(copy.c_str(), &mess);
        HParamBlockRec pb;
        Str255 name;
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioNamePtr = pstr(name, "Blank:");
        pb.volumeParam.ioVolIndex = -1;
        if(PBHGetVInfo(&pb, false) == noErr)
            vref = pb.volumeParam.ioVRefNum;
    }

    static void TearDownTestSuite()
    {
        if(!vref)
            return;
        ParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioVRefNum = vref;
        EXPECT_EQ(noErr, PBUnmountVol(&pb));
        vref = 0;
        std::error_code ec;
        fs::remove(copy, ec);
    }

    void SetUp() override { ASSERT_NE(0, vref) << "the blank volume didn't mount"; }

    StringPtr name(const std::string& s)
    {
        static Str255 bufs[4];
        static int k;
        return pstr(bufs[k++ & 3], s);
    }

    OSErr writeFile(long dir, const std::string& n, long size, int seed)
    {
        GUEST<INTEGER> ref;
        OSErr err = HOpenDF(vref, dir, name(n), fsRdWrPerm, &ref);
        if(err)
            return err;
        Ptr p = NewPtr(size);
        for(long i = 0; i < size; i++)
            p[i] = (char)(seed + i * 13);
        GUEST<LONGINT> count = size;
        err = FSWrite(ref, &count, p);
        DisposePtr(p);
        FSClose(ref);
        return err;
    }
};
short HFSPlusWriteTest::vref = 0;

/* DirCreate and CatMove, through their parameter blocks */
OSErr DirCreate(short vref, long parent, StringPtr name, GUEST<LONGINT> *made)
{
    HParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.fileParam.ioNamePtr = name;
    pb.fileParam.ioVRefNum = vref;
    pb.fileParam.ioDirID = parent;
    OSErr err = PBDirCreate(&pb, false);
    *made = pb.fileParam.ioDirID;
    return err;
}

OSErr CatMove(short vref, long dirID, StringPtr name, long newDirID, StringPtr newName)
{
    CMovePBRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioNamePtr = name;
    pb.ioVRefNum = vref;
    pb.ioDirID = dirID;
    pb.ioNewDirID = newDirID;
    pb.ioNewName = newName[0] ? newName : nullptr;
    return PBCatMove(&pb, false);
}
std::string HFSPlusWriteTest::copy;
} // namespace

TEST_F(HFSPlusWriteTest, Writable)
{
    HParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioVRefNum = vref;
    ASSERT_EQ(noErr, PBHGetVInfo(&pb, false));
    EXPECT_FALSE(pb.volumeParam.ioVAtrb & 0x8080);             /* not locked */
}

TEST_F(HFSPlusWriteTest, CreateWriteRead)
{
    ASSERT_EQ(noErr, HCreate(vref, 2, name("Written"), "TEXT"_4, "ttxt"_4));
    EXPECT_EQ(dupFNErr, HCreate(vref, 2, name("WRITTEN"), "TEXT"_4, "ttxt"_4));
    EXPECT_EQ(bdNamErr, HCreate(vref, 2, name("Thirty-two characters long name!"), "TEXT"_4, "ttxt"_4));
    ASSERT_EQ(noErr, writeFile(2, "Written", 100000, 3));
    GUEST<INTEGER> ref;
    ASSERT_EQ(noErr, HOpenDF(vref, 2, name("Written"), fsRdPerm, &ref));
    GUEST<LONGINT> eof;
    GetEOF(ref, &eof);
    EXPECT_EQ(100000, (LONGINT)eof);
    Ptr p = NewPtr(100000);
    GUEST<LONGINT> count = 100000;
    EXPECT_EQ(noErr, FSRead(ref, &count, p));
    int bad = 0;
    for(long i = 0; i < 100000; i++)
        bad += (uint8_t)p[i] != (uint8_t)(3 + i * 13);
    EXPECT_EQ(0, bad);
    DisposePtr(p);
    FSClose(ref);
    /* opened for writing twice: opWrErr; deleting it open: fBsyErr */
    GUEST<INTEGER> a, b;
    ASSERT_EQ(noErr, HOpenDF(vref, 2, name("Written"), fsRdWrPerm, &a));
    EXPECT_EQ(opWrErr, HOpenDF(vref, 2, name("Written"), fsRdWrPerm, &b));
    EXPECT_EQ(fBsyErr, HDelete(vref, 2, name("Written")));
    FSClose(a);
}

TEST_F(HFSPlusWriteTest, FoldersRenameMoveDelete)
{
    GUEST<LONGINT> dir, sub;
    ASSERT_EQ(noErr, DirCreate(vref, 2, name("Folder"), &dir));
    ASSERT_EQ(noErr, DirCreate(vref, dir, name("Sub"), &sub));
    ASSERT_EQ(noErr, HCreate(vref, dir, name("Inside"), "TEXT"_4, "ttxt"_4));
    EXPECT_EQ(badMovErr, CatMove(vref, 2, name("Folder"), sub, name("")));
    EXPECT_EQ(fBsyErr, HDelete(vref, 2, name("Folder")));
    ASSERT_EQ(noErr, HRename(vref, dir, name("Inside"), name("Renamed Caf\x8E")));
    ASSERT_EQ(noErr, CatMove(vref, dir, name("Renamed Caf\x8E"), sub, name("")));
    CInfoPBRec c;
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = name(":Folder:Sub:Renamed CAF\x8E");    /* relative; case-insensitive */
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = 2;
    ASSERT_EQ(noErr, PBGetCatInfo(&c, false));
    EXPECT_EQ((LONGINT)sub, (LONGINT)c.hFileInfo.ioFlParID);
    EXPECT_EQ(noErr, HDelete(vref, sub, name("Renamed Caf\x8E")));
    EXPECT_EQ(noErr, HDelete(vref, dir, name("Sub")));
    EXPECT_EQ(noErr, HDelete(vref, 2, name("Folder")));
    EXPECT_EQ(fnfErr, HDelete(vref, 2, name("Folder")));
}

TEST_F(HFSPlusWriteTest, ManyFilesPastTheirExtents)
{
    GUEST<LONGINT> dir;
    ASSERT_EQ(noErr, DirCreate(vref, 2, name("Many"), &dir));
    char n[40];
    for(int i = 0; i < 300; i++)
    {
        snprintf(n, sizeof n, "File %03d with a longer name", (i * 37) % 300);
        ASSERT_EQ(noErr, HCreate(vref, dir, name(n), "TEXT"_4, "ttxt"_4)) << n;
    }
    /* two files appended in turns: each in many extents */
    ASSERT_EQ(noErr, HCreate(vref, dir, name("A"), "BINA"_4, "ttxt"_4));
    ASSERT_EQ(noErr, HCreate(vref, dir, name("B"), "BINA"_4, "ttxt"_4));
    Ptr p = NewPtr(8192);
    GUEST<INTEGER> a, b;
    ASSERT_EQ(noErr, HOpenDF(vref, dir, name("A"), fsRdWrPerm, &a));
    ASSERT_EQ(noErr, HOpenDF(vref, dir, name("B"), fsRdWrPerm, &b));
    for(int r = 0; r < 20; r++)
        for(GUEST<INTEGER> *ref : { &a, &b })
        {
            memset(p, r, 8192);
            GUEST<LONGINT> count = 8192;
            ASSERT_EQ(noErr, FSWrite(*ref, &count, p));
        }
    FSClose(a);
    FSClose(b);
    ASSERT_EQ(noErr, HOpenDF(vref, dir, name("A"), fsRdPerm, &a));
    int bad = 0;
    for(int r = 0; r < 20; r++)
    {
        GUEST<LONGINT> count = 8192;
        ASSERT_EQ(noErr, FSRead(a, &count, p));
        for(int i = 0; i < 8192; i++)
            bad += p[i] != r;
    }
    EXPECT_EQ(0, bad);
    FSClose(a);
    DisposePtr(p);
    for(int i = 0; i < 300; i += 2)
    {
        snprintf(n, sizeof n, "File %03d with a longer name", i);
        ASSERT_EQ(noErr, HDelete(vref, dir, name(n))) << n;
    }
    CInfoPBRec c;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = dir;
    c.dirInfo.ioFDirIndex = -1;
    ASSERT_EQ(noErr, PBGetCatInfo(&c, false));
    EXPECT_EQ(152, (uint16_t)c.dirInfo.ioDrNmFls);
    EXPECT_EQ(noErr, FlushVol(nullptr, vref));
}

/* An HFSX volume (mkfs.hfsplus -s): names in binary order, so names
   differing only in case are different names. */
#ifdef EXECUTOR_HFSX_BLANK
namespace
{
class HFSXWriteTest : public HFSPlusWriteTest
{
protected:
    static void SetUpTestSuite() { mount(EXECUTOR_HFSX_BLANK); }
};
} // namespace

TEST_F(HFSXWriteTest, NamesAreCaseSensitive)
{
    ASSERT_EQ(noErr, HCreate(vref, 2, name("Written"), "TEXT"_4, "ttxt"_4));
    ASSERT_EQ(noErr, HCreate(vref, 2, name("WRITTEN"), "TEXT"_4, "ttxt"_4));
    EXPECT_EQ(dupFNErr, HCreate(vref, 2, name("Written"), "TEXT"_4, "ttxt"_4));
    ASSERT_EQ(noErr, writeFile(2, "Written", 1000, 1));
    ASSERT_EQ(noErr, writeFile(2, "WRITTEN", 2000, 2));
    GUEST<INTEGER> ref;
    EXPECT_EQ(fnfErr, HOpenDF(vref, 2, name("written"), fsRdPerm, &ref));
    ASSERT_EQ(noErr, HOpenDF(vref, 2, name("WRITTEN"), fsRdPerm, &ref));
    GUEST<LONGINT> eof;
    GetEOF(ref, &eof);
    EXPECT_EQ(2000, (LONGINT)eof);
    FSClose(ref);
    /* a rename changing only case; then it's free again */
    ASSERT_EQ(noErr, HRename(vref, 2, name("Written"), name("written")));
    EXPECT_EQ(dupFNErr, HRename(vref, 2, name("written"), name("WRITTEN")));
    ASSERT_EQ(noErr, HCreate(vref, 2, name("Written"), "TEXT"_4, "ttxt"_4));
    GUEST<LONGINT> dir;
    ASSERT_EQ(noErr, DirCreate(vref, 2, name("folder"), &dir));
    ASSERT_EQ(noErr, DirCreate(vref, 2, name("Folder"), &dir));
    CInfoPBRec c;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = 2;
    c.dirInfo.ioFDirIndex = -1;
    ASSERT_EQ(noErr, PBGetCatInfo(&c, false));
    EXPECT_EQ(5, (uint16_t)c.dirInfo.ioDrNmFls);
    for(const char *n : { "Written", "written", "WRITTEN" })
        EXPECT_EQ(noErr, HDelete(vref, 2, name(n))) << n;
    EXPECT_EQ(noErr, HDelete(vref, 2, name("Folder")));
    EXPECT_EQ(noErr, HDelete(vref, 2, name("folder")));
    EXPECT_EQ(noErr, FlushVol(nullptr, vref));
}
#endif
#endif

#endif
