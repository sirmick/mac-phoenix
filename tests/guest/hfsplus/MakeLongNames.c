/* MakeLongNames.c — fills the HFS+ volume "LongNames" with what only the
 * HFS Plus calls (Mac OS 9's FSRef API: FSCreateFileUnicode & co.) can
 * make: names longer than 31 characters, names in decomposed Unicode, names
 * with no MacRoman form; plus files in more than 8 extents (the extents
 * overflow B-tree). Run on Mac OS 9.0.4 to make the long-name fixture for
 * Executor's HFS+ reader (docs/executor/HFSPlus.md):
 *   tools/mpw/mpw build tests/guest/hfsplus -o tests/guest/hfsplus
 *   tests/guest/hfsplus/make_fixture.py BLANK OUT
 * What it made, and the FSSpec names Mac OS 9 gave each item (the short
 * names the HFS API sees), go to Host:longnames.txt, a line per item:
 *   ITEM <unicode name as UTF-16 hex> err <n> spec <hex of FSSpec name> parID <n> id <n>
 * A 68k application, so it runs on any Mac OS 9 and on Executor. */
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
#include <OSUtils.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;

static FILE *out;
static SInt64 zero64;      /* SC has no long long: SInt64 is a struct */
static short vref;

/* ASCII with "%XXXX" for any other UTF-16 code unit. */
static UniCharCount uni(const char *s, UniChar *u)
{
    UniCharCount n = 0;
    while(*s)
    {
        if(*s == '%')
        {
            unsigned v = 0;
            int i;
            for(i = 1; i <= 4; i++)
            {
                char c = s[i];
                v = v * 16 + (c >= 'A' ? c - 'A' + 10 : c - '0');
            }
            u[n++] = (UniChar)v;
            s += 5;
        }
        else
            u[n++] = (UniChar)(unsigned char)*s++;
    }
    return n;
}

static void put_unicode(const UniChar *u, UniCharCount n)
{
    UniCharCount i;
    for(i = 0; i < n; i++)
        fprintf(out, "%04X", (unsigned)u[i]);
}

static void put_pstr(const unsigned char *p)
{
    int i;
    for(i = 1; i <= p[0]; i++)
        fprintf(out, "%02X", (unsigned)p[i]);
}

static void report(const char *what, const UniChar *u, UniCharCount n, OSErr err, const FSSpec *spec, long id)
{
    fprintf(out, "%s ", what);
    put_unicode(u, n);
    fprintf(out, " err %d spec ", (int)err);
    if(err == noErr)
    {
        put_pstr(spec->name);
        fprintf(out, " parID %ld id %ld", (long)spec->parID, id);
    }
    else
        fprintf(out, "-");
    fprintf(out, "\n");
    fflush(out);
}

static long node_id(const FSRef *ref)
{
    FSCatalogInfo ci;
    if(FSGetCatalogInfo(ref, kFSCatInfoNodeID, &ci, NULL, NULL, NULL) != noErr)
        return -1;
    return (long)ci.nodeID;
}

static OSErr write_fork(const FSRef *ref, Boolean rsrc, const void *data, long len)
{
    HFSUniStr255 fork;
    SInt16 refnum;
    OSErr err;
    ByteCount actual;

    if(rsrc)
        FSGetResourceForkName(&fork);
    else
        FSGetDataForkName(&fork);
    err = FSOpenFork(ref, fork.length, fork.unicode, fsWrPerm, &refnum);
    if(err != noErr)
        return err;
    err = FSWriteFork(refnum, fsAtMark, zero64, len, data, &actual);
    FSCloseFork(refnum);
    return err;
}

/* A file named NAME in PARENT: its data fork the text "<what> data\r", a
   resource fork if RSRC, type and creator. */
static OSErr make_file(const FSRef *parent, const char *name, OSType type, OSType creator,
                       Boolean rsrc, FSRef *made)
{
    UniChar u[255];
    UniCharCount n = uni(name, u);
    FSCatalogInfo ci;
    FSSpec spec;
    FInfo *fi = (FInfo *)ci.finderInfo;
    char text[300];
    OSErr err;

    memset(&ci, 0, sizeof ci);
    fi->fdType = type;
    fi->fdCreator = creator;
    err = FSCreateFileUnicode(parent, n, u, kFSCatInfoFinderInfo, &ci, made, &spec);
    report("FILE", u, n, err, &spec, err ? 0 : node_id(made));
    if(err != noErr)
        return err;
    sprintf(text, "%s data\r", name);
    err = write_fork(made, false, text, strlen(text));
    if(err == noErr && rsrc)
    {
        sprintf(text, "%s resource fork\r", name);
        err = write_fork(made, true, text, strlen(text));
    }
    if(err != noErr)
        fprintf(out, "WRITE %s err %d\n", name, (int)err);
    return err;
}

static OSErr make_folder(const FSRef *parent, const char *name, FSRef *made)
{
    UniChar u[255];
    UniCharCount n = uni(name, u);
    FSSpec spec;
    UInt32 dirID = 0;
    OSErr err = FSCreateDirectoryUnicode(parent, n, u, kFSCatInfoNone, NULL, made, &spec, &dirID);
    report("FOLDER", u, n, err, &spec, (long)dirID);
    return err;
}

/* Two files written in turn, a clump at a time, so each lands in many
   extents: more than the 8 a catalog record holds. */
static void make_fragmented(const FSRef *parent)
{
    FSRef refs[2];
    SInt16 forks[2];
    HFSUniStr255 dfork;
    unsigned char *buf = (unsigned char *)NewPtr(65536);     /* too big for near data */
    long i;
    int f, round;
    OSErr err;

    FSGetDataForkName(&dfork);
    for(f = 0; f < 2; f++)
    {
        err = make_file(parent, f ? "Fragmented B.bin" : "Fragmented A.bin", 'BINA', 'hfs+', false, &refs[f]);
        if(err == noErr)
            err = FSOpenFork(&refs[f], dfork.length, dfork.unicode, fsWrPerm, &forks[f]);
        if(err != noErr)
        {
            fprintf(out, "FRAGMENT open err %d\n", (int)err);
            return;
        }
    }
    for(round = 0; round < 24; round++)
        for(f = 0; f < 2; f++)
        {
            ByteCount actual;
            for(i = 0; i < 65536; i++)
                buf[i] = (unsigned char)(i * 7 + round * 31 + f * 101);
            err = FSWriteFork(forks[f], fsFromLEOF, zero64, 65536, buf, &actual);
            if(err != noErr)
                fprintf(out, "FRAGMENT write %d/%d err %d\n", f, round, (int)err);
            FSFlushFork(forks[f]);
        }
    FSCloseFork(forks[0]);
    FSCloseFork(forks[1]);
    DisposePtr((Ptr)buf);
    fprintf(out, "FRAGMENTED 2 files x 24 x 65536 bytes\n");
}

/* The classic File Manager given an item's short FSSpec name: can it open
   it? (Mac OS 9 resolves the "#<CNID>" form.) */
static void check_classic_open(const FSRef *ref)
{
    FSSpec spec;
    short refnum;
    long len = 0;
    OSErr err = FSGetCatalogInfo(ref, kFSCatInfoNone, NULL, NULL, &spec, NULL);
    if(err == noErr)
        err = FSpOpenDF(&spec, fsRdPerm, &refnum);
    if(err == noErr)
    {
        GetEOF(refnum, &len);
        FSClose(refnum);
    }
    fprintf(out, "CLASSIC ");
    put_pstr(spec.name);
    fprintf(out, " open err %d eof %ld\n", (int)err, len);
}

int main(void)
{
    HParamBlockRec pb;
    FSSpec rootSpec;
    FSRef root, item, folder;
    OSErr err;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    InitCursor();

    out = fopen("Host:longnames.txt", "w");
    if(!out)
        return 1;

    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioNamePtr = (StringPtr) "\pLongNames:";
    pb.volumeParam.ioVolIndex = -1;
    err = PBHGetVInfoSync(&pb);
    fprintf(out, "VOLUME err %d sig %04X\n", (int)err, (unsigned)pb.volumeParam.ioVSigWord);
    if(err != noErr)
        goto done;
    vref = pb.volumeParam.ioVRefNum;
    err = FSMakeFSSpec(vref, fsRtDirID, "\p", &rootSpec);
    if(err == noErr)
        err = FSpMakeFSRef(&rootSpec, &root);
    fprintf(out, "ROOT err %d\n", (int)err);
    if(err != noErr)
        goto done;

    make_file(&root, "This is a file with a rather long name.txt", 'TEXT', 'ttxt', false, &item);
    check_classic_open(&item);
    make_file(&root, "A long name with no extension at all here", 'TEXT', 'ttxt', false, &item);
    check_classic_open(&item);
    make_file(&root, "Exactly thirty-one characters!!", 'TEXT', 'ttxt', false, &item);
    make_file(&root, "Exactly thirty-two characters!!!", 'TEXT', 'ttxt', false, &item);
    check_classic_open(&item);
    /* e + combining acute, e + combining grave, em dash */
    make_file(&root, "Cafe%0301 cre%0300me %2014 accented and long name.txt", 'TEXT', 'ttxt', false, &item);
    check_classic_open(&item);
    make_file(&root, "Short Cafe%0301.txt", 'TEXT', 'ttxt', false, &item);
    check_classic_open(&item);
    make_file(&root, "Greek %03B1%03B2%03B3.txt", 'TEXT', 'ttxt', false, &item);
    check_classic_open(&item);
    make_file(&root, "%65E5%672C%8A9E long Japanese name.txt", 'TEXT', 'ttxt', false, &item);
    make_file(&root, "Twins with the same long prefix - one.txt", 'TEXT', 'ttxt', false, &item);
    make_file(&root, "Twins with the same long prefix - two.txt", 'TEXT', 'ttxt', false, &item);
    make_file(&root, "Archive.with.many.dots.and.a.long.name.tar.gz", 'Gzip', 'Gzip', false, &item);
    make_file(&root, "Long named document with both forks.rsrc", 'rsrc', 'RSED', true, &item);
    check_classic_open(&item);
    if(make_folder(&root, "A folder whose name is longer than 31 characters", &folder) == noErr)
    {
        make_file(&folder, "inner.txt", 'TEXT', 'ttxt', false, &item);
        make_file(&folder, "Another long file name inside the long folder.txt", 'TEXT', 'ttxt', false, &item);
        check_classic_open(&item);
    }
    make_fragmented(&root);
    FlushVol(NULL, vref);

done:
    fprintf(out, "---\n");
    fclose(out);
    return 0;
}
