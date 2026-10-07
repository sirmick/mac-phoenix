/* Copyright 1992 - 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <OSUtil.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <file/file.h>
#include <hfs/hfs.h>
#include <base/cpu.h>

#include <algorithm>

using namespace Executor;

filecontrolblock *Executor::ROMlib_getfreefcbp(void)
{
    short length;
    filecontrolblock *fcbp, *efcbp;

    length = *(GUEST<INTEGER> *)LM(FCBSPtr);
    fcbp = (filecontrolblock *)((short *)LM(FCBSPtr) + 1);
    efcbp = (filecontrolblock *)((char *)LM(FCBSPtr) + length);
    for(; fcbp < efcbp && fcbp->fcbFlNum;
        fcbp = (filecontrolblock *)((char *)fcbp + LM(FSFCBLen)))
        ;
    return fcbp < efcbp ? fcbp : 0;
}

filecontrolblock *Executor::ROMlib_refnumtofcbp(uint16_t refnum)
{
    uint16_t len;
    filecontrolblock *retval;

    if(refnum < sizeof(short) || refnum % LM(FSFCBLen) != sizeof(short))
        return 0;
    len = *(GUEST<uint16_t> *)LM(FCBSPtr);
    if(refnum >= len)
        return 0;
    retval = (filecontrolblock *)((char *)LM(FCBSPtr) + refnum);
    if(retval->fcbFlNum == 0)
        retval = 0;
    return retval;
}

static LONGINT pbabsoffset(IOParam *pb, filecontrolblock *fcbp)
{
    switch(pb->ioPosMode & 0x3)
    {
        case fsAtMark:
            return fcbp->fcbCrPs;
        case fsFromStart:
            return pb->ioPosOffset;
        case fsFromLEOF:
            return fcbp->fcbEOF + pb->ioPosOffset;
        case fsFromMark:
            return fcbp->fcbCrPs + pb->ioPosOffset;
        default:
            return -1;
    }
}

static LONGINT xtntbnotophys(xtntrec xtr, unsigned short bno,
                             unsigned short *nphyscontigp)
{
    unsigned short bc;
    LONGINT retblock;

    bc = xtr[0].blockcount;
    if(bno < bc)
    {
        *nphyscontigp = bc - bno;
        retblock = xtr[0].blockstart + bno;
    }
    else
    {
        bno -= bc;
        bc = xtr[1].blockcount;
        if(bno < bc)
        {
            *nphyscontigp = bc - bno;
            retblock = xtr[1].blockstart + bno;
        }
        else
        {
            bno -= bc;
            bc = xtr[2].blockcount;
            if(bno < bc)
            {
                *nphyscontigp = bc - bno;
                retblock = xtr[2].blockstart + bno;
            }
            else
                retblock = -1;
        }
    }
    return retblock;
}

compretval Executor::ROMlib_xtntcompare(void *firstp, void *secondp)
{
    xtntkey *xp1 = (xtntkey *)firstp;
    xtntkey *xp2 = (xtntkey *)secondp;

    if(xp1->xkrFNum < xp2->xkrFNum)
        return firstisless;
    else if(xp1->xkrFNum > xp2->xkrFNum)
        return firstisgreater;
    else
    {
        if(xp1->xkrFkType < xp2->xkrFkType)
            return firstisless;
        else if(xp1->xkrFkType > xp2->xkrFkType)
            return firstisgreater;
        else
        {
            if(xp1->xkrFABN < xp2->xkrFABN)
                return firstisless;
            else if(xp1->xkrFABN > xp2->xkrFABN)
                return firstisgreater;
            else
                return same;
        }
    }
}

compretval Executor::ROMlib_catcompare(void *firstp, void *secondp)
{
    catkey *ckp1 = (catkey *)firstp;
    catkey *ckp2 = (catkey *)secondp;

    if(ckp1->ckrParID < ckp2->ckrParID)
        return firstisless;
    if(ckp1->ckrParID > ckp2->ckrParID)
        return firstisgreater;
    else
        return (compretval)Executor::RelString((StringPtr)ckp1->ckrCName, (StringPtr)ckp2->ckrCName,
                                               false, true);
}

void Executor::ROMlib_makextntkey(xtntkey *keyp, Forktype forkwanted,
                                  LONGINT flnum, uint16_t bno)
{
    keyp->xkrKeyLen = 7;
    keyp->xkrFkType = forkwanted;
    keyp->xkrFNum = flnum;
    keyp->xkrFABN = bno;
}

void Executor::ROMlib_makextntparam(btparam *btpb, HVCB *vcbp,
                                    Forktype forkwanted, LONGINT flnum,
                                    uint16_t bno)
{
    btpb->vcbp = vcbp;
    ROMlib_makextntkey((xtntkey *)&btpb->tofind, forkwanted, flnum, bno);
    btpb->fp = ROMlib_xtntcompare;
    btpb->refnum = vcbp->vcbXTRef;
    btpb->leafindex = -1;
}

static xtntkey *fcbpbnotoxkeyp(filecontrolblock *fcbp, uint16_t bno)
{
    Forktype forkwanted;
    xtntkey *xkeyp;
    btparam btparamblock;
    OSErr err;

    forkwanted = fcbp->fcbMdRByt & RESOURCEBIT ? resourcefork : datafork;
    ROMlib_makextntparam(&btparamblock, fcbp->fcbVPtr, forkwanted,
                         fcbp->fcbFlNum, bno);
    err = ROMlib_keyfind(&btparamblock);
#if 0
    ROMlib_cleancache(btparamblock.vcbp);
#endif
    xkeyp = (xtntkey *)btparamblock.foundp;
    if(err != noErr || xkeyp->xkrFkType != forkwanted || xkeyp->xkrFNum != fcbp->fcbFlNum)
        return 0;
    return xkeyp;
}

LONGINT Executor::ROMlib_logtophys(filecontrolblock *fcbp, LONGINT absoffset,
                                   LONGINT *nphyscontigp)
{
    uint16_t bno;
    HVCB *vcbp;
    LONGINT alblksiz, retblock;
    xtntkey *xkeyp;
    LONGINT skip;
    LONGINT alinphys;
    unsigned short nphysalcontig;

    vcbp = (HVCB *)fcbp->fcbVPtr;
    alblksiz = vcbp->vcbAlBlkSiz;
    bno = absoffset / alblksiz;
    skip = absoffset % alblksiz;
    retblock = xtntbnotophys(fcbp->fcbExtRec, bno, &nphysalcontig);
    if(retblock == -1)
    {
        xkeyp = fcbpbnotoxkeyp(fcbp, bno);
        if(!xkeyp)
            return -1;
        retblock = xtntbnotophys((xtntdesc *)DATAPFROMKEY(xkeyp),
                                 bno - xkeyp->xkrFABN, &nphysalcontig);
        if(retblock == -1)
            return -1;
    }
    alinphys = alblksiz / PHYSBSIZE;
    *nphyscontigp = alinphys * nphysalcontig - (skip / PHYSBSIZE);
    return vcbp->vcbAlBlSt * (LONGINT)PHYSBSIZE + retblock * alblksiz + skip;
}

static OSErr pbtofcbp(IOParam *pb, filecontrolblock **fcbpp, accesstype rw)
{
    OSErr retval;

    *fcbpp = ROMlib_refnumtofcbp(pb->ioRefNum);
    if(!*fcbpp)
        retval = rfNumErr;
    else
    {
        if(rw == writing)
        {
            retval = ROMlib_writefcbp(*fcbpp);
            if(retval == noErr)
                retval = ROMlib_writevcbp((*fcbpp)->fcbVPtr);
        }
        else
            retval = noErr;
    }
    return retval;
}

static void setbits(HVCB *vcbp, ULONGINT bno, ULONGINT ntoset,
                    unsigned char lookfor)
{
    unsigned char *cp, *ecp;
    ULONGINT ebno;
    INTEGER startbit, stopbit;
    unsigned char mask, want;
    ULONGINT first_map_block, last_map_block;

    if(lookfor)
        vcbp->vcbFreeBks = vcbp->vcbFreeBks - (ntoset);
    else
        vcbp->vcbFreeBks = vcbp->vcbFreeBks + (ntoset);
    vcbp->vcbFlags |= VCBDIRTY;
    /* bno -= vcbp->vcbAlBlSt; not sure about this */
    ebno = bno + ntoset;
    cp = (unsigned char *)vcbp->vcbMAdr + bno / 8 + MADROFFSET;
    ecp = (unsigned char *)vcbp->vcbMAdr + ebno / 8 + MADROFFSET;
    startbit = bno % 8;
    stopbit = ebno % 8;
    if(cp == ecp)
    {
        mask = (0xFF >> startbit) & (0xFF << (8 - stopbit));
        if(lookfor)
            *cp |= mask;
        else
            *cp &= ~mask;
    }
    else
    {
        if(startbit)
        {
            mask = 0xFF >> startbit;
            if(lookfor)
                *cp++ |= mask;
            else
                *cp++ &= ~mask;
        }
        want = lookfor ? 0xFF : 0;
        while(cp != ecp)
            *cp++ = want;
        if(stopbit)
        {
            mask = 0xFF << (8 - stopbit);
            if(lookfor)
                *cp |= mask;
            else
                *cp &= ~mask;
        }
    }
    first_map_block = bno / (PHYSBSIZE * 8);
    last_map_block = (ebno - 1) / (PHYSBSIZE * 8);
    ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs,
                        (vcbp->vcbVBMSt + first_map_block) * (ULONGINT)PHYSBSIZE,
                        last_map_block - first_map_block + 1,
                        vcbp->vcbMAdr + MADROFFSET + first_map_block * PHYSBSIZE,
                        writing, (GUEST<LONGINT> *)0);
    vcbsync(vcbp);
}

static ULONGINT countbits(HVCB *vcbp, ULONGINT bno, unsigned char lookfor)
{
    unsigned char *cp, c;
    unsigned char mask, want;
    ULONGINT retval, max;
    INTEGER bit;

    /* assert(lookfor <= 1); */
    /* bno -= vcbp->vcbAlBlSt; not sure about this */
    retval = 0;
    max = vcbp->vcbNmAlBlks - bno;
    cp = (unsigned char *)vcbp->vcbMAdr + bno / 8 + MADROFFSET;
#if 0
    {
    	Size madrlen;
    	madrlen = GetPtrSize(vcbp->vcbMAdr);
    	madrlen = madrlen;
    }
#endif
    bit = bno % 8;
    if(bit)
    {
        c = *cp++;
        mask = (unsigned char)~0 >> bit;
        want = lookfor ? mask : 0;
        if((c & mask) == want)
        {
            retval += 8 - bit;
        }
        else
        {
            mask = 1 << (7 - bit);
            if(lookfor)
                for(; c & mask; mask >>= 1)
                    retval++;
            else
                for(; !(c & mask); mask >>= 1)
                    retval++;
            return retval;
        }
    }
    want = lookfor ? 0xFF : 0;
    while(retval < max && *cp++ == want)
    {
        retval += 8;
    }
    if(retval < max)
    {
        c = cp[-1];
        mask = 1 << 7;
        if(lookfor)
            for(; c & mask; mask >>= 1)
                retval++;
        else
            for(; !(c & mask); mask >>= 1)
                retval++;
    }
    return std::min(max, retval);
}

static Boolean fillextent(xtntdesc *xp, ULONGINT *nallocneededp, HVCB *vcbp,
                          uint16_t *newabnp)
{
    INTEGER nempty;
    ULONGINT toextend;
    ULONGINT needed, max, nfree, search;
    xtntrec tmpxtnt;
    xtntdesc *tmpxp;
    Boolean retval;

    needed = *nallocneededp;
    max = vcbp->vcbNmAlBlks;
    for(nempty = 3; nempty > 0 && xp->blockcount; nempty--, xp++)
        *newabnp += xp->blockcount;
    if(nempty != 3)
    {
        --xp;
        toextend = countbits(vcbp, xp->blockstart + xp->blockcount, 0);
        if(toextend)
        {
            toextend = std::min(toextend, needed);
            needed -= toextend;
            setbits(vcbp, xp->blockstart + xp->blockcount, toextend, 1);
            xp->blockcount = xp->blockcount + (toextend);
        }
        ++xp;
    }
    if(needed && nempty)
    {
        tmpxtnt[0].blockcount = 0;
        tmpxtnt[1].blockcount = 0;
        tmpxtnt[2].blockcount = 0;
        nfree = 0;
        /*
 * NOTE: the condition in the for loop might look strange.  The point is it
 *       isn't worth counting the number of free blocks from a given point if
 *       even in the best case it couldn't be more than the number you already
 *       know about.
 */
        for(search = countbits(vcbp, 0, 1) /*vcbp->vcbAlBlSt*/; max - search > tmpxtnt[2].blockcount;)
        {
            nfree = countbits(vcbp, search, 0);
#if 0
	    if (nfree == 0)
	        warning_unexpected ("nfree == 0");
#endif
            if(nfree >= needed)
            {
                tmpxtnt[0].blockcount = nfree;
                tmpxtnt[0].blockstart = search;
                break;
            }
            if(nfree > tmpxtnt[1].blockcount)
            {
                if(nfree > tmpxtnt[0].blockcount)
                {
                    tmpxtnt[2].blockcount = tmpxtnt[1].blockcount;
                    tmpxtnt[2].blockstart = tmpxtnt[1].blockstart;
                    tmpxtnt[1].blockcount = tmpxtnt[0].blockcount;
                    tmpxtnt[1].blockstart = tmpxtnt[0].blockstart;
                    tmpxtnt[0].blockcount = nfree;
                    tmpxtnt[0].blockstart = search;
                }
                else
                {
                    tmpxtnt[2].blockcount = tmpxtnt[1].blockcount;
                    tmpxtnt[2].blockstart = tmpxtnt[1].blockstart;
                    tmpxtnt[1].blockcount = nfree;
                    tmpxtnt[1].blockstart = search;
                }
            }
            else if(nfree > tmpxtnt[2].blockcount)
            {
                tmpxtnt[2].blockcount = nfree;
                tmpxtnt[2].blockstart = search;
            }
            search += nfree;
            search += countbits(vcbp, search, 1);
        }
        tmpxp = tmpxtnt;
        while(needed > 0 && --nempty >= 0 && tmpxp->blockcount)
        {
            xp->blockstart = tmpxp->blockstart;
            xp->blockcount = std::min<ULONGINT>(tmpxp->blockcount, needed);
#if 1
            if(xp->blockcount == 0)
                warning_unexpected("blockcount = 0");
#endif
            needed -= xp->blockcount;
            setbits(vcbp, xp->blockstart, xp->blockcount, 1);
            ++xp;
            ++tmpxp;
        }
    }
    *newabnp += *nallocneededp - needed;
    retval = needed < (ULONGINT)*nallocneededp;
    *nallocneededp = needed;
    return retval;
}

static void smokexpvcbp(ULONGINT tosmoke, xtntdesc *xp, HVCB *vcbp)
{
    if(tosmoke <= xp[2].blockcount)
    {
        xp[2].blockcount = xp[2].blockcount - (tosmoke);
        setbits(vcbp, xp[2].blockstart + xp[2].blockcount, tosmoke, 0);
    }
    else
    {
        setbits(vcbp, xp[2].blockstart, xp[2].blockcount, 0);
        tosmoke -= xp[2].blockcount;
        xp[2].blockcount = 0;
        if(tosmoke <= xp[1].blockcount)
        {
            xp[1].blockcount = xp[1].blockcount - tosmoke;
            setbits(vcbp, xp[1].blockstart + xp[1].blockcount,
                    tosmoke, 0);
        }
        else
        {
            setbits(vcbp, xp[1].blockstart, xp[1].blockcount, 0);
            tosmoke -= xp[1].blockcount;
            xp[1].blockcount = 0;
            xp[0].blockcount = xp[0].blockcount - tosmoke;
            setbits(vcbp, xp[0].blockstart + xp[0].blockcount, tosmoke, 0);
        }
    }
}

OSErr Executor::ROMlib_allochelper(IOParam *pb, Boolean async, alloctype alloc,
                                   Boolean writefcbp)
{
    filecontrolblock *fcbp;
    OSErr err, err1;
    LONGINT neweof, totallength, newneweof;
    uint16_t clumpsize, newabn;
    HVCB *vcbp;
    LONGINT tosmoke;
    ULONGINT nallocneeded, oldnallocneeded;
    Boolean needtoshrink;
    xtntdesc *xp;
    xtntkey *xtkeyp;
    xtntrec saverec;
    Byte savetype;
    btparam btparamrec;

    err = pbtofcbp(pb, &fcbp, writing);
    if(err != noErr)
    {
        fs_err_hook(err);
        /*-->*/ PBRETURN(pb, err);
    }

    switch(alloc)
    {
        case seteof:
            neweof = (ULONGINT)pb->ioMisc;
            break;
        case allocany:
        case alloccontig:
            neweof = fcbp->fcbPLen + pb->ioReqCount;
            break;
        default:
            warning_unexpected("unknown allocator1");
            err = fsDSIntErr;
            fs_err_hook(err);
            PBRETURN(pb, err);
    }
#if 0
/*
 * Bill showed that the Mac *doesn't* do this
 */
    if ((LONGINT) neweof < 0) /* NOTE: This is a guess that Mac stuff does */
	neweof = 0;			/* this */
#endif
    clumpsize = fcbp->fcbClmpSize;
    vcbp = (HVCB *)fcbp->fcbVPtr;
    if(!clumpsize)
        clumpsize = vcbp->vcbClpSiz;

#if 0
    clumpsize = vcbp->vcbAlBlkSiz;	/* i.e. ignore clumpsize; that's what
    					   the Mac really does */
#endif

    /* with Jaz Drives, clumpsize can *still* be zero! */
    if(clumpsize == 0)
    {
        warning_unexpected("clump size still 0");
        clumpsize = PHYSBSIZE;
    }

    newneweof = (neweof + clumpsize - 1) / clumpsize * clumpsize;
    newneweof = (newneweof + vcbp->vcbAlBlkSiz - 1) / vcbp->vcbAlBlkSiz
        * vcbp->vcbAlBlkSiz;
    if(newneweof >= neweof)
        neweof = newneweof;
    else /* overflow */
        neweof = ((unsigned long)(long)-1) / clumpsize * clumpsize;

    if(neweof < fcbp->fcbPLen)
    {
        needtoshrink = true;
        nallocneeded = (fcbp->fcbPLen - neweof) / vcbp->vcbAlBlkSiz;
    }
    else
    {
        needtoshrink = false;
        nallocneeded = (neweof - fcbp->fcbPLen) / vcbp->vcbAlBlkSiz;
    }

    xp = fcbp->fcbExtRec;
    totallength = vcbp->vcbAlBlkSiz * (xp[0].blockcount + xp[1].blockcount + xp[2].blockcount);
    if(needtoshrink)
    {
        if(neweof < totallength)
        {
            tosmoke = (totallength - neweof) / vcbp->vcbAlBlkSiz;
            smokexpvcbp(tosmoke, xp, vcbp);
            if(writefcbp)
                fcbp->fcbMdRByt |= DIRTYBIT;
        }
        if(fcbp->fcbPLen > totallength)
        {
            newabn = std::max(neweof, totallength) / vcbp->vcbAlBlkSiz;
            if(!(xtkeyp = fcbpbnotoxkeyp(fcbp, newabn)))
            {
                neweof = fcbp->fcbPLen;
                warning_unexpected("couldn't translate fcbp, newabn 1");
                err = fsDSIntErr;
                goto done;
            }
            xp = (xtntdesc *)DATAPFROMKEY(xtkeyp);
            totallength = vcbp->vcbAlBlkSiz * (xp[0].blockcount + xp[1].blockcount + xp[2].blockcount);
            tosmoke = xtkeyp->xkrFABN * vcbp->vcbAlBlkSiz + totallength
                - neweof;
            savetype = xtkeyp->xkrFkType;
            if(tosmoke < totallength)
            {
                gui_assert(xtkeyp && xtkeyp->xkrFNum == fcbp->fcbFlNum && xtkeyp->xkrFkType == savetype);
                smokexpvcbp(tosmoke, xp, vcbp);
                ROMlib_dirtyleaf((anykey *)xtkeyp, vcbp);
                err = ROMlib_btnext((anykey **)&xtkeyp, (anykey *)xtkeyp, vcbp);
                if(err != noErr)
                    goto done;
            }
            if(xtkeyp && xtkeyp->xkrFNum == fcbp->fcbFlNum && xtkeyp->xkrFkType == savetype)
            {
                memmove(saverec, DATAPFROMKEY(xtkeyp),
                        (LONGINT)sizeof(saverec));
                ROMlib_makextntparam(&btparamrec, vcbp, 0, 0, 0);
                btparamrec.tofind = *(anykey *)xtkeyp;
                btparamrec.leafindex = -1;
                btparamrec.success = true;
                btparamrec.foundp = (anykey *)xtkeyp;
            }
            else
                btparamrec.success = false;
            while(btparamrec.success)
            {
                xtkeyp = (xtntkey *)btparamrec.foundp;
                gui_assert(xtkeyp && xtkeyp->xkrFNum == fcbp->fcbFlNum && xtkeyp->xkrFkType == savetype);
                xp = (xtntdesc *)DATAPFROMKEY(btparamrec.foundp);
                setbits(vcbp, xp[0].blockstart, xp[0].blockcount, 0);
                setbits(vcbp, xp[1].blockstart, xp[1].blockcount, 0);
                setbits(vcbp, xp[2].blockstart, xp[2].blockcount, 0);
                newabn = btparamrec.tofind.xtntk.xkrFABN + xp[0].blockcount + xp[1].blockcount + xp[2].blockcount;
                err = ROMlib_btdelete(&btparamrec);
                if(err != noErr)
                    goto done;
                btparamrec.tofind.xtntk.xkrFABN = newabn;
                err = ROMlib_keyfind(&btparamrec);
                if(err != noErr)
                    goto done;
            }
        }
    }
    else if(nallocneeded > 0)
    {
        if((nallocneeded > (ULONGINT)vcbp->vcbFreeBks))
        {
            if((alloc == seteof || alloc == alloccontig))
            {
                neweof = fcbp->fcbPLen;
                err = dskFulErr;
                /*-->*/ goto done;
            }
        }
        if(fcbp->fcbPLen == totallength)
        { /* possibly some room in this one */
            newabn = 0;
            if(fillextent(xp, &nallocneeded, vcbp, &newabn) && writefcbp)
                fcbp->fcbMdRByt |= DIRTYBIT;
        }
        else
        {
            newabn = fcbp->fcbPLen / vcbp->vcbAlBlkSiz;
            if(!(xtkeyp = fcbpbnotoxkeyp(fcbp, newabn)))
            {
                neweof = fcbp->fcbPLen;
                warning_unexpected("couldn't translate fcbp, newabn 2");
                err = fsDSIntErr;
                goto done;
            }
            xp = (xtntdesc *)DATAPFROMKEY(xtkeyp);
            newabn = xtkeyp->xkrFABN;
            if(fillextent(xp, &nallocneeded, vcbp, &newabn))
                ROMlib_dirtyleaf((anykey *)xtkeyp, vcbp);
        }
        while(nallocneeded > 0)
        {
            xtkeyp = ROMlib_newextentrecord(fcbp, newabn);
            if(!xtkeyp)
            {
                warning_unexpected("newextent failed");
                err = fsDSIntErr;
                neweof = newabn * vcbp->vcbAlBlkSiz;
                /*-->*/ goto done;
            }
            xp = (xtntdesc *)DATAPFROMKEY(xtkeyp);
            oldnallocneeded = nallocneeded;
            if(fillextent(xp, &nallocneeded, vcbp, &newabn))
                ROMlib_dirtyleaf((anykey *)xtkeyp, vcbp);
            if(nallocneeded == oldnallocneeded)
            {
                neweof = newabn * vcbp->vcbAlBlkSiz;
                err = dskFulErr;
                /*-->*/ goto done;
            }
        }
    }
done:
    if(writefcbp && (needtoshrink || nallocneeded > 0))
    { /* cleancache must NOT be called if we are trashing */
        err1 = ROMlib_cleancache(vcbp); /* blocks before we delete a file */
        if(err == noErr)
            err1 = ROMlib_flushvcbp(vcbp);
    }
    else
        err1 = noErr;
    switch(alloc)
    {
        case seteof:
            if(err == noErr)
                fcbp->fcbEOF = guest_cast<int32_t>(pb->ioMisc);
            break;
        case allocany:
        case alloccontig:
            pb->ioActCount = neweof - fcbp->fcbPLen;
            break;
        default:
            warning_unexpected("unknown allocator2");
            err = fsDSIntErr;
            fs_err_hook(err);
            PBRETURN(pb, err);
    }
    fcbp->fcbPLen = neweof;
    fcbp->fcbMdRByt |= 0x80;

    if(err == noErr)
        err = err1;
    fs_err_hook(err);
    PBRETURN(pb, err);
}

OSErr Executor::hfsPBSetEOF(ParmBlkPtr pb, Boolean async)
{
    OSErr err;
    err = ROMlib_allochelper((IOParam *)pb, async, seteof, true);
    fs_err_hook(err);
    return err;
}

OSErr Executor::hfsPBAllocate(ParmBlkPtr pb, Boolean async)
{
    OSErr err;
    err = ROMlib_allochelper((IOParam *)pb, async, allocany, true);
    fs_err_hook(err);
    return err;
}

OSErr Executor::hfsPBAllocContig(ParmBlkPtr pb, Boolean async)
{
    OSErr err;

    err = ROMlib_allochelper((IOParam *)pb, async, alloccontig, true);
    fs_err_hook(err);
    return err;
}

#define RETURN(x)             \
    do                        \
    {                         \
        pb->ioResult = x; \
        goto DONE;            \
    } while(0)

static OSErr PBReadWrite(IOParam *pb, Boolean async, accesstype rw)
{
    filecontrolblock *fcbp;
    LONGINT absoffset, totransfer, neweot, templ, physblock, actl;
    OSErr err;
    OSErr newerr;
    LONGINT ntoskip, ntocopy, nphyscontig, nblockstogo, thisrun;
#if 0
    LONGINT i, actl2;
#endif
    char tbuf[PHYSBSIZE + sizeof(LONGINT)]; /* goes away eventually */
    char *tempbuf;
    Ptr bufp;
    HVCB *vcbp;
    INTEGER ntoslide;
    char save0, save1, save2;

    tempbuf = (char *)(((uintptr_t)tbuf + 3) / 4 * 4);
    pb->ioResult = noErr;
    pb->ioActCount = 0;
    totransfer = pb->ioReqCount;

    vcbp = 0;
    newerr = pbtofcbp(pb, &fcbp, rw);
    if(newerr != noErr)
    {
        fs_err_hook(newerr);
        /*-->*/ RETURN(newerr);
    }
    vcbp = fcbp->fcbVPtr;

    absoffset = pbabsoffset(pb, fcbp);
    if(absoffset < 0)
    {
        err = posErr;
        fs_err_hook(err);
        RETURN(err);
    }
    pb->ioPosOffset = absoffset;
    if(totransfer < 0)
    {
        err = paramErr;
        fs_err_hook(err);
        RETURN(err);
    }
    neweot = absoffset + totransfer;
    if(neweot > fcbp->fcbEOF)
    {
        if(rw == reading)
        {
            totransfer = fcbp->fcbEOF - absoffset;
            pb->ioResult = eofErr;
        }
        else
        {
            templ = (LONGINT)pb->ioMisc;
#if defined(MAC)
            pb->ioMisc = (Ptr)neweot;
#else /* !defined(MAC) */
            pb->ioMisc = (LONGINT)neweot;
#endif /* !defined(MAC) */
            PBSetEOF((ParmBlkPtr)pb, false);
            if(pb->ioResult != noErr)
                totransfer = fcbp->fcbPLen - absoffset;
        }
    }
    ntoskip = absoffset % PHYSBSIZE;
    nphyscontig = 0;
    bufp = pb->ioBuffer;
#if !defined(LETGCCWAIL)
    physblock = 0;
#endif /* LETGCCWAIL */
    if(ntoskip)
    {
        absoffset -= ntoskip;
        physblock = ROMlib_logtophys(fcbp, absoffset, &nphyscontig);
        if(nphyscontig < 1)
        {
            fprintf(stderr, "1 absoffset = %d, nphyscontig = %d\n",
                    (LONGINT)absoffset, (LONGINT)nphyscontig);
            err = ioErr;
            fs_err_hook(err);
            RETURN(err);
        }
        newerr = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs, physblock, 1,
                                     (Ptr)tempbuf,
                                     reading, (GUEST<LONGINT> *)0);
        if(newerr != noErr)
        {
            fs_err_hook(newerr);
            RETURN(newerr);
        }
        ntocopy = std::min(totransfer, PHYSBSIZE - ntoskip);
        if(rw == reading)
            BlockMove((Ptr)tempbuf + ntoskip, (Ptr)bufp, (Size)ntocopy);
        else
        {
            memmove((char *)tempbuf + ntoskip, bufp, (LONGINT)ntocopy);
            newerr = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs, physblock, 1,
                                         (Ptr)tempbuf, writing,
                                         (GUEST<LONGINT> *)0);
            if(newerr != noErr)
            {
                fs_err_hook(newerr);
                RETURN(newerr);
            }
        }

        pb->ioPosOffset = pb->ioPosOffset + (ntocopy);
        pb->ioActCount = pb->ioActCount + (ntocopy);
        totransfer -= ntocopy;
        absoffset += PHYSBSIZE;
        physblock += PHYSBSIZE;
        bufp += ntocopy;
        --nphyscontig;
    }
    if(totransfer >= PHYSBSIZE)
    {
        nblockstogo = totransfer / PHYSBSIZE;
        while(nblockstogo > 0)
        {
            if(nphyscontig == 0)
            {
                physblock = ROMlib_logtophys(fcbp, absoffset, &nphyscontig);
                if(nphyscontig < 1)
                {
                    fprintf(stderr, "2 absoffset = %d, nphyscontig = %d\n",
                            (LONGINT)absoffset, (LONGINT)nphyscontig);
                    err = ioErr;
                    fs_err_hook(err);
                    RETURN(err);
                }
            }
            thisrun = std::min(nphyscontig, nblockstogo);
#if 0
	    if (((LONGINT)bufp & 3) == 0) {
		newerr = ROMlib_transphysblk (&vcbp->hfs, physblock, thisrun,
					      bufp, rw, &actl);
		actl = actl;
		if (rw == reading)
		    ROMlib_destroy_blocks(US_TO_SYN68K(bufp), actl, true);
	    } else {
/* TODO:  Hubris attenuation:  SPEED THIS UP FOR EXCEL 4.x INSTALLERS!!!! */
		newerr = noErr;
		actl = 0;
		for (i = 0; newerr == noErr && i < thisrun; ++i) {
		    if (rw == writing)
			memmove(tempbuf, bufp + (LONGINT) PHYSBSIZE * i,
				PHYSBSIZE);
		    newerr = ROMlib_transphysblk (&vcbp->hfs,
						  physblock +
						  (LONGINT) PHYSBSIZE * i,
						  1, (Ptr) tempbuf, rw,
						  &actl2);
		    actl2 = actl2;
		    if (rw == reading)
			BlockMove((Ptr) tempbuf,
			   (Ptr) bufp + (LONGINT) PHYSBSIZE * i, (Size) actl2);
		    actl += actl2;
		}
	    }
#else
            ntoslide = (uintptr_t)bufp & 3;
            bufp -= ntoslide;
            save0 = bufp[0];
            save1 = bufp[1];
            save2 = bufp[2];
            if(ntoslide && rw == writing)
                BlockMove(bufp + ntoslide, bufp, thisrun * PHYSBSIZE);

            GUEST<LONGINT> tmpActl;
            newerr = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs, physblock, thisrun,
                                         bufp,
                                         rw, &tmpActl);
            actl = tmpActl.get();
            if(ntoslide)
            {
                BlockMove(bufp, bufp + ntoslide, actl);
                switch(ntoslide)
                {
                    case 3:
                        bufp[2] = save2;
                    /* FALL THROUGH */
                    case 2:
                        bufp[1] = save1;
                    /* FALL THROUGH */
                    case 1:
                        bufp[0] = save0;
                        /* FALL THROUGH */
                }
            }
            bufp += ntoslide;
            if(rw == reading)
                ROMlib_destroy_blocks(US_TO_SYN68K(bufp), actl, true);
#endif
            pb->ioPosOffset = pb->ioPosOffset + (actl);
            pb->ioActCount = pb->ioActCount + (actl);
            if(newerr != noErr)
            {
                fs_err_hook(newerr);
                RETURN(newerr);
            }
            bufp += thisrun * (LONGINT)PHYSBSIZE;
            absoffset += thisrun * (LONGINT)PHYSBSIZE;
            physblock += thisrun * (LONGINT)PHYSBSIZE;
            totransfer -= thisrun * (LONGINT)PHYSBSIZE;
            nblockstogo -= thisrun;
            nphyscontig -= thisrun;
        }
    }
    if(totransfer > 0)
    {
        if(nphyscontig == 0)
        {
            physblock = ROMlib_logtophys(fcbp, absoffset, &nphyscontig);
            if(nphyscontig < 1)
            {
                fprintf(stderr, "2 absoffset = %d, nphyscontig = %d\n",
                        (LONGINT)absoffset, (LONGINT)nphyscontig);
                err = ioErr;
                fs_err_hook(err);
                RETURN(err);
            }
        }
        newerr = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs, physblock, 1,
                                     (Ptr)tempbuf,
                                     reading, nullptr);
        if(newerr != noErr)
        {
            fs_err_hook(newerr);
            RETURN(newerr);
        }
        if(rw == reading)
            BlockMove((Ptr)tempbuf, (Ptr)bufp, (Size)totransfer);
        else
        {
            memmove((char *)tempbuf, bufp, (LONGINT)totransfer);
            newerr = ROMlib_transphysblk(&((VCBExtra *)vcbp)->u.hfs, physblock, 1,
                                         (Ptr)tempbuf, writing,
                                         nullptr);
            if(newerr != noErr)
            {
                fs_err_hook(newerr);
                RETURN(newerr);
            }
        }
        pb->ioPosOffset = pb->ioPosOffset + (totransfer);
        pb->ioActCount = pb->ioActCount + (totransfer);
    }
    fcbp->fcbCrPs = pb->ioPosOffset;
DONE:
    if(vcbp)
        newerr = ROMlib_cleancache(vcbp);
    else
        newerr = noErr;
    if(pb->ioResult == noErr)
        pb->ioResult = newerr;
    fs_err_hook(pb->ioResult);
    return pb->ioResult;
}

#undef RETURN

OSErr Executor::hfsPBRead(ParmBlkPtr pb, Boolean async)
{
    OSErr err;
    err = PBReadWrite((IOParam *)pb, async, reading);
    fs_err_hook(err);
    return err;
}

OSErr Executor::hfsPBWrite(ParmBlkPtr pb, Boolean async)
{
    OSErr err;

    err = PBReadWrite((IOParam *)pb, async, writing);
    fs_err_hook(err);
    return err;
}

static OSErr dirtyfcbp(filecontrolblock *fcbp)
{
    ULONGINT catpos;
    short refnum;
    HVCB *vcbp;
    cacheentry *cachep;
    catkey key;
    unsigned char *namep;
    anykey *retkeyp;
    filerec *frp;
    OSErr err;
    INTEGER dumint;

    if(fcbp->fcbMdRByt & DIRTYBIT)
    {
        refnum = (char *)fcbp - (char *)LM(FCBSPtr);
        vcbp = fcbp->fcbVPtr;
        if((catpos = fcbp->fcbCatPos))
        {
            err = Executor::ROMlib_getcache(&cachep, fcbp->fcbVPtr->vcbCTRef,
                                            catpos, (cacheflagtype)0);
            if(err != noErr)
            {
                fs_err_hook(err);
                return err;
            }
            namep = fcbp->fcbCName;
            ROMlib_makecatkey(&key, fcbp->fcbDirID, namep[0], (Ptr)namep + 1);
            if(!Executor::ROMlib_searchnode((btnode *)cachep->buf, (void *)&key, ROMlib_catcompare,
                                            &retkeyp, &dumint))
            {
                btparam btparamrec;

                btparamrec.vcbp = fcbp->fcbVPtr;
                btparamrec.tofind.catk = key;
                btparamrec.fp = Executor::ROMlib_catcompare;
                btparamrec.refnum = fcbp->fcbVPtr->vcbCTRef;
                err = ROMlib_keyfind(&btparamrec);
                if(err == noErr)
                {
                    if(btparamrec.success)
                    {
                        retkeyp = btparamrec.foundp;
                        cachep = btparamrec.trail[btparamrec.leafindex].cachep;
                    }
                    else
                        err = fsDSIntErr;
                }
                if(err)
                {
                    warning_unexpected("err = %d", err);
                    return err;
                }
            }
            frp = (filerec *)DATAPFROMKEY(retkeyp);

            if(fcbp->fcbMdRByt & FLOCKEDBIT)
                frp->filFlags |= FSOFTLOCKBIT;
            else
                frp->filFlags &= ~FSOFTLOCKBIT;

#if 0
	    frp->filTyp           = fcbp->fcbTypByt;
	    frp->filStBlk         = fcbp->fcbSBlk;
	    frp->filClpSize       = fcbp->fcbClmpSize;
/* When this line was present, Excel wouldn't recognize its own files */
	    frp->filUsrWds.fdType = fcbp->fcbFType;
#endif

            if(fcbp->fcbMdRByt & RESOURCEBIT)
            {
                frp->filRLgLen = fcbp->fcbEOF;
                frp->filRPyLen = fcbp->fcbPLen;
                memmove(frp->filRExtRec, fcbp->fcbExtRec,
                        (LONGINT)sizeof(frp->filRExtRec));
            }
            else
            {
                frp->filLgLen = fcbp->fcbEOF;
                frp->filPyLen = fcbp->fcbPLen;
                memmove(frp->filExtRec, fcbp->fcbExtRec,
                        (LONGINT)sizeof(frp->filExtRec));
            }
            cachep->flags |= CACHEDIRTY;
        }
        else if(refnum == vcbp->vcbXTRef || refnum == vcbp->vcbCTRef)
        {
            err = noErr;
            vcbp->vcbFlags |= VCBDIRTY;
        }
        else
        {
            warning_unexpected("no catpos (nor are we XTRef or CTRef)");
            err = fsDSIntErr;
        }
        if(err == noErr)
            fcbp->fcbMdRByt &= ~DIRTYBIT;
        fs_err_hook(err);
        return err;
    }
    else
        return noErr;
}

OSErr Executor::hfsPBFlushFile(ParmBlkPtr pb, Boolean async)
{
    filecontrolblock *fcbp;
    OSErr err;

    fcbp = ROMlib_refnumtofcbp(pb->ioParam.ioRefNum);
    if(!fcbp)
        err = rfNumErr;
    else
        err = dirtyfcbp(fcbp);
    if(err == noErr)
        err = ROMlib_flushvcbp(fcbp->fcbVPtr);

    fs_err_hook(err);
    PBRETURN((IOParam *)pb, err);
}

OSErr Executor::hfsPBClose(ParmBlkPtr pb, Boolean async)
{
    filecontrolblock *fcbp;
    OSErr err;

    fcbp = ROMlib_refnumtofcbp(pb->ioParam.ioRefNum);
    if(!fcbp)
        err = rfNumErr;
    else
    {
        err = PBFlushFile(pb, async);
        fcbp->fcbFlNum = 0;
    }
    fs_err_hook(err);
    PBRETURN((IOParam *)pb, err);
}

OSErr Executor::ROMlib_makecatkey(catkey *keyp, LONGINT dirid, INTEGER namelen,
                                  Ptr namep)
{
    OSErr retval;

    if((unsigned)namelen > 31)
    {
        namelen = 31;
        retval = bdNamErr;
    }
    else
        retval = noErr;

    keyp->ckrKeyLen = namelen + 1 + sizeof(LONGINT) + 1;
    keyp->ckrResrv1 = 0;
    keyp->ckrParID = dirid;
    keyp->ckrCName[0] = namelen;
    memmove(keyp->ckrCName + 1, namep, (LONGINT)namelen);
    fs_err_hook(retval);
    return retval;
}

/*
 * TODO -- FIXME -- NOTE:
 *
 * On the Mac, if you do a PBGetCatInfo on Cirrus 80:Utilities::, you will
 * get info about Cirrus 80, *but* the parent directory will be ... that of
 * Utilities.  Findentry doesn't return that info, since we pull it out of
 * the catalog key instead.
 */

static OSErr findentry(LONGINT dirid, StringPtr name, btparam *btpb,
                       filekind *kindp, Boolean ignorename)
{
    filerec *retval;
    INTEGER namelen;
    unsigned char *namep, *colonp, *endp;
    void *recp;
    short rectype;
    OSErr err;
    INTEGER coloncoloncount;

    retval = 0;
    if(name == 0)
        ignorename = true;
    if(ignorename)
    {
        namelen = 0;
        namep = (StringPtr) "";
    }
    else
    {
        namelen = name[0];
        namep = name + 1;
    }

    if(namelen && namep[0] == ':')
    { /* advance over leading ':' */
        --namelen;
        ++namep;
    }
    if(namep[namelen - 1] == ':')
    { /* trailing colon means we want */
        --namelen; /* only a directory */
        *kindp = (filekind)(*kindp & directory);
    }

    if(namelen <= 0 && !ignorename && (*kindp & (directory | thread)) == 0)
    {
        err = bdNamErr;
        fs_err_hook(err);
        /*-->*/ return err;
    }
    if(namelen < 0)
    {
        err = bdNamErr;
        fs_err_hook(err);
        /*-->*/ return err;
    }
    gui_assert(namelen >= 0);

    coloncoloncount = 0;
    for(;;)
    {
        err = ROMlib_cleancache(btpb->vcbp);
        if(err != noErr)
        {
            fs_err_hook(err);
            /*-->*/ return err;
        }
        colonp = (unsigned char *)ROMlib_indexn((char *)namep, ':', namelen);
        if(colonp)
            endp = colonp;
        else
            endp = namep + namelen;
        err = ROMlib_makecatparam(btpb, btpb->vcbp, dirid, endp - namep, (Ptr)namep);
        if(err == noErr)
            err = ROMlib_keyfind(btpb);
        if(err != noErr)
        {
            fs_err_hook(err);
            /*-->*/ return err;
        }
        if(!btpb->success)
        {
#if 0
/*-->*/	    err = colonp ? dirNFErr : fnfErr;
#else
            /*-->*/ err = ROMlib_errortype(btpb);
#endif
            fs_err_hook(err);
            return err;
        }
        recp = DATAPFROMKEY(btpb->foundp);
        rectype = *(unsigned char *)recp;
        if(colonp)
        { /* expect a directory */
            switch(rectype)
            {
                case DIRTYPE:
                    dirid = ((directoryrec *)recp)->dirDirID;
                    break;
                case THREADTYPE:
                    if(((catkey *)&btpb->tofind)->ckrCName[0])
                    {
                        warning_unexpected("thread with name");
                        err = fsDSIntErr;
                        fs_err_hook(err);
                        /*-->*/ return err;
                    }
                    dirid = ((threadrec *)recp)->thdParID;
                    break;
                default:
                    warning_unexpected("unknown rectype1 in findentry");
                    err = fsDSIntErr;
                    fs_err_hook(err);
                    /*-->*/ return err;
            }
            namelen -= ((catkey *)&btpb->tofind)->ckrCName[0] + 1;
            namep += ((catkey *)&btpb->tofind)->ckrCName[0] + 1;
        }
        else
        { /* expect a regular file */
            switch(rectype)
            {
                case FILETYPE:
                    if(!(*kindp & regular))
                    {
                        err = bdNamErr;
                        fs_err_hook(err);
                        /*-->*/ return err;
                    }
                    *kindp = regular;
                    break;
                case DIRTYPE:
                    if(!(*kindp & directory))
                    {
                        err = bdNamErr;
                        fs_err_hook(err);
                        /*-->*/ return err;
                    }
                    *kindp = directory;
                    break;
                case THREADTYPE:
                    if(*kindp & thread)
                        *kindp = thread; /* we're done now */
                    else if((ignorename || endp == namep) && (*kindp & directory))
                    {
                        dirid = ((threadrec *)recp)->thdParID;
                        if(ignorename || coloncoloncount == 1)
                        {
                            namep = ((threadrec *)recp)->thdCName + 1;
                            namelen = ((threadrec *)recp)->thdCName[0];
                        }
                        else
                            ++coloncoloncount;
                        /*-->*/ continue; /* avoid return below */
                    }
                    else
                    {
                        err = dirNFErr;
                        warning_trace_info("*kindp = 0x%x, ignorename = %d, "
                                           "endp = %p, namep = %p",
                                           *kindp, ignorename, endp, namep);
                        fs_err_hook(err);
                        /*-->*/ return err;
                    }
                    break;
                default:
                    warning_unexpected("unknown rectype2 in findentry");
                    err = fsDSIntErr;
                    fs_err_hook(err);
                    /*-->*/ return err;
            }
            /*-->*/ return noErr;
        }
    }
}

/*
 * looking for matches of the form "xxx:yyy:zzz:" in "xxx"
 */

/* #warning Use of memcmp is WRONG here.  All filesystem name matching code */
/* #warning should do the right things with case and diacritical marks */

static Boolean dir_prefixes_volume(StringPtr dirnamep, StringPtr volnamep)
{
    if(!dirnamep) /* don't dereference 0 if it's passed in */
        return false;
    if(dirnamep[0] <= volnamep[0]) /* must be larger, for ":" if nothing else */
        /*-->*/ return false;
    if(memcmp(dirnamep + 1, volnamep + 1, volnamep[0]) != 0)
        /*-->*/ return false;
    return dirnamep[volnamep[0] + 1] == ':';
}

OSErr Executor::ROMlib_findvcbandfile(IOParam *pb, LONGINT dirid,
                                      btparam *btpb, filekind *kindp,
                                      Boolean ignorename)
{
    OSErr err;
    int badness;
    LONGINT dir;
    GUEST<StringPtr> savep;

    badness = 0;
    dir = 0;
    if(pb->ioNamePtr)
    {
        StringPtr namep;

        namep = pb->ioNamePtr;
        if(namep[0] == 1 && namep[1] == ':')
            ignorename = true;
    }
    do
    {
        err = noErr;
        savep = pb->ioNamePtr;
        if(ignorename)
            pb->ioNamePtr = 0;
        btpb->vcbp = ROMlib_findvcb(pb->ioVRefNum, pb->ioNamePtr, &dir,
                                    true);
        pb->ioNamePtr = savep;
        if(dir == 0)
        {
            dir = dirid;
            badness = 1;
        }
        if(!btpb->vcbp)
        {
            err = nsvErr;
            /*-->*/ break;
        }
        else
        {
            Str255 tempstr;
            StringPtr to_look_for_p;

            if(!ignorename
               && dir_prefixes_volume(pb->ioNamePtr, btpb->vcbp->vcbVN))
                dirid = 1;
            else
                ROMlib_adjustdirid(&dirid, btpb->vcbp, pb->ioVRefNum);

            if(!ignorename && dirid == 2 && pb->ioNamePtr
               && pb->ioNamePtr[0] == 0 && (*kindp & directory))
            {
                dirid = 1;
                str255assign(tempstr, btpb->vcbp->vcbVN);
                ++tempstr[0];
                tempstr[tempstr[0]] = ':';
                to_look_for_p = tempstr;
            }
            else
                to_look_for_p = pb->ioNamePtr;

            err = findentry(dirid, to_look_for_p, btpb, kindp, ignorename);
            if(err)
            {
                ++badness;
                dirid = dir;
            }
            else
                badness = 0;
        }
    } while(badness == 1);
    fs_err_hook(err);
    return err;
}

/*
 * ROMlib_alreadyopen checks to see whether a given file is already open and if
 * so whether that causes a conflict (the conflicting refnum is filled in).
 */

OSErr Executor::ROMlib_alreadyopen(HVCB *vcbp, LONGINT flnum,
                                   SignedByte *permp, GUEST<INTEGER> *refnump,
                                   busyconcern_t busy)
{
    short length;
    filecontrolblock *fcbp, *efcbp;
    SignedByte temp;
    GUEST<INTEGER> tempshort;
    Byte busybit;
    OSErr err;

    if(!permp)
    {
        permp = &temp;
        temp = fsWrPerm;
    }

    if(!refnump)
        refnump = &tempshort;

    if(*permp == fsRdPerm)
        return noErr;
    busybit = busy == resourcebusy ? RESOURCEBIT : 0;
    length = *(GUEST<INTEGER> *)LM(FCBSPtr);
    fcbp = (filecontrolblock *)((GUEST<INTEGER> *)LM(FCBSPtr) + 1);
    efcbp = (filecontrolblock *)((char *)LM(FCBSPtr) + length);
    for(; fcbp < efcbp; fcbp = (filecontrolblock *)((char *)fcbp + LM(FSFCBLen)))
        if(fcbp->fcbVPtr == vcbp && fcbp->fcbFlNum == flnum && (busy == eitherbusy
                                                                        || (fcbp->fcbMdRByt & RESOURCEBIT) == busybit)
           && ((fcbp->fcbMdRByt & WRITEBIT) || permp == &temp))
            switch(*permp)
            {
                case fsCurPerm:
                    if(!(fcbp->fcbMdRByt & SHAREDBIT))
                    {
                        *permp = fsRdPerm;
                        return noErr;
                    }
                    break;
                case fsWrPerm:
                case fsRdWrPerm:
                    *refnump = (char *)fcbp - (char *)LM(FCBSPtr);
                    err = opWrErr;
                    fs_err_hook(err);
                    return err;
                    break;
                case fsRdWrShPerm:
                    if(!(fcbp->fcbMdRByt & SHAREDBIT))
                    {
                        *refnump = (char *)fcbp - (char *)LM(FCBSPtr);
                        err = opWrErr;
                        fs_err_hook(err);
                        return err;
                    }
                    break;
            }
    return noErr;
}

static OSErr PBOpenHelper(IOParam *pb, Forktype ft, LONGINT dirid,
                          Boolean async)
{
    filecontrolblock *fcbp;
    OSErr err;
    SignedByte permssn;
    cacheentry *cachep;
    btparam btparamrec;
    filekind kind;
    filerec *frp;
    catkey *catkeyp;

    pb->ioRefNum = 0; /* in case some program ignores a failure */
    kind = regular;
    err = ROMlib_findvcbandfile(pb, dirid, &btparamrec, &kind, false);
    if(err != noErr)
    {
        fs_err_hook(err);
        PBRETURN(pb, err);
    }

    err = ROMlib_cleancache(btparamrec.vcbp);
    if(err != noErr)
    {
        fs_err_hook(err);
        PBRETURN(pb, err);
    }
    permssn = pb->ioPermssn;
    frp = (filerec *)DATAPFROMKEY(btparamrec.foundp);
    err = ROMlib_alreadyopen(btparamrec.vcbp, frp->filFlNum, &permssn,
                             &pb->ioRefNum, ft == resourcefork ? resourcebusy : databusy);
    if(err != noErr)
    {
        fs_err_hook(err);
        PBRETURN(pb, err);
    }
    fcbp = ROMlib_getfreefcbp();
    if(!fcbp)
    {
        err = tmfoErr;
        fs_err_hook(err);
        /*-->*/ PBRETURN(pb, err);
    }

    fcbp->fcbFlNum = frp->filFlNum;
    if(frp->filFlags & FSOFTLOCKBIT)
    {
        switch(permssn)
        {
            case fsCurPerm:
                permssn = fsRdPerm;
                break;
            case fsWrPerm:
            case fsRdWrPerm:
            case fsRdWrShPerm:
                fcbp->fcbFlNum = 0;
                err = permErr;
                fs_err_hook(err);
                PBRETURN(pb, err);
        }
        fcbp->fcbMdRByt = FLOCKEDBIT;
    }
    else
    {
        if(permssn == fsCurPerm)
            permssn = fsRdWrPerm;
        fcbp->fcbMdRByt = 0;
    }
    switch(permssn)
    {
        case fsRdPerm:
            break;
        case fsWrPerm:
        case fsRdWrPerm:
            fcbp->fcbMdRByt |= WRITEBIT;
            break;
        case fsRdWrShPerm:
            fcbp->fcbMdRByt |= (WRITEBIT | SHAREDBIT);
            break;
        default:
            fcbp->fcbFlNum = 0;
            warning_unexpected("unknown permission");
            err = fsDSIntErr;
            fs_err_hook(err);
            PBRETURN(pb, err);
    }

    if(ft == resourcefork)
        fcbp->fcbMdRByt |= RESOURCEBIT;
    else
        fcbp->fcbMdRByt &= ~RESOURCEBIT;

    if(ft == datafork)
    {
        fcbp->fcbEOF = frp->filLgLen;
        fcbp->fcbPLen = frp->filPyLen;
        fcbp->fcbSBlk = frp->filStBlk;
        memmove((char *)fcbp->fcbExtRec, (char *)frp->filExtRec,
                (LONGINT)sizeof(frp->filExtRec));
    }
    else
    {
        fcbp->fcbEOF = frp->filRLgLen;
        fcbp->fcbPLen = frp->filRPyLen;
        fcbp->fcbSBlk = frp->filRStBlk;
        memmove((char *)fcbp->fcbExtRec, (char *)frp->filRExtRec,
                (LONGINT)sizeof(frp->filRExtRec));
    }
    fcbp->fcbCrPs = 0;
    fcbp->fcbVPtr = btparamrec.vcbp;
#if defined(MAC)
    fcbp->fcbBfAdr = pb->ioMisc;
#else /* !defined(MAC) */
    fcbp->fcbBfAdr = guest_cast<Ptr>(pb->ioMisc);
#endif /* !defined(MAC) */
    fcbp->fcbFlPos = 0;

    fcbp->fcbClmpSize.set(frp->filClpSize.get());
    if(!fcbp->fcbClmpSize)
        fcbp->fcbClmpSize = btparamrec.vcbp->vcbClpSiz;

    fcbp->fcbBTCBPtr = 0; /* Used only for B-trees I think */
    fcbp->fcbFType = frp->filUsrWds.fdType;
    catkeyp = (catkey *)btparamrec.foundp;
    cachep = ROMlib_addrtocachep((Ptr)catkeyp, btparamrec.vcbp);
    fcbp->fcbCatPos = cachep->logblk;
    fcbp->fcbDirID = catkeyp->ckrParID;
    str255assign(fcbp->fcbCName, catkeyp->ckrCName);
    pb->ioRefNum = (char *)fcbp - (char *)LM(FCBSPtr);
    PBRETURN(pb, noErr);
}

#undef RETURN

OSErr Executor::hfsPBOpen(ParmBlkPtr pb, Boolean async)
{
    OSErr err;
    err = PBOpenHelper((IOParam *)pb, datafork, 0L, async);
    fs_err_hook(err);
    return err;
}

OSErr Executor::hfsPBOpenRF(ParmBlkPtr pb, Boolean async)
{
    OSErr err;
    err = PBOpenHelper((IOParam *)pb, resourcefork, 0L, async);
    fs_err_hook(err);
    return err;
}

OSErr Executor::hfsPBHOpen(HParmBlkPtr pb, Boolean async)
{
    OSErr err;

    err = PBOpenHelper((IOParam *)pb, datafork, pb->fileParam.ioDirID,
                       async);
    fs_err_hook(err);
    return err;
}

OSErr Executor::hfsPBHOpenRF(HParmBlkPtr pb, Boolean async)
{
    OSErr err;

    err = PBOpenHelper((IOParam *)pb, resourcefork, pb->fileParam.ioDirID,
                       async);
    fs_err_hook(err);
    return err;
}

/*
 * NOTE:  I've tried playing around with the LockRange and UnlockRange calls
 *        and I don't know what they do.  They seem to be unimplemented on
 *        our Mac+.  Perhaps when they were created there was no idea that
 *        multifinder would exist so they don't do anything if you're not
 *        on a network.
 */

OSErr Executor::hfsPBLockRange(ParmBlkPtr pb, Boolean async)
{
    PBRETURN((IOParam *)pb, noErr);
}

OSErr Executor::hfsPBUnlockRange(ParmBlkPtr pb, Boolean async)
{
    PBRETURN((IOParam *)pb, noErr);
}

OSErr Executor::hfsPBGetFPos(ParmBlkPtr pb, Boolean async)
{
    filecontrolblock *fcbp;
    OSErr err;

    fcbp = ROMlib_refnumtofcbp(pb->ioParam.ioRefNum);
    if(!fcbp)
    {
        err = rfNumErr;
        fs_err_hook(err);
        /*-->*/ PBRETURN((IOParam *)pb, err);
    }
    pb->ioParam.ioReqCount = 0;
    pb->ioParam.ioActCount = 0;
    pb->ioParam.ioPosMode = 0;
    pb->ioParam.ioPosOffset = fcbp->fcbCrPs;

    PBRETURN((IOParam *)pb, noErr);
}

OSErr Executor::hfsPBSetFPos(ParmBlkPtr pb, Boolean async)
{
    filecontrolblock *fcbp;
    LONGINT newpos;
    OSErr retval;

    fcbp = ROMlib_refnumtofcbp(pb->ioParam.ioRefNum);
    if(!fcbp)
    {
        retval = rfNumErr;
        fs_err_hook(retval);
        /*-->*/ PBRETURN((IOParam *)pb, retval);
    }
    retval = noErr;
    newpos = pbabsoffset((IOParam *)pb, fcbp);
    if(newpos < 0)
        retval = posErr;
    else if(newpos > fcbp->fcbEOF)
    {
        retval = eofErr;
        fcbp->fcbCrPs = fcbp->fcbEOF;
    }
    else
        fcbp->fcbCrPs = newpos;
    pb->ioParam.ioPosOffset = fcbp->fcbCrPs;
    fs_err_hook(retval);
    PBRETURN((IOParam *)pb, retval);
}

OSErr Executor::hfsPBGetEOF(ParmBlkPtr pb, Boolean async)
{
    filecontrolblock *fcbp;
    OSErr err;

    fcbp = ROMlib_refnumtofcbp(pb->ioParam.ioRefNum);
    if(!fcbp)
    {
        err = rfNumErr;
        fs_err_hook(err);
        /*-->*/ PBRETURN((IOParam *)pb, err);
    }
#if defined(MAC)
    pb->ioParam.ioMisc = (Ptr)fcbp->fcbEOF;
#else /* !defined(MAC) */
    pb->ioParam.ioMisc = fcbp->fcbEOF;
#endif /* !defined(MAC) */

    PBRETURN((IOParam *)pb, noErr);
}
