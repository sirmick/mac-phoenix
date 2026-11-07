/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ResourceMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <ToolboxUtil.h>
#include <res/resource.h>
#include <mman/mman.h>
#include <file/file.h>

#include <map>
#include <vector>

using namespace Executor;

typedef res_sorttype_t *sorttypeptr;

typedef GUEST<sorttypeptr> *sorttypehand;

static LONGINT addtype(resmaphand, ResType);
static LONGINT addname(resmaphand, ConstStringPtr);
static OSErr writemap(resmaphand);
static void fillst(sorttypehand, resref *, resref *);
static LONGINT walkst(res_sorttype_t *, res_sorttype_t *, INTEGER, LONGINT);
static void compactdata(resmaphand);

void Executor::C_SetResInfo(Handle res, INTEGER id, ConstStringPtr name)
{
    resmaphand map;
    typref *tr;
    resref *rr;
    unsigned char *sp;
    INTEGER sl;
    OSErr err;

    ROMlib_setreserr(ROMlib_findres(res, &map, &tr, &rr));
    if(LM(ResErr) != noErr)
        return;
    if(rr->ratr & resProtected)
    {
        ROMlib_setreserr(resAttrErr); /* IV-18 */
        return;
    }
    rr->rid = id;
    if(name)
    {
        sl = name[0];
        if(*(sp = (unsigned char *)*map + (*map)->namoff + rr->noff) < sl || rr->noff == -1)
        {
            SetHandleSize((Handle)map, (*map)->rh.maplen + sl + 1);
            err = MemError();
            if(ROMlib_setreserr(err))
                return;
            rr->noff = (*map)->rh.maplen - (*map)->namoff;
            (*map)->rh.maplen = (*map)->rh.maplen + sl + 1;
            sp = (unsigned char *)*map + (*map)->namoff + rr->noff;
            warning_unimplemented("we leak space here");
        }
        str255assign(sp, name);
    }
}

void Executor::C_SetResAttrs(Handle res, INTEGER attrs)
{
    resmaphand map;
    typref *tr;
    resref *rr;

    ROMlib_setreserr(ROMlib_findres(res, &map, &tr, &rr));
    if(LM(ResErr) != noErr)
        return;
    rr->ratr = attrs;
}

void Executor::C_ChangedResource(Handle res)
{
    resmaphand map;
    typref *tr;
    resref *rr;
    Size oldsize, newsize;

    ROMlib_setreserr(ROMlib_findres(res, &map, &tr, &rr));
    if(LM(ResErr) != noErr)
        return;
    if(rr->ratr & resProtected)
    {
        ROMlib_setreserr(resAttrErr); /* IV-18 */
        return;
    }
    rr->ratr |= resChanged;
    (*map)->resfatr |= mapChanged;
    if(rr->doff[0] != 0xff || rr->doff[1] != 0xff || rr->doff[2] != 0xff)
    {
        oldsize = ROMlib_SizeResource(res, false);
        newsize = GetHandleSize((Handle)rr->rhand);
        if(newsize > oldsize)
        {
            (*map)->resfatr |= mapCompact;
            rr->doff[0] = rr->doff[1] = rr->doff[2] = 0xff;
        }
    }
}

static LONGINT addtype(resmaphand map, ResType typ)
{
    typref *tr, t;
    INTEGER i;
    LONGINT off;

    WALKTR(map, i, tr)
    if(tr->rtyp == typ)
        return ((LONGINT)((char *)tr - (char *)*map));
    EWALKTR(tr)
    ROMlib_resTypesChanged();
    t.rtyp = typ;
    t.nres = -1;
    t.rloff = NAMEOFF(map) - TYPEOFF(map);
    off = (LONGINT)((char *)tr - (char *)*map);
    Munger((Handle)map, off, (Ptr) "", (LONGINT)0, (Ptr)&t, sizeof(t));
    NUMTMINUS1(map) += 1;
    MAPLEN(map) += sizeof(t);
    NAMEOFF(map) += sizeof(t);
    WALKTR(map, i, tr)
    tr->rloff = tr->rloff + sizeof(t);
    EWALKTR(tr)
    return (off);
}

static LONGINT addname(resmaphand map, ConstStringPtr name)
{
    LONGINT retval;

    if(!name || !name[0])
        retval = -1;
    else
    {
        LONGINT namelen;

        namelen = name[0] + 1;

        Munger((Handle)map, MAPLEN(map), (Ptr) "", (LONGINT)0, (Ptr)name,
               namelen);
        retval = MAPLEN(map) - NAMEOFF(map);
        MAPLEN(map) += namelen;
    }
    return retval;
}

void Executor::C_AddResource(Handle data, ResType typ, INTEGER id,
                             ConstStringPtr name)
{
    LONGINT toff, roff;
    typref *tr, *tr2;
    resref r, *rr;
    resmaphand map;
    INTEGER i, j;

    if(!data)
    {
        ROMlib_setreserr(addResFailed);
        return;
    }
    ROMlib_setreserr(noErr);
    map = ROMlib_rntohandl(LM(CurMap), (Handle *)0);
    if(!map)
    {
        ROMlib_setreserr(resFNotFound);
        return;
    }
    if(ROMlib_findmapres(map, data, &tr, &rr) == noErr)
    {
        ROMlib_setreserr(addResFailed);
        /*-->*/ return;
    }
    ROMlib_setreserr(noErr);
    toff = addtype(map, typ);
    r.rid = id;
    r.noff = addname(map, name);
    r.ratr = resChanged;
    r.doff[0] = r.doff[1] = r.doff[2] = 0xff;
    (*map)->resfatr |= mapChanged;
    r.rhand = data;
    HSetRBit(data);
    tr = (typref *)((char *)*map + toff);
#if 1
    WALKRR(map, tr, j, rr)
    if(rr->rid > id)
        break;
    EWALKRR(rr)
#else /* 0 */
    rr = (resref *)((char *)*map + (*map)->typoff + tr->rloff);
#endif /* 0 */
    roff = (LONGINT)((char *)rr - (char *)*map);
    /* roff is from the beginning of the map */
    Munger((Handle)map, roff, (Ptr) "", (LONGINT)0, (Ptr)&r,
           sizeof(resref));
    roff -= TYPEOFF(map);
    /* roff is from the beginning of the typelist */
    MAPLEN(map) += sizeof(resref);
    NAMEOFF(map) += sizeof(resref);
    tr2 = (typref *)((char *)*map + toff);
    tr2->nres = tr2->nres + 1;
    WALKTR(map, i, tr)
    if(tr->rloff >= roff && tr != tr2)
        /*  if (tr->rloff > roff)  would probably work, but I'm chicken
								      -- ctm */
        tr->rloff = tr->rloff + sizeof(resref);
    EWALKTR(tr)
}

void Executor::C_RemoveResource(Handle res)
{
    resmaphand map;
    typref *tr;
    resref *rr;
    LONGINT troff, rroff, nmoff, nlen;
    INTEGER i, j, tloss;

#if !defined(LETGCCWAIL)
    nlen = 0;
#endif /* LETGCCWAIL */

    if(!res)
    {
        ROMlib_setreserr(rmvResFailed);
        /*-->*/ return;
    }
    map = ROMlib_rntohandl(LM(CurMap), (Handle *)0);
    if(!map)
    {
        ROMlib_setreserr(resFNotFound);
        /*-->*/ return;
    }
    ROMlib_setreserr(ROMlib_findmapres(map, res, &tr, &rr));
    if(LM(ResErr) != noErr)
        /*-->*/ return;
    if(rr->ratr & resProtected)
    {
        ROMlib_setreserr(rmvResFailed);
        /*-->*/ return;
    }
    HSetState(res, HGetState(res) & ~RSRCBIT);
    troff = (char *)tr - (char *)*map;
    rroff = (char *)rr - (char *)*map;
    if(rr->noff != -1)
    {
        nmoff = rr->noff + NAMEOFF(map);
        nlen = *((unsigned char *)*map + nmoff) + 1;
        Munger((Handle)map, nmoff, (Ptr)0, nlen, (Ptr) "", (LONGINT)0);
        nmoff -= NAMEOFF(map);
        MAPLEN(map) -= nlen;
    }
    else
        nmoff = 0x7fff;
    (*map)->resfatr |= mapChanged | mapCompact;
    Munger((Handle)map, rroff, (Ptr)0, (LONGINT)sizeof(resref),
           (Ptr) "", (LONGINT)0);
    rroff -= TYPEOFF(map);
    MAPLEN(map) -= sizeof(resref);
    NAMEOFF(map) -= sizeof(resref);
    tr = (typref *)((char *)*map + troff);
    tr->nres = tr->nres - 1;
    if(tr->nres == -1)
    {
        ROMlib_resTypesChanged();
        NUMTMINUS1(map) -= 1;
        Munger((Handle)map, troff, (Ptr)0, (LONGINT)sizeof(typref), (Ptr) "",
               (LONGINT)0);
        tloss = sizeof(typref);
        MAPLEN(map) -= sizeof(typref);
        NAMEOFF(map) -= sizeof(typref);
    }
    else
        tloss = 0;
    WALKTR(map, i, tr)
    if(tr->rloff > rroff)
        tr->rloff = tr->rloff - sizeof(resref);
    tr->rloff = tr->rloff - tloss;
    WALKRR(map, tr, j, rr)
    if(rr->noff > nmoff)
        rr->noff = rr->noff - nlen;
    EWALKRR(rr)
    EWALKTR(tr)
}

/* writemap:  given a map, writemap writes it out */

static OSErr writemap(resmaphand map)
{
    OSErr terr;
    LONGINT lc;

    terr = SetFPos((*map)->resfn, fsFromStart, 0L);
    if(terr != noErr)
        return (terr);
    lc = sizeof(reshead);
    terr = FSWriteAll((*map)->resfn, inout(lc), (Ptr) & ((*map)->rh));
    if(terr != noErr)
        return (terr);
    terr = SetFPos((*map)->resfn, fsFromStart, (*map)->rh.rmapoff);
    if(terr != noErr)
        return (terr);
    lc = (*map)->rh.maplen;

    auto saveAttr = (*map)->resfatr;
    (*map)->resfatr &= ~mapChanged;

    terr = FSWriteAll((*map)->resfn, inout(lc), (Ptr)*map);
    if(terr != noErr)
        (*map)->resfatr = saveAttr;

    return (terr);
}

/* writeResourceInternal is the helper routine for WriteResource, the
	      resource file associated with map is updated to include
	      the resource pointed to by rr */

static void writeResourceInternal(resmaphand map, resref *rr)
{
    LONGINT rsize, newloc, lc;
    GUEST<LONGINT> swappedrsize;
    Handle res;

    res = (Handle)rr->rhand;
    if((rr->ratr & resChanged) && res)
    {
        rsize = GetHandleSize(res);
        /* MacPhoenix: grown since ChangedResource looked (marked changed,
           then resized), it goes at the end, as a resource ChangedResource
           saw grown does: in its old place it overwrote the next ones. */
        if(!(rr->doff[0] == 0xFF && rr->doff[1] == 0xFF && rr->doff[2] == 0xFF))
        {
            GUEST<LONGINT> disksize;
            lc = sizeof disksize;
            if(SetFPos((*map)->resfn, fsFromStart, (*map)->rh.rdatoff + B3TOLONG(rr->doff)) != noErr
               || FSReadAll((*map)->resfn, inout(lc), (Ptr)&disksize) != noErr
               || rsize > disksize)
            {
                rr->doff[0] = rr->doff[1] = rr->doff[2] = 0xFF;
                (*map)->resfatr |= mapChanged;
            }
        }
        if(rr->doff[0] == 0xFF && rr->doff[1] == 0xFF && rr->doff[2] == 0xFF)
        {
            newloc = (*map)->rh.rmapoff - (*map)->rh.rdatoff;
            (*map)->rh.rmapoff = (*map)->rh.rmapoff + rsize + sizeof(LONGINT);
            (*map)->rh.datlen = (*map)->rh.datlen + rsize + sizeof(LONGINT);
            B3ASSIGN(rr->doff, newloc);
        }
        else
            newloc = B3TOLONG(rr->doff);
        ROMlib_setreserr(SetFPos((*map)->resfn, fsFromStart, (*map)->rh.rdatoff + newloc));
        if(LM(ResErr) != noErr)
            return;
        lc = sizeof(LONGINT);
        swappedrsize = rsize;
        ROMlib_setreserr(FSWriteAll((*map)->resfn, inout(lc), (Ptr)&swappedrsize));
        if(LM(ResErr) != noErr)
            return;
        lc = rsize;
        ROMlib_setreserr(FSWriteAll((*map)->resfn, inout(lc), *res));
        if(LM(ResErr) != noErr)
            return;
        rr->ratr &= ~resChanged;
    }
    else
        ROMlib_setreserr(noErr);
}

static void fillst(sorttypehand st, resref *rp, resref *rep)
{
    res_sorttype_t *end = *st, *sp;
    LONGINT newoff;

    end++->diskoff = -1; /* real disk offsets are always > 0 */
    while(rp != rep)
    {
        newoff = B3TOLONG(rp->doff);
        for(sp = end - 1; newoff < sp->diskoff; sp--)
            ;
        sp++;
        BlockMoveData((Ptr)sp, (Ptr)(sp + 1), (Size)(end - sp) * sizeof(res_sorttype_t));
        sp->diskoff = newoff;
        sp->rrptr = rp;
        end++;
        rp++;
    }
}

/* MacPhoenix: the data compacted in memory, then written: each resource's,
   in disk order, from its handle if it changed, else from the file. Moved
   in place, a changed resource bigger than its old place overwrote the
   next ones before they were read (THINK C 6's project conversion: a
   resource grown after ChangedResource, others removed), and two entries
   sharing their data lost it. */
static LONGINT walkst(res_sorttype_t *sp, res_sorttype_t *sep, INTEGER fn,
                      LONGINT datoff)
{
    std::vector<uint8_t> data;
    std::map<LONGINT, LONGINT> moved; /* disk offset -> new offset */
    const LONGINT chunk = 16384; /* the File Manager reads and writes guest memory */
    Ptr buf = NewPtrSys(chunk);
    gui_assert(buf);

    for(; sp != sep; sp++)
    {
        resref *rr = sp->rrptr;
        Handle h = (Handle)rr->rhand;
        LONGINT doff = data.size();
        if((rr->ratr & resChanged) && h && (*h || sp->diskoff == 0xFFFFFF))
        {
            LONGINT size = *h ? GetHandleSize(h) : 0;
            GUEST<LONGINT> size_s = size;
            data.insert(data.end(), (uint8_t *)&size_s, (uint8_t *)&size_s + 4);
            data.insert(data.end(), (uint8_t *)*h, (uint8_t *)*h + size);
            rr->ratr &= ~resChanged;
        }
        else if(sp->diskoff == 0xFFFFFF)
            continue; /* never written and nothing to write */
        else if(auto it = moved.find(sp->diskoff); it != moved.end())
            doff = it->second;
        else
        {
            GUEST<LONGINT> size_s;
            LONGINT lc = sizeof size_s;
            SetFPos(fn, fsFromStart, datoff + sp->diskoff);
            FSReadAll(fn, inout(lc), (Ptr)&size_s);
            LONGINT size = size_s;
            gui_assert(lc == sizeof size_s && size >= 0);
            data.resize(doff + 4 + size);
            memcpy(&data[doff], &size_s, 4);
            for(LONGINT done = 0; done < size; done += lc)
            {
                lc = std::min<LONGINT>(size - done, chunk);
                FSReadAll(fn, inout(lc), buf);
                gui_assert(lc > 0);
                memcpy(&data[doff + 4 + done], buf, lc);
            }
            moved[sp->diskoff] = doff;
        }
        B3ASSIGN(rr->doff, doff);
    }
    SetFPos(fn, fsFromStart, datoff);
    for(size_t done = 0; done < data.size();)
    {
        LONGINT lc = std::min<size_t>(data.size() - done, chunk);
        memcpy(buf, &data[done], lc);
        FSWriteAll(fn, inout(lc), buf);
        gui_assert(lc > 0);
        done += lc;
    }
    DisposePtr(buf);
    return data.size();
}

static void compactdata(resmaphand map)
{
    INTEGER nres;
    LONGINT resoff, datlen;
    sorttypehand st;
    resref *rr;
    INTEGER mapstate, ststate;

    resoff = TYPEOFF(map) + sizeof(INTEGER) + (NUMTMINUS1(map) + 1) * sizeof(typref);
    nres = (NAMEOFF(map) - resoff) / sizeof(resref);
    st = (sorttypehand)NewHandle((Size)sizeof(res_sorttype_t) * (nres + 1));
    mapstate = HGetState((Handle)map);
    HLock((Handle)map);
    rr = (resref *)((char *)*map + resoff);
    fillst(st, rr, rr + nres);
    ststate = HGetState((Handle)st);
    HLock((Handle)st);
    datlen = walkst(*st + 1, *st + nres + 1, (*map)->resfn,
                    (*map)->rh.rdatoff);
    HSetState((Handle)st, ststate);
    HSetState((Handle)map, mapstate);
    (*map)->resfatr |= mapChanged;
    (*map)->resfatr &= ~mapCompact;
    (*map)->rh.rmapoff = sizeof(reshead) + sizeof(rsrvrec) + datlen;
    (*map)->rh.datlen = datlen;
    DisposeHandle((Handle)st);
}

void Executor::C_UpdateResFile(INTEGER rn)
{
    resmaphand map;
    INTEGER i, j;
    typref *tr;
    resref *rr;
    Boolean needtowalk;
    fcbrec *fp;
    OSErr err;
    IOParam iopb;

    map = ROMlib_rntohandl(rn, (Handle *)0);
    if(!map)
    {
        ROMlib_setreserr(resFNotFound);
        return;
    }
    ROMlib_setreserr(noErr);
    /*
     * NOTE:  This implementation looks more true to what IMI-125 implies than
     *	  the one (above) it replaces.
     */
    fp = PRNTOFPERR((*map)->resfn, &err);
    if(err == noErr && (fp->fcflags & fcwriteperm))
    {
        needtowalk = true;
        if((*map)->resfatr & mapCompact)
        {
            compactdata(map);
            needtowalk = false;
        }
        if((*map)->resfatr & mapChanged)
        {
            if(needtowalk)
            {
                WALKTANDR(map, i, tr, j, rr)
                writeResourceInternal(map, rr);
                if(LM(ResErr) != noErr)
                    return;
                EWALKTANDR(tr, rr)
            }
            ROMlib_setreserr(writemap(map));
            if(LM(ResErr) != noErr)
                return;
        }
        iopb.ioRefNum = (*map)->resfn;
        PBFlushFile((ParmBlkPtr)&iopb, false);
    }
}

void Executor::C_WriteResource(Handle res)
{
    resmaphand map;
    typref *tr;
    resref *rr;

    ROMlib_setreserr(ROMlib_findres(res, &map, &tr, &rr));
    if(LM(ResErr) != noErr)
        return;
    writeResourceInternal(map, rr);
}

void Executor::C_SetResPurge(Boolean install)
{
    /* NOP */
    ROMlib_setreserr(noErr);
}
