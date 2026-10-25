/* VolDump.c -- what the classic File Manager says about a volume, as text,
 * so Mac OS 9.0.4 and Executor can be compared (docs/executor/HFSPlus.md):
 * the volume's PBHGetVInfo, then every item in index order
 * (PBGetCatInfo ioFDirIndex 1, 2...): its name, IDs, attributes, Finder
 * info, lengths, dates; the same item found again by name, by full path
 * and through "::"; each fork opened read-only and read to the end
 * (Adler-32; one of 256 MB or more only probed: its head, its end, the
 * 2 GB - 1 cap), and opened for writing (the error a locked volume gives);
 * the files-only enumeration of PBHGetFInfo. Names are hex (MacRoman).
 * The probes that write (create, open for writing) only on a locked
 * volume: a writable one is left as it was.
 *
 * Host:voldump.cfg names the volume (first line); the dump goes to
 * Host:voldump.txt, "---" its last line. 68k: runs on Executor and any
 * Mac OS. */
#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Files.h>
#include <Events.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;

static FILE *out;
static short vref;
static Boolean locked;      /* the volume: write probes only then */
static unsigned char volName[64];
static char *buf;
enum { kBuf = 32768 };
#define kBig 0x10000000L    /* a fork this long is probed, not read through */

static void hex(const unsigned char *p, int n)
{
    int i;
    for(i = 0; i < n; i++)
        fprintf(out, "%02X", (unsigned)p[i]);
}

static void pname(const unsigned char *p)
{
    if(p[0])
        hex(p + 1, p[0]);
    else
        fprintf(out, "-");
}

static unsigned long adler(unsigned long a, const unsigned char *p, long n)
{
    unsigned long s1 = a & 0xFFFF, s2 = a >> 16;
    long i;
    for(i = 0; i < n; i++)
    {
        s1 = (s1 + p[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    return s2 << 16 | s1;
}

/* path + ":" + name, as a Pascal string (empty if it won't fit) */
static void join(unsigned char *dst, const unsigned char *path, const unsigned char *name)
{
    if(path[0] + 1 + name[0] > 255)
    {
        dst[0] = 0;
        return;
    }
    memcpy(dst, path, path[0] + 1);
    dst[++dst[0]] = ':';
    memcpy(dst + dst[0] + 1, name + 1, name[0]);
    dst[0] += name[0];
}

static void fork(long dirID, StringPtr name, Boolean rsrc)
{
    HParamBlockRec pb;
    OSErr err;
    long eof = 0, total = 0;
    unsigned long sum = 1;
    short ref;

    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioNamePtr = name;
    pb.ioParam.ioVRefNum = vref;
    pb.fileParam.ioDirID = dirID;
    pb.ioParam.ioPermssn = fsRdPerm;
    err = rsrc ? PBHOpenRFSync(&pb) : PBHOpenDFSync(&pb);
    fprintf(out, "  %s open %d", rsrc ? "rsrc" : "data", (int)err);
    if(err == noErr)
    {
        ref = pb.ioParam.ioRefNum;
        err = GetEOF(ref, &eof);
        if(eof >= kBig || eof < 0 || err)
        {
            /* a big fork: its head, its last 5000 bytes, the 2 GB - 1 cap */
            long n = kBuf, pos = 0;
            fprintf(out, " geteof %d eof %ld", (int)err, eof);
            err = FSRead(ref, &n, buf);
            fprintf(out, " head %d/%ld %08lX", (int)err, n, adler(1, (unsigned char *)buf, n));
            err = SetFPos(ref, fsFromStart, eof - 5000);
            n = kBuf;
            fprintf(out, " tailpos %d", (int)err);
            err = FSRead(ref, &n, buf);
            GetFPos(ref, &pos);
            fprintf(out, " tail %d/%ld %08lX at %ld", (int)err, n, adler(1, (unsigned char *)buf, n), pos);
            err = SetFPos(ref, fsFromStart, 0x7FFFFFFF);
            n = 16;
            fprintf(out, " cap %d", (int)err);
            err = FSRead(ref, &n, buf);
            fprintf(out, " capread %d/%ld", (int)err, n);
            err = SetFPos(ref, fsFromLEOF, 0);
            GetFPos(ref, &pos);
            fprintf(out, " leof %d at %ld", (int)err, pos);
        }
        else
        {
            for(;;)
            {
                long n = kBuf;
                err = FSRead(ref, &n, buf);
                sum = adler(sum, (unsigned char *)buf, n);
                total += n;
                if(err != noErr)
                    break;
            }
            fprintf(out, " eof %ld read %ld end %d adler %08lX", eof, total, (int)err, sum);
        }
        /* a second path while this one is open, and past the end */
        {
            long n = 16;
            SetFPos(ref, fsFromStart, eof);
            err = FSRead(ref, &n, buf);
            fprintf(out, " pastend %d/%ld", (int)err, n);
            err = SetFPos(ref, fsFromStart, eof + 1);
            fprintf(out, " seekpast %d", (int)err);
            n = 4;
            if(locked)
            {
                err = FSWrite(ref, &n, buf);
                fprintf(out, " write %d", (int)err);
            }
        }
        FSClose(ref);
    }
    if(!locked)                 /* on a writable volume the probes would change it */
    {
        fprintf(out, "\n");
        return;
    }
    pb.ioParam.ioPermssn = fsRdWrPerm;
    err = rsrc ? PBHOpenRFSync(&pb) : PBHOpenDFSync(&pb);
    fprintf(out, " rdwr %d", (int)err);
    if(err == noErr)
    {
        /* a write path on a locked volume: what writing through it gives */
        long n = 4;
        ref = pb.ioParam.ioRefNum;
        err = FSWrite(ref, &n, buf);
        fprintf(out, " write %d/%ld", (int)err, n);
        err = SetEOF(ref, eof + 1);
        fprintf(out, " seteof %d", (int)err);
        n = 1024;
        err = Allocate(ref, &n);
        fprintf(out, " allocate %d", (int)err);
        FSClose(ref);
    }
    fprintf(out, "\n");
}

static void item(CInfoPBRec *c, long dirID, const unsigned char *path, const unsigned char *name)
{
    Boolean dir = (c->hFileInfo.ioFlAttrib & 0x10) != 0;
    CInfoPBRec again;
    HParamBlockRec hp;
    unsigned char full[256], other[64];
    OSErr err;

    fprintf(out, "%s ", dir ? "DIR " : "FILE");
    pname(name);
    fprintf(out, " id %ld par %ld attrib %02X", (long)c->hFileInfo.ioDirID, (long)c->hFileInfo.ioFlParID,
            (unsigned)(unsigned char)c->hFileInfo.ioFlAttrib);
    if(dir)
    {
        fprintf(out, " valence %u dinfo ", (unsigned)c->dirInfo.ioDrNmFls);
        hex((unsigned char *)&c->dirInfo.ioDrUsrWds, 16);
        fprintf(out, " dxinfo ");
        hex((unsigned char *)&c->dirInfo.ioDrFndrInfo, 16);
        fprintf(out, " cr %lu md %lu bk %lu\n", c->dirInfo.ioDrCrDat, c->dirInfo.ioDrMdDat, c->dirInfo.ioDrBkDat);
    }
    else
    {
        fprintf(out, " finfo ");
        hex((unsigned char *)&c->hFileInfo.ioFlFndrInfo, 16);
        fprintf(out, " fxinfo ");
        hex((unsigned char *)&c->hFileInfo.ioFlXFndrInfo, 16);
        fprintf(out, " data %ld/%ld rsrc %ld/%ld cr %lu md %lu bk %lu clp %ld\n",
                c->hFileInfo.ioFlLgLen, c->hFileInfo.ioFlPyLen, c->hFileInfo.ioFlRLgLen, c->hFileInfo.ioFlRPyLen,
                c->hFileInfo.ioFlCrDat, c->hFileInfo.ioFlMdDat, c->hFileInfo.ioFlBkDat, c->hFileInfo.ioFlClpSiz);
    }

    /* by name in its folder */
    memset(&again, 0, sizeof again);
    memcpy(other, name, name[0] + 1);
    again.hFileInfo.ioNamePtr = other;
    again.hFileInfo.ioVRefNum = vref;
    again.hFileInfo.ioDirID = dirID;
    err = PBGetCatInfoSync(&again);
    fprintf(out, "  byname %d id %ld", (int)err, err ? 0L : (long)again.hFileInfo.ioDirID);

    /* by full path, the volume found by name */
    join(full, path, name);
    if(full[0])
    {
        memset(&again, 0, sizeof again);
        again.hFileInfo.ioNamePtr = full;
        err = PBGetCatInfoSync(&again);
        fprintf(out, " bypath %d id %ld", (int)err, err ? 0L : (long)again.hFileInfo.ioDirID);
    }

    /* the same name in upper case: HFS names don't care */
    {
        int i;
        memcpy(other, name, name[0] + 1);
        for(i = 1; i <= other[0]; i++)
            if(other[i] >= 'a' && other[i] <= 'z')
                other[i] -= 32;
        memset(&again, 0, sizeof again);
        again.hFileInfo.ioNamePtr = other;
        again.hFileInfo.ioVRefNum = vref;
        again.hFileInfo.ioDirID = dirID;
        err = PBGetCatInfoSync(&again);
        fprintf(out, " upper %d id %ld", (int)err, err ? 0L : (long)again.hFileInfo.ioDirID);
    }

    /* a folder: itself by ioFDirIndex -1 (its name comes back), and its
       parent through "::" */
    if(dir)
    {
        memset(&again, 0, sizeof again);
        other[0] = 0;
        again.hFileInfo.ioNamePtr = other;
        again.hFileInfo.ioVRefNum = vref;
        again.hFileInfo.ioDirID = c->hFileInfo.ioDirID;
        again.hFileInfo.ioFDirIndex = -1;
        err = PBGetCatInfoSync(&again);
        fprintf(out, " self %d ", (int)err);
        pname(other);
        memset(&again, 0, sizeof again);
        again.hFileInfo.ioNamePtr = (StringPtr) "\p::";
        again.hFileInfo.ioVRefNum = vref;
        again.hFileInfo.ioDirID = c->hFileInfo.ioDirID;
        err = PBGetCatInfoSync(&again);
        fprintf(out, " up %d id %ld", (int)err, err ? 0L : (long)again.hFileInfo.ioDirID);
    }
    else
    {
        /* PBHGetFInfo, and FSMakeFSSpec */
        FSSpec spec;
        memset(&hp, 0, sizeof hp);
        hp.fileParam.ioNamePtr = (StringPtr)name;
        hp.fileParam.ioVRefNum = vref;
        hp.fileParam.ioDirID = dirID;
        err = PBHGetFInfoSync(&hp);
        fprintf(out, " hgetfinfo %d", (int)err);
        err = FSMakeFSSpec(vref, dirID, name, &spec);
        fprintf(out, " fsspec %d par %ld ", (int)err, err ? 0L : spec.parID);
        if(err == noErr)
            pname(spec.name);
    }
    fprintf(out, "\n");
}

static void walk(long dirID, const unsigned char *path)
{
    short i;
    int files = 0;
    for(i = 1;; i++)
    {
        CInfoPBRec c;
        unsigned char name[256], sub[256];
        OSErr err;

        memset(&c, 0, sizeof c);
        name[0] = 0;
        c.hFileInfo.ioNamePtr = name;
        c.hFileInfo.ioVRefNum = vref;
        c.hFileInfo.ioDirID = dirID;
        c.hFileInfo.ioFDirIndex = i;
        err = PBGetCatInfoSync(&c);
        if(err == fnfErr)
            break;
        if(err != noErr)
        {
            fprintf(out, "INDEX %d err %d\n", i, (int)err);
            break;
        }
        item(&c, dirID, path, name);
        if(c.hFileInfo.ioFlAttrib & 0x10)
        {
            join(sub, path, name);
            if(sub[0])
                walk(c.dirInfo.ioDrDirID, sub);
        }
        else
        {
            files++;
            fork(dirID, name, false);
            fork(dirID, name, true);
        }
    }
    /* PBHGetFInfo by index counts files only */
    for(i = 1;; i++)
    {
        HParamBlockRec hp;
        unsigned char name[256];
        memset(&hp, 0, sizeof hp);
        name[0] = 0;
        hp.fileParam.ioNamePtr = name;
        hp.fileParam.ioVRefNum = vref;
        hp.fileParam.ioDirID = dirID;
        hp.fileParam.ioFDirIndex = i;
        if(PBHGetFInfoSync(&hp) != noErr)
            break;
    }
    fprintf(out, "FOLDER %ld catinfo-files %d finfo-files %d\n", dirID, files, i - 1);
}

int main(void)
{
    HParamBlockRec pb;
    FILE *cfg;
    char line[64];
    unsigned char vname[66], path[256];
    OSErr err;
    int n;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    InitCursor();

    buf = NewPtr(kBuf);
    out = fopen("Host:voldump.txt", "w");
    if(!out || !buf)
        return 1;
    cfg = fopen("Host:voldump.cfg", "r");
    if(!cfg || !fgets(line, sizeof line, cfg))
    {
        fprintf(out, "no Host:voldump.cfg\n---\n");
        fclose(out);
        return 1;
    }
    fclose(cfg);
    n = strcspn(line, "\r\n");
    line[n] = 0;
    volName[0] = n;
    memcpy(volName + 1, line, n);
    memcpy(vname, volName, n + 1);
    vname[++vname[0]] = ':';

    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioNamePtr = vname;
    pb.volumeParam.ioVolIndex = -1;
    err = PBHGetVInfoSync(&pb);
    fprintf(out, "VOLUME %d ", (int)err);
    pname(volName);
    if(err == noErr)
    {
        vref = pb.volumeParam.ioVRefNum;
        locked = (pb.volumeParam.ioVAtrb & 0x8080) != 0;
        fprintf(out, " sig %04X attrib %04X nmfls %u alblksiz %ld nmalblks %u frblk %u clpsiz %ld"
                " filcnt %ld dircnt %ld nxtcnid %ld cr %lu md %lu bk %lu fndr ",
                (unsigned)pb.volumeParam.ioVSigWord, (unsigned)pb.volumeParam.ioVAtrb,
                (unsigned)pb.volumeParam.ioVNmFls, pb.volumeParam.ioVAlBlkSiz,
                (unsigned)pb.volumeParam.ioVNmAlBlks, (unsigned)pb.volumeParam.ioVFrBlk,
                pb.volumeParam.ioVClpSiz, pb.volumeParam.ioVFilCnt, pb.volumeParam.ioVDirCnt,
                pb.volumeParam.ioVNxtCNID, pb.volumeParam.ioVCrDate, pb.volumeParam.ioVLsMod,
                pb.volumeParam.ioVBkUp);
        hex((unsigned char *)pb.volumeParam.ioVFndrInfo, 32);
        fprintf(out, "\n");

        /* a file there: created? (wPrErr: locked) */
        if(locked)
        {
            err = HCreate(vref, fsRtDirID, "\pVolDump probe", 'TEXT', 'ttxt');
            fprintf(out, "CREATE %d\n", (int)err);
            if(err == noErr)
                HDelete(vref, fsRtDirID, "\pVolDump probe");
            err = DirCreate(vref, fsRtDirID, "\pVolDump folder", (long *)&n);
            fprintf(out, "DIRCREATE %d\n", (int)err);
        }

        memcpy(path, volName, volName[0] + 1);
        walk(fsRtDirID, path);
    }
    fprintf(out, "---\n");
    fclose(out);
    return 0;
}
