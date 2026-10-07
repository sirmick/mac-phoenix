/* Copyright 1992 - 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <OSUtil.h>
#include <FileMgr.h>
#include <SegmentLdr.h>
#include <ToolboxEvent.h>
#include <MemoryMgr.h>
#include <SysErr.h>
#include <DialogMgr.h>
#include <ResourceMgr.h>

#include <hfs/hfs.h>
#include <file/file.h>
#include <hfs/partition.h>
#include <algorithm>

#if defined(MSDOS) || defined(CYGWIN32)
#include "dosdisk.h"
#include "aspi.h"
#elif defined(_WIN32)
// ### TODO: new win32 OS code does not yet
// include the direct disk access stuff
#else
#include <unistd.h>
#include <sys/socket.h>
#include <net/route.h>
#include <net/if.h>
#if defined(MACOSX_)
#include <sys/disk.h>
#endif
#endif
#include <rsys/unixio.h>

using namespace Executor;

void Executor::ROMlib_hfsinit(void)
{
}

/*
 * NOTE: The weird name constructed below is inherited from HFS_XFer.util.c,
 *	 which is an ARDI written NEXTSTEP atrocity.
 */

#if !defined(__linux__) && !defined(__APPLE__)
#define EJECTABLE(buf) false
#else
/* #warning this is not the proper way to tell if something is ejectable */
#define EJECTABLE(buf) (buf[strlen(buf) - 3] == 'f' && buf[strlen(buf) - 2] == 'd')
#endif

#define ASSIGN_NAME_MODE_STRING(buf, lenp, filename, sbuf)                       \
    do                                                                           \
    {                                                                            \
        char ejectable;                                                          \
                                                                                 \
        ejectable = EJECTABLE(filename);                                         \
        *(lenp) = strlen((filename)) + 1 + 6 + 1 + 1 + 1;                        \
        (buf) = (char *)alloca(*(lenp));                                         \
        sprintf((buf), "%s%c%06o%c%c", (filename), 0, (sbuf).st_mode & 07777, 0, \
                ejectable);                                                      \
    } while(0)

#define NRETRIES 5

#if !defined(MSDOS) && !defined(CYGWIN32)
[[maybe_unused]]
#endif
static void eject_floppy_notify(void)
{

    /*
 * We make the test for 'ALRT' because when Executor shuts down, an ejectable
 * volume may be unmounted after the volume with the System file has already
 * been unmounted.  In the past, this caused a crash.  It may make sense to
 * deliberately unmount last the volume containing System, but that's a fix
 * for another day.
 */

    if(LM(WWExist) == EXIST_YES && GetResource("ALRT"_4, EJECTALERTID))
        Alert(EJECTALERTID, nullptr);
}

OSErr Executor::ROMlib_ejectfloppy(LONGINT floppyfd)
{
    OSErr err;

    err = noErr;
#if defined(MSDOS) || defined(CYGWIN32)
    if(floppyfd == -1 || (floppyfd & (DOSFDBIT | ASPIFDBIT)))
    {
        if(floppyfd != -1)
        {
            if(floppyfd & DOSFDBIT)
            {
                floppyfd &= ~DOSFDBIT;
                dosdisk_close(floppyfd, true);
            }
        }
        eject_floppy_notify();
    }
    else
    {
#endif
#if defined(MACOSX_)
        if(floppyfd != -1 && ioctl(floppyfd, DKIOCEJECT, (char *)0) < 0)
        {
            fprintf(stderr, "couldn't eject disk\n");
            err = ioErr;
        }
#endif
        if(floppyfd != -1)
            close(floppyfd);
#if defined(__linux__) || defined(MACOSX_)
        eject_floppy_notify();
#endif
#if defined(MSDOS) || defined(CYGWIN32)
    }
#endif
    return err;
}

void Executor::ROMlib_OurClose(void)
{
    HVCB *vcbp, *next;
    ParamBlockRec pbr;

    for(vcbp = (HVCB *)LM(VCBQHdr).qHead; vcbp; vcbp = next)
    {
        next = (HVCB *)vcbp->qLink;
        pbr.ioParam.ioNamePtr = 0;
        if(vcbp->vcbCTRef)
        {
            pbr.ioParam.ioVRefNum = vcbp->vcbVRefNum;
            PBUnmountVol(&pbr);
#if defined(MACOSX_) || defined(MACOSX_)
            if(!(vcbp->vcbAtrb & VNONEJECTABLEBIT) && vcbp->vcbDrvNum)
                ROMlib_ejectfloppy(((VCBExtra *)vcbp)->u.hfs.fd);
#endif
        }
        else
            ;   // ### TODO
    }
}

#if 0
static Boolean isejectable(const charCx( *dname), LONGINT fd)
{
    Boolean retval;
#if defined(MACOSX_) || defined(MACOSX_)
    struct scsi_req sr;
    char inqbuf[sizeof(struct inquiry_reply) + 3];
    struct inquiry_replyCx( *inqp);
    const charCx( *p);
#endif

    /* look for rfd[0-9] */
    retval = false;
#if defined(MACOSX_) || defined(__APPLE__)
    for (p = dname; p = index(p, 'r'); ++p) {
	if (p[1] == 'f' && p[2] == 'd' && isdigit(p[3])) {
	    retval = true;
/*-->*/	    break;
	}
    }
    if (!retval) {
	inqp = (struct inquiry_reply *) (((LONGINT) inqbuf + 3) / 4Cx( * 4));
	bzero (&sr, sizeof(sr));
	sr.sr_cdb.cdb_c6.c6_opcode = C6OP_INQUIRY;
	sr.sr_cdb.cdb_c6.c6_len	   = sizeofCx((*inqp));
	sr.sr_dma_dir	           = SR_DMA_RD;
	sr.sr_addr	           = (caddr_t) inqp;
	sr.sr_dma_max	           = sr.sr_cdb.cdb_c6.c6_len;
	sr.sr_ioto	           = 1;
	if (ioctl(fd, SGIOCREQ, &sr) == 0 && sr.sr_io_status == 0 &&
							    inqp->ir_removable)
	    retval = true;
    }
#endif
    return retval;
}
#endif

/*
 * NOTE: messp below points to the longint that will be filled in with
 *	 the status of the first disk insert event that we create, but
 *	 because we can have multiple drives mounted on one physical
 *	 drive, we wind put using PostEvent for all but the first.
 *
 *	 The above method sounds pretty hacky to me, and should probably
 *	 be replaced once we find a cheaper way to be notified that
 *	 a disk can be read (probably use a signal on the pipe).
 */

static LONGINT try_to_open_disk(const char *dname, LONGINT *bsizep,
                                LONGINT *maxbytesp, drive_flags_t *flagsp,
                                uint32_t *offsetp)
{
    LONGINT floppyfd;
    int len;

    *flagsp = 0;
    len = strlen(dname) + 1; /* first component: name */
    len += strlen(dname + len) + 1; /* second component: permission */
    if(!dname[len])
        *flagsp |= DRIVE_FLAGS_FIXED; /* third component: ejectable */

#if !defined(CYGWIN32)
#define EXTRA_BITS O_EXCL
#else
#define EXTRA_BITS 0
#endif


    // MacPhoenix: images shared with the ROM emulator are mounted locked.
    floppyfd = ROMlib_readonly_images ? -1
                                      : open(dname, O_BINARY | O_RDWR | EXTRA_BITS, 0000);

    if(floppyfd < 0)
    {
        *flagsp |= DRIVE_FLAGS_LOCKED;
        floppyfd = open(dname, O_BINARY | O_RDONLY | EXTRA_BITS, 0000);
    }

    if(floppyfd >= 0)
    {
        struct stat sbuf;

        *bsizep = PHYSBSIZE;
        *maxbytesp = 1024L * 1024;

        if(fstat(floppyfd, &sbuf) >= 0 && (S_IFREG & sbuf.st_mode))
            *offsetp = sbuf.st_size % PHYSBSIZE;
        else
            *offsetp = 0;
    }

    return floppyfd;
}

static LONGINT
read_driver_block_size(LONGINT fd, LONGINT bsize, LONGINT maxbytes,
                       char aligned_buf[])
{
    LONGINT retval;

    retval = PHYSBSIZE;
    if(ROMlib_readwrite(fd, aligned_buf, PHYSBSIZE, 0, reading, bsize,
                        maxbytes)
       == noErr)
    {
        if(aligned_buf[0] == 0x45 && aligned_buf[1] == 0x52)
        {
            retval = (unsigned short)(*(GUEST<uint16_t> *)&aligned_buf[2]);
            warning_fs_log("fd = 0x%x, block size = %d", fd, retval);
        }
    }
    return retval;
}

static bool
charcmp(char c1, char c2)
{
    bool retval;

    if(c1 == c2)
        retval = true;
    else if(c1 == '/')
        retval = c2 == '\\';
    else if(c1 == '\\')
        retval = c2 == '/';
    else
        retval = tolower(c1) == tolower(c2);
    return retval;
}

static int
slashstrcmp(const char *p1, const char *p2)
{
    int retval;

    retval = 0;

    while(*p1 || *p2)
    {
        if(!charcmp(*p1, *p2))
        {
            retval = -1;
            break;
        }
        ++p1;
        ++p2;
    }
    return retval;
}

static INTEGER ROMlib_driveno = 3;
static INTEGER ROMlib_ejdriveno = 2;

/*
 * NOTE: The way we handle drive information is pretty messed up right now.
 * In general the correct information is in the VCBExtra; we only recently
 * began putting it in the DriveExtra and right now we only use the info
 * in the DriveExtra to allow us to format floppies -- no other formatting
 * is currently permitted.  The problem is there's no easy way to map drive
 * characteristics from the non-Mac host into Mac type information unless
 * we can pull the information out of the Mac filesystem.
 */

DrvQExtra *
Executor::ROMlib_addtodq(ULONGINT drvsize, const char *devicename, INTEGER partition,
                         INTEGER drefnum, drive_flags_t flags, hfs_access_t *hfsp)
{
    INTEGER dno;
    DrvQExtra *dqp;
    DrvQEl *dp;
    int strl;
    GUEST<THz> saveZone;
    static bool seen_floppy = false;

    saveZone = LM(TheZone);
    LM(TheZone) = LM(SysZone);
#if !defined(LETGCCWAIL)
    dqp = (DrvQExtra *)0;
#endif
    dno = 0;
    for(dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
    {
        dqp = (DrvQExtra *)((char *)dp - sizeof(LONGINT));
        if(dqp->partition == partition && slashstrcmp((char *)dqp->devicename, devicename) == 0)
        {
            dno = dqp->dq.dQDrive;
            /*-->*/ break;
        }
    }
    if(!dno)
    {
        if((flags & DRIVE_FLAGS_FLOPPY) && !seen_floppy)
        {
            dno = 1;
            seen_floppy = true;
        }
        else
        {
            if((flags & DRIVE_FLAGS_FIXED) || ROMlib_ejdriveno == 3)
                dno = ROMlib_driveno++;
            else
                dno = ROMlib_ejdriveno++;
        }
        dqp = (DrvQExtra *)NewPtr(sizeof(DrvQExtra));
        dqp->flags = 1 << 7; /* is not single sided */
        if(flags & DRIVE_FLAGS_LOCKED)
            dqp->flags = dqp->flags | 1L << 31;
        if(flags & DRIVE_FLAGS_FIXED)
            dqp->flags = dqp->flags | 8L << 16;
        else
            dqp->flags = dqp->flags | 2; /* IMIV-181 says
							   it can be 1 or 2 */

        /*	dqp->dq.qLink will be set up when we Enqueue this baby */
        dqp->dq.dQDrvSz = drvsize;
        dqp->dq.dQDrvSz2 = drvsize >> 16;
        dqp->dq.qType = 1;
        dqp->dq.dQDrive = dno;
        dqp->dq.dQRefNum = drefnum;
        dqp->dq.dQFSID = 0;
        if(!devicename)
            dqp->devicename = 0;
        else
        {
            strl = strlen(devicename);
            dqp->devicename = NewPtr(strl + 1);
            strcpy((char *)dqp->devicename, devicename);
        }
        dqp->partition = partition;
        if(hfsp)
            dqp->hfs = *hfsp;
        else
        {
            memset(&dqp->hfs, 0, sizeof(dqp->hfs));
            dqp->hfs.fd = -1;
        }
        Enqueue((QElemPtr)&dqp->dq, &LM(DrvQHdr));
    }
    LM(TheZone) = saveZone;
    return dqp;
}

void
Executor::try_to_mount_disk(const char *dname, LONGINT floppyfd, GUEST<LONGINT> *messp,
                            LONGINT bsize, LONGINT maxbytes, drive_flags_t flags,
                            uint32_t offset_in)
{
    INTEGER partition;
    ParamBlockRec pb;
    char *tbuf, *buf;
    INTEGER drivenum;
    LONGINT offset;
    Boolean foundmap, first;
    oldblock1_t *oldmapp;
    partmapentry_t *partp;
    int i;
    LONGINT mess;
    OSErr err;
    DrvQExtra *dqp;
    hfs_access_t hfs;
    LONGINT driver_block_size;

    *messp = 0;
    tbuf = (char *)alloca(bsize + 3);
    buf = (char *)(((uintptr_t)tbuf + 3) & ~3);
    partition = 0;

    if(floppyfd >= 0)
        driver_block_size = read_driver_block_size(floppyfd, bsize, maxbytes,
                                                   buf);
    else
        driver_block_size = PHYSBSIZE;

    hfs.fd = (flags & DRIVE_FLAGS_FLOPPY) ? floppyfd : -1;
    hfs.offset = offset_in;
    hfs.bsize = bsize;
    hfs.maxbytes = maxbytes;

    dqp = ROMlib_addtodq(2048L * 2, dname, partition, OURHFSDREF, flags,
                         &hfs);

    if(floppyfd < 0)
        /*-->*/ return;

    drivenum = dqp->dq.dQDrive;

    pb.ioParam.ioVRefNum = drivenum;

    foundmap = false;
    first = true;
    offset = hfs.offset + PARTOFFSET * driver_block_size;
    if(ROMlib_readwrite(floppyfd, buf, PHYSBSIZE, offset, reading,
                        bsize, maxbytes)
       == noErr)
    {
        if(buf[0] == PARMAPSIG0 && buf[1] == PARMAPSIG1)
        {
            warning_fs_log("found partition sig at %d, floppyfd = 0x%x",
                           offset, floppyfd);
            partp = (partmapentry_t *)buf;
            do
            {
                if(strncmp((char *)partp->pmPartType, HFSPARTTYPE, 32)
                   == 0)
                {
                    foundmap = true;
                    if(!first)
                    {
                        ++partition;
                        dqp = ROMlib_addtodq(2048L * 2, dname,
                                             partition, OURHFSDREF, flags,
                                             &hfs);
                        drivenum = dqp->dq.dQDrive;
                        pb.ioParam.ioVRefNum = drivenum;
                    }
                    dqp->hfs.offset = hfs.offset + (partp->pmPyPartStart
                                                    * driver_block_size);
                    err = hfsPBMountVol(&pb, floppyfd, dqp->hfs.offset, bsize,
                                        maxbytes, flags, dqp);
                    mess = ((LONGINT)err << 16) | drivenum;
                    if(first)
                    {
                        *messp = mess;
                        first = false;
                    }
                    else
                        PPostEvent(diskEvt, mess, (GUEST<EvQElPtr> *)0);
                }
                offset += driver_block_size;
            } while(ROMlib_readwrite(floppyfd, buf, PHYSBSIZE, offset,
                                     reading, bsize, maxbytes)
                        == noErr
                    && buf[0] == PARMAPSIG0 && buf[1] == PARMAPSIG1);
        }
        else if(buf[0] == OLDMAPSIG0 && buf[1] == OLDMAPSIG1)
        {
            oldmapp = (oldblock1_t *)buf;
            for(i = 0; i < NOLDENTRIES && (oldmapp->oldmapentry[i].pdStart != 0 || oldmapp->oldmapentry[i].pdSize != 0 || oldmapp->oldmapentry[i].pdFSID != 0); ++i)
            {
                /*
* NOTE: We initially tried looking for 'TFS1' in pdFSID, but our Cirrus 80
*	 didn't use that id.
*/
                if(!first)
                {
                    ++partition;
                    dqp = ROMlib_addtodq(2048L * 2, dname, partition,
                                         OURHFSDREF, flags, &hfs);
                    drivenum = dqp->dq.dQDrive;
                    pb.ioParam.ioVRefNum = drivenum;
                }
                dqp->hfs.offset = hfs.offset + (oldmapp->oldmapentry[i].pdStart
                                                * driver_block_size);
                err = hfsPBMountVol(&pb, floppyfd, dqp->hfs.offset,
                                    bsize, maxbytes, flags, dqp);
                mess = ((LONGINT)err << 16) | drivenum;
                if(first)
                {
                    *messp = mess;
                    first = false;
                }
                else
                    PPostEvent(diskEvt, mess, (GUEST<EvQElPtr> *)0);
            }
            foundmap = true;
        }
    }

    if(!foundmap)
    {
        for(offset = hfs.offset + VOLUMEINFOBLOCKNO * PHYSBSIZE, i = 4;
            --i >= 0; offset += PHYSBSIZE)
        {
            if(i == 0)
            {
                if(ROMlib_magic_offset == -1)
                    /*-->*/ continue;
                else
                    offset = ROMlib_magic_offset;
            }
            err = ROMlib_readwrite(floppyfd, buf, PHYSBSIZE, offset, reading,
                                   bsize, maxbytes);
            if(err != noErr)
            {
                warning_unexpected("fd = 0x%x err = %d, offset = %d, "
                                   "bsize = %d, maxbytes = %d",
                                   floppyfd,
                                   err, offset, bsize, maxbytes);
                /*-->*/ break;
            }
            if(buf[0] == 'B' && buf[1] == 'D')
            {
                warning_fs_log("Found HFS volume on 0x%x %d", floppyfd,
                               offset);
                offset -= VOLUMEINFOBLOCKNO * PHYSBSIZE;
                foundmap = true;
                /*-->*/ break;
            }
            else if(buf[0] == 'H' && buf[1] == '+')
            {
                warning_fs_log("Found HFS+ volume on 0x%x %d", floppyfd,
                               offset);
            }
            else
            {
                warning_fs_log("fd = 0x%x, offset = %d, sig = 0x%02x%02x",
                               floppyfd, offset, buf[0], buf[1]);
            }
        }
        if(foundmap)
        {
            dqp->hfs.offset = offset;
            err = hfsPBMountVol(&pb, floppyfd, offset, bsize, maxbytes,
                                flags, dqp);
            *messp = ((LONGINT)err << 16) | drivenum;
        }
    }
}

void Executor::ROMlib_openfloppy(const char *dname, GUEST<LONGINT> *messp)
{
    LONGINT floppyfd;
    LONGINT bsize, maxbytes;
    drive_flags_t flags;
    uint32_t offset;

    *messp = 0;
    floppyfd = try_to_open_disk(dname, &bsize, &maxbytes, &flags, &offset);
    if(floppyfd >= 0)
        try_to_mount_disk(dname, floppyfd, messp, bsize, maxbytes, flags,
                          offset);
}

bool Executor::ROMlib_readonly_images = false;

void Executor::ROMlib_openharddisk(const char *dname, GUEST<LONGINT> *messp)
{
    char *newbuf;
    long len;
    struct stat sbuf;

    *messp = 0;
    if(stat(dname, &sbuf) == 0)
    {
        ASSIGN_NAME_MODE_STRING(newbuf, &len, dname, sbuf);
        ROMlib_openfloppy(newbuf, messp);
    }
}

#define JUMPTODONEIF(x) \
    if((x))             \
    {                   \
        err = ioErr;    \
        goto DONE;      \
    }

OSErr Executor::ROMlib_readwrite(LONGINT fd, char *buffer, LONGINT count,
                                 LONGINT offset, accesstype rw,
                                 LONGINT blocksize, LONGINT maxtransfer)
{
    char *newbuffer;
    LONGINT remainder, totransfer;
    Boolean needlseek;
    OSErr err;
    int (*readfp)(int fd, void *buf, int nbytes);
    int (*writefp)(int fd, const void *buf, int nbytes);
    off_t (*seekfp)(int fd, off_t where, int how);

    if(blocksize > 18 * 1024)
    {
        warning_unexpected("fd = 0x%x, block size = %d", fd, blocksize);
        return fsDSIntErr;
    }

    if(blocksize == 0)
    {
        blocksize = 2048;
        warning_unexpected("fd = 0x%x, zero block size", fd);
    }
    seekfp = (off_t(*)(int, off_t, int))lseek;
    readfp = (int (*)(int, void *, int))read;
    writefp = (int (*)(int, const void *, int))write;

    err = noErr;
    newbuffer = 0;
    needlseek = true;
    if((remainder = offset % blocksize))
    { /* |xxxDATA| */
        remainder = blocksize - remainder;
        totransfer = std::min(count, remainder);
        newbuffer = (char *)(((uintptr_t)alloca(blocksize + 3) + 3) & ~3);
        offset = offset / blocksize * blocksize;
        JUMPTODONEIF(seekfp(fd, offset, SEEK_SET) < 0)
        needlseek = false;
        JUMPTODONEIF(readfp(fd, newbuffer, blocksize) != blocksize)
        if(rw == reading)
        {
            memmove(buffer, newbuffer + blocksize - remainder, totransfer);
        }
        else
        {
            memmove(newbuffer + blocksize - remainder, buffer, totransfer);
            JUMPTODONEIF(seekfp(fd, offset, SEEK_SET) < 0)
            JUMPTODONEIF(writefp(fd, newbuffer, blocksize) != blocksize)
        }
        buffer += totransfer;
        count -= totransfer;
        offset += blocksize;
    }
    if(count >= blocksize)
    { /* |DATADATADATA...| */
        remainder = count % blocksize;
        count -= remainder;
        if(needlseek)
        {
            JUMPTODONEIF(seekfp(fd, offset, SEEK_SET) < 0)
            needlseek = false;
        }
        while(count)
        {
            totransfer = std::min(maxtransfer, count);
            if(rw == reading)
            {
                JUMPTODONEIF(readfp(fd, buffer, totransfer) != totransfer)
            }
            else
            {
                JUMPTODONEIF(writefp(fd, buffer, totransfer) != totransfer)
            }
            buffer += totransfer;
            count -= totransfer;
            offset += totransfer;
        }
        count = remainder;
    }
    if(count)
    { /* |DATAxxx| */
        if(!newbuffer)
            newbuffer = (char *)(((uintptr_t)alloca(blocksize + 3) + 3) & ~3);
        if(needlseek)
            JUMPTODONEIF(seekfp(fd, offset, SEEK_SET) < 0)
        JUMPTODONEIF(readfp(fd, newbuffer, blocksize) != blocksize)
        if(rw == reading)
        {
            memmove(buffer, newbuffer, count);
        }
        else
        {
            memmove(newbuffer, buffer, count);
            JUMPTODONEIF(seekfp(fd, offset, SEEK_SET) < 0)
            JUMPTODONEIF(writefp(fd, newbuffer, blocksize) != blocksize)
        }
    }
DONE:
    return err;
}

OSErr
Executor::ROMlib_transphysblk(hfs_access_t *hfsp, LONGINT physblock, short nphysblocks,
                              Ptr bufp, accesstype rw, GUEST<LONGINT> *actp)
{
    LONGINT fd;
    OSErr err;

#if defined(MAC)
    ioParam pb;

    pb.ioVRefNum = vcbp->vcbDrvNum;
    pb.ioRefNum = vcbp->vcbDRefNum;
    pb.ioBuffer = bufp;
    pb.ioReqCount = PHYSBSIZE * (LONGINT nphysblocks);
    pb.ioPosMode = fsFromStart;
    pb.ioPosOffset = physblock;
    err = rw == reading ? PBRead((ParmBlkPtr)&pb, false) : PBWrite((ParmBlkPtr)&pb, false);
    if(actp)
        *actp = pb.ioActCount;
#else
    fd = hfsp->fd;

    err = ROMlib_readwrite(fd, bufp,
                           (LONGINT)nphysblocks * PHYSBSIZE,
                           physblock + hfsp->offset, rw, hfsp->bsize,
                           hfsp->maxbytes);
    if(actp)
        *actp = err != noErr ? 0 : ((LONGINT)nphysblocks * PHYSBSIZE);

#endif
    if(err != noErr)
        warning_unexpected("fd = 0x%x, err in transphysblock (err = %d)",
                           fd, err);
    return err;
}

char *Executor::ROMlib_indexn(char *str, char tofind, INTEGER length)
{
    while(--length >= 0)
        if(*str++ == tofind)
            return str - 1;
    return 0;
}

#if !defined(str255assign)
void Executor::str255assign(StringPtr dstp, StringPtr srcp)
{
    memmove(dstp, srcp, (size_t)srcp[0] + 1);
}
#endif /* !defined(str255assign) */

/*
 * ROMlib_indexqueue returns a pointer to the n'th entry on a queue.
 * ROMlib_indexqueue is one based; not zero based.
 */

void *Executor::ROMlib_indexqueue(QHdr *qp, short index)
{
    QElemPtr p;

    for(p = qp->qHead; (--index > 0) && p; p = p->vcbQElem.qLink)
        ;
    return p;
}

OSErr Executor::ROMlib_writefcbp(filecontrolblock *fcbp)
{
    Byte flags;
    OSErr retval;

    flags = fcbp->fcbMdRByt;
    if(!(flags & WRITEBIT))
        retval = wrPermErr;
    else if(flags & FLOCKEDBIT)
        retval = fLckdErr;
    else
        retval = noErr;
    return retval;
}

OSErr Executor::ROMlib_writevcbp(HVCB *vcbp)
{
    INTEGER vflags;
    OSErr retval;

    vflags = vcbp->vcbAtrb;
    if(vflags & VSOFTLOCKBIT)
        retval = vLckdErr;
    else if(vflags & VHARDLOCKBIT)
        retval = wPrErr;
    else
        retval = noErr;
    return retval;
}
