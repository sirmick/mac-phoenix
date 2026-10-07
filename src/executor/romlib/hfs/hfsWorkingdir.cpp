/* Copyright 1992 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <OSUtil.h>
#include <FileMgr.h>
#include <hfs/hfs.h>
#include <file/file.h>
#include <MemoryMgr.h>

using namespace Executor;

/*
 * TODO: use this working directory stuff in ROMlib
 */

OSErr Executor::ROMlib_dirbusy(LONGINT dirid, HVCB *vcbp)
{
#if defined(MAC)
    wdentry *wdp, *ewdp;

    for(wdp = (wdentry *)(LM(WDCBsPtr) + sizeof(INTEGER)),
    ewdp = (wdentry *)(LM(WDCBsPtr) + *(INTEGER *)LM(WDCBsPtr));
        wdp != ewdp; wdp++)
        ;
    return wdp == ewdp ? noErr : fBsyErr;
#else
    return noErr;
#endif
}

OSErr Executor::ROMlib_mkwd(WDPBPtr pb, HVCB *vcbp, LONGINT dirid,
                            LONGINT procid)
{
    wdentry *wdp, *ewdp, *firstfreep;
    OSErr retval;
    INTEGER n_wd_bytes, new_n_wd_bytes;
    Ptr newptr;
    GUEST<THz> saveZone;

    firstfreep = 0;
    for(wdp = (wdentry *)(LM(WDCBsPtr) + sizeof(INTEGER)),
    ewdp = (wdentry *)(LM(WDCBsPtr) + *(GUEST<INTEGER> *)LM(WDCBsPtr));
        wdp != ewdp; wdp++)
    {
        if(!firstfreep && !wdp->vcbp)
            firstfreep = wdp;
        if(wdp->vcbp == vcbp && wdp->dirid == dirid && wdp->procid == procid)
        {
            pb->ioVRefNum = WDPTOWDNUM(wdp);
            /*-->*/ return noErr;
        }
    }
    if(!firstfreep)
    {
        n_wd_bytes = *(GUEST<INTEGER> *)LM(WDCBsPtr);
        new_n_wd_bytes = (n_wd_bytes - sizeof(INTEGER)) * 2 + sizeof(INTEGER);
        saveZone = LM(TheZone);
        LM(TheZone) = LM(SysZone);
        newptr = NewPtr(new_n_wd_bytes);
        LM(SysZone) = LM(TheZone);
        if(!newptr)
            retval = tmwdoErr;
        else
        {
            BlockMoveData(LM(WDCBsPtr), newptr, n_wd_bytes);
            DisposePtr(LM(WDCBsPtr));
            LM(WDCBsPtr) = newptr;
            *(GUEST<INTEGER> *)newptr = new_n_wd_bytes;
            firstfreep = (wdentry *)(newptr + n_wd_bytes);
            retval = noErr;
        }
    }
    else
        retval = noErr;
    if(retval == noErr)
    {
        firstfreep->vcbp = vcbp;
        firstfreep->dirid = dirid;
        firstfreep->procid = procid;
        pb->ioVRefNum = WDPTOWDNUM(firstfreep);
        retval = noErr;
    }
    return retval;
}

OSErr Executor::hfsPBOpenWD(WDPBPtr pb, Boolean async)
{
    LONGINT dirid;
    OSErr retval;
    filekind kind;
    btparam btparamrec;
    HVCB *vcbp;
    StringPtr namep;

    kind = (filekind)(regular | directory);
    retval = ROMlib_findvcbandfile((IOParam *)pb, pb->ioWDDirID,
                                   &btparamrec, &kind, false);
    if(retval != noErr)
        PBRETURN(pb, retval);
    vcbp = btparamrec.vcbp;
    retval = ROMlib_cleancache(vcbp);
    if(retval != noErr)
        PBRETURN(pb, retval);
    namep = pb->ioNamePtr;
    if(kind == directory && namep && namep[0])
        dirid = ((directoryrec *)DATAPFROMKEY(btparamrec.foundp))->dirDirID;
    else
        dirid = pb->ioWDDirID;
    retval = ROMlib_mkwd(pb, vcbp, dirid, pb->ioWDProcID);

    PBRETURN(pb, retval);
}

OSErr Executor::hfsPBCloseWD(WDPBPtr pb, Boolean async)
{
    wdentry *wdp;
    OSErr retval;

    retval = noErr;
    if(ISWDNUM(pb->ioVRefNum))
    {
        wdp = WDNUMTOWDP(pb->ioVRefNum);
        if(wdp)
            wdp->vcbp = 0;
        else
            retval = nsvErr;
    }
    PBRETURN(pb, retval);
}

OSErr Executor::hfsPBGetWDInfo(WDPBPtr pb, Boolean async)
{
    OSErr retval;
    wdentry *wdp, *ewdp;
    INTEGER i;
    Boolean foundelsewhere;
    HVCB *vcbp;

    foundelsewhere = false;
    retval = noErr;
    wdp = 0;
    if(pb->ioWDIndex > 0)
    {
        i = pb->ioWDIndex;
        wdp = (wdentry *)(LM(WDCBsPtr) + sizeof(INTEGER));
        ewdp = (wdentry *)(LM(WDCBsPtr) + *(GUEST<INTEGER> *)LM(WDCBsPtr));
        if(pb->ioVRefNum < 0)
        {
            for(; wdp != ewdp; wdp++)
                if(wdp->vcbp && wdp->vcbp->vcbVRefNum == pb->ioVRefNum && --i <= 0)
                    break;
        }
        else if(pb->ioVRefNum == 0)
        {
            for(; wdp != ewdp && i > 1; ++wdp)
                if(wdp->vcbp)
                    --i;
        }
        else /* if (pb->ioVRefNum > 0 */
        {
            for(; wdp != ewdp; wdp++)
                if(wdp->vcbp->vcbDrvNum == pb->ioVRefNum && --i <= 0)
                    break;
        }
        if(wdp == ewdp || !wdp->vcbp)
            wdp = 0;
    }
    else if(ISWDNUM(pb->ioVRefNum))
        wdp = WDNUMTOWDP(pb->ioVRefNum);
    else
    {
        vcbp = ROMlib_findvcb(pb->ioVRefNum, (StringPtr)0, (LONGINT *)0,
                              true);
        if(vcbp)
        {
            if(pb->ioNamePtr)
                str255assign(pb->ioNamePtr, (StringPtr)vcbp->vcbVN);
            pb->ioWDProcID = 0;
            pb->ioVRefNum = pb->ioWDVRefNum = vcbp->vcbVRefNum;
            pb->ioWDDirID = (vcbp == LM(DefVCBPtr)) ? DefDirID : toGuest(2);
            foundelsewhere = true;
        }
    }

    if(!foundelsewhere)
    {
        if(wdp)
        {
            if(pb->ioNamePtr)
                str255assign(pb->ioNamePtr,
                             (StringPtr)wdp->vcbp->vcbVN);
            if(pb->ioWDIndex > 0)
                pb->ioVRefNum = wdp->vcbp->vcbVRefNum;
            pb->ioWDProcID = wdp->procid;
            pb->ioWDVRefNum = wdp->vcbp->vcbVRefNum;
            pb->ioWDDirID = wdp->dirid;
        }
        else
            retval = nsvErr;
    }

    PBRETURN(pb, retval);
}

OSErr
Executor::GetWDInfo(INTEGER wd, GUEST<INTEGER> *vrefp, GUEST<LONGINT> *dirp, GUEST<LONGINT> *procp)
{
    OSErr retval;

    WDPBRec wdp;
    memset(&wdp, 0, sizeof wdp);
    wdp.ioVRefNum = wd;
    retval = PBGetWDInfo(&wdp, false);
    if(retval == noErr)
    {
        *vrefp = wdp.ioVRefNum;
        *dirp = wdp.ioWDDirID;
        *procp = wdp.ioWDProcID;
    }
    return retval;
}

void Executor::ROMlib_adjustdirid(LONGINT *diridp, HVCB *vcbp, INTEGER vrefnum)
{
    wdentry *wdp;

    if(*(ULONGINT *)diridp <= 1 && ISWDNUM(vrefnum))
    {
        wdp = WDNUMTOWDP(vrefnum);
        if(wdp->vcbp == vcbp)
            *diridp = wdp->dirid;
    }
    else if(*diridp == 0 && !vrefnum /* vcbp == LM(DefVCBPtr) */)
        *diridp = DefDirID;
    if(*diridp == 0)
        *diridp = 2;
}
