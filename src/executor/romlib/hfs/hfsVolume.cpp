/* Copyright 1992 - 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <OSUtil.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <hfs/hfs.h>
#include <file/file.h>
#include <hfs/hfs_plus.h>


using namespace Executor;

/*
 * TODO: support read and write count.  This will make it possible to
 *	 discriminate between identically named disks (i.e. when you
 *	 have two Untitled disks that have been ejected).  Currently
 *	 disks are matched by name and name only.
 */

/*
 * NOTE: in the routines below there is no freeing of memory if an error is
 *       detected.  This should be done sometime.
 */

static OSErr readvolumebitmap(HVCB *vcbp, volumeinfoPtr vp)
{
    OSErr err;
    short nphysrequired;

    if(vp->drSigWord != 0x4244)
    {
        GUEST<uint32_t> *words;

        err = noMacDskErr;

        words = (GUEST<uint32_t> *)vp;
        warning_fs_log("sigword = 0x%02x (%08x %08x %08x %08x)",
                       toHost(vp->drSigWord), (unsigned)words[0], (unsigned)words[1],
                       (unsigned)words[2], (unsigned)words[3]);
    }
    else
    {
        nphysrequired = NPHYSREQ(ROUNDUP8(vp->drNmAlBlks) / 8);
        vcbp->vcbMAdr = NewPtr(PHYSBSIZE * nphysrequired + MADROFFSET);
        vcbp->vcbMLen = nphysrequired + MADROFFSET;
        /*really add MADROFFSET?*/
        if(!vcbp->vcbMAdr)
            err = MemError();
        else
            err = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs,
                                      vp->drVBMSt * (ULONGINT)PHYSBSIZE,
                                      nphysrequired,
                                      vcbp->vcbMAdr + MADROFFSET, reading,
                                      nullptr);
    }
    return err;
}

static OSErr initcache(HVCB *vcbp)
{
    GUEST<THz> savezone;
    cachehead *headp;
    cacheentry *cachep;

    savezone = LM(TheZone);
    LM(TheZone) = LM(SysZone);
    vcbp->vcbCtlBuf = NewPtr(sizeof(cachehead) + NCACHEENTRIES * sizeof(cacheentry));
    if(!vcbp->vcbCtlBuf)
        return MemError();
    LM(TheZone) = savezone;
    headp = (cachehead *)vcbp->vcbCtlBuf;
    headp->nitems = NCACHEENTRIES;
    headp->flags = 0;
    headp->flink = (cacheentry *)(headp + 1);
    headp->blink = headp->flink + NCACHEENTRIES - 1;

    for(cachep = headp->flink; cachep <= headp->blink; cachep++)
    {
        cachep->flink = cachep + 1;
        cachep->blink = cachep - 1;
        cachep->vptr = 0;
        cachep->fileno = 0;
        cachep->flags = CACHEFREE;
    }
    headp->flink->blink = (cacheentry *)headp;
    headp->blink->flink = (cacheentry *)headp;

    return noErr;
}

static bool
is_hfs_plus_wrapper(volumeinfoPtr vp)
{
    bool retval;

    retval = vp->drVCSize == 0x482b;
    return retval;
}

static OSErr
check_volume_size(volumeinfoPtr vp)
{
    OSErr retval;

    if(is_hfs_plus_wrapper(vp))
    {
        warning_unexpected("Found wrapped HFS+ volume");
        retval = noErr;
    }
    else
    {
        unsigned short nmalblks;
        unsigned long dralblksiz;

        nmalblks = vp->drNmAlBlks;
        dralblksiz = vp->drAlBlkSiz;
        retval = ((long long)nmalblks * dralblksiz >= 2LL * 1024 * 1024 * 1024
                      ? paramErr
                      : noErr);
        if(retval != noErr)
            warning_unexpected("drNmAlBlks = %d, drAlBlkSiz = %lu",
                               (int)nmalblks, dralblksiz);
    }
    return retval;
}

static OSErr readvolumeinfo(HVCB *vcbp) /* call once during mounting */
{
    OSErr err;

    vcbp->vcbBufAdr = NewPtr((Size)PHYSBSIZE);
    if(!vcbp)
        err = MemError();
    else
    {
        err = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs,
                                  (ULONGINT)VOLUMEINFOBLOCKNO * PHYSBSIZE,
                                  1, vcbp->vcbBufAdr, reading,
                                  nullptr);
        if(err == noErr)
            err = check_volume_size((volumeinfoPtr)vcbp->vcbBufAdr);
        if(err == noErr)
        {
            err = readvolumebitmap(vcbp, (volumeinfoPtr)vcbp->vcbBufAdr);
            if(err == noErr)
                err = initcache(vcbp);
        }
    }
    return err;
}

#define VOLUMEINFOBACKUP(vcbp) \
    ((vcbp->vcbNmAlBlks * vcbp->vcbAlBlkSiz) + (vcbp->vcbAlBlSt * PHYSBSIZE))

void Executor::vcbsync(HVCB *vcbp)
{
#ifndef _WIN32
    fsync(((VCBExtra *)vcbp)->u.hfs.fd);
#endif
}

static OSErr writevolumeinfo(HVCB *vcbp, Ptr p)
{
    OSErr err;

    err = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs,
                              (ULONGINT)VOLUMEINFOBLOCKNO * PHYSBSIZE,
                              1, p, writing, nullptr);
    if(err == noErr)
        err = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs,
                                  (ULONGINT)VOLUMEINFOBACKUP(vcbp), 1, p,
                                  writing, nullptr);
    vcbsync(vcbp);
    return err;
}

OSErr Executor::ROMlib_flushvcbp(HVCB *vcbp)
{
    Ptr realp, p;
    OSErr retval;
    volumeinfoPtr vip;
    filecontrolblock *fcbp;

    retval = ROMlib_flushcachevcbp(vcbp);
    if(retval == noErr)
    {
        if(vcbp->vcbFlags & VCBDIRTY)
        {
            realp = (Ptr)alloca((Size)512 + 4); /* needs to be aligned on unix */

            p = (Ptr)(((uintptr_t)realp + 3) & ~3L);
            vip = (volumeinfoPtr)p;
            memmove(&vip->drSigWord, &vcbp->vcbSigWord, (LONGINT)64);
            memmove(&vip->drVolBkUp, &vcbp->vcbVolBkUp, (LONGINT)66);
            fcbp = (filecontrolblock *)((char *)LM(FCBSPtr)
                                        + vcbp->vcbXTRef);
            vip->drXTFlSize = fcbp->fcbPLen;
            memmove(&vip->drXTExtRec, &fcbp->fcbExtRec,
                    (LONGINT)sizeof(fcbp->fcbExtRec));
            fcbp = (filecontrolblock *)((char *)LM(FCBSPtr)
                                        + vcbp->vcbCTRef);
            vip->drCTFlSize = fcbp->fcbPLen;
            memmove(&vip->drCTExtRec, &fcbp->fcbExtRec,
                    (LONGINT)sizeof(fcbp->fcbExtRec));
            retval = writevolumeinfo(vcbp, p);
            vcbp->vcbFlags &= ~VCBDIRTY;
        }
    }
    return retval;
}

static HVCB *vcbbyname(StringPtr name)
{
    HVCB *vcbp;

    for(vcbp = (HVCB *)LM(VCBQHdr).qHead; vcbp && !EqualString(vcbp->vcbVN, name, false, true);
        vcbp = (HVCB *)vcbp->qLink)
        ;
    return vcbp;
}

HVCB *Executor::ROMlib_vcbbydrive(short vrefnum)
{
    HVCB *vcbp;

    for(vcbp = (HVCB *)LM(VCBQHdr).qHead;
        vcbp && vcbp->vcbDrvNum != vrefnum;
        vcbp = (HVCB *)vcbp->qLink)
        ;
    return vcbp;
}

DrvQExtra *
Executor::ROMlib_dqbydrive(short vrefnum)
{
    DrvQEl *dp;
    DrvQExtra *retval;
    GUEST<INTEGER> swapped_vrefnum;

    swapped_vrefnum = vrefnum;
    retval = 0;
    for(dp = (DrvQEl *)LM(DrvQHdr).qHead;
        dp && (retval = (DrvQExtra *)((char *)dp - sizeof(LONGINT)),
              retval->dq.dQDrive != swapped_vrefnum);
        dp = (DrvQEl *)dp->qLink)
        ;
    return dp ? retval : 0;
}

HVCB *Executor::ROMlib_vcbbyvrn(short vrefnum)
{
    HVCB *vcbp;

    for(vcbp = (HVCB *)LM(VCBQHdr).qHead;
        vcbp && vcbp->vcbVRefNum != vrefnum;
        vcbp = (HVCB *)vcbp->qLink)
        ;
    return vcbp;
}

HVCB *Executor::ROMlib_findvcb(short vrefnum, StringPtr name, LONGINT *diridp,
                                      Boolean usedefault)
{
    HVCB *vcbp;
    INTEGER namelen;
    Str255 tempname;
    char *colonp;
    wdentry *wdp;

    namelen = name ? name[0] : 0;
    vcbp = 0;
    if(namelen && name[1] != ':' && (colonp = ROMlib_indexn((char *)name + 2, ':', namelen - 1)))
    {
        tempname[0] = colonp - (char *)name - 1;
        memmove((char *)tempname + 1, (char *)name + 1, (LONGINT)tempname[0]);
        vcbp = vcbbyname(tempname);
        if(vcbp && diridp)
            *diridp = 1;
    }
    else
    {
        if(vrefnum > 0)
            vcbp = ROMlib_vcbbydrive(vrefnum);
        else if(vrefnum < 0)
        {
            if(ISWDNUM(vrefnum))
            {
                wdp = WDNUMTOWDP(vrefnum);
                vcbp = wdp->vcbp;
                if(diridp)
                    *diridp = wdp->dirid;
            }
            else
                vcbp = ROMlib_vcbbyvrn(vrefnum);
        }
        else if(usedefault || (!name && !vrefnum))
        {
            vcbp = (HVCB *)LM(DefVCBPtr);
            if(diridp)
                *diridp = DefDirID;
        }
    }
    return vcbp;
}

static INTEGER drvtodref(INTEGER vref) /* TODO:  flesh this out */
{
#if 0
  switch (vref) {
	case 1:
	case 2:
	  return -5;
	case 3:
	case 4:
	  return -2;
	default:
	  return 0;
  }
#else
    return OURHFSDREF;
#endif
}

static INTEGER openxtnt(LONGINT filnum, LONGINT clpsize, LONGINT filsize,
                        xtntrec xtr, HVCB *vcbp)
{
    filecontrolblock *fcbp;
    INTEGER retval;

    fcbp = ROMlib_getfreefcbp();
    if(fcbp)
    {
        fcbp->fcbFlNum = filnum;
        fcbp->fcbMdRByt = 0;
        fcbp->fcbTypByt = 0;
        fcbp->fcbSBlk = 0;
        fcbp->fcbEOF = filsize;
        fcbp->fcbPLen = filsize;
        fcbp->fcbCrPs = 0;
        fcbp->fcbVPtr = vcbp;
        fcbp->fcbBfAdr = 0;
        fcbp->fcbFlPos = 0;
        fcbp->fcbClmpSize = clpsize;
        fcbp->fcbBTCBPtr = 0;
        memmove(fcbp->fcbExtRec, xtr, (LONGINT)sizeof(xtntrec));
        fcbp->fcbFType = 0;
        fcbp->fcbCatPos = 0;
        fcbp->fcbDirID = 0;
        fcbp->fcbCName[0] = 0;
        retval = (char *)fcbp - (char *)LM(FCBSPtr);
    }
    else
        retval = 0;
    return retval;
}

#define XTNUM 3
#define CTNUM 4

INTEGER Executor::ROMlib_nextvrn = 0; /* TODO: low memory global */

OSErr
Executor::hfsPBMountVol(ParmBlkPtr pb, LONGINT floppyfd, LONGINT offset, LONGINT bsize,
                        LONGINT maxbytes, drive_flags_t flags, DrvQExtra *dqp)
{
    HVCB *vcbp, *vcbp2;
    OSErr err;
    volumeinfoPtr vip;
    Boolean alreadythere;
    ULONGINT nblocks;
    GUEST<THz> saveZone;

    warning_fs_log("floppyfd = 0x%x, offset = %d, bsize = %d, maxbytes = %d "
                   "flags = 0x%x",
                   floppyfd, offset, bsize, maxbytes, flags);
    saveZone = LM(TheZone);
    LM(TheZone) = LM(SysZone);
    vcbp = ROMlib_vcbbydrive(pb->volumeParam.ioVRefNum);
    if(vcbp)
        err = volOnLinErr;
    else
    {
        vcbp = (HVCB *)NewPtr((Size)sizeof(VCBExtra));
        memset(vcbp, 0, sizeof(VCBExtra));
        ((VCBExtra *)vcbp)->u.hfs.fd = floppyfd;
        ((VCBExtra *)vcbp)->u.hfs.offset = offset;
        ((VCBExtra *)vcbp)->u.hfs.bsize = bsize;
        ((VCBExtra *)vcbp)->u.hfs.maxbytes = maxbytes;
        if(!vcbp)
            err = MemError();
        else
        {
            err = readvolumeinfo(vcbp);
            if(err == noErr)
            {
                vip = (volumeinfoPtr)vcbp->vcbBufAdr;
                alreadythere = false;
                for(vcbp2 = (HVCB *)LM(VCBQHdr).qHead; vcbp2;
                    vcbp2 = (HVCB *)vcbp2->qLink)
                    if(EqualString(vcbp2->vcbVN, vip->drVN, true, true)
                       && vcbp2->vcbDrvNum == 0)
                    {
#if 1
                        vcbp2->vcbBufAdr = vcbp->vcbBufAdr;
                        vcbp2->vcbMAdr = vcbp->vcbMAdr;
                        vcbp2->vcbCtlBuf = vcbp->vcbCtlBuf;
#endif
                        ((VCBExtra *)vcbp2)->u.hfs.fd = ((VCBExtra *)vcbp)->u.hfs.fd;
                        DisposePtr((Ptr)vcbp);
                        alreadythere = true;
                        vcbp = vcbp2;
                        break;
                    }
                memmove(&vcbp->vcbSigWord, &vip->drSigWord, (LONGINT)64);

                nblocks = (vcbp->vcbAlBlkSiz / PHYSBSIZE) * vcbp->vcbNmAlBlks + vcbp->vcbAlBlSt + 2;
                dqp->dq.dQDrvSz = nblocks;
                dqp->dq.dQDrvSz2 = nblocks >> 16;
                dqp->dq.qType = 1;

                vcbp->vcbDrvNum = pb->volumeParam.ioVRefNum;
                vcbp->vcbDRefNum = drvtodref(pb->volumeParam.ioVRefNum);
                vcbp->vcbFSID = 0;
                if(!alreadythere)
                    vcbp->vcbVRefNum = --ROMlib_nextvrn;
                vcbp->vcbDirIndex = 0;
                vcbp->vcbDirBlk = 0;
                vcbp->vcbFlags = 0;
                memmove(&vcbp->vcbVolBkUp, &vip->drVolBkUp, (LONGINT)66);

                vcbp->vcbXTAlBlks = vip->drXTFlSize / vip->drAlBlkSiz;
                vcbp->vcbCTAlBlks = vip->drCTFlSize / vip->drAlBlkSiz;

                vcbp->vcbXTRef = openxtnt(XTNUM, vip->drXTClpSiz,
                                             vip->drXTFlSize, vip->drXTExtRec, vcbp);
                vcbp->vcbCTRef = openxtnt(CTNUM, vip->drCTClpSiz,
                                             vip->drCTFlSize, vip->drCTExtRec, vcbp);

                vcbp->vcbDirIDM = 0;
                vcbp->vcbOffsM = 0;
                vcbp->vcbAtrb = 0;
                if(flags & DRIVE_FLAGS_FIXED)
                    vcbp->vcbAtrb |= VNONEJECTABLEBIT;

                if(!vcbp->vcbCTRef)
                    err = tmfoErr;
                if(err == noErr)
                {
                    if(!(flags & DRIVE_FLAGS_LOCKED))
                    {
                        OSErr err2;
                        char buffer[PHYSBSIZE + 3];
                        Ptr buf;

                        buf = (Ptr)(((uintptr_t)buffer + 3) & ~3L);
                        err2 = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs,
                                                   (ULONGINT)VOLUMEINFOBLOCKNO
                                                       * PHYSBSIZE,
                                                   1, buf, reading,
                                                   nullptr);
                        if(err2 == noErr)
                        {
                            err2 = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs,
                                                       (ULONGINT)VOLUMEINFOBLOCKNO
                                                           * PHYSBSIZE,
                                                       1, buf,
                                                       writing, nullptr);
                            if(err2 == noErr)
                                err2 = ROMlib_flushvcbp(vcbp);
                        }
                        if(err2 != noErr)
                            flags |= DRIVE_FLAGS_LOCKED;
                    }
                    if(flags & DRIVE_FLAGS_LOCKED)
                        vcbp->vcbAtrb |= VHARDLOCKBIT;
                    if(!alreadythere)
                        Enqueue((QElemPtr)vcbp, &LM(VCBQHdr));
                    pb->volumeParam.ioVRefNum = vcbp->vcbVRefNum;
                    if(!LM(DefVCBPtr))
                    {
                        LM(DefVCBPtr) = vcbp;
                        LM(DefVRefNum) = vcbp->vcbVRefNum;
                        DefDirID = 2;
                    }
                }
            }
        }
    }
    LM(TheZone) = saveZone;
    warning_fs_log("err = %d", err);
    PBRETURN((VolumeParam *)pb, err);
}

static void goofyclip(GUEST<uint16_t> *up)
{
    if(*up > 0x7C00) /* IMIV-130 */
        *up = 0x7C00;
}

/*
 * getworkingdir returns the directory id associated with vrefnum
 */

static LONGINT getworkingdir(INTEGER vrefnum)
{
    LONGINT retval;
    wdentry *wdp;

    if(ISWDNUM(vrefnum))
    {
        wdp = WDNUMTOWDP(vrefnum);
        retval = wdp->dirid;
    }
    else
        retval = 0;
    return retval;
}

/*
 * getnmfls finds a directory's valence
 */

static unsigned short getnmfls(HVCB *vcbp, INTEGER workingdirnum)
{
    LONGINT dirid;
    catkey key;
    threadrec *thp;
    unsigned short retval;
    btparam btparamrec;
    OSErr err;

    dirid = getworkingdir(workingdirnum);
    err = ROMlib_makecatparam(&btparamrec, vcbp, dirid, 0, (Ptr)0);
    if(err == noErr)
        err = ROMlib_keyfind(&btparamrec);
    if(err == noErr && btparamrec.success)
    {
        thp = (threadrec *)DATAPFROMKEY(btparamrec.foundp);
        key.ckrParID = thp->thdParID;
        str255assign(key.ckrCName, thp->thdCName);
        key.ckrKeyLen = sizeof(LONGINT) + 2 + key.ckrCName[0];
        err = ROMlib_keyfind(&btparamrec);
        // FIXME: #warning autc04: This does not seem right. Added .raw() here to preserve original executor behavior.
        // FIXME: #warning waitwat? "key" is never used again
        if(err == noErr && btparamrec.success)
            retval = ((directoryrec *)DATAPFROMKEY(btparamrec.foundp))->dirVal.raw();
        else
            retval = 0;
    }
    else
        retval = 0;
    return retval;
}

typedef enum { mfs,
               hfs } fstype;

static OSErr commonGetVInfo(HVolumeParam *pb, Boolean async, fstype fs)
{
    HVCB *vcbp;
    INTEGER workingdirnum;

    if(pb->ioVolIndex > 0)
    {
        vcbp = (HVCB *)ROMlib_indexqueue(&LM(VCBQHdr), pb->ioVolIndex);
        workingdirnum = 0;
    }
    else
    {
        if(pb->ioVolIndex == 0)
            vcbp = (HVCB *)ROMlib_findvcb(pb->ioVRefNum, (StringPtr)0,
                                          (LONGINT *)0, false);
        else /* if (pb->ioVolIndex < 0) */
            vcbp = (HVCB *)ROMlib_findvcb(pb->ioVRefNum, pb->ioNamePtr,
                                          (LONGINT *)0, true);
        workingdirnum = getworkingdir(pb->ioVRefNum);
    }

    if(!vcbp)
        /*-->*/ PBRETURN(pb, nsvErr);

    if(/*pb->ioVolIndex >= 0 &&*/ pb->ioNamePtr)
        str255assign(pb->ioNamePtr, (StringPtr)vcbp->vcbVN);
    pb->ioVCrDate = vcbp->vcbCrDate;
    pb->ioVAtrb = vcbp->vcbAtrb;

    if(workingdirnum)
        pb->ioVNmFls = getnmfls(vcbp, workingdirnum);
    else
        pb->ioVNmFls = vcbp->vcbNmFls;

    pb->ioVNmAlBlks = vcbp->vcbNmAlBlks;
    pb->ioVAlBlkSiz = vcbp->vcbAlBlkSiz;
    pb->ioVClpSiz = vcbp->vcbClpSiz;
    pb->ioAlBlSt = vcbp->vcbAlBlSt;
    pb->ioVNxtCNID = vcbp->vcbNxtCNID;
    pb->ioVFrBlk = vcbp->vcbFreeBks;
    switch(fs)
    {
        case mfs:
            ((VolumeParam *)pb)->ioVLsBkUp = vcbp->vcbVolBkUp;
            ((VolumeParam *)pb)->ioVDirSt = 0;
            ((VolumeParam *)pb)->ioVBlLn = 0;
            if(!workingdirnum)
                pb->ioVRefNum = vcbp->vcbVRefNum;
            goofyclip((GUEST<uint16_t> *)&pb->ioVNmAlBlks);
            goofyclip((GUEST<uint16_t> *)&pb->ioVFrBlk);
            break;
        case hfs:
            pb->ioVLsMod = vcbp->vcbLsMod;
            pb->ioVBitMap = vcbp->vcbVBMSt;
#if !defined(THINKCMESSED)
            pb->ioVAllocPtr = vcbp->vcbAllocPtr;
#else /* THINKCMESSED */
            pb->ioAllocPtr = vcbp->vcbAllocPtr;
#endif /* THINKCMESSED */
            pb->ioVRefNum = vcbp->vcbVRefNum;
            pb->ioVSigWord = vcbp->vcbSigWord;
            pb->ioVDrvInfo = vcbp->vcbDrvNum;
            pb->ioVDRefNum = vcbp->vcbDRefNum;
            pb->ioVFSID = vcbp->vcbFSID;
            pb->ioVBkUp = vcbp->vcbVolBkUp;
            pb->ioVSeqNum = vcbp->vcbVSeqNum;
            pb->ioVWrCnt = vcbp->vcbWrCnt;
            pb->ioVFilCnt = vcbp->vcbFilCnt;
            pb->ioVDirCnt = vcbp->vcbDirCnt;
            memmove(pb->ioVFndrInfo, vcbp->vcbFndrInfo,
                    (LONGINT)sizeof(pb->ioVFndrInfo));
            break;
    }
    PBRETURN(pb, noErr);
}

OSErr Executor::hfsPBGetVInfo(ParmBlkPtr pb, Boolean async)
{
    return commonGetVInfo((HVolumeParam *)pb, async, mfs);
}

OSErr Executor::hfsPBHGetVInfo(HParmBlkPtr pb, Boolean async)
{
    return commonGetVInfo((HVolumeParam *)pb, async, hfs);
}

#define ATRBMASK VSOFTLOCKBIT

OSErr Executor::hfsPBSetVInfo(HParmBlkPtr pb, Boolean async)
{
    OSErr err;
    HVCB *vcbp;

    vcbp = ROMlib_findvcb(pb->volumeParam.ioVRefNum,
                          pb->volumeParam.ioNamePtr, (LONGINT *)0, false);
    if(vcbp)
    {
        if(vcbp->vcbAtrb & VHARDLOCKBIT)
            err = wPrErr;
        else
        {
            if(pb->volumeParam.ioNamePtr)
                str255assign((StringPtr)vcbp->vcbVN,
                             pb->volumeParam.ioNamePtr);
            vcbp->vcbCrDate = pb->volumeParam.ioVCrDate;
            vcbp->vcbLsMod = pb->volumeParam.ioVLsMod;
            vcbp->vcbAtrb = (vcbp->vcbAtrb & ~ATRBMASK) | (pb->volumeParam.ioVAtrb & ATRBMASK);
            vcbp->vcbClpSiz = pb->volumeParam.ioVClpSiz;
            vcbp->vcbVolBkUp = pb->volumeParam.ioVBkUp;
            vcbp->vcbVSeqNum = pb->volumeParam.ioVSeqNum;
            memmove(vcbp->vcbFndrInfo, pb->volumeParam.ioVFndrInfo,
                    (LONGINT)32);
            vcbp->vcbFlags |= VCBDIRTY;
            err = noErr;
        }
    }
    else
        err = nsvErr;
    PBRETURN((VolumeParam *)pb, err);
}

static OSErr getvolcommon(VolumeParam *pb)
{
    OSErr err;

    if(!LM(DefVCBPtr))
        err = nsvErr;
    else
    {
        err = noErr;
        if(pb->ioNamePtr)
            str255assign(pb->ioNamePtr, (StringPtr)LM(DefVCBPtr)->vcbVN);
        pb->ioVRefNum = LM(DefVRefNum);
    }
    return err;
}

OSErr Executor::hfsPBGetVol(ParmBlkPtr pb, Boolean async)
{
    OSErr err;

    err = getvolcommon((VolumeParam *)pb);
    PBRETURN((VolumeParam *)pb, err);
}

GUEST<LONGINT> Executor::DefDirID = 2;

OSErr Executor::hfsPBHGetVol(WDPBPtr pb, Boolean async)
{
    wdentry *wdp;
    OSErr err;

    err = getvolcommon((VolumeParam *)pb);
    pb->ioWDDirID = DefDirID;
    if(err == noErr)
    {
        if(ISWDNUM(LM(DefVRefNum)))
        {
            wdp = WDNUMTOWDP(LM(DefVRefNum));
            pb->ioWDProcID = wdp->procid;
            pb->ioWDVRefNum = wdp->vcbp->vcbVRefNum;
        }
        else
        {
            pb->ioWDProcID = 0;
            pb->ioWDVRefNum = LM(DefVRefNum);
        }
    }
    PBRETURN(pb, err);
}

/*
 * NOTE: Considerable change related to PBSetVol, PBHSetVol were made
 *	 just now (Sat Aug  1 16:13:35 MDT 1992).  These routines
 *	 have been giving us trouble for some time.  Tech Note 140
 *	 implies that there is a "DefDirID" buried somewhere in low
 *	 global space.  Sometime we should try to ferret it out.
 *
 */

static OSErr setvolhelper(VolumeParam *pb, Boolean aysnc, LONGINT dirid,
                          Boolean convertzeros)
{
    HVCB *vcbp;
    GUEST<HVCB *> newDefVCBPtr;
    OSErr err, err1;
    LONGINT newdir;
    GUEST<LONGINT> newDefDirID;
    GUEST<INTEGER> newDefVRefNum;
    CInfoPBRec cpb;

    /*
 * CinemationCD hack ... they store a directory as a 2-byte quantity and
 *	sign extend it.  This will only help us recover the sign bit.
 */
    if(dirid < 0)
        dirid = 64 * 1024 + dirid;

    newdir = 0;
    vcbp = ROMlib_findvcb(pb->ioVRefNum, pb->ioNamePtr,
                          &newdir, false);
    if(!vcbp)
        err = nsvErr;
    else
    {
        err = noErr;
        newDefVCBPtr = vcbp;
        newDefDirID = 0;
        if(newdir > 2)
        { /* picked up working directory */
            newDefDirID = dirid ? dirid : newdir;
            newDefVRefNum = pb->ioVRefNum;
        }
        else if(newdir == 1)
        { /* picked up by name */
            newDefDirID = newdir;
            newDefVRefNum = vcbp->vcbVRefNum;
        }
        else
        {
            newDefVRefNum = pb->ioVRefNum;
            if(dirid == 0 && convertzeros)
                newDefDirID = 2;
            else
                newDefDirID = dirid;
        }

        if(!convertzeros && pb->ioNamePtr)
        { /* this could change things */
            if(pb->ioNamePtr[0] == 0)
                cpb.hFileInfo.ioNamePtr = 0; /* otherwise we fill in */
            else
                cpb.hFileInfo.ioNamePtr = pb->ioNamePtr;
            cpb.hFileInfo.ioVRefNum = pb->ioVRefNum;
            cpb.hFileInfo.ioFDirIndex = 0;
            cpb.hFileInfo.ioDirID = dirid;
            /*
 * NOTE: the else case was added after seeing Excel 4 Installer do a setvol
 *	 to a file, presumably with the intent to set it to the parent id.
 *
 *	 Also we make this try twice as a PM PMSP
 */
            do
            {
                if((err1 = PBGetCatInfo(&cpb, false)) == noErr)
                {
                    if(cpb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
                        newDefDirID = cpb.dirInfo.ioDrDirID;
                    else
                        newDefDirID = cpb.hFileInfo.ioFlParID;
                }
            } while(err1 && cpb.hFileInfo.ioDirID == 0 && (cpb.hFileInfo.ioDirID = 2));
        }
        if(newDefDirID)
            DefDirID = newDefDirID;
        else if(LM(DefVCBPtr) != newDefVCBPtr || LM(DefVRefNum) != newDefVRefNum)
            DefDirID = 2;
        LM(DefVCBPtr) = newDefVCBPtr;
        LM(DefVRefNum) = newDefVRefNum;
    }
    PBRETURN(pb, err);
}

OSErr Executor::hfsPBSetVol(ParmBlkPtr pb, Boolean async)
{
    return setvolhelper((VolumeParam *)pb, async, 0, true);
}

OSErr Executor::hfsPBHSetVol(WDPBPtr pb, Boolean async)
{
    return setvolhelper((VolumeParam *)pb, async, pb->ioWDDirID, false);
}

OSErr Executor::hfsPBFlushVol(ParmBlkPtr pb, Boolean async)
{
    VCB *vcbp;
    OSErr err;

    vcbp = ROMlib_findvcb(pb->volumeParam.ioVRefNum,
                          pb->volumeParam.ioNamePtr, (LONGINT *)0, true);
    if(vcbp)
        err = ROMlib_flushvcbp(vcbp);
    else
        err = nsvErr;
    PBRETURN((VolumeParam *)pb, err);
}

static void closeallvcbfiles(HVCB *vcbp)
{
    filecontrolblock *fcbp, *efcbp;
    IOParam iopb;
    short length;

    length = *(GUEST<INTEGER> *)LM(FCBSPtr);
    fcbp = (filecontrolblock *)((short *)LM(FCBSPtr) + 1);
    efcbp = (filecontrolblock *)((char *)LM(FCBSPtr) + length);
    for(; fcbp < efcbp; fcbp = (filecontrolblock *)((char *)fcbp + LM(FSFCBLen)))
        if(fcbp->fcbFlNum && fcbp->fcbVPtr == vcbp)
        {
            iopb.ioRefNum = (char *)fcbp - (char *)LM(FCBSPtr);
            /* my */ PBFlushFile((ParmBlkPtr)&iopb, false);
        }
}

OSErr Executor::hfsPBUnmountVol(ParmBlkPtr pb)
{
    OSErr err;
    HVCB *vcbp;

    vcbp = ROMlib_findvcb(pb->volumeParam.ioVRefNum,
                          pb->volumeParam.ioNamePtr, (LONGINT *)0, false);
    if(vcbp)
    {
        closeallvcbfiles(vcbp);
        err = ROMlib_flushvcbp(vcbp);
        Dequeue((QElemPtr)vcbp, &LM(VCBQHdr));
        DisposePtr(vcbp->vcbMAdr);
        DisposePtr(vcbp->vcbBufAdr);
        DisposePtr(vcbp->vcbCtlBuf);
        DisposePtr((Ptr)vcbp);
    }
    else
        err = nsvErr;
    PBRETURN((VolumeParam *)pb, err);
}

static OSErr offlinehelper(VolumeParam *pb, HVCB *vcbp)
{
    OSErr err, err1, err2;
    IOParam iop;

    err = /* my */ PBFlushVol((ParmBlkPtr)pb, false);
    err1 = 0;
    err2 = 0;
    if(err == noErr)
    {
        if(vcbp)
        {
            iop.ioRefNum = vcbp->vcbXTRef;
            err1 = PBClose((ParmBlkPtr)&iop, false);
            iop.ioRefNum = vcbp->vcbCTRef;
            err2 = PBClose((ParmBlkPtr)&iop, false);
#if 1
            DisposePtr(vcbp->vcbMAdr);
            vcbp->vcbMAdr = 0;
            DisposePtr(vcbp->vcbBufAdr);
            vcbp->vcbBufAdr = 0;
            DisposePtr(vcbp->vcbCtlBuf);
            vcbp->vcbCtlBuf = 0;
#endif
            vcbp->vcbDrvNum = 0;
            /* TODO:  look for offline flags in mpw equate files and set them */
        }
        else
            err = nsvErr;
    }
#if !defined(MAC)
#if 0
    if (err == noErr)
	err = updatefloppy();
#endif
#endif
    if(err == noErr)
        err = err1;
    if(err == noErr)
        err = err2;
    return err;
}

OSErr Executor::hfsPBOffLine(ParmBlkPtr pb)
{
    OSErr err;
    HVCB *vcbp;

    vcbp = ROMlib_findvcb(pb->volumeParam.ioVRefNum,
                          pb->volumeParam.ioNamePtr, (LONGINT *)0, false);
    if(vcbp)
    {
        if(vcbp->vcbDrvNum)
        {
            vcbp->vcbDRefNum = -vcbp->vcbDrvNum;
            err = offlinehelper((VolumeParam *)pb, vcbp);
        }
        else
            err = noErr;
    }
    else
        err = nsvErr;
    PBRETURN((VolumeParam *)pb, err);
}

OSErr Executor::hfsPBEject(ParmBlkPtr pb)
{
    OSErr err;
    HVCB *vcbp;
    INTEGER vref;

    vref = pb->volumeParam.ioVRefNum;
    vcbp = ROMlib_findvcb(vref,
                          pb->volumeParam.ioNamePtr, (LONGINT *)0, false);
    if(vcbp)
    {
        if(vcbp->vcbDrvNum)
        {
            vcbp->vcbDRefNum = vcbp->vcbDrvNum;
            err = offlinehelper((VolumeParam *)pb, vcbp);
        }
        else
        {
            if(vcbp->vcbDRefNum < 0) /* offline */
                vcbp->vcbDRefNum = vcbp->vcbDRefNum * -1;
            err = noErr;
        }
    }
    else
    {
        if(vref == 1 || vref == 2)
            err = noErr; /* They're explicitly ejecting a particular drive */
        else
            err = nsvErr;
    }
#if !defined(MAC)
    if(err == noErr)
        err = ROMlib_ejectfloppy(vcbp ? ((VCBExtra *)vcbp)->u.hfs.fd : -1);
#endif
    PBRETURN((VolumeParam *)pb, err);
}

OSErr Executor::ROMlib_pbvolrename(IOParam *pb, StringPtr newnamep)
{
    OSErr err;
    HParamBlockRec hpb;
    Str255 name_copy;

    str255assign(name_copy, pb->ioNamePtr);
    hpb.volumeParam.ioNamePtr = name_copy;
    hpb.volumeParam.ioVRefNum = pb->ioVRefNum;
    hpb.volumeParam.ioVolIndex = -1;
    err = /* my */ PBHGetVInfo((HParmBlkPtr)&hpb, false);
    if(err == noErr)
    {
        hpb.volumeParam.ioNamePtr = newnamep;
        err = /* my */ PBSetVInfo((HParmBlkPtr)&hpb, false);
    }
    return err;
}
