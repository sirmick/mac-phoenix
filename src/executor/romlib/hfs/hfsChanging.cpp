/* Copyright 1992-1993 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <OSUtil.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <hfs/hfs.h>

using namespace Executor;

typedef enum {
    GetOp,
    SetOp,
    LockOp,
    UnlockOp
} changeop;

static OSErr PBFInfoHelper(changeop op, FileParam *pb, LONGINT dirid,
                           Boolean async)
{
    OSErr err, err1;
    HVCB *vcbp;
    filerec *frp = nullptr;
    catkey *catkeyp = nullptr;
    btparam btparamrec;
    filekind kind;

    vcbp = 0;
    if(op == GetOp && (pb->ioFDirIndex > 0))
        err = ROMlib_btpbindex((IOParam *)pb, dirid, &vcbp, &frp, &catkeyp,
                               true);
    else
    {
        kind = regular;
        err = ROMlib_findvcbandfile((IOParam *)pb, dirid, &btparamrec, &kind,
                                    false);
        if(err == noErr)
        {
            if(btparamrec.success)
            {
                vcbp = btparamrec.vcbp;
                frp = (filerec *)DATAPFROMKEY(btparamrec.foundp);
                catkeyp = (catkey *)btparamrec.foundp;
            }
            else
                err = fnfErr;
        }
    }
    if(err == noErr)
    {
        switch(op)
        {
            case GetOp:
                if(pb->ioFDirIndex > 0 && pb->ioNamePtr)
                    str255assign(pb->ioNamePtr, catkeyp->ckrCName);
                pb->ioFlAttrib = open_attrib_bits(frp->filFlNum, vcbp,
                                                     &pb->ioFRefNum);
                pb->ioFlAttrib |= frp->filFlags & INHERITED_FLAG_BITS;
                pb->ioFlVersNum = 0;
                memmove(&pb->ioFlFndrInfo, &frp->filUsrWds,
                        (LONGINT)sizeof(pb->ioFlFndrInfo));
                pb->ioFlNum = frp->filFlNum;
                pb->ioFlStBlk = frp->filStBlk;
                pb->ioFlLgLen = frp->filLgLen;
                pb->ioFlPyLen = frp->filPyLen;
                pb->ioFlRStBlk = frp->filRStBlk;
                pb->ioFlRLgLen = frp->filRLgLen;
                pb->ioFlRPyLen = frp->filRPyLen;
                pb->ioFlCrDat = frp->filCrDat;
                pb->ioFlMdDat = frp->filMdDat;
                break;
            case SetOp:
                memmove(&frp->filUsrWds, &pb->ioFlFndrInfo,
                        (LONGINT)sizeof(frp->filUsrWds));
                frp->filCrDat = pb->ioFlCrDat;
                frp->filMdDat = pb->ioFlMdDat;
                ROMlib_dirtyleaf(frp, vcbp);
                ROMlib_flushvcbp(vcbp);
                break;
            case LockOp:
                frp->filFlags |= FSOFTLOCKBIT;
                ROMlib_dirtyleaf(frp, vcbp);
                break;
            case UnlockOp:
                frp->filFlags &= ~FSOFTLOCKBIT;
                ROMlib_dirtyleaf(frp, vcbp);
                break;
        }
    }
    if(vcbp)
    {
        err1 = ROMlib_cleancache(vcbp);
        if(err == noErr)
            err = err1;
    }
    PBRETURN(pb, err);
}

OSErr Executor::hfsPBGetFInfo(ParmBlkPtr pb, Boolean async)
{
    return PBFInfoHelper(GetOp, (FileParam *)pb, 0L, async);
}

OSErr Executor::hfsPBHGetFInfo(HParmBlkPtr pb, Boolean async)
{
    return PBFInfoHelper(GetOp, (FileParam *)pb, pb->fileParam.ioDirID, async);
}

OSErr Executor::hfsPBSetFInfo(ParmBlkPtr pb, Boolean async)
{
    return PBFInfoHelper(SetOp, (FileParam *)pb, 0L, async);
}

OSErr Executor::hfsPBHSetFInfo(HParmBlkPtr pb, Boolean async)
{
    return PBFInfoHelper(SetOp, (FileParam *)pb, pb->fileParam.ioDirID, async);
}

OSErr Executor::hfsPBSetFLock(ParmBlkPtr pb, Boolean async)
{
    return PBFInfoHelper(LockOp, (FileParam *)pb, 0L, async);
}

OSErr Executor::hfsPBHSetFLock(HParmBlkPtr pb, Boolean async)
{
    return PBFInfoHelper(LockOp, (FileParam *)pb, pb->fileParam.ioDirID, async);
}

OSErr Executor::hfsPBRstFLock(ParmBlkPtr pb, Boolean async)
{
    return PBFInfoHelper(UnlockOp, (FileParam *)pb, 0L, async);
}

OSErr Executor::hfsPBHRstFLock(HParmBlkPtr pb, Boolean async)
{
    return PBFInfoHelper(UnlockOp, (FileParam *)pb,
                         pb->fileParam.ioDirID, async);
}

OSErr Executor::hfsPBSetFVers(ParmBlkPtr pb, Boolean async)
{
    PBRETURN((IOParam *)pb, wrgVolTypErr);
}

void
ROMlib_fcbrename(HVCB *vcbp, GUEST<LONGINT> swapped_parid, StringPtr oldnamep,
                 StringPtr newnamep)
{
    short length;
    filecontrolblock *fcbp, *efcbp;
    GUEST<HVCB *> swapped_vcbp;

    swapped_vcbp = vcbp;
    length = *(GUEST<INTEGER> *)LM(FCBSPtr);
    fcbp = (filecontrolblock *)((GUEST<INTEGER> *)LM(FCBSPtr) + 1);
    efcbp = (filecontrolblock *)((char *)LM(FCBSPtr) + length);
    for(; fcbp < efcbp;
        fcbp = (filecontrolblock *)((char *)fcbp + LM(FSFCBLen)))
    {
        if(fcbp->fcbDirID == swapped_parid
           && fcbp->fcbVPtr == swapped_vcbp
           && RelString(fcbp->fcbCName, oldnamep, false, true) == 0)
            str255assign(fcbp->fcbCName, newnamep);
    }
}

static OSErr
renamehelper(IOParam *pb, Boolean async, LONGINT dirid, filekind kind)
{
    OSErr err, err1;
    btparam btparamrec, btparamrec2;
    IOParam npb;

    err = ROMlib_findvcbandfile(pb, dirid, &btparamrec, &kind, false);
    if(err == noErr)
    {
        npb = *pb;
        npb.ioNamePtr = guest_cast<StringPtr>(pb->ioMisc);
        err = ROMlib_findvcbandfile(&npb, dirid, &btparamrec2, &kind, false);
        if(err != fnfErr)
        {
            if(err != bdNamErr)
                err = dupFNErr;
        }
        else
        {
            err = ROMlib_writevcbp(btparamrec.vcbp);
            if(err == noErr)
            {
                err = ROMlib_btrename(&btparamrec,
                                      guest_cast<StringPtr>(pb->ioMisc));
                if(err == noErr)
                    ROMlib_fcbrename(btparamrec.vcbp,
                                     btparamrec.tofind.catk.ckrParID,
                                     (StringPtr)&btparamrec.tofind.catk.ckrCName[0],
                                     guest_cast<StringPtr>(pb->ioMisc));
            }
            err1 = ROMlib_cleancache(btparamrec.vcbp);
            if(err1 == noErr)
                err1 = ROMlib_flushvcbp(btparamrec.vcbp);
            if(err == noErr)
                err = err1;
        }
    }
    /*
 * This first test of !pb->ioNamePtr makes me nervous.  Perhaps we should
 * use thread information to locate the directory "dirid" instead of assuming
 * a volumerename is needed.  TODO -- FIXME -- test on Mac.
 */

    if(err == noErr)
    {
        StringPtr nameptr;

        nameptr = pb->ioNamePtr;
        if(!pb->ioNamePtr
           || (ROMlib_indexn((char *)nameptr + 1, ':', nameptr[0])
               == (char *)nameptr + nameptr[0]))
        {
            err = ROMlib_pbvolrename(pb, guest_cast<StringPtr>(pb->ioMisc));
            dirid = 1;
        }
    }
    PBRETURN(pb, err);
}

OSErr Executor::hfsPBRename(ParmBlkPtr pb, Boolean async)
{
    return renamehelper((IOParam *)pb, async, 0L, regular);
}

OSErr Executor::hfsPBHRename(HParmBlkPtr pb, Boolean async)
{
    return renamehelper((IOParam *)pb, async, pb->fileParam.ioDirID,
                        (filekind)(regular | directory));
}
