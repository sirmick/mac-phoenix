/* VolWrite.c -- the classic File Manager's changes to a volume, a fixed
 * script whose every result goes to Host:volwrite.txt, so Mac OS 9.0.4 and
 * Executor can be compared on the same image (docs/executor/HFSPlus.md):
 * creating files and folders (bad names, duplicates in any case), writing
 * forks (positions, SetEOF both ways, Allocate, a second path, the
 * Resource Manager), Finder info and locks, renames, moves, deletes,
 * hundreds of files, FSpExchangeFiles. The volume is the one named in Host:volwrite.cfg; it
 * may hold, made beforehand, a folder "Long" with long-named files:
 *   "A file whose name is longer than thirty-one.txt" (opened, written and
 *   renamed by its short name) and "Another long name, to be deleted.txt".
 * A second line "big" runs big() instead: a fork near 2 GB.
 * Results: "OP ... -> err [values]" a line each, "---" last. 68k. */
#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Files.h>
#include <Resources.h>
#include <Events.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;

static FILE *out;
static short vref;
static char *buf;
enum { kBuf = 65536 };

static StringPtr P(const char *s)
{
    static unsigned char p[4][256];
    static int k;
    unsigned char *q = p[k++ & 3];
    q[0] = strlen(s);
    memcpy(q + 1, s, q[0]);
    return q;
}

static void fill(long n, int seed)
{
    long i;
    for(i = 0; i < n; i++)
        buf[i] = (char)(seed + i * 13 + i / 97);
}

static long dirIDOf(long parent, const char *name)
{
    CInfoPBRec c;
    memset(&c, 0, sizeof c);
    c.dirInfo.ioNamePtr = P(name);
    c.dirInfo.ioVRefNum = vref;
    c.dirInfo.ioDrDirID = parent;
    if(PBGetCatInfoSync(&c) != noErr)
        return 0;
    return c.dirInfo.ioDrDirID;
}

/* a file's lengths as PBGetCatInfo gives them */
static void lengths(long dir, const char *name)
{
    CInfoPBRec c;
    OSErr err;
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = P(name);
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = dir;
    err = PBGetCatInfoSync(&c);
    fprintf(out, "  info %s -> %d", name, (int)err);
    if(!err)
        fprintf(out, " attrib %02X data %ld/%ld rsrc %ld/%ld", (unsigned)(unsigned char)c.hFileInfo.ioFlAttrib,
                c.hFileInfo.ioFlLgLen, c.hFileInfo.ioFlPyLen, c.hFileInfo.ioFlRLgLen, c.hFileInfo.ioFlRPyLen);
    fprintf(out, "\n");
}

static void volume(const char *when)
{
    HParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioVRefNum = vref;
    PBHGetVInfoSync(&pb);
    fprintf(out, "VOLUME %s attrib %04X nmfls %u frblk %u filcnt %ld dircnt %ld\n", when,
            (unsigned)pb.volumeParam.ioVAtrb, (unsigned)pb.volumeParam.ioVNmFls,
            (unsigned)pb.volumeParam.ioVFrBlk, pb.volumeParam.ioVFilCnt, pb.volumeParam.ioVDirCnt);
}

static OSErr writeAt(short ref, long pos, long n, int seed)
{
    OSErr err = SetFPos(ref, fsFromStart, pos);
    long count = n;
    if(err == noErr)
    {
        fill(n, seed);
        err = FSWrite(ref, &count, buf);
    }
    fprintf(out, "  write %ld+%ld -> %d (%ld)\n", pos, n, (int)err, count);
    return err;
}

static void files(long root)
{
    OSErr err;
    short ref, ref2;
    long eof, dir, sub, n;

    fprintf(out, "CREATE plain -> %d\n", (int)HCreate(vref, root, P("Plain file"), 'TEXT', 'ttxt'));
    fprintf(out, "CREATE again -> %d\n", (int)HCreate(vref, root, P("Plain file"), 'TEXT', 'ttxt'));
    fprintf(out, "CREATE other case -> %d\n", (int)HCreate(vref, root, P("PLAIN FILE"), 'TEXT', 'ttxt'));
    fprintf(out, "CREATE 32 chars -> %d\n", (int)HCreate(vref, root, P("Thirty-two characters long name!"), 'TEXT', 'ttxt'));
    fprintf(out, "CREATE empty -> %d\n", (int)HCreate(vref, root, P(""), 'TEXT', 'ttxt'));
    fprintf(out, "CREATE accent -> %d\n", (int)HCreate(vref, root, P("Caf\216 cr\217me \321 ok"), 'TEXT', 'ttxt'));
    fprintf(out, "CREATE slash -> %d\n", (int)HCreate(vref, root, P("Slash/name"), 'TEXT', 'ttxt'));
    err = DirCreate(vref, root, P("Folder"), &dir);
    fprintf(out, "DIRCREATE Folder -> %d id %ld\n", (int)err, err ? 0L : dir);
    err = DirCreate(vref, dir, P("Sub"), &sub);
    fprintf(out, "DIRCREATE Folder:Sub -> %d\n", (int)err);
    fprintf(out, "DIRCREATE again -> %d\n", (int)DirCreate(vref, root, P("folder"), &n));
    fprintf(out, "CREATE in Sub by path -> %d\n", (int)HCreate(vref, 0, P(":Folder:Sub:Inner"), 'TEXT', 'ttxt'));

    /* writing */
    err = HOpenDF(vref, root, P("Plain file"), fsRdWrPerm, &ref);
    fprintf(out, "OPEN rdwr -> %d\n", (int)err);
    if(err == noErr)
    {
        fprintf(out, "OPEN again rdwr -> %d\n", (int)HOpenDF(vref, root, P("Plain file"), fsRdWrPerm, &ref2));
        err = HOpenDF(vref, root, P("Plain file"), fsRdPerm, &ref2);
        fprintf(out, "OPEN again read -> %d\n", (int)err);
        writeAt(ref, 0, 1000, 1);
        lengths(root, "Plain file");
        writeAt(ref, 500, 100, 2);
        writeAt(ref, 70000, 3000, 3);          /* past the end: grows */
        GetEOF(ref, &eof);
        fprintf(out, "  eof %ld\n", eof);
        if(err == noErr)
        {
            long count = 50;
            SetFPos(ref2, fsFromStart, 490);
            err = FSRead(ref2, &count, buf);
            fprintf(out, "  read other path -> %d %02X%02X\n", (int)err, (unsigned)(unsigned char)buf[9], (unsigned)(unsigned char)buf[10]);
            count = 10;
            fprintf(out, "  write read path -> %d\n", (int)FSWrite(ref2, &count, buf));
            FSClose(ref2);
        }
        fprintf(out, "  seteof 5000 -> %d\n", (int)SetEOF(ref, 5000));
        lengths(root, "Plain file");
        fprintf(out, "  seteof 200000 -> %d\n", (int)SetEOF(ref, 200000));
        lengths(root, "Plain file");
        n = 300000;
        err = Allocate(ref, &n);
        fprintf(out, "  allocate 300000 -> %d (%ld)\n", (int)err, n);
        lengths(root, "Plain file");
        fprintf(out, "  delete open -> %d\n", (int)HDelete(vref, root, P("Plain file")));
        fprintf(out, "  close -> %d\n", (int)FSClose(ref));
        lengths(root, "Plain file");
    }
    err = HOpenDF(vref, root, P("Plain file"), fsRdPerm, &ref);
    if(err == noErr)
    {
        long count = 3000;
        SetFPos(ref, fsFromStart, 4990);
        err = FSRead(ref, &count, buf);
        fprintf(out, "READ end -> %d %ld\n", (int)err, count);
        FSClose(ref);
    }

    /* how a fork grows as it is written */
    fprintf(out, "CREATE big -> %d\n", (int)HCreate(vref, root, P("Big file"), 'BINA', 'ttxt'));
    err = HOpenDF(vref, root, P("Big file"), fsRdWrPerm, &ref);
    if(err == noErr)
    {
        writeAt(ref, 0, 60000, 7);
        lengths(root, "Big file");
        writeAt(ref, 60000, 10, 8);
        lengths(root, "Big file");
        writeAt(ref, 60010, 9000, 9);
        lengths(root, "Big file");
        FSClose(ref);
        lengths(root, "Big file");
    }

    /* resource fork, by hand and through the Resource Manager */
    err = HOpenRF(vref, root, P("Plain file"), fsRdWrPerm, &ref);
    fprintf(out, "OPENRF -> %d\n", (int)err);
    if(err == noErr)
    {
        writeAt(ref, 0, 777, 4);
        FSClose(ref);
    }
    lengths(root, "Plain file");
    HCreateResFile(vref, root, P("Resource file"));
    fprintf(out, "HCREATERESFILE -> %d\n", (int)ResError());
    ref = HOpenResFile(vref, root, P("Resource file"), fsRdWrPerm);
    fprintf(out, "HOPENRESFILE -> %d\n", (int)ResError());
    if(ref != -1)
    {
        Handle h = NewHandle(2000);
        HLock(h);
        fill(2000, 5);
        BlockMoveData(buf, *h, 2000);
        HUnlock(h);
        AddResource(h, 'TEST', 128, P("test resource"));
        fprintf(out, "  addresource -> %d\n", (int)ResError());
        CloseResFile(ref);
        fprintf(out, "  closeresfile -> %d\n", (int)ResError());
        ref = HOpenResFile(vref, root, P("Resource file"), fsRdPerm);
        h = Get1Resource('TEST', 128);
        fprintf(out, "  reopened: %d size %ld\n", h != NULL, h ? GetHandleSize(h) : 0L);
        CloseResFile(ref);
    }
    lengths(root, "Resource file");

    /* Finder info, locks */
    {
        FInfo fi;
        err = HGetFInfo(vref, root, P("Plain file"), &fi);
        fi.fdType = 'APPL';
        fi.fdCreator = 'ABCD';
        fi.fdFlags = 0x2000;
        fprintf(out, "SETFINFO -> %d\n", (int)HSetFInfo(vref, root, P("Plain file"), &fi));
        err = HGetFInfo(vref, root, P("Plain file"), &fi);
        fprintf(out, "  finfo -> %d %.4s %.4s %04X\n", (int)err, (char *)&fi.fdType, (char *)&fi.fdCreator, (unsigned)fi.fdFlags);
    }
    fprintf(out, "LOCK -> %d\n", (int)HSetFLock(vref, root, P("Plain file")));
    lengths(root, "Plain file");
    fprintf(out, "  open locked rdwr -> %d\n", (int)HOpenDF(vref, root, P("Plain file"), fsRdWrPerm, &ref));
    fprintf(out, "  rename locked -> %d\n", (int)HRename(vref, root, P("Plain file"), P("Renamed plain")));
    fprintf(out, "  delete locked -> %d\n", (int)HDelete(vref, root, P("Plain file")));
    fprintf(out, "UNLOCK -> %d\n", (int)HRstFLock(vref, root, P("Plain file")));
    lengths(root, "Plain file");

    /* renames, moves */
    fprintf(out, "RENAME case only -> %d\n", (int)HRename(vref, root, P("Plain file"), P("PLAIN file")));
    fprintf(out, "RENAME to existing -> %d\n", (int)HRename(vref, root, P("PLAIN file"), P("Resource file")));
    fprintf(out, "RENAME too long -> %d\n", (int)HRename(vref, root, P("PLAIN file"), P("Thirty-two characters long name!")));
    fprintf(out, "RENAME -> %d\n", (int)HRename(vref, root, P("PLAIN file"), P("Moved file")));
    fprintf(out, "RENAME folder -> %d\n", (int)HRename(vref, root, P("Folder"), P("Renamed folder")));
    fprintf(out, "CATMOVE file -> %d\n", (int)CatMove(vref, root, P("Moved file"), dir, P("")));
    fprintf(out, "CATMOVE into own child -> %d\n", (int)CatMove(vref, root, P("Renamed folder"), sub, P("")));
    HCreate(vref, root, P("Moved file"), 'TEXT', 'ttxt');
    fprintf(out, "CATMOVE onto a name taken -> %d\n", (int)CatMove(vref, root, P("Moved file"), dir, P("")));
    fprintf(out, "DELETE folder not empty -> %d\n", (int)HDelete(vref, root, P("Renamed folder")));
    fprintf(out, "DELETE -> %d\n", (int)HDelete(vref, root, P("Moved file")));
    fprintf(out, "DELETE gone -> %d\n", (int)HDelete(vref, root, P("Moved file")));
    lengths(dir, "Moved file");

    /* SetCatInfo: dates */
    {
        CInfoPBRec c;
        memset(&c, 0, sizeof c);
        c.hFileInfo.ioNamePtr = P("Resource file");
        c.hFileInfo.ioVRefNum = vref;
        c.hFileInfo.ioDirID = root;
        PBGetCatInfoSync(&c);
        c.hFileInfo.ioFlCrDat = 3000000000UL;
        c.hFileInfo.ioFlMdDat = 3100000000UL;
        c.hFileInfo.ioDirID = root;
        err = PBSetCatInfoSync(&c);
        memset(&c, 0, sizeof c);
        c.hFileInfo.ioNamePtr = P("Resource file");
        c.hFileInfo.ioVRefNum = vref;
        c.hFileInfo.ioDirID = root;
        PBGetCatInfoSync(&c);
        fprintf(out, "SETCATINFO -> %d cr %lu md %lu\n", (int)err, c.hFileInfo.ioFlCrDat, c.hFileInfo.ioFlMdDat);
    }
}

static void longNames(long root)
{
    long dir = dirIDOf(root, "Long");
    short ref;
    OSErr err;
    unsigned char shortName[64];
    CInfoPBRec c;
    if(!dir)
    {
        fprintf(out, "LONG none\n");
        return;
    }
    /* the first item's short name, by index */
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = shortName;
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = dir;
    c.hFileInfo.ioFDirIndex = 1;
    err = PBGetCatInfoSync(&c);
    shortName[shortName[0] + 1] = 0;
    fprintf(out, "LONG first -> %d %s\n", (int)err, (char *)shortName + 1);
    err = HOpenDF(vref, dir, shortName, fsRdWrPerm, &ref);
    fprintf(out, "LONG open -> %d\n", (int)err);
    if(err == noErr)
    {
        writeAt(ref, 0, 4000, 6);
        FSClose(ref);
    }
    lengths(dir, (char *)shortName + 1);
    fprintf(out, "LONG rename -> %d\n", (int)HRename(vref, dir, shortName, P("Short now")));
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = shortName;
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = dir;
    c.hFileInfo.ioFDirIndex = 1;
    err = PBGetCatInfoSync(&c);
    shortName[shortName[0] + 1] = 0;
    fprintf(out, "LONG first now -> %d %s\n", (int)err, (char *)shortName + 1);
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = shortName;
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = dir;
    c.hFileInfo.ioFDirIndex = 2;
    err = PBGetCatInfoSync(&c);
    shortName[shortName[0] + 1] = 0;
    fprintf(out, "LONG second -> %d %s\n", (int)err, (char *)shortName + 1);
    fprintf(out, "LONG delete -> %d\n", (int)HDelete(vref, dir, shortName));
}

static void many(long root)
{
    long dir, i;
    OSErr err = DirCreate(vref, root, P("Many"), &dir);
    int bad = 0;
    char name[40];
    for(i = 0; i < 400 && err == noErr; i++)
    {
        short ref;
        sprintf(name, "Item %03ld %s", (i * 37) % 400, i % 3 ? "with a longer name" : "");
        err = HCreate(vref, dir, P(name), 'TEXT', 'ttxt');
        if(err == noErr && i % 5 == 0 && HOpenDF(vref, dir, P(name), fsRdWrPerm, &ref) == noErr)
        {
            long n = 100 + i * 10;
            fill(n, (int)i);
            FSWrite(ref, &n, buf);
            FSClose(ref);
        }
    }
    fprintf(out, "MANY create -> %d (%ld)\n", (int)err, i);
    for(i = 0; i < 400; i += 2)
    {
        sprintf(name, "Item %03ld %s", (i * 37) % 400, i % 3 ? "with a longer name" : "");
        bad += HDelete(vref, dir, P(name)) != noErr;
    }
    fprintf(out, "MANY delete half -> %d failed\n", bad);
    {
        CInfoPBRec c;
        memset(&c, 0, sizeof c);
        c.dirInfo.ioVRefNum = vref;
        c.dirInfo.ioDrDirID = dir;
        c.dirInfo.ioFDirIndex = -1;
        PBGetCatInfoSync(&c);
        fprintf(out, "MANY valence %u\n", (unsigned)c.dirInfo.ioDrNmFls);
    }
}

/* a file as the exchange left it: its ID, type, lengths, first bytes */
static void swapped(long root, const char *name, long idA, long idB)
{
    CInfoPBRec c;
    short ref;
    long n = 4;
    OSErr err;
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = P(name);
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = root;
    err = PBGetCatInfoSync(&c);
    fprintf(out, "  %s -> %d", name, (int)err);
    if(err)
    {
        fprintf(out, "\n");
        return;
    }
    fprintf(out, " id %s type %.4s creator %.4s data %ld/%ld rsrc %ld/%ld", c.hFileInfo.ioDirID == idA ? "A's" :
            c.hFileInfo.ioDirID == idB ? "B's" : "new", (char *)&c.hFileInfo.ioFlFndrInfo.fdType,
            (char *)&c.hFileInfo.ioFlFndrInfo.fdCreator, c.hFileInfo.ioFlLgLen, c.hFileInfo.ioFlPyLen,
            c.hFileInfo.ioFlRLgLen, c.hFileInfo.ioFlRPyLen);
    if(HOpenDF(vref, root, P(name), fsRdPerm, &ref) == noErr)
    {
        FSRead(ref, &n, buf);
        FSClose(ref);
        fprintf(out, " starts %02X%02X%02X%02X", (unsigned char)buf[0], (unsigned char)buf[1],
                (unsigned char)buf[2], (unsigned char)buf[3]);
    }
    fprintf(out, "\n");
}

static void makeSwap(long root, const char *name, OSType type, long data, long rsrc, int seed, long *id)
{
    short ref;
    CInfoPBRec c;
    HCreate(vref, root, P(name), type, type);
    if(HOpenDF(vref, root, P(name), fsRdWrPerm, &ref) == noErr)
    {
        writeAt(ref, 0, data, seed);
        FSClose(ref);
    }
    if(rsrc && HOpenRF(vref, root, P(name), fsRdWrPerm, &ref) == noErr)
    {
        writeAt(ref, 0, rsrc, seed + 1);
        FSClose(ref);
    }
    memset(&c, 0, sizeof c);
    c.hFileInfo.ioNamePtr = P(name);
    c.hFileInfo.ioVRefNum = vref;
    c.hFileInfo.ioDirID = root;
    PBGetCatInfoSync(&c);
    *id = c.hFileInfo.ioDirID;
}

/* FSpExchangeFiles, as safe saves use it: what moves with the contents and
   what stays with the names (IDs, Finder info, dates), and a path open
   on one of them across the exchange */
static void exchange(long root)
{
    FSSpec a, b, d;
    long idA, idB, dir, n;
    short ref;
    OSErr err;
    FCBPBRec f;
    unsigned char fname[64];
    CInfoPBRec ca, cb;

    makeSwap(root, "Swap A", 'AAAA', 1000, 300, 1, &idA);
    makeSwap(root, "Swap B", 'BBBB', 5000, 0, 9, &idB);
    memset(&ca, 0, sizeof ca);
    ca.hFileInfo.ioNamePtr = P("Swap A");
    ca.hFileInfo.ioVRefNum = vref;
    ca.hFileInfo.ioDirID = root;
    PBGetCatInfoSync(&ca);
    /* dates of their own, to see which way they go */
    ca.hFileInfo.ioFlCrDat = 1000000;
    ca.hFileInfo.ioFlMdDat = 2000000;
    ca.hFileInfo.ioDirID = root;
    PBSetCatInfoSync(&ca);
    memset(&cb, 0, sizeof cb);
    cb.hFileInfo.ioNamePtr = P("Swap B");
    cb.hFileInfo.ioVRefNum = vref;
    cb.hFileInfo.ioDirID = root;
    PBGetCatInfoSync(&cb);
    cb.hFileInfo.ioFlCrDat = 3000000;
    cb.hFileInfo.ioFlMdDat = 4000000;
    cb.hFileInfo.ioDirID = root;
    PBSetCatInfoSync(&cb);

    FSMakeFSSpec(vref, root, P("Swap A"), &a);
    FSMakeFSSpec(vref, root, P("Swap B"), &b);
    err = HOpenDF(vref, root, P("Swap A"), fsRdPerm, &ref);
    fprintf(out, "EXCHANGE open A -> %d\n", (int)err);
    err = FSpExchangeFiles(&a, &b);
    fprintf(out, "EXCHANGE A B -> %d\n", (int)err);
    swapped(root, "Swap A", idA, idB);
    swapped(root, "Swap B", idA, idB);
    {
        CInfoPBRec c;
        int i;
        for(i = 0; i < 2; i++)
        {
            memset(&c, 0, sizeof c);
            c.hFileInfo.ioNamePtr = P(i ? "Swap B" : "Swap A");
            c.hFileInfo.ioVRefNum = vref;
            c.hFileInfo.ioDirID = root;
            PBGetCatInfoSync(&c);
            fprintf(out, "  %s dates cr %lu md %lu\n", i ? "Swap B" : "Swap A", c.hFileInfo.ioFlCrDat,
                    c.hFileInfo.ioFlMdDat);
        }
    }
    /* the path opened on A before */
    {
        long eof = 0;
        n = 4;
        GetEOF(ref, &eof);
        SetFPos(ref, fsFromStart, 0);
        FSRead(ref, &n, buf);
        memset(&f, 0, sizeof f);
        f.ioNamePtr = fname;
        f.ioRefNum = ref;
        f.ioFCBIndx = 0;
        err = PBGetFCBInfoSync(&f);
        fname[fname[0] + 1] = 0;
        fprintf(out, "  open path -> eof %ld starts %02X%02X%02X%02X fcbinfo %d name %s file %s\n", eof,
                (unsigned char)buf[0], (unsigned char)buf[1], (unsigned char)buf[2], (unsigned char)buf[3],
                (int)err, (char *)fname + 1, f.ioFCBFlNm == idA ? "A's" : f.ioFCBFlNm == idB ? "B's" : "other");
        FSClose(ref);
    }
    /* back again, a folder, a missing file, itself */
    fprintf(out, "EXCHANGE B A -> %d\n", (int)FSpExchangeFiles(&b, &a));
    swapped(root, "Swap A", idA, idB);
    DirCreate(vref, root, P("Swap folder"), &dir);
    FSMakeFSSpec(vref, root, P("Swap folder"), &d);
    fprintf(out, "EXCHANGE A folder -> %d\n", (int)FSpExchangeFiles(&a, &d));
    FSMakeFSSpec(vref, root, P("Swap missing"), &d);
    fprintf(out, "EXCHANGE A missing -> %d\n", (int)FSpExchangeFiles(&a, &d));
    fprintf(out, "EXCHANGE A A -> %d\n", (int)FSpExchangeFiles(&a, &a));
    swapped(root, "Swap A", idA, idB);
    HDelete(vref, root, P("Swap folder"));
    fprintf(out, "DELETE swaps -> %d %d\n", (int)HDelete(vref, root, P("Swap A")), (int)HDelete(vref, root, P("Swap B")));
}

/* "big" (the config's second line), on a volume of 4 KB blocks with room
   for 2 GB: a fork grown to the longest a classic path may have (L, the
   last whole block under 2 GB) and past it, written across it, allocated
   past it, cut back. */
#define kL 0x7FFFF000L
static void eofOf(short ref)
{
    long eof = 0, pos = 0;
    OSErr err = GetEOF(ref, &eof);
    GetFPos(ref, &pos);
    fprintf(out, "  eof -> %d %ld at %ld\n", (int)err, eof, pos);
}

static void seteof(short ref, long eof)
{
    fprintf(out, "SETEOF %ld -> %d\n", eof, (int)SetEOF(ref, eof));
    eofOf(ref);
}

static void big(long root)
{
    short ref, ref2;
    long n;
    OSErr err;

    fprintf(out, "CREATE grow -> %d\n", (int)HCreate(vref, root, P("Grow"), 'BINA', 'ttxt'));
    err = HOpenDF(vref, root, P("Grow"), fsRdWrPerm, &ref);
    fprintf(out, "OPEN grow -> %d\n", (int)err);
    if(err)
        return;
    seteof(ref, kL - 4096);
    lengths(root, "Grow");
    seteof(ref, kL);
    lengths(root, "Grow");
    seteof(ref, kL + 1);
    seteof(ref, 0x7FFFFFFFL);
    lengths(root, "Grow");
    writeAt(ref, kL - 100, 100, 1);
    writeAt(ref, kL - 50, 100, 2);
    eofOf(ref);
    writeAt(ref, kL, 1, 3);
    writeAt(ref, kL, 0, 4);
    fprintf(out, "SETFPOS L+10 -> %d\n", (int)SetFPos(ref, fsFromStart, kL + 10));
    fprintf(out, "SETFPOS L -> %d\n", (int)SetFPos(ref, fsFromStart, kL));
    n = 1024L * 1024;
    err = Allocate(ref, &n);
    fprintf(out, "ALLOCATE 1M -> %d (%ld)\n", (int)err, n);
    lengths(root, "Grow");
    /* the end read back */
    {
        long count = 200, i, bad = 0;
        SetFPos(ref, fsFromStart, kL - 200);
        err = FSRead(ref, &count, buf);
        for(i = 0; i < 50; i++)
            bad += buf[100 + i] != (char)(1 + i * 13 + i / 97);
        fprintf(out, "READ end -> %d (%ld) first-write-bytes-bad %ld\n", (int)err, count, bad);
    }
    /* a second path sees it */
    err = HOpenDF(vref, root, P("Grow"), fsRdPerm, &ref2);
    fprintf(out, "OPEN again -> %d\n", (int)err);
    if(!err)
    {
        eofOf(ref2);
        FSClose(ref2);
    }
    seteof(ref, 1000);
    lengths(root, "Grow");
    n = 0x10000000L;
    err = Allocate(ref, &n);
    fprintf(out, "ALLOCATE 256M -> %d (%ld)\n", (int)err, n);
    lengths(root, "Grow");
    fprintf(out, "CLOSE -> %d\n", (int)FSClose(ref));
    /* grown to L, closed, opened again */
    fprintf(out, "CREATE full -> %d\n", (int)HCreate(vref, root, P("Full"), 'BINA', 'ttxt'));
    if(HOpenDF(vref, root, P("Full"), fsRdWrPerm, &ref) == noErr)
    {
        seteof(ref, kL);
        FSClose(ref);
    }
    err = HOpenDF(vref, root, P("Full"), fsRdPerm, &ref);
    fprintf(out, "OPEN full -> %d\n", (int)err);
    if(!err)
    {
        eofOf(ref);
        FSClose(ref);
    }
    lengths(root, "Full");
    fprintf(out, "DELETE full -> %d\n", (int)HDelete(vref, root, P("Full")));
}

int main(void)
{
    HParamBlockRec pb;
    FILE *cfg;
    char line[64], mode[16];
    Boolean bigMode;
    unsigned char vname[66];
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
    out = fopen("Host:volwrite.txt", "w");
    cfg = fopen("Host:volwrite.cfg", "r");
    if(!out || !buf || !cfg || !fgets(line, sizeof line, cfg))
        return 1;
    bigMode = fgets(mode, sizeof mode, cfg) && !strncmp(mode, "big", 3);
    fclose(cfg);
    n = strcspn(line, "\r\n");
    vname[0] = n + 1;
    memcpy(vname + 1, line, n);
    vname[n + 1] = ':';
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioNamePtr = vname;
    pb.volumeParam.ioVolIndex = -1;
    err = PBHGetVInfoSync(&pb);
    fprintf(out, "VOLUME find -> %d\n", (int)err);
    if(err == noErr)
    {
        vref = pb.volumeParam.ioVRefNum;
        volume("before");
        if(bigMode)
            big(fsRtDirID);
        else
        {
            files(fsRtDirID);
            volume("after files");
            longNames(fsRtDirID);
            many(fsRtDirID);
            exchange(fsRtDirID);
        }
        fprintf(out, "FLUSHVOL -> %d\n", (int)FlushVol(NULL, vref));
        volume("at the end");
    }
    fprintf(out, "---\n");
    fclose(out);
    return 0;
}
