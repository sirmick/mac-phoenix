/* FSRefTest.c -- Mac OS 9.0's HFS Plus APIs (FSRefs, Unicode names, 64-bit
 * forks), a fixed script whose every result goes to Host:fsreftest.txt, so
 * Mac OS 9.0.4 and Executor can be compared (docs/executor/HFSPlus.md).
 *
 * Host:fsreftest.cfg, a line per volume: "read NAME" (looked at only:
 * volume info, refs made every way, catalog info, iteration, Unicode
 * lookups, forks read, search) or "write NAME" (a folder "FSRef work"
 * made there and everything done inside it: Unicode creates, renames,
 * moves, exchanges, deletes, catalog info set, every fork call). Items are
 * named by their Unicode names (hex), never by IDs a volume chose, unless
 * they are the fixture's. "big NAME": the root's files read past 2 GB
 * through the fork calls. "---" last. 68k, Universal Interfaces 3.3.2. */
#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Files.h>
#include <TextCommon.h>
#include <Events.h>
#include <OSUtils.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;

static FILE *out;
static char *buf;
enum { kBuf = 32768 };
static HFSUniStr255 *uname;     /* NewPtr: big */
static FSCatalogInfo *info;

static void ustr(const HFSUniStr255 *u)
{
    int i;
    fprintf(out, "u\"");
    for(i = 0; i < u->length && i < 255; i++)
    {
        UniChar c = u->unicode[i];
        if(c >= 0x20 && c < 0x7F && c != '"' && c != '\\')
            fprintf(out, "%c", (char)c);
        else
            fprintf(out, "\\u%04X", (unsigned)c);
    }
    fprintf(out, "\"");
}

/* ASCII into UTF-16 */
static UniCharCount U(const char *s, UniChar *u)
{
    UniCharCount n = 0;
    while(*s)
        u[n++] = (unsigned char)*s++;
    return n;
}

static void pstr(const unsigned char *p)
{
    int i;
    fprintf(out, "\"");
    for(i = 1; i <= p[0]; i++)
        if(p[i] >= 0x20 && p[i] < 0x7F && p[i] != '"')
            fprintf(out, "%c", p[i]);
        else
            fprintf(out, "\\x%02X", p[i]);
    fprintf(out, "\"");
}

static void wide64(const char *label, UInt32 hi, UInt32 lo)
{
    if(hi)
        fprintf(out, " %s %lu:%lu", label, hi, lo);
    else
        fprintf(out, " %s %lu", label, lo);
}

static unsigned long nowSecs(void)
{
    unsigned long t;
    GetDateTime(&t);
    return t;
}

/* a UTCDateTime: "-" if zero; when writing, "now" if within a day of now
   (local vs UTC either way); else its seconds */
static Boolean writing;
static void date(const char *label, const UTCDateTime *d)
{
    unsigned long s = d->lowSeconds, n = nowSecs();
    if(d->highSeconds == 0 && s == 0)
        fprintf(out, " %s -", label);
    else if(writing && d->highSeconds == 0 && (s > n ? s - n : n - s) < 86400UL)
        fprintf(out, " %s now", label);
    else
        fprintf(out, " %s %lu", label, s);
}

/* catalog info, the fields whichInfo asked for (IDs the fixture fixed: kept) */
static void catinfo(FSCatalogInfoBitmap which, const FSCatalogInfo *c, Boolean ids)
{
    if(which & kFSCatInfoNodeFlags)
        fprintf(out, " flags %04X", (unsigned)c->nodeFlags);
    if(ids && (which & kFSCatInfoParentDirID))
        fprintf(out, " par %lu", c->parentDirID);
    if(ids && (which & kFSCatInfoNodeID))
        fprintf(out, " id %lu", c->nodeID);
    if(which & kFSCatInfoTextEncoding)
        fprintf(out, " enc %lu", c->textEncodingHint);
    if(which & kFSCatInfoCreateDate)
        date("cr", &c->createDate);
    if(which & kFSCatInfoContentMod)
        date("md", &c->contentModDate);
    if(which & kFSCatInfoAttrMod)
        date("amd", &c->attributeModDate);
    if(which & kFSCatInfoAccessDate)
        date("acc", &c->accessDate);
    if(which & kFSCatInfoBackupDate)
        date("bk", &c->backupDate);
    if(which & kFSCatInfoPermissions)
        fprintf(out, " perm %08lX/%08lX/%08lX/%08lX", c->permissions[0], c->permissions[1], c->permissions[2],
                c->permissions[3]);
    if(which & kFSCatInfoFinderInfo)
    {
        int i;
        fprintf(out, " fndr ");
        for(i = 0; i < 16; i++)
            fprintf(out, "%02X", c->finderInfo[i]);
    }
    if(which & kFSCatInfoFinderXInfo)
    {
        int i;
        fprintf(out, " xfndr ");
        for(i = 0; i < 16; i++)
            fprintf(out, "%02X", c->extFinderInfo[i]);
    }
    if(which & kFSCatInfoValence)
        fprintf(out, " valence %lu", c->valence);
    if(which & kFSCatInfoDataSizes)
    {
        wide64("data", c->dataLogicalSize.hi, c->dataLogicalSize.lo);
        wide64("/", c->dataPhysicalSize.hi, c->dataPhysicalSize.lo);
    }
    if(which & kFSCatInfoRsrcSizes)
    {
        wide64("rsrc", c->rsrcLogicalSize.hi, c->rsrcLogicalSize.lo);
        wide64("/", c->rsrcPhysicalSize.hi, c->rsrcPhysicalSize.lo);
    }
    if(which & kFSCatInfoSharingFlags)
        fprintf(out, " share %02X", (unsigned)c->sharingFlags);
    if(which & kFSCatInfoUserPrivs)
        fprintf(out, " privs %02X", (unsigned)c->userPrivileges);
}

/* what a ref names: its Unicode name, kind, and (ids) IDs */
static void describe(const FSRef *ref, Boolean ids)
{
    OSErr err = FSGetCatalogInfo(ref, kFSCatInfoNodeFlags | kFSCatInfoNodeID | kFSCatInfoParentDirID, info, uname,
                                 NULL, NULL);
    fprintf(out, " -> %d", (int)err);
    if(err)
        return;
    fprintf(out, " ");
    ustr(uname);
    fprintf(out, " %s", (info->nodeFlags & kFSNodeIsDirectoryMask) ? "dir" : "file");
    if(ids)
        fprintf(out, " id %lu par %lu", info->nodeID, info->parentDirID);
}

static short vref;
static FSRef root;
static Boolean onHFSPlus;

static void volume(const char *name)
{
    FSVolumeInfo vi;
    FSVolumeRefNum actual;
    OSErr err;
    HParamBlockRec pb;
    unsigned char vname[64];

    vname[0] = strlen(name) + 1;
    memcpy(vname + 1, name, vname[0] - 1);
    vname[vname[0]] = ':';
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioNamePtr = vname;
    pb.volumeParam.ioVolIndex = -1;
    err = PBHGetVInfoSync(&pb);
    vref = err ? 0 : pb.volumeParam.ioVRefNum;
    fprintf(out, "VOLUME %s find %d\n", name, (int)err);
    if(!vref)
        return;
    memset(&vi, 0, sizeof vi);
    err = FSGetVolumeInfo(vref, 0, &actual, kFSVolInfoGettableInfo, &vi, uname, &root);
    fprintf(out, "  getvolumeinfo -> %d same %d ", (int)err, actual == vref);
    ustr(uname);
    onHFSPlus = vi.signature == 'H+';
    fprintf(out, " sig %04X fsid %u flags %04X files %lu folders %lu", (unsigned)vi.signature,
            (unsigned)vi.filesystemID, (unsigned)vi.flags & ~0x0060, vi.fileCount, vi.folderCount);
    wide64("total", vi.totalBytes.hi, vi.totalBytes.lo);
    wide64("free", vi.freeBytes.hi, vi.freeBytes.lo);
    fprintf(out, " blk %lu/%lu/%lu clumps %lu/%lu nextid %lu", vi.blockSize, vi.totalBlocks, vi.freeBlocks,
            vi.rsrcClumpSize, vi.dataClumpSize, vi.nextCatalogID);
    date("cr", &vi.createDate);
    fprintf(out, "\n  root");
    describe(&root, true);
    fprintf(out, "\n");
    err = FSGetVolumeInfo(vref, 0, NULL, 0x10000L, &vi, NULL, NULL);
    fprintf(out, "  getvolumeinfo bad bitmap -> %d\n", (int)err);
    err = FSGetVolumeInfo(0, 0, &actual, kFSVolInfoNone, NULL, NULL, NULL);
    fprintf(out, "  getvolumeinfo default -> %d\n", (int)err);
    err = FSGetVolumeInfo(-32000, 0, &actual, kFSVolInfoNone, NULL, NULL, NULL);
    fprintf(out, "  getvolumeinfo nsv -> %d\n", (int)err);
}

/* refs made every way to items of the read-only fixture */
static void refs(void)
{
    FSSpec spec;
    FSRef a, b, c;
    OSErr err;
    UniChar u[300];
    UniCharCount n;
    FSRefParam pb;

    fprintf(out, "REFS\n");
    err = FSMakeFSSpec(vref, fsRtDirID, "\pSystem Folder", &spec);
    err = err ? err : FSpMakeFSRef(&spec, &a);
    fprintf(out, "  fspmakefsref System Folder");
    describe(&a, true);
    fprintf(out, "\n");
    n = U("System Folder", u);
    err = FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &b);
    fprintf(out, "  makefsrefunicode -> %d compare %d\n", (int)err, (int)FSCompareFSRefs(&a, &b));
    n = U("SYSTEM folder", u);
    err = FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &b);
    fprintf(out, "  makefsrefunicode other case -> %d compare %d\n", (int)err, err ? 0 : (int)FSCompareFSRefs(&a, &b));
    fprintf(out, "  compare root/System Folder -> %d\n", (int)FSCompareFSRefs(&root, &a));
    /* a long Unicode name, as stored and composed */
    n = U("This is a file with a rather long name.txt", u);
    err = FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &c);
    fprintf(out, "  makefsrefunicode long");
    describe(&c, true);
    fprintf(out, "\n");
    {
        static const UniChar composed[] = { 'S', 'h', 'o', 'r', 't', ' ', 'C', 'a', 'f', 0x00E9, '.', 't', 'x', 't' };
        static const UniChar decomposed[] = { 'S', 'h', 'o', 'r', 't', ' ', 'C', 'a', 'f', 'e', 0x0301, '.', 't', 'x', 't' };
        err = FSMakeFSRefUnicode(&root, 14, composed, kTextEncodingUnknown, &b);
        fprintf(out, "  makefsrefunicode composed e-acute -> %d\n", (int)err);
        err = FSMakeFSRefUnicode(&root, 15, decomposed, kTextEncodingUnknown, &c);
        fprintf(out, "  makefsrefunicode decomposed e-acute -> %d compare %d\n", (int)err,
                err ? 0 : (int)FSCompareFSRefs(&b, &c));
    }
    n = U("No such item", u);
    err = FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &b);
    fprintf(out, "  makefsrefunicode missing -> %d\n", (int)err);
    err = FSMakeFSRefUnicode(&root, 0, u, kTextEncodingUnknown, &b);
    fprintf(out, "  makefsrefunicode empty -> %d", (int)err);
    if(!err)
        describe(&b, true);
    fprintf(out, "\n");
    /* through a file */
    n = U("Short Caf", u);
    n = U("inner.txt", u);
    err = FSMakeFSRefUnicode(&c, n, u, kTextEncodingUnknown, &b);
    fprintf(out, "  makefsrefunicode in a file -> %d\n", (int)err);
    /* PBMakeFSRef: by path, by dirID and name, the short #CNID name */
    memset(&pb, 0, sizeof pb);
    pb.ioNamePtr = "\p:A folder whose name is longe#22:inner.txt";
    pb.ioVRefNum = vref;
    pb.ioDirID = fsRtDirID;
    pb.newRef = &b;
    err = PBMakeFSRefSync(&pb);
    fprintf(out, "  pbmakefsref short-name path");
    if(err)
        fprintf(out, " -> %d", (int)err);
    else
        describe(&b, true);
    fprintf(out, "\n");
    pb.ioNamePtr = "\pNo such thing";
    err = PBMakeFSRefSync(&pb);
    fprintf(out, "  pbmakefsref missing -> %d\n", (int)err);
    pb.ioNamePtr = NULL;
    pb.ioDirID = 34;
    err = PBMakeFSRefSync(&pb);
    fprintf(out, "  pbmakefsref dirID 34, no name");
    if(err)
        fprintf(out, " -> %d", (int)err);
    else
        describe(&b, true);
    fprintf(out, "\n");
    pb.ioNamePtr = "\p";
    pb.ioDirID = 2;
    err = PBMakeFSRefSync(&pb);
    fprintf(out, "  pbmakefsref root, empty name");
    if(err)
        fprintf(out, " -> %d", (int)err);
    else
        describe(&b, true);
    fprintf(out, "\n");
    /* a ref that's nothing */
    memset(&b, 0, sizeof b);
    err = FSGetCatalogInfo(&b, kFSCatInfoNodeID, info, NULL, NULL, NULL);
    fprintf(out, "  zero ref getcataloginfo -> %d\n", (int)err);
    memset(&b, 0xFF, sizeof b);
    err = FSGetCatalogInfo(&b, kFSCatInfoNodeID, info, NULL, NULL, NULL);
    fprintf(out, "  ones ref getcataloginfo -> %d\n", (int)err);
}

/* FSGetCatalogInfo on fixture items, every field, and its FSSpec and parent */
static void infos(void)
{
    static const char *names[] = { "System Folder", "This is a file with a rather long name.txt",
                                   "Long named document with both forks.rsrc", "Fragmented A.bin",
                                   "Colon:in a name", "Slash/in a name", 0 };
    int i;
    fprintf(out, "INFO\n");
    for(i = 0; names[i]; i++)
    {
        FSRef ref, parent;
        FSSpec spec;
        UniChar u[300];
        UniCharCount n = U(names[i], u);
        OSErr err = FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &ref);
        fprintf(out, "  %s -> %d\n", names[i], (int)err);
        if(err)
            continue;
        memset(info, 0xEE, sizeof *info);
        err = FSGetCatalogInfo(&ref, kFSCatInfoGettableInfo, info, uname, &spec, &parent);
        fprintf(out, "    all -> %d ", (int)err);
        ustr(uname);
        catinfo(kFSCatInfoGettableInfo & ~kFSCatInfoAccessDate, info, true);
        fprintf(out, " vol-same %d\n", info->volume == vref);
        fprintf(out, "    spec par %ld ", spec.parID);
        pstr(spec.name);
        fprintf(out, " parent");
        describe(&parent, true);
        fprintf(out, "\n");
    }
    {
        FSRef ref;
        OSErr err;
        FSGetCatalogInfo(&root, kFSCatInfoNone, NULL, NULL, NULL, NULL);
        err = FSGetCatalogInfo(&root, kFSCatInfoNodeID, NULL, NULL, NULL, NULL);
        fprintf(out, "  null catinfo -> %d\n", (int)err);
        err = FSGetCatalogInfo(&root, 0x80000000UL, info, NULL, NULL, NULL);
        fprintf(out, "  reserved bit -> %d\n", (int)err);
        err = FSGetCatalogInfo(&root, kFSCatInfoGettableInfo, info, uname, NULL, &ref);
        fprintf(out, "  root -> %d ", (int)err);
        ustr(uname);
        catinfo(kFSCatInfoNodeFlags | kFSCatInfoNodeID | kFSCatInfoParentDirID | kFSCatInfoValence, info, true);
        fprintf(out, " parent");
        describe(&ref, true);
        fprintf(out, "\n");
    }
}

/* every item of a folder through an iterator, n at a time */
static void iterate(const FSRef *container, const char *label, Boolean ids)
{
    FSIterator it;
    OSErr err;
    ItemCount got, total = 0;
    Boolean changed;
    FSRef *refsOut = (FSRef *)NewPtr(sizeof(FSRef) * 3);
    FSCatalogInfo *infosOut = (FSCatalogInfo *)NewPtr(sizeof(FSCatalogInfo) * 3);
    HFSUniStr255 *namesOut = (HFSUniStr255 *)NewPtr(sizeof(HFSUniStr255) * 3);
    FSSpec *specsOut = (FSSpec *)NewPtr(sizeof(FSSpec) * 3);

    err = FSOpenIterator(container, kFSIterateFlat, &it);
    fprintf(out, "ITERATE %s open -> %d\n", label, (int)err);
    if(err)
        return;
    for(;;)
    {
        int i;
        err = FSGetCatalogInfoBulk(it, 3, &got, &changed,
                                   kFSCatInfoNodeFlags | kFSCatInfoNodeID | kFSCatInfoDataSizes | kFSCatInfoFinderInfo,
                                   infosOut, refsOut, specsOut, namesOut);
        fprintf(out, "  bulk -> %d got %lu changed %d\n", (int)err, got, (int)changed);
        for(i = 0; i < got; i++)
        {
            fprintf(out, "    ");
            ustr(&namesOut[i]);
            catinfo(kFSCatInfoNodeFlags | kFSCatInfoDataSizes, &infosOut[i], ids);
            if(ids)
                fprintf(out, " id %lu", infosOut[i].nodeID);
            fprintf(out, " spec ");
            pstr(specsOut[i].name);
            fprintf(out, " ref");
            describe(&refsOut[i], false);
            fprintf(out, "\n");
        }
        total += got;
        if(err)
            break;
    }
    fprintf(out, "  total %lu\n", total);
    err = FSGetCatalogInfoBulk(it, 3, &got, NULL, kFSCatInfoNone, NULL, NULL, NULL, NULL);
    fprintf(out, "  bulk again -> %d got %lu\n", (int)err, got);
    err = FSGetCatalogInfoBulk(it, 0, &got, NULL, kFSCatInfoNone, NULL, NULL, NULL, NULL);
    fprintf(out, "  bulk zero -> %d\n", (int)err);
    fprintf(out, "  close -> %d\n", (int)FSCloseIterator(it));
    fprintf(out, "  close again -> %d\n", (int)FSCloseIterator(it));
    err = FSOpenIterator(container, kFSIterateSubtree, &it);
    fprintf(out, "  open subtree -> %d", (int)err);
    if(!err)
    {
        err = FSGetCatalogInfoBulk(it, 3, &got, NULL, kFSCatInfoNone, NULL, NULL, NULL, NULL);
        fprintf(out, " bulk -> %d", (int)err);
        FSCloseIterator(it);
    }
    fprintf(out, "\n");
    err = FSOpenIterator(container, 4, &it);
    fprintf(out, "  open bad flags -> %d\n", (int)err);
    if(!err)
        FSCloseIterator(it);
    DisposePtr((Ptr)refsOut);
    DisposePtr((Ptr)infosOut);
    DisposePtr((Ptr)namesOut);
    DisposePtr((Ptr)specsOut);
}

/* a fork read through the fork calls */
static void readFork(const FSRef *ref, Boolean rsrc, const char *label)
{
    HFSUniStr255 fork;
    SInt16 refNum;
    OSErr err;
    SInt64 pos, size;
    ByteCount got;
    long eof;

    if(rsrc)
        FSGetResourceForkName(&fork);
    else
        FSGetDataForkName(&fork);
    err = FSOpenFork(ref, fork.length, fork.unicode, fsRdPerm, &refNum);
    fprintf(out, "  %s open -> %d", label, (int)err);
    if(err)
    {
        fprintf(out, "\n");
        return;
    }
    err = FSGetForkSize(refNum, &size);
    fprintf(out, " size -> %d", (int)err);
    wide64("", size.hi, size.lo);
    err = FSReadFork(refNum, fsFromStart, (pos.hi = 0, pos.lo = 0, pos), 16, buf, &got);
    fprintf(out, " read16 -> %d (%lu) %02X%02X%02X%02X", (int)err, got, (unsigned char)buf[0],
            (unsigned char)buf[1], (unsigned char)buf[2], (unsigned char)buf[3]);
    err = FSReadFork(refNum, fsAtMark, (pos.hi = 0, pos.lo = 0, pos), kBuf, buf, &got);
    fprintf(out, " rest -> %d (%lu)", (int)err, got);
    err = FSGetForkPosition(refNum, &pos);
    wide64("at", pos.hi, pos.lo);
    pos.hi = -1;
    pos.lo = (UInt32)-4;
    err = FSSetForkPosition(refNum, fsFromLEOF, pos);
    fprintf(out, " setpos eof-4 -> %d", (int)err);
    err = FSReadFork(refNum, fsAtMark, (pos.hi = 0, pos.lo = 0, pos), 16, buf, &got);
    fprintf(out, " read -> %d (%lu)", (int)err, got);
    pos.hi = 0;
    pos.lo = 1;
    err = FSSetForkPosition(refNum, fsFromLEOF, pos);
    fprintf(out, " setpos past -> %d", (int)err);
    err = FSReadFork(refNum, fsFromStart | 0x80, (pos.hi = 0, pos.lo = 0, pos), 4, buf, &got);
    fprintf(out, " newline mode -> %d", (int)err);
    err = GetEOF(refNum, &eof);
    fprintf(out, " geteof -> %d", (int)err);
    if(!err)
        fprintf(out, " %ld", eof);
    err = FSWriteFork(refNum, fsFromStart, (pos.hi = 0, pos.lo = 0, pos), 1, buf, &got);
    fprintf(out, " write -> %d", (int)err);
    fprintf(out, " close -> %d", (int)FSCloseFork(refNum));
    fprintf(out, " close again -> %d\n", (int)FSCloseFork(refNum));
}

static void forksRead(void)
{
    FSRef ref;
    UniChar u[300];
    UniCharCount n;
    OSErr err;
    CatPositionRec it;
    SInt64 size;
    UInt64 phys;

    fprintf(out, "FORKS read\n");
    {
        HFSUniStr255 d, r;
        FSGetDataForkName(&d);
        FSGetResourceForkName(&r);
        fprintf(out, "  data fork name ");
        ustr(&d);
        fprintf(out, " resource fork name ");
        ustr(&r);
        fprintf(out, "\n");
    }
    n = U("Long named document with both forks.rsrc", u);
    err = FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &ref);
    readFork(&ref, false, "both data");
    readFork(&ref, true, "both rsrc");
    it.initialize = 0;
    for(;;)
    {
        err = FSIterateForks(&ref, &it, uname, &size, &phys);
        fprintf(out, "  iterateforks -> %d", (int)err);
        if(err)
            break;
        fprintf(out, " ");
        ustr(uname);
        wide64("size", size.hi, size.lo);
        wide64("phys", phys.hi, phys.lo);
        fprintf(out, "\n");
    }
    fprintf(out, "\n");
    {
        SInt16 refNum;
        static const UniChar bad[] = { 'n', 'o', 'p', 'e' };
        err = FSOpenFork(&ref, 4, bad, fsRdPerm, &refNum);
        fprintf(out, "  open named fork -> %d\n", (int)err);
        if(!err)
            FSCloseFork(refNum);
    }
    n = U("Fragmented A.bin", u);
    if(FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &ref) == noErr)
        readFork(&ref, false, "fragmented");
    n = U("System Folder", u);
    if(FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &ref) == noErr)
    {
        HFSUniStr255 d;
        SInt16 refNum;
        FSGetDataForkName(&d);
        err = FSOpenFork(&ref, d.length, d.unicode, fsRdPerm, &refNum);
        fprintf(out, "  open a folder's fork -> %d\n", (int)err);
    }
}

/* FSCatalogSearch over the volume: by partial name */
static void search(void)
{
    FSIterator it;
    FSSearchParams sp;
    UniChar u[64];
    ItemCount got;
    Boolean changed;
    OSErr err;
    int rounds = 0;

    fprintf(out, "SEARCH\n");
    err = FSOpenIterator(&root, kFSIterateSubtree, &it);
    fprintf(out, "  open subtree -> %d\n", (int)err);
    if(err)
        return;
    memset(&sp, 0, sizeof sp);
    memset(info, 0, sizeof *info);
    sp.searchInfo1 = info;
    sp.searchInfo2 = info;
    sp.searchBits = fsSBPartialName;
    sp.searchNameLength = U("long", u);
    sp.searchName = u;
    do
    {
        int i;
        FSRef *r = (FSRef *)NewPtr(sizeof(FSRef) * 4);
        HFSUniStr255 *nm = (HFSUniStr255 *)NewPtr(sizeof(HFSUniStr255) * 4);
        err = FSCatalogSearch(it, &sp, 4, &got, &changed, kFSCatInfoNone, NULL, r, NULL, nm);
        if(err == errFSBadSearchParams && rounds == 0)
        {
            /* (searchTime too) */
            sp.searchTime = 0;
            sp.searchInfo1 = NULL;
            sp.searchInfo2 = NULL;
            fprintf(out, "  search no infos -> %d\n", (int)FSCatalogSearch(it, &sp, 4, &got, &changed,
                                                                         kFSCatInfoNone, NULL, r, NULL, nm));
            sp.searchInfo1 = info;
            sp.searchInfo2 = info;
        }
        fprintf(out, "  search -> %d got %lu\n", (int)err, got);
        for(i = 0; i < got; i++)
        {
            fprintf(out, "    ");
            ustr(&nm[i]);
            fprintf(out, "\n");
        }
        DisposePtr((Ptr)r);
        DisposePtr((Ptr)nm);
    } while(err == noErr && ++rounds < 20);
    FSCloseIterator(it);
    err = FSOpenIterator(&root, kFSIterateFlat, &it);
    if(!err)
    {
        err = FSCatalogSearch(it, &sp, 4, &got, NULL, kFSCatInfoNone, NULL, NULL, NULL, NULL);
        fprintf(out, "  search flat -> %d\n", (int)err);
        FSCloseIterator(it);
    }
}

/* ---- write ---------------------------------------------------------------- */

static FSRef work;

static OSErr createU(const FSRef *parent, const char *name, Boolean dir, FSRef *made, const char *label)
{
    UniChar u[300];
    UniCharCount n = U(name, u);
    FSSpec spec;
    UInt32 dirID;
    OSErr err = dir ? FSCreateDirectoryUnicode(parent, n, u, kFSCatInfoNone, NULL, made, &spec, &dirID)
                    : FSCreateFileUnicode(parent, n, u, kFSCatInfoNone, NULL, made, &spec);
    fprintf(out, "  %s -> %d", label, (int)err);
    if(!err)
    {
        fprintf(out, " spec ");
        pstr(spec.name);
        describe(made, false);
    }
    fprintf(out, "\n");
    return err;
}

static void listWork(const char *when)
{
    char label[64];
    sprintf(label, "work %s", when);
    iterate(&work, label, false);
}

static void writes(void)
{
    FSRef f, g, d, sub, x;
    UniChar u[300];
    UniCharCount n;
    OSErr err;
    int i;

    fprintf(out, "WRITE\n");
    n = U("FSRef work", u);
    err = FSMakeFSRefUnicode(&root, n, u, kTextEncodingUnknown, &work);
    if(!err)
        fprintf(out, "  (a work folder was left: %d)\n", (int)err);
    else if(createU(&root, "FSRef work", true, &work, "create work folder"))
        return;

    if(createU(&work, "A file with a name longer than thirty-one characters.txt", false, &f, "create long"))
        createU(&work, "Fork file", false, &f, "create fork file");      /* HFS: no long names */
    createU(&work, "A FILE WITH A NAME LONGER THAN THIRTY-ONE CHARACTERS.TXT", false, &g, "create long other case");
    createU(&work, "Short", false, &g, "create short");
    createU(&work, "Sub folder", true, &d, "create dir");
    createU(&work, "Colon: and slash/ in it", false, &x, "create colon slash");
    {
        char longest[300];
        for(i = 0; i < 255; i++)
            longest[i] = 'a' + i % 26;
        longest[255] = 0;
        createU(&work, longest, false, &x, "create 255");
        longest[255] = 'z';
        longest[256] = 0;
        createU(&work, longest, false, &x, "create 256");
    }
    err = FSCreateFileUnicode(&work, 0, u, kFSCatInfoNone, NULL, &x, NULL);
    fprintf(out, "  create empty name -> %d\n", (int)err);
    {
        static const UniChar greek[] = { 0x03B1, 0x03B2, 0x03B3, ' ', 'G', 'r', 'e', 'e', 'k' };
        FSSpec spec;
        err = FSCreateFileUnicode(&work, 9, greek, kFSCatInfoNone, NULL, &x, &spec);
        fprintf(out, "  create greek -> %d", (int)err);
        if(!err)
        {
            fprintf(out, " spec ");
            pstr(spec.name);
            describe(&x, false);
            FSDeleteObject(&x);
        }
        fprintf(out, "\n");
    }
    /* created with catalog info */
    memset(info, 0, sizeof *info);
    memcpy(info->finderInfo, "TEXTttxt", 8);
    info->createDate.lowSeconds = 3000000000UL;
    info->contentModDate.lowSeconds = 3000000100UL;
    info->nodeFlags = kFSNodeLockedMask;
    n = U("With info", u);
    err = FSCreateFileUnicode(&work, n, u, kFSCatInfoFinderInfo | kFSCatInfoCreateDate | kFSCatInfoContentMod,
                              info, &x, NULL);
    fprintf(out, "  create with info -> %d", (int)err);
    if(!err)
    {
        FSGetCatalogInfo(&x, kFSCatInfoFinderInfo | kFSCatInfoCreateDate | kFSCatInfoContentMod | kFSCatInfoNodeFlags,
                         info, NULL, NULL, NULL);
        catinfo(kFSCatInfoFinderInfo | kFSCatInfoCreateDate | kFSCatInfoContentMod | kFSCatInfoNodeFlags, info, false);
    }
    fprintf(out, "\n");
    err = FSCreateFileUnicode(&work, n, u, kFSCatInfoNodeFlags, info, &x, NULL);
    fprintf(out, "  create with nodeflags -> %d\n", (int)err);

    /* set catalog info: Finder info, dates, the lock */
    memset(info, 0, sizeof *info);
    memcpy(info->finderInfo, "APPLsimp", 8);
    info->createDate.lowSeconds = 3100000000UL;
    info->backupDate.lowSeconds = 3100000200UL;
    err = FSSetCatalogInfo(&g, kFSCatInfoFinderInfo | kFSCatInfoCreateDate | kFSCatInfoBackupDate, info);
    fprintf(out, "  setcataloginfo -> %d", (int)err);
    FSGetCatalogInfo(&g, kFSCatInfoFinderInfo | kFSCatInfoCreateDate | kFSCatInfoBackupDate, info, NULL, NULL, NULL);
    catinfo(kFSCatInfoFinderInfo | kFSCatInfoCreateDate | kFSCatInfoBackupDate, info, false);
    fprintf(out, "\n");
    info->nodeFlags = kFSNodeLockedMask;
    err = FSSetCatalogInfo(&g, kFSCatInfoNodeFlags, info);
    FSGetCatalogInfo(&g, kFSCatInfoNodeFlags, info, NULL, NULL, NULL);
    fprintf(out, "  lock -> %d flags %04X delete locked -> %d", (int)err, (unsigned)info->nodeFlags,
            (int)FSDeleteObject(&g));
    info->nodeFlags = 0;
    err = FSSetCatalogInfo(&g, kFSCatInfoNodeFlags, info);
    FSGetCatalogInfo(&g, kFSCatInfoNodeFlags, info, NULL, NULL, NULL);
    fprintf(out, " unlock -> %d flags %04X\n", (int)err, (unsigned)info->nodeFlags);
    err = FSSetCatalogInfo(&g, kFSCatInfoValence, info);
    fprintf(out, "  set valence -> %d\n", (int)err);
    err = FSSetCatalogInfo(&g, kFSCatInfoDataSizes, info);
    fprintf(out, "  set sizes -> %d\n", (int)err);

    /* renames */
    {
        FSRef r;
        n = U("Renamed with a much longer Unicode name than HFS allows.txt", u);
        err = FSRenameUnicode(&f, n, u, kTextEncodingUnknown, &r);
        fprintf(out, "  rename long -> %d", (int)err);
        if(!err)
            describe(&r, false);
        fprintf(out, " old ref");
        describe(&f, false);
        fprintf(out, "\n");
        if(!err)
            f = r;
        n = U("SHORT", u);
        err = FSRenameUnicode(&g, n, u, kTextEncodingUnknown, &r);
        fprintf(out, "  rename case only -> %d", (int)err);
        if(!err)
            describe(&r, false), g = r;
        fprintf(out, "\n");
        n = U("Sub folder", u);
        err = FSRenameUnicode(&g, n, u, kTextEncodingUnknown, &r);
        fprintf(out, "  rename onto existing -> %d\n", (int)err);
        err = FSRenameUnicode(&g, 0, u, kTextEncodingUnknown, &r);
        fprintf(out, "  rename empty -> %d\n", (int)err);
    }
    /* moves */
    {
        FSRef r;
        err = FSMoveObject(&g, &d, &r);
        fprintf(out, "  move into folder -> %d", (int)err);
        if(!err)
            describe(&r, false), g = r;
        fprintf(out, " old ref");
        describe(&g, false);
        fprintf(out, "\n");
        err = FSMoveObject(&d, &d, &r);
        fprintf(out, "  move folder into itself -> %d\n", (int)err);
        err = FSMoveObject(&f, &g, &r);
        fprintf(out, "  move into a file -> %d\n", (int)err);
        createU(&d, "Sub sub", true, &sub, "create sub sub");
        err = FSMoveObject(&d, &sub, &r);
        fprintf(out, "  move folder into its child -> %d\n", (int)err);
    }
    /* forks */
    {
        HFSUniStr255 data, rsrc;
        SInt16 a, b = 0, t;
        SInt64 pos, size;
        ByteCount got;
        UInt64 amount;
        long eof;
        FSForkInfo fi;
        SInt16 iter, actual;
        FSRef owner;

        FSGetDataForkName(&data);
        FSGetResourceForkName(&rsrc);
        fprintf(out, "FORKS write\n");
        err = FSOpenFork(&f, data.length, data.unicode, fsRdWrPerm, &a);
        fprintf(out, "  open rdwr -> %d\n", (int)err);
        err = FSOpenFork(&f, data.length, data.unicode, fsRdWrPerm, &b);
        fprintf(out, "  open rdwr again -> %d\n", (int)err);
        if(!err)
            FSCloseFork(b);
        err = FSOpenFork(&f, data.length, data.unicode, fsRdPerm, &t);
        fprintf(out, "  open rd while written -> %d\n", (int)err);
        if(!err)
            b = t;
        for(i = 0; i < 1000; i++)
            buf[i] = (char)(i * 7 + 3);
        err = FSWriteFork(a, fsFromStart, (pos.hi = 0, pos.lo = 0, pos), 1000, buf, &got);
        fprintf(out, "  write 1000 -> %d (%lu)", (int)err, err ? 0 : got);
        if(FSGetForkPosition(a, &pos) == noErr)
            wide64("at", pos.hi, pos.lo);
        if(FSGetForkSize(a, &size) == noErr)
            wide64("size", size.hi, size.lo);
        fprintf(out, "\n");
        err = FSWriteFork(a, fsFromLEOF, (pos.hi = 0, pos.lo = 100, pos), 10, buf, &got);
        fprintf(out, "  write past eof -> %d (%lu)", (int)err, err ? 0 : got);
        if(FSGetForkSize(a, &size) == noErr)
            wide64("size", size.hi, size.lo);
        fprintf(out, "\n");
        err = FSWriteFork(a, fsFromMark, (pos.hi = -1, pos.lo = (UInt32)-20, pos), 5, buf, &got);
        fprintf(out, "  write from mark -20 -> %d (%lu)", (int)err, err ? 0 : got);
        if(FSGetForkPosition(a, &pos) == noErr)
            wide64("at", pos.hi, pos.lo);
        fprintf(out, "\n");
        err = FSWriteFork(a, fsFromStart | 0x80, (pos.hi = 0, pos.lo = 0, pos), 5, buf, &got);
        fprintf(out, "  write newline mode -> %d\n", (int)err);
        err = b ? FSReadFork(b, fsFromStart, (pos.hi = 0, pos.lo = 0, pos), kBuf, buf, &got) : -1;
        fprintf(out, "  read by the other path -> %d (%lu)\n", (int)err, b ? got : 0);
        err = FSSetForkSize(a, fsFromStart, (pos.hi = 0, pos.lo = 500, pos));
        fprintf(out, "  setforksize 500 -> %d", (int)err);
        if(FSGetForkSize(a, &size) == noErr)
            wide64("size", size.hi, size.lo);
        if(FSGetForkPosition(a, &pos) == noErr)
            wide64("at", pos.hi, pos.lo);
        if(b && FSGetForkSize(b, &size) == noErr)
            wide64("other path", size.hi, size.lo);
        fprintf(out, "\n");
        err = FSSetForkSize(a, fsFromLEOF, (pos.hi = 0, pos.lo = 4000, pos));
        fprintf(out, "  setforksize +4000 -> %d", (int)err);
        if(FSGetForkSize(a, &size) == noErr)
            wide64("size", size.hi, size.lo);
        fprintf(out, "\n");
        {
            SInt64 off, req;
            off.hi = 0;
            off.lo = 0;
            req.hi = 0;
            req.lo = 100000;
            err = FSAllocateFork(a, kFSAllocDefaultFlags, fsFromLEOF, off, *(UInt64 *)&req, &amount);
            fprintf(out, "  allocate 100000 -> %d", (int)err);
            if(!err)
                wide64("got", amount.hi, amount.lo);
            FSGetCatalogInfo(&f, kFSCatInfoDataSizes, info, NULL, NULL, NULL);
            catinfo(kFSCatInfoDataSizes, info, false);
            fprintf(out, "\n");
            err = FSAllocateFork(a, 8, fsFromLEOF, off, *(UInt64 *)&req, &amount);
            fprintf(out, "  allocate bad flags -> %d\n", (int)err);
            req.lo = 0x7FFFFFFF;
            req.hi = 4;
            err = FSAllocateFork(a, kFSAllocAllOrNothingMask, fsFromLEOF, off, *(UInt64 *)&req, &amount);
            fprintf(out, "  allocate all-or-nothing huge -> %d", (int)err);
            fprintf(out, "\n");
        }
        fprintf(out, "  flush -> %d\n", (int)FSFlushFork(a));
        /* the open fork's control block */
        memset(&fi, 0, sizeof fi);
        err = FSGetForkCBInfo(a, 0, NULL, &actual, &fi, &owner, uname);
        fprintf(out, "  forkcbinfo -> %d", (int)err);
        if(!err)
        {
            fprintf(out, " same %d flags %02X perm %d vol-same %d", actual == a, (unsigned)(unsigned char)fi.flags,
                    (int)fi.permissions, fi.volume == vref);
            wide64("pos", fi.currentPosition.hi, fi.currentPosition.lo);
            wide64("eof", fi.logicalEOF.hi, fi.logicalEOF.lo);
            wide64("peof", fi.physicalEOF.hi, fi.physicalEOF.lo);
            fprintf(out, " fork ");
            ustr(uname);
            fprintf(out, " owner");
            describe(&owner, false);
        }
        fprintf(out, "\n");
        iter = 0;
        i = 0;
        while(FSGetForkCBInfo(0, vref, &iter, &actual, &fi, NULL, NULL) == noErr && i < 100)
            i++;
        fprintf(out, "  forkcbinfo iterate: %s\n", i >= 2 ? "two or more" : i == 1 ? "one" : "none");
        err = FSGetForkCBInfo(12345, 0, NULL, &actual, &fi, NULL, NULL);
        fprintf(out, "  forkcbinfo bad refnum -> %d\n", (int)err);
        /* the classic calls on a fork refnum */
        fprintf(out, "  classic geteof -> %d", (int)GetEOF(a, &eof));
        eof = 10;
        err = FSRead(a, &eof, buf);
        fprintf(out, " fsread -> %d\n", (int)err);
        if(b)
            FSCloseFork(b);
        fprintf(out, "  close -> %d\n", (int)FSCloseFork(a));
        /* the resource fork, created and deleted */
        err = FSOpenFork(&f, rsrc.length, rsrc.unicode, fsRdWrPerm, &a);
        fprintf(out, "  open rsrc -> %d", (int)err);
        if(!err)
        {
            err = FSWriteFork(a, fsFromStart, (pos.hi = 0, pos.lo = 0, pos), 300, buf, &got);
            fprintf(out, " write -> %d", (int)err);
            FSCloseFork(a);
        }
        fprintf(out, "\n");
        fprintf(out, "  createfork rsrc -> %d\n", (int)FSCreateFork(&f, rsrc.length, rsrc.unicode));
        fprintf(out, "  createfork data -> %d\n", (int)FSCreateFork(&f, data.length, data.unicode));
        fprintf(out, "  deletefork rsrc -> %d", (int)FSDeleteFork(&f, rsrc.length, rsrc.unicode));
        FSGetCatalogInfo(&f, kFSCatInfoRsrcSizes, info, NULL, NULL, NULL);
        catinfo(kFSCatInfoRsrcSizes, info, false);
        fprintf(out, "\n");
        fprintf(out, "  deletefork data -> %d", (int)FSDeleteFork(&f, data.length, data.unicode));
        FSGetCatalogInfo(&f, kFSCatInfoDataSizes, info, NULL, NULL, NULL);
        catinfo(kFSCatInfoDataSizes, info, false);
        fprintf(out, "\n");
        {
            static const UniChar named[] = { 'n', 'a', 'm', 'e', 'd' };
            fprintf(out, "  createfork named -> %d\n", (int)FSCreateFork(&f, 5, named));
        }
    }
    /* exchange */
    {
        FSRef p, q;
        SInt16 r;
        SInt64 pos;
        ByteCount got;
        HFSUniStr255 data;
        FSGetDataForkName(&data);
        createU(&work, "Swap one", false, &p, "create swap one");
        createU(&work, "Swap two", false, &q, "create swap two");
        if(FSOpenFork(&p, data.length, data.unicode, fsRdWrPerm, &r) == noErr)
        {
            FSWriteFork(r, fsFromStart, (pos.hi = 0, pos.lo = 0, pos), 111, buf, &got);
            FSCloseFork(r);
        }
        err = FSExchangeObjects(&p, &q);
        fprintf(out, "  exchangeobjects -> %d", (int)err);
        FSGetCatalogInfo(&p, kFSCatInfoDataSizes, info, uname, NULL, NULL);
        fprintf(out, " one ");
        ustr(uname);
        catinfo(kFSCatInfoDataSizes, info, false);
        FSGetCatalogInfo(&q, kFSCatInfoDataSizes, info, uname, NULL, NULL);
        fprintf(out, " two ");
        ustr(uname);
        catinfo(kFSCatInfoDataSizes, info, false);
        fprintf(out, "\n");
        fprintf(out, "  exchangeobjects with folder -> %d\n", (int)FSExchangeObjects(&p, &d));
        fprintf(out, "  exchangeobjects itself -> %d\n", (int)FSExchangeObjects(&p, &p));
    }
    listWork("before deletes");
    /* deletes */
    fprintf(out, "DELETE\n");
    fprintf(out, "  folder with items -> %d\n", (int)FSDeleteObject(&d));
    {
        FSIterator it;
        ItemCount got;
        FSRef *r = (FSRef *)NewPtr(sizeof(FSRef) * 20);
        int rounds;
        for(rounds = 0; rounds < 5; rounds++)
        {
            /* everything in the work folder and its folders, deepest first */
            if(FSOpenIterator(&work, kFSIterateFlat, &it))
                break;
            err = FSGetCatalogInfoBulk(it, 20, &got, NULL, kFSCatInfoNone, NULL, r, NULL, NULL);
            FSCloseIterator(it);
            if(!got)
                break;
            for(i = 0; i < got; i++)
            {
                FSIterator in;
                ItemCount gin;
                FSRef *r2 = (FSRef *)NewPtr(sizeof(FSRef) * 20);
                if(FSOpenIterator(&r[i], kFSIterateFlat, &in) == noErr)
                {
                    FSGetCatalogInfoBulk(in, 20, &gin, NULL, kFSCatInfoNone, NULL, r2, NULL, NULL);
                    FSCloseIterator(in);
                    while(gin--)
                    {
                        FSIterator in2;
                        ItemCount g2;
                        FSRef *r3 = (FSRef *)NewPtr(sizeof(FSRef) * 20);
                        if(FSOpenIterator(&r2[gin], kFSIterateFlat, &in2) == noErr)
                        {
                            FSGetCatalogInfoBulk(in2, 20, &g2, NULL, kFSCatInfoNone, NULL, r3, NULL, NULL);
                            FSCloseIterator(in2);
                            while(g2--)
                                FSDeleteObject(&r3[g2]);
                        }
                        DisposePtr((Ptr)r3);
                        FSDeleteObject(&r2[gin]);
                    }
                }
                DisposePtr((Ptr)r2);
                err = FSDeleteObject(&r[i]);
                if(err)
                {
                    info->nodeFlags = 0;
                    FSSetCatalogInfo(&r[i], kFSCatInfoNodeFlags, info);
                    err = FSDeleteObject(&r[i]);
                }
            }
        }
        DisposePtr((Ptr)r);
    }
    listWork("after deletes");
    fprintf(out, "  work folder -> %d\n", (int)FSDeleteObject(&work));
    fprintf(out, "  deleted ref");
    describe(&work, false);
    fprintf(out, "\n");
}

/* "big NAME": every file in the volume's root through the fork calls, at
   positions a long can't hold */
static void big(void)
{
    FSIterator it;
    ItemCount got;
    FSRef *r = (FSRef *)NewPtr(sizeof(FSRef) * 16);
    HFSUniStr255 *nm = (HFSUniStr255 *)NewPtr(sizeof(HFSUniStr255) * 16);
    HFSUniStr255 data;
    int i;

    fprintf(out, "BIG\n");
    FSGetDataForkName(&data);
    if(FSOpenIterator(&root, kFSIterateFlat, &it))
        return;
    FSGetCatalogInfoBulk(it, 16, &got, NULL, kFSCatInfoNone, NULL, r, NULL, nm);
    FSCloseIterator(it);
    for(i = 0; i < got; i++)
    {
        SInt16 ref;
        SInt64 size, pos;
        ByteCount n;
        OSErr err;
        unsigned long sum;
        int k;

        if(nm[i].length && nm[i].unicode[0] == 'D')
            continue;                           /* the Desktop files */
        FSGetCatalogInfo(&r[i], kFSCatInfoDataSizes | kFSCatInfoNodeFlags, info, NULL, NULL, NULL);
        if(info->nodeFlags & kFSNodeIsDirectoryMask)
            continue;
        fprintf(out, "  ");
        ustr(&nm[i]);
        catinfo(kFSCatInfoDataSizes, info, false);
        err = FSOpenFork(&r[i], data.length, data.unicode, fsRdPerm, &ref);
        fprintf(out, " open -> %d", (int)err);
        if(err)
        {
            fprintf(out, "\n");
            continue;
        }
        FSGetForkSize(ref, &size);
        wide64("size", size.hi, size.lo);
        /* across 2 GB */
        pos.hi = 0;
        pos.lo = 0x7FFFFFF8UL;
        err = FSReadFork(ref, fsFromStart, pos, 16, buf, &n);
        for(sum = 0, k = 0; k < n; k++)
            sum = sum * 31 + (unsigned char)buf[k];
        fprintf(out, " read@2G-8 -> %d (%lu) %08lX", (int)err, err && err != eofErr ? 0 : n, sum);
        FSGetForkPosition(ref, &pos);
        wide64("at", pos.hi, pos.lo);
        /* the last 16 bytes */
        pos.hi = -1;
        pos.lo = (UInt32)-16;
        err = FSReadFork(ref, fsFromLEOF, pos, 64, buf, &n);
        for(sum = 0, k = 0; k < n; k++)
            sum = sum * 31 + (unsigned char)buf[k];
        fprintf(out, " tail -> %d (%lu) %08lX", (int)err, n, sum);
        FSGetForkPosition(ref, &pos);
        wide64("at", pos.hi, pos.lo);
        pos.hi = 0;
        pos.lo = 1;
        fprintf(out, " pastend -> %d", (int)FSSetForkPosition(ref, fsFromLEOF, pos));
        FSCloseFork(ref);
        fprintf(out, "\n");
    }
    DisposePtr((Ptr)r);
    DisposePtr((Ptr)nm);
}

int main(void)
{
    FILE *cfg;
    char line[80];

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    InitCursor();

    buf = NewPtr(kBuf);
    uname = (HFSUniStr255 *)NewPtr(sizeof(HFSUniStr255));
    info = (FSCatalogInfo *)NewPtr(sizeof(FSCatalogInfo));
    out = fopen("Host:fsreftest.txt", "w");
    cfg = fopen("Host:fsreftest.cfg", "r");
    if(!out || !buf || !cfg || !uname || !info)
        return 1;
    fprintf(out, "SIZES FSRef %d FSCatalogInfo %d FSRefParam %d FSForkIOParam %d FSCatalogBulkParam %d"
                 " FSForkInfo %d FSForkCBInfoParam %d FSVolumeInfo %d FSVolumeInfoParam %d FSSearchParams %d\n",
            (int)sizeof(FSRef), (int)sizeof(FSCatalogInfo), (int)sizeof(FSRefParam), (int)sizeof(FSForkIOParam),
            (int)sizeof(FSCatalogBulkParam), (int)sizeof(FSForkInfo), (int)sizeof(FSForkCBInfoParam),
            (int)sizeof(FSVolumeInfo), (int)sizeof(FSVolumeInfoParam), (int)sizeof(FSSearchParams));
    while(fgets(line, sizeof line, cfg))
    {
        int n = strcspn(line, "\r\n");
        line[n] = 0;
        if(!strncmp(line, "read ", 5))
        {
            volume(line + 5);
            if(!vref)
                continue;
            refs();
            infos();
            iterate(&root, "root", true);
            forksRead();
            search();
        }
        else if(!strncmp(line, "big ", 4))
        {
            volume(line + 4);
            if(vref)
                big();
        }
        else if(!strncmp(line, "write ", 6))
        {
            writing = true;
            volume(line + 6);
            if(vref)
                writes();
        }
    }
    fclose(cfg);
    fprintf(out, "---\n");
    fclose(out);
    return 0;
}
