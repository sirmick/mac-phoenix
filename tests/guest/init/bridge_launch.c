/*
 * bridge_launch.c - App launch implementation for Bridge INIT
 *
 * Parses a "Volume:Filename" path, resolves via FSMakeFSSpec,
 * and calls LaunchApplication with launchContinue.
 */
#include <Files.h>
#include <Processes.h>
#include <string.h>

/*
 * Launch an application by path.
 *
 * path is a Pascal string in the form "Volume:Filename" or just "Filename".
 * Uses FSMakeFSSpec to resolve the path, then LaunchApplication.
 *
 * Returns noErr on success, or a Mac OS error code.
 */
OSErr do_launch(const unsigned char *path)
{
    FSSpec spec;
    LaunchParamBlockRec lpb;
    OSErr err;

    /* FSMakeFSSpec can parse full "Volume:path" strings directly
     * when vRefNum=0 and dirID=0 (uses the default volume rules).
     * For ExtFS paths like "Host:MacTestSuite", this resolves correctly. */
    err = FSMakeFSSpec(0, 0, path, &spec);
    if (err != noErr)
        return err;

    /* Build LaunchParamBlockRec */
    memset(&lpb, 0, sizeof(lpb));
    lpb.launchBlockID = extendedBlock;       /* 'LC' = 0x4C43 */
    lpb.launchEPBLength = extendedBlockLen;  /* size of extended fields */
    lpb.launchControlFlags = launchContinue | launchNoFileFlags;
    lpb.launchAppSpec = &spec;

    err = LaunchApplication(&lpb);
    return err;
}
