/* Copyright 1992 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <OSUtil.h>
#include <FileMgr.h>
#include <hfs/hfs.h>

using namespace Executor;

typedef enum { create,
               delete1 } createop;

static OSErr freeallblocks(HVCB *vcbp, filerec *frp)
{
    OSErr retval;
    filecontrolblock *fcbp;
    ParamBlockRec pbr;

    fcbp = ROMlib_getfreefcbp();
    if(!fcbp)
        retval = tmfoErr;
    else
    {
        fcbp->fcbVPtr = vcbp;
        fcbp->fcbFlNum = frp->filFlNum;
        fcbp->fcbPLen = frp->filPyLen;
        memmove((char *)fcbp->fcbExtRec, (char *)frp->filExtRec,
                (LONGINT)sizeof(frp->filExtRec));
        fcbp->fcbMdRByt = WRITEBIT;
        pbr.ioParam.ioMisc = 0;
        pbr.ioParam.ioRefNum = (char *)fcbp - (char *)LM(FCBSPtr);
        retval = ROMlib_allochelper((IOParam *)&pbr, false, seteof, false);
        if(retval == noErr)
        {
            fcbp->fcbPLen = frp->filRPyLen;
            memmove((char *)fcbp->fcbExtRec, (char *)frp->filRExtRec,
                    (LONGINT)sizeof(frp->filRExtRec));
            fcbp->fcbMdRByt = WRITEBIT | RESOURCEBIT;
            retval = ROMlib_allochelper((IOParam *)&pbr, false, seteof, false);
        }
    }
    fcbp->fcbFlNum = 0;
    return retval;
}

static OSErr createhelper(IOParam *pb, Boolean async, createop op,
                          LONGINT dirid, filekind kind)
{
    OSErr err, err1;
    filekind curkind;
    btparam btparamrec;
    directoryrec *drp;
    filerec *frp;
    HVCB *vcbp;

    curkind = (filekind)(regular | directory);
    err = ROMlib_findvcbandfile(pb, dirid, &btparamrec, &curkind, false);
    vcbp = btparamrec.vcbp;
    switch(err)
    {
        case noErr:
            if(op == create)
                err = dupFNErr;
            else
            {
                if(curkind == directory)
                {
                    drp = (directoryrec *)DATAPFROMKEY(btparamrec.foundp);
                    err = ROMlib_dirbusy(drp->dirDirID, vcbp);
                    if(err == noErr)
                    {
                        if(drp->dirVal != 0)
                            err = fBsyErr;
                        else
                            err = ROMlib_writevcbp(vcbp);
                    }
                    if(err == noErr)
                        err = ROMlib_dirdelete(&btparamrec);
                }
                else
                {
                    frp = (filerec *)DATAPFROMKEY(btparamrec.foundp);
                    if(ROMlib_alreadyopen(vcbp, frp->filFlNum,
                                          (SignedByte *)0, 0, eitherbusy)
                       != noErr)
                        err = fBsyErr;
#if 0
		else
		    err = ROMlib_dirtyleaf(frp, vcbp);
#endif
                    if(err == noErr)
                        err = ROMlib_writevcbp(vcbp);
                    if(err == noErr)
                    {
                        err = freeallblocks(vcbp, frp);
                        if(err == noErr)
                            err = ROMlib_filedelete(&btparamrec, regular);
                    }
                }
            }
            break;
        case fnfErr:
            if(op == delete1)
                err = fnfErr;
            else
            {
                err = ROMlib_writevcbp(vcbp);
                if(err == noErr)
                {
                    if(kind == directory)
                        err = ROMlib_btcreateemptydir(&btparamrec,
                                                      &((HFileParam *)pb)->ioDirID);
                    else
                        err = ROMlib_btcreateemptyfile(&btparamrec);
                }
            }
            break;
    }
    if(vcbp)
        err1 = ROMlib_cleancache(vcbp);
    else
        err1 = noErr;
    if(err1 == noErr && vcbp)
        err1 = ROMlib_flushvcbp(vcbp);
    if(err == noErr)
        err = err1;
    PBRETURN(pb, err);
}

OSErr Executor::hfsPBCreate(ParmBlkPtr pb, Boolean async)
{
    return createhelper((IOParam *)pb, async, create, (LONGINT)0, regular);
}

OSErr Executor::hfsPBHCreate(HParmBlkPtr pb, Boolean async)
{
    return createhelper((IOParam *)pb, async, create, pb->fileParam.ioDirID,
                        regular);
}

OSErr Executor::hfsPBDirCreate(HParmBlkPtr pb, Boolean async)
{
    return createhelper((IOParam *)pb, async, create, pb->fileParam.ioDirID,
                        directory);
}

OSErr Executor::hfsPBDelete(ParmBlkPtr pb, Boolean async)
{
    return createhelper((IOParam *)pb, async, delete1, (LONGINT)0,
                        (filekind)(regular | directory));
}

OSErr Executor::hfsPBHDelete(HParmBlkPtr pb, Boolean async)
{
    return createhelper((IOParam *)pb, async, delete1, pb->fileParam.ioDirID,
                        (filekind)(regular | directory));
}
