#include "gtest/gtest.h"
#include <Quickdraw.h>
#include <Windows.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <Files.h>
#include <Resources.h>
#include <Folders.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>

extern "C"
int mkdir(const char*, mode_t)
{
    return -1;
}

extern "C"
char *getcwd(char *buf, size_t size)
{
    buf[0] = '/';
    buf[1] = 0;
    return buf;
} 

// No libatomic for 68k; the app is single-threaded, so plain arithmetic
// does (googletest's reference counts use it).
extern "C"
unsigned int __atomic_fetch_sub_4(volatile void *p, unsigned int v, int)
{
    unsigned int old = *(volatile unsigned int *)p;
    *(volatile unsigned int *)p = old - v;
    return old;
}

int main(int argc, char **argv)
{
    static char filterArg[300];
    char *args[] = { argv[0], filterArg, nullptr };
#ifdef USE_CONSOLE
    std::cout << "Running Executor 2000 test suite..." << std::endl;
    // side effect: initialized Toolbox,
#else
    // "out" goes next to the application, wherever it was launched from:
    // the default directory becomes the folder holding our resource file.
    {
        Str63 name;
        FCBPBRec fcb = {};
        fcb.ioRefNum = CurResFile();
        fcb.ioNamePtr = name;
        if(PBGetFCBInfoSync(&fcb) == noErr)
            HSetVol(nullptr, fcb.ioFCBVRefNum, fcb.ioFCBParID);
    }
    freopen("out", "w", stdout);
    // Line by line, so a crash leaves the test it happened in at the end.
    setvbuf(stdout, nullptr, _IOLBF, 0);
    // A Mac application gets no arguments: a "filter" file beside it (one
    // line, googletest's --gtest_filter syntax) picks the tests to run.
    if(FILE *f = fopen("filter", "r"))
    {
        char line[256] = "";
        fgets(line, sizeof line, f);
        fclose(f);
        line[strcspn(line, "\r\n")] = 0;
        if(line[0])
        {
            snprintf(filterArg, sizeof filterArg, "--gtest_filter=%s", line);
            argc = 2;
            argv = args;
        }
    }
    // The tests make and delete files in the default directory: give them
    // a folder of their own in the startup disk's Temporary Items (where
    // the app lives can be a shared folder, which isn't HFS: Basilisk II's
    // ExtFS bombs on GetWDInfo).
    {
        short vRefNum;
        long dirID, sub;
        if(FindFolder(kOnSystemDisk, kTemporaryFolderType, kCreateFolder, &vRefNum, &dirID) == noErr)
        {
            Str63 name = "\pMacPhoenixTests";
            OSErr err = DirCreate(vRefNum, dirID, name, &sub);
            if(err == dupFNErr)
            {
                CInfoPBRec cpb = {};
                cpb.dirInfo.ioNamePtr = name;
                cpb.dirInfo.ioVRefNum = vRefNum;
                cpb.dirInfo.ioDrDirID = dirID;
                err = PBGetCatInfoSync(&cpb);
                sub = cpb.dirInfo.ioDrDirID;
            }
            if(err == noErr)
                err = HSetVol(nullptr, vRefNum, sub);
        }
    }
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    TEInit();
    InitDialogs(nullptr);
#endif

    testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    // The harness (tests/test_toolbox_suite.sh) waits for this line.
    printf("MACPHOENIX-DONE %d\n", result);
    fflush(stdout);
#ifdef USE_CONSOLE
    std::cout << "Press a key to exit - Test suite result: " << result << std::endl;
    getchar();
#endif
    return result;
}

