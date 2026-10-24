/* An HFS+ volume through the File Manager (file/hfsplusvolume.cpp): the
   long-name fixture made on Mac OS 9.0.4, mounted as Executor mounts disk
   images (ROMlib_openharddisk), read through the classic calls; what
   9.0.4 answered for the same calls (tests/guest/hfsplus/oracle-9.0.4). In
   the wasm build this runs under node, through emscripten's files.
   MakeLongNames wrote its "\r" with MPW's C, where '\r' is 0x0A. */
#include "gtest/gtest.h"

#include "compat.h"

#include <FileMgr.h>
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
        ROMlib_openharddisk(EXECUTOR_HFSPLUS_FIXTURE, &mess);
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

#endif
