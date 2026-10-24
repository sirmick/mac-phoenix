/* VolWrite.c -- the classic File Manager's changes to a volume, a fixed
 * script whose every result goes to Host:volwrite.txt, so Mac OS 9.0.4 and
 * Executor can be compared on the same image (docs/executor/HFSPlus.md):
 * creating files and folders (bad names, duplicates in any case), writing
 * forks (positions, SetEOF both ways, Allocate, a second path, the
 * Resource Manager), Finder info and locks, renames, moves, deletes,
 * hundreds of files. The volume is the one named in Host:volwrite.cfg; it
 * may hold, made beforehand, a folder "Long" with long-named files:
 *   "A file whose name is longer than thirty-one.txt" (opened, written and
 *   renamed by its short name) and "Another long name, to be deleted.txt".
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

int main(void)
{
    HParamBlockRec pb;
    FILE *cfg;
    char line[64];
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
        files(fsRtDirID);
        volume("after files");
        longNames(fsRtDirID);
        many(fsRtDirID);
        fprintf(out, "FLUSHVOL -> %d\n", (int)FlushVol(NULL, vref));
        volume("at the end");
    }
    fprintf(out, "---\n");
    fclose(out);
    return 0;
}
