/* The Printing Manager's driver, as a 7.6.1 Quadra opens it (a probe, with
   the ImageWriter chosen in the Chooser): STR -8192 names it, PrOpen finds
   it in the Extensions folder, makes it the current resource file and puts
   its reference number in PrRefNum ($952), where applications linked with
   the old printing glue (Persuasion 3.0) look for its PDEFs; PrClose
   closes it, the file before it current again, PrRefNum left alone. */
#include "gtest/gtest.h"

#include "compat.h"

#include <base/common.h>
#include <PrintMgr.h>
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <AliasMgr.h>
#include <MemoryMgr.h>
#include <LowMem.h>
#include <ToolboxUtil.h>

using namespace Executor;

namespace
{
const unsigned char driver_name[] = "\x0dTest Printer!";

class PrintDriver : public testing::Test
{
public:
    GUEST<INTEGER> vref;
    GUEST<LONGINT> dirid;
    INTEGER chooser = -1;
    INTEGER before;

    PrintDriver()
    {
        before = CurResFile();
        EXPECT_EQ(noErr, FindFolder(0, kExtensionFolderType, true, &vref, &dirid));
        HCreateResFile(vref, dirid, driver_name);
        INTEGER rn = HOpenResFile(vref, dirid, driver_name, fsRdWrPerm);
        EXPECT_NE(-1, rn);
        Handle pdef = NewHandle(4);
        AddResource(pdef, 'PDEF', 0, PSTR(""));
        CloseResFile(rn);
        UseResFile(before);
    }

    /* the Chooser's choice, STR -8192, in a file on the resource chain */
    void choose(const unsigned char *name = driver_name)
    {
        CreateResFile(PSTR("test-chooser"));
        chooser = OpenResFile(PSTR("test-chooser"));
        ASSERT_NE(-1, chooser);
        Handle h = NewHandle(name[0] + 1);
        memcpy(*h, name, name[0] + 1);
        AddResource(h, 'STR ', -8192, PSTR(""));
        before = CurResFile();
    }

    ~PrintDriver()
    {
        if(chooser != -1)
        {
            CloseResFile(chooser);
            FSDelete(PSTR("test-chooser"), 0);
        }
        HDelete(vref, dirid, driver_name);
    }
};
}

TEST_F(PrintDriver, OpenFromExtensions)
{
    choose();
    LM(PrRefNum) = -1;
    PrOpen();
    EXPECT_EQ(noErr, PrError());
    INTEGER rn = LM(PrRefNum);
    EXPECT_NE(-1, rn);
    EXPECT_EQ(rn, CurResFile()) << "the driver is the current resource file";
    EXPECT_NE(nullptr, Get1Resource('PDEF', 0));

    PrClose();
    EXPECT_EQ(noErr, PrError());
    EXPECT_EQ(rn, LM(PrRefNum)) << "PrClose leaves PrRefNum";
    EXPECT_EQ(before, CurResFile());
    UseResFile(rn);
    EXPECT_EQ(resFNotFound, ResError()) << "the driver closed";
    UseResFile(before);
}

/* the chosen driver isn't there: fnfErr, PrRefNum untouched (the Quadra
   says -43 with no printer chosen) */
TEST_F(PrintDriver, DriverMissing)
{
    choose((const unsigned char *)"\x0fNo Such Printer");
    LM(PrRefNum) = -1;
    PrOpen();
    EXPECT_EQ(fnfErr, PrError());
    EXPECT_EQ(-1, LM(PrRefNum));
    EXPECT_EQ(before, CurResFile());
    PrClose();
}
