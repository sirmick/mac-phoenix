/* An HFS volume's unmounting (hfs/hfsVolume.cpp): as on a Mac, a volume
   with files open on it is busy (fBsyErr) and stays mounted, the Disks
   panel's eject and the Finder's Put Away say so; its own files (the
   B-trees, the desktop database's FCB) don't count. A blank floppy made
   by hformat, a fresh copy each test. */
#include "gtest/gtest.h"

#include "compat.h"

#include <FileMgr.h>
#include <rsys/filesystem.h>
#include <file/file.h>
#include <hfs/hfs.h>
#include <Finder.h>

#include <cstring>
#include <string>

using namespace Executor;

#ifdef EXECUTOR_HFS_BLANK
namespace
{
class HFSUnmountTest : public testing::Test
{
protected:
    short vref = 0;
    std::string copy;
    StringPtr name(const std::string& s)
    {
        static Str255 nameBuf; /* guest-addressable, as the fixture isn't */
        nameBuf[0] = (uint8_t)s.size();
        memcpy(nameBuf + 1, s.data(), s.size());
        return nameBuf;
    }

    void SetUp() override
    {
        copy = (fs::temp_directory_path() / fs::unique_path("hfs-blank-%%%%-%%%%.img")).string();
        std::error_code ec;
        fs::copy_file(EXECUTOR_HFS_BLANK, copy, ec);
        ASSERT_FALSE(ec);
        GUEST<LONGINT> mess;
        ROMlib_openharddisk(copy.c_str(), &mess);
        HParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioNamePtr = name("Blank:");
        pb.volumeParam.ioVolIndex = -1;
        ASSERT_EQ(noErr, PBHGetVInfo(&pb, false));
        vref = pb.volumeParam.ioVRefNum;
    }

    void TearDown() override
    {
        if(mounted())
        {
            ParamBlockRec pb;
            memset(&pb, 0, sizeof pb);
            pb.volumeParam.ioVRefNum = vref;
            hfsPBUnmountVol(&pb, true);
        }
        std::error_code ec;
        fs::remove(copy, ec);
    }

    OSErr unmount()
    {
        ParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioVRefNum = vref;
        return PBUnmountVol(&pb);
    }

    bool mounted()
    {
        HParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioVRefNum = vref;
        return vref && PBHGetVInfo(&pb, false) == noErr;
    }
};
} // namespace

TEST_F(HFSUnmountTest, Idle)
{
    EXPECT_EQ(noErr, unmount());
    EXPECT_FALSE(mounted());
}

TEST_F(HFSUnmountTest, OpenFileIsBusy)
{
    ASSERT_EQ(noErr, HCreate(vref, 2, name("Open"), "TEXT"_4, "ttxt"_4));
    GUEST<INTEGER> ref;
    ASSERT_EQ(noErr, HOpenDF(vref, 2, name("Open"), fsRdWrPerm, &ref));
    EXPECT_EQ(fBsyErr, unmount());
    EXPECT_TRUE(mounted());
    /* the file is still good */
    char data[] = "still here";
    GUEST<LONGINT> count = sizeof data;
    EXPECT_EQ(noErr, FSWrite(ref, &count, (Ptr)data));
    EXPECT_EQ(noErr, FSClose(ref));
    EXPECT_EQ(noErr, unmount());
    EXPECT_FALSE(mounted());
}

TEST_F(HFSUnmountTest, OpenResourceForkIsBusy)
{
    ASSERT_EQ(noErr, HCreate(vref, 2, name("Forked"), "TEXT"_4, "ttxt"_4));
    GUEST<INTEGER> ref;
    ASSERT_EQ(noErr, HOpenRF(vref, 2, name("Forked"), fsRdWrPerm, &ref));
    EXPECT_EQ(fBsyErr, unmount());
    EXPECT_EQ(noErr, FSClose(ref));
    EXPECT_EQ(noErr, unmount());
}

TEST_F(HFSUnmountTest, DesktopDatabaseIsNotBusy)
{
    DTPBRec dt;
    memset(&dt, 0, sizeof dt);
    dt.ioVRefNum = vref;
    ASSERT_EQ(noErr, PBDTOpenInform(&dt));
    ASSERT_NE(0, dt.ioDTRefNum);
    EXPECT_EQ(noErr, unmount());
    EXPECT_FALSE(mounted());
    /* the database went with the volume */
    EXPECT_FALSE(ROMlib_desktop_refnum_p(dt.ioDTRefNum));
}

/* The end (ROMlib_OurClose): open files are closed, the volume goes. */
TEST_F(HFSUnmountTest, ForcedClosesFiles)
{
    ASSERT_EQ(noErr, HCreate(vref, 2, name("Left"), "TEXT"_4, "ttxt"_4));
    GUEST<INTEGER> ref;
    ASSERT_EQ(noErr, HOpenDF(vref, 2, name("Left"), fsRdWrPerm, &ref));
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioVRefNum = vref;
    EXPECT_EQ(noErr, hfsPBUnmountVol(&pb, true));
    EXPECT_FALSE(mounted());
    EXPECT_EQ(nullptr, ROMlib_refnumtofcbp(ref));
}

/* GetFInfo/SetFInfo of a folder's name: fnfErr, as on a Mac (and on
   Executor's HFS+ and host volumes), not bdNamErr. CodeWarrior checks
   its access paths with FSMakeFSSpec then FSpGetFInfo and takes any
   other error for a folder that isn't there ("Couldn't find project
   file"). */
using HFSVolumeTest = HFSUnmountTest;
TEST_F(HFSVolumeTest, FInfoOfAFolderIsFnfErr)
{
    FSSpec spec;
    EXPECT_EQ(fnfErr, FSMakeFSSpec(vref, 2, name("Folder"), &spec));
    GUEST<LONGINT> dirID;
    ASSERT_EQ(noErr, FSpDirCreate(&spec, -1 /* smSystemScript */, &dirID));
    FInfo fi;
    EXPECT_EQ(fnfErr, HGetFInfo(vref, 2, name("Folder"), &fi));
    ASSERT_EQ(noErr, FSMakeFSSpec(vref, 2, name("Folder"), &spec));
    EXPECT_EQ(fnfErr, FSpGetFInfo(&spec, &fi));
    memset(&fi, 0, sizeof fi);
    EXPECT_EQ(fnfErr, FSpSetFInfo(&spec, &fi));
    /* still bdNamErr for a name that can't be one; fnfErr for none */
    EXPECT_EQ(bdNamErr, HGetFInfo(vref, 2, name(std::string(40, 'x')), &fi));
    EXPECT_EQ(fnfErr, HGetFInfo(vref, 2, name("Nothing"), &fi));
    /* a file is still found */
    ASSERT_EQ(noErr, HCreate(vref, dirID, name("File"), "ttxt"_4, "TEXT"_4));
    EXPECT_EQ(noErr, HGetFInfo(vref, dirID, name("File"), &fi));
    EXPECT_EQ("TEXT"_4, fi.fdType);
}
#endif
