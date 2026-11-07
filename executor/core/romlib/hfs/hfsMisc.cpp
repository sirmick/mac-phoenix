/* Copyright 1992 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <OSUtil.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <hfs/hfs.h>
#include <file/file.h>

using namespace Executor;

#if defined(TESTFCB)
void testfcb()
{
    short length;
    filecontrolblock *fp;
    INTEGER i;

    length = *(short *)LM(FCBSPtr);
    fp = (filecontrolblock *)((short *)LM(FCBSPtr) + 1);
    printf("length = %d, length / 94 = %d, length mod 94 = %d\n",
           length, length / 94, length % 94);
    for(i = 0; i < 40 && i < length / 94; i++, fp++)
    {
        printf("# %ld flags 0x%x vers %d sblk %d EOF %ld PLEN %ld mark %ld\n"
               "vptr 0x%lx pbuffer 0x%lx FlPos %d clmpsiz %ld BTCBPtr 0x%lx\n"
               "ext (%d %d) (%d %d) (%d %d) FNDR '%c%c%c%c' CatPos 0x%lx\n"
               "parid %ld name %s\n",
               fp->fcbFlNum, fp->fcbMdRByt,
               fp->fcbTypByt, fp->fcbSBlk, fp->fcbEOF, fp->fcbPLen, fp->fcbCrPs,
               fp->fcbVPtr, fp->fcbBfAdr, fp->fcbFlPos, fp->fcbClmpSize,
               fp->fcbBTCBPtr,
               fp->fcbExtRec[0].blockstart, fp->fcbExtRec[0].blockcount,
               fp->fcbExtRec[1].blockstart, fp->fcbExtRec[1].blockcount,
               fp->fcbExtRec[2].blockstart, fp->fcbExtRec[2].blockcount,
               (short)(fp->fcbFType >> 24), (short)(fp->fcbFType >> 16),
               (short)fp->fcbFType >> 8, (short)fp->fcbFType,
               fp->fcbCatPos, fp->fcbDirID, fp->fcbCName + 1);
    }
}
#endif /* TESTFCB */

