/*
 * CollectionTest.c -- the Collection Manager from PowerPC code
 * (CollectionsLib): items by tag and ID, their order, attributes, locked
 * items, tags, owners, copying, exception procedures, flattening (to a
 * handle and through a procedure) and back. On Executor, collections.cpp;
 * on the G3 with 9.0.4, Apple's. One line per check, "PASS name" or
 * "FAIL name: why"; "INFO" lines say what was seen (the flattened bytes
 * among them); then "---", in Host:collection_results.txt
 * (tests/test_guest_suite.sh --results collection_results.txt). Built
 * with Retro68 by build-retro68.sh.
 */

#include <Types.h>
#include <Memory.h>
#include <Collections.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok, const char *why, long code)
{
    if(ok)
        printf("PASS %s\n", name);
    else
    {
        printf("FAIL %s: %s (%ld)\n", name, why, code);
        failures++;
    }
}

static void tagstr(char *out, CollectionTag tag)
{
    out[0] = (char)(tag >> 24);
    out[1] = (char)(tag >> 16);
    out[2] = (char)(tag >> 8);
    out[3] = (char)tag;
    out[4] = 0;
}

static void dump(const char *what, const unsigned char *p, long n)
{
    long i;
    printf("INFO %s (%ld bytes):", what, n);
    for(i = 0; i < n; i++)
        printf("%s%02x", (i % 4) ? "" : " ", p[i]);
    printf("\n");
}

static void list(const char *what, Collection c)
{
    LONGINT n = CountCollectionItems(c), i;
    printf("INFO %s: %ld items", what, (long)n);
    for(i = 1; i <= n; i++)
    {
        CollectionTag tag = 0;
        LONGINT id = 0, size = 0, attrs = 0;
        char t[5];
        OSErr err = GetIndexedCollectionItemInfo(c, i, &tag, &id, &size, &attrs);
        tagstr(t, tag);
        printf(" [%ld %s/%ld size %ld attrs %08lx err %d]", (long)i, t, (long)id, (long)size,
               (unsigned long)attrs, err);
    }
    printf("\n");
}

/* The items as "TAG/id:size:attrs ..." -- compared with what Apple's
   Collection Manager on 9.0.4 gives (the INFO lines show both). */
static char listing[512];
static int withSizes = 1;
static const char *items(Collection c)
{
    LONGINT n = CountCollectionItems(c), i;
    listing[0] = 0;
    for(i = 1; i <= n; i++)
    {
        CollectionTag tag = 0;
        LONGINT id = 0, size = 0, attrs = 0;
        char t[5], one[48];
        GetIndexedCollectionItemInfo(c, i, &tag, &id, &size, &attrs);
        tagstr(t, tag);
        if(withSizes)
            sprintf(one, "%s%s/%ld:%ld:%08lx", i > 1 ? " " : "", t, (long)id, (long)size, (unsigned long)attrs);
        else
            sprintf(one, "%s%s/%ld:%08lx", i > 1 ? " " : "", t, (long)id, (unsigned long)attrs);
        if(strlen(listing) + strlen(one) < sizeof listing)
            strcat(listing, one);
    }
    return listing;
}

static void expect(const char *name, const char *got, const char *want)
{
    if(!strcmp(got, want))
        printf("PASS %s\n", name);
    else
    {
        printf("FAIL %s: got \"%s\", want \"%s\"\n", name, got, want);
        failures++;
    }
}

/* Flattening through a procedure: what it is handed, appended. */
static unsigned char stream[1024];
static long streamLen, flattenCalls;

static pascal OSErr flattenProc(LONGINT size, void *data, void *refCon)
{
    flattenCalls++;
    if(streamLen + size > (long)sizeof stream)
        return memFullErr;
    memcpy(stream + streamLen, data, size);
    streamLen += size;
    return noErr;
}

/* ... and unflattening: what it is asked for, from the stream. */
static long readPos, unflattenCalls;
static pascal OSErr unflattenProc(LONGINT size, void *data, void *refCon)
{
    unflattenCalls++;
    if(readPos + size > streamLen)
        return eofErr;
    memcpy(data, stream + readPos, size);
    readPos += size;
    return noErr;
}

static long exceptionCalls;
static OSErr exceptionStatus;
static pascal OSErr exceptionProc(Collection c, OSErr status)
{
    exceptionCalls++;
    exceptionStatus = status;
    return 1234;
}

int main(void)
{
    Collection c, c2, c3;
    OSErr err;
    LONGINT size, attrs, id, index;
    CollectionTag tag;
    unsigned char buf[16];
    char t[5];
    Handle h;
    int i;

    freopen("Host:collection_results.txt", "w", stdout);

    /* ---- a new collection ---- */
    c = NewCollection();
    check("new", c != NULL, "NewCollection nil", 0);
    if(!c)
    {
        printf("DONE %d\n---\n", failures);
        return 0;
    }
    check("new empty", CountCollectionItems(c) == 0, "items", CountCollectionItems(c));
    printf("INFO default attributes %08lx\n", (unsigned long)GetCollectionDefaultAttributes(c));
    printf("INFO owners %ld\n", (long)CountCollectionOwners(c));

    /* ---- items: added out of order ---- */
    err = AddCollectionItem(c, 'BBBB', 2, 3, "two");
    check("add B/2", err == noErr, "err", err);
    err = AddCollectionItem(c, 'AAAA', 5, 4, "five");
    check("add A/5", err == noErr, "err", err);
    err = AddCollectionItem(c, 'BBBB', 1, 3, "one");
    check("add B/1", err == noErr, "err", err);
    err = AddCollectionItem(c, 'AAAA', -1, 5, "minus");
    check("add A/-1", err == noErr, "err", err);
    err = AddCollectionItem(c, 'CCCC', 0, 0, NULL);
    check("add C/0 empty", err == noErr, "err", err);
    check("count 5", CountCollectionItems(c) == 5, "items", CountCollectionItems(c));
    list("after adding B/2 A/5 B/1 A/-1 C/0", c);
    expect("order: tags, then IDs", items(c),
           "AAAA/-1:5:40000000 AAAA/5:4:40000000 BBBB/1:3:40000000 BBBB/2:3:40000000 CCCC/0:0:40000000");
    check("default attributes", GetCollectionDefaultAttributes(c) == 0x40000000, "attrs",
          GetCollectionDefaultAttributes(c));

    /* GetCollectionItem: the size alone, then the data into a larger
       buffer with a smaller size given */
    size = 0;
    err = GetCollectionItem(c, 'AAAA', 5, &size, NULL);
    check("get size", err == noErr && size == 4, "err/size", err ? err : size);
    memset(buf, 0xEE, sizeof buf);
    size = 2;
    err = GetCollectionItem(c, 'AAAA', 5, &size, buf);
    printf("INFO get A/5 with size 2 in: err %d size out %ld\n", err, (long)size);
    dump("buffer after", buf, 8);
    check("get with a size: copies that much, gives the item's",
          err == noErr && size == 4 && buf[0] == 'f' && buf[1] == 'i' && buf[2] == 0xEE, "size", size);
    memset(buf, 0xEE, sizeof buf);
    err = GetCollectionItem(c, 'AAAA', 5, NULL, buf);
    printf("INFO get A/5 with no size: err %d\n", err);
    dump("buffer after", buf, 8);
    check("get with no size: copies it all", err == noErr && !memcmp(buf, "five", 4) && buf[4] == 0xEE,
          "err", err);
    err = GetCollectionItem(c, 'AAAA', 6, &size, NULL);
    check("get missing", err == collectionItemNotFoundErr, "err", err);
    err = GetIndexedCollectionItem(c, 99, &size, NULL);
    check("indexed out of range", err == collectionIndexRangeErr, "err", err);
    err = GetIndexedCollectionItem(c, 0, &size, NULL);
    printf("INFO indexed 0: err %d\n", err);
    check("indexed 0", err == collectionIndexRangeErr, "err", err);

    /* item info: an existing item's attributes, then replaced */
    err = SetCollectionItemInfo(c, 'BBBB', 1, 0x0000FFFF, 0x00000005);
    check("set info", err == noErr, "err", err);
    err = GetCollectionItemInfo(c, 'BBBB', 1, &index, &size, &attrs);
    printf("INFO B/1 info: err %d index %ld size %ld attrs %08lx\n", err, (long)index, (long)size,
           (unsigned long)attrs);
    err = AddCollectionItem(c, 'BBBB', 1, 6, "ONE!!!");
    check("replace B/1", err == noErr, "err", err);
    err = GetCollectionItemInfo(c, 'BBBB', 1, &index, &size, &attrs);
    printf("INFO B/1 after replacing: err %d index %ld size %ld attrs %08lx\n", err, (long)index,
           (long)size, (unsigned long)attrs);
    check("replacing keeps the attributes", err == noErr && index == 3 && size == 6 && attrs == 0x40000005,
          "attrs", attrs);

    /* ---- tags ---- */
    printf("INFO tags %ld:", (long)CountCollectionTags(c));
    for(i = 1; i <= CountCollectionTags(c); i++)
    {
        err = GetIndexedCollectionTag(c, i, &tag);
        tagstr(t, tag);
        printf(" %s(%d)", t, err);
    }
    printf("\n");
    check("tag exists", CollectionTagExists(c, 'AAAA') && !CollectionTagExists(c, 'ZZZZ'), "exists", 0);
    check("tagged count", CountTaggedCollectionItems(c, 'BBBB') == 2, "count",
          CountTaggedCollectionItems(c, 'BBBB'));
    err = GetTaggedCollectionItemInfo(c, 'BBBB', 1, &id, &index, &size, &attrs);
    printf("INFO first BBBB: err %d id %ld index %ld size %ld\n", err, (long)id, (long)index, (long)size);
    check("first BBBB", err == noErr && id == 1 && index == 3 && size == 6, "id", id);
    err = GetTaggedCollectionItem(c, 'BBBB', 3, &size, NULL);
    printf("INFO third BBBB: err %d\n", err);
    check("third BBBB", err == collectionIndexRangeErr, "err", err);
    {
        char got[64] = "";
        for(i = 1; i <= CountCollectionTags(c); i++)
        {
            GetIndexedCollectionTag(c, i, &tag);
            tagstr(t, tag);
            strcat(got, t);
        }
        expect("tags in order", got, "AAAABBBBCCCC");
    }

    /* ---- locked items ---- */
    err = SetCollectionItemInfo(c, 'AAAA', 5, (LONGINT)kCollectionLockMask, (LONGINT)kCollectionLockMask);
    check("lock A/5", err == noErr, "err", err);
    printf("INFO locked: replace %d", AddCollectionItem(c, 'AAAA', 5, 1, "x"));
    printf(" remove %d", RemoveCollectionItem(c, 'AAAA', 5));
    err = GetCollectionItemInfo(c, 'AAAA', 5, &index, &size, &attrs);
    printf(" removeindexed %d", RemoveIndexedCollectionItem(c, index));
    printf(" replaceindexed %d", ReplaceIndexedCollectionItem(c, index, 1, "x"));
    printf(" setinfo user %d", SetCollectionItemInfo(c, 'AAAA', 5, 0x1, 0x1));
    printf(" unlock %d\n", SetCollectionItemInfo(c, 'AAAA', 5, (LONGINT)kCollectionLockMask, 0));
    err = SetCollectionItemInfo(c, 'AAAA', 5, (LONGINT)kCollectionLockMask, (LONGINT)kCollectionLockMask);
    list("A/5 locked again", c);

    /* ---- the default attributes, for later items ---- */
    SetCollectionDefaultAttributes(c, 0x0000FFFF, 0x00000002);
    printf("INFO default attributes now %08lx\n", (unsigned long)GetCollectionDefaultAttributes(c));
    err = AddCollectionItem(c, 'DDDD', 7, 2, "hi");
    err = GetCollectionItemInfo(c, 'DDDD', 7, &index, &size, &attrs);
    printf("INFO D/7 attrs %08lx\n", (unsigned long)attrs);
    check("default attributes for new items", attrs == 0x40000002, "attrs", attrs);
    /* one not persistent */
    err = AddCollectionItem(c, 'EEEE', 1, 4, "temp");
    err = SetCollectionItemInfo(c, 'EEEE', 1, (LONGINT)kCollectionPersistenceMask, 0);
    check("not persistent", err == noErr, "err", err);
    list("before flattening", c);

    /* ---- flattening ---- */
    h = NewHandle(0);
    err = FlattenCollectionToHdl(c, h);
    check("flatten to handle", err == noErr, "err", err);
    HLock(h);
    dump("flattened to handle", (unsigned char *)*h, GetHandleSize(h));
    HUnlock(h);
    streamLen = flattenCalls = 0;
    err = FlattenCollection(c, NewCollectionFlattenUPP(flattenProc), NULL);
    check("flatten", err == noErr, "err", err);
    printf("INFO flatten calls %ld, %ld bytes, same as the handle's: %d\n", flattenCalls, streamLen,
           streamLen == GetHandleSize(h) && !memcmp(stream, *h, streamLen));
    streamLen = flattenCalls = 0;
    err = FlattenPartialCollection(c, NewCollectionFlattenUPP(flattenProc), NULL, 0x0000FFFF, 0x00000005);
    printf("INFO flatten partial user 5: err %d\n", err);
    dump("partial", stream, streamLen);

    c2 = NewCollection();
    err = UnflattenCollectionFromHdl(c2, h);
    check("unflatten from handle", err == noErr, "err", err);
    list("unflattened from handle", c2);
    /* Without sizes: Apple's 9.0.4 swaps the data offsets of an item
       replaced by larger data and its neighbour in the stream it writes
       (both A/-1 and B/1 come back with the other's size); the plain
       collection below is checked byte for byte. */
    withSizes = 0;
    expect("unflattened: the persistent items", items(c2),
           "AAAA/-1:40000000 BBBB/1:40000005 CCCC/0:40000000 DDDD/7:40000002");
    withSizes = 1;
    check("unflattened default attributes", GetCollectionDefaultAttributes(c2) == 0x40000002, "attrs",
          GetCollectionDefaultAttributes(c2));
    printf("INFO unflattened default attributes %08lx\n", (unsigned long)GetCollectionDefaultAttributes(c2));
    readPos = unflattenCalls = 0;
    streamLen = flattenCalls = 0;
    FlattenCollection(c, NewCollectionFlattenUPP(flattenProc), NULL);
    c3 = NewCollection();
    AddCollectionItem(c3, 'AAAA', 5, 3, "old");
    AddCollectionItem(c3, 'ZZZZ', 9, 3, "zed");
    err = UnflattenCollection(c3, NewCollectionFlattenUPP(unflattenProc), NULL);
    printf("INFO unflatten into a collection with items: err %d calls %ld read %ld\n", err,
           unflattenCalls, readPos);
    list("unflattened into A/5 old, Z/9", c3);
    withSizes = 0;
    expect("unflattening empties the collection first", items(c3),
           "AAAA/-1:40000000 BBBB/1:40000005 CCCC/0:40000000 DDDD/7:40000002");
    withSizes = 1;
    DisposeCollection(c3);

    /* a bad version */
    HLock(h);
    (*h)[0] = 9;
    HUnlock(h);
    c3 = NewCollection();
    err = UnflattenCollectionFromHdl(c3, h);
    check("unflatten bad version", err == collectionVersionErr, "err", err);
    DisposeCollection(c3);

    /* ---- the handle routines ---- */
    SetHandleSize(h, 3);
    BlockMoveData("abc", *h, 3);
    err = AddCollectionItemHdl(c, 'FFFF', 1, h);
    err = GetCollectionItem(c, 'FFFF', 1, &size, NULL);
    check("add from handle", err == noErr && size == 3, "err/size", err ? err : size);
    DisposeHandle(h);

    /* ---- owners, copies ---- */
    c3 = CloneCollection(c);
    printf("INFO clone is the same collection: %d, owners %ld\n", c3 == c, (long)CountCollectionOwners(c));
    check("clone: the same, one more owner", c3 == c && CountCollectionOwners(c) == 2, "owners",
          CountCollectionOwners(c));
    DisposeCollection(c3);
    printf("INFO owners after one dispose %ld\n", (long)CountCollectionOwners(c));
    check("dispose: one owner less", CountCollectionOwners(c) == 1, "owners", CountCollectionOwners(c));
    c3 = NewCollection();
    AddCollectionItem(c3, 'ZZZZ', 9, 3, "zed");
    c3 = CopyCollection(c, c3);
    list("copied over a collection with Z/9", c3);
    expect("copy: all the items, the target emptied", items(c3),
           "AAAA/-1:5:40000000 BBBB/1:6:40000005 CCCC/0:1:40000000 DDDD/7:2:40000002 EEEE/1:4:00000002 FFFF/1:3:40000002");
    check("copy: the default attributes", GetCollectionDefaultAttributes(c3) == 0x40000002, "attrs",
          GetCollectionDefaultAttributes(c3));
    printf("INFO copy's default attributes %08lx\n", (unsigned long)GetCollectionDefaultAttributes(c3));
    DisposeCollection(c3);

    /* ---- exception procedures ---- */
    SetCollectionExceptionProc(c2, NewCollectionExceptionUPP(exceptionProc));
    check("exception proc set", GetCollectionExceptionProc(c2) != NULL, "nil", 0);
    err = GetCollectionItem(c2, 'QQQQ', 1, &size, NULL);
    printf("INFO with an exception procedure: err %d calls %ld status %d\n", err, exceptionCalls,
           exceptionStatus);
    check("exception procedure: called, its result returned",
          err == 1234 && exceptionCalls == 1 && exceptionStatus == collectionItemNotFoundErr, "err", err);

    /* ---- purging, emptying ---- */
    PurgeCollectionTag(c2, 'BBBB');
    list("purged tag BBBB", c2);
    withSizes = 0;
    expect("purge a tag", items(c2), "AAAA/-1:40000000 CCCC/0:40000000 DDDD/7:40000002");
    withSizes = 1;
    PurgeCollection(c, 0x0000FFFF, 0x00000002);
    list("purged user attributes 2", c);
    expect("purge by attributes", items(c), "AAAA/-1:5:40000000 BBBB/1:6:40000005 CCCC/0:1:40000000");
    EmptyCollection(c);
    list("emptied", c);
    check("empty", CountCollectionItems(c) == 0, "items", CountCollectionItems(c));

    /* ---- a locked item, operation by operation, each on a fresh one ---- */
    {
        static const char *names[] = { "remove", "removeindexed", "replaceindexed", "setinfo user",
                                       "unlock", "purge", "purgetag", "empty" };
        int op;
        printf("INFO locked item:");
        for(op = 0; op < 8; op++)
        {
            LONGINT n;
            c3 = NewCollection();
            AddCollectionItem(c3, 'LLLL', 1, 4, "lock");
            AddCollectionItem(c3, 'MMMM', 1, 4, "free");
            SetCollectionItemInfo(c3, 'LLLL', 1, (LONGINT)kCollectionLockMask, (LONGINT)kCollectionLockMask);
            err = 0;
            switch(op)
            {
                case 0: err = RemoveCollectionItem(c3, 'LLLL', 1); break;
                case 1: err = RemoveIndexedCollectionItem(c3, 1); break;
                case 2: err = ReplaceIndexedCollectionItem(c3, 1, 2, "xx"); break;
                case 3: err = SetCollectionItemInfo(c3, 'LLLL', 1, 0x1, 0x1); break;
                case 4: err = SetCollectionItemInfo(c3, 'LLLL', 1, (LONGINT)kCollectionLockMask, 0); break;
                case 5: PurgeCollection(c3, 0, 0); break;
                case 6: PurgeCollectionTag(c3, 'LLLL'); break;
                case 7: EmptyCollection(c3); break;
            }
            n = CountCollectionItems(c3);
            size = 0;
            attrs = 0;
            GetCollectionItemInfo(c3, 'LLLL', 1, &index, &size, &attrs);
            printf(" %s=%d/%ld items/size %ld/attrs %08lx", names[op], err, (long)n,
                   (long)size, (unsigned long)attrs);
            {
                /* Apple's: the lock keeps an item's data from being
                   replaced, nothing else */
                static const struct { int err; long n; long size; unsigned long attrs; } want[] = {
                    { 0, 1, 0, 0 }, { 0, 1, 0, 0 }, { collectionItemLockedErr, 2, 4, 0xc0000000 },
                    { 0, 2, 4, 0xc0000001 }, { 0, 2, 4, 0x40000000 }, { 0, 0, 0, 0 },
                    { 0, 1, 0, 0 }, { 0, 0, 0, 0 } };
                char name[48];
                sprintf(name, "locked item: %s", names[op]);
                check(name, err == want[op].err && n == want[op].n && size == want[op].size
                      && (unsigned long)attrs == want[op].attrs, "err/items", err ? err : n);
            }
            DisposeCollection(c3);
        }
        printf("\n");
    }

    /* ---- the handle routines on an existing item ---- */
    c3 = NewCollection();
    AddCollectionItem(c3, 'HHHH', 1, 5, "hello");
    h = NewHandle(100);
    err = GetCollectionItemHdl(c3, 'HHHH', 1, h);
    printf("INFO item into a 100-byte handle: err %d size %ld\n", err, (long)GetHandleSize(h));
    check("item into a handle: resized", err == noErr && GetHandleSize(h) == 5, "size", GetHandleSize(h));
    SetHandleSize(h, 2);
    err = GetIndexedCollectionItemHdl(c3, 1, h);
    printf("INFO indexed item into a 2-byte handle: err %d size %ld\n", err, (long)GetHandleSize(h));
    SetHandleSize(h, 3);
    BlockMoveData("xyz", *h, 3);
    err = ReplaceIndexedCollectionItemHdl(c3, 1, h);
    err = GetCollectionItem(c3, 'HHHH', 1, &size, NULL);
    printf("INFO replaced from a handle: err %d size %ld\n", err, (long)size);
    memset(buf, 0xEE, sizeof buf);
    size = 2;
    err = GetIndexedCollectionItem(c3, 1, &size, buf);
    printf("INFO indexed with size 2 in: err %d size %ld\n", err, (long)size);
    dump("buffer after", buf, 4);
    check("indexed with a size", err == noErr && size == 3 && buf[0] == 'x' && buf[1] == 'y' && buf[2] == 0xEE,
          "size", size);
    DisposeHandle(h);

    /* ---- a plain collection, flattened: the format byte for byte ---- */
    DisposeCollection(c3);
    c3 = NewCollection();
    AddCollectionItem(c3, 'TEST', 3, 4, "bird");
    AddCollectionItem(c3, 'TEST', 2, 3, "foo");
    AddCollectionItem(c3, 'ABCD', 1, 0, NULL);
    SetCollectionItemInfo(c3, 'TEST', 2, 0x0000FFFF, 0x00000102);
    h = NewHandle(0);
    err = FlattenCollectionToHdl(c3, h);
    HLock(h);
    dump("plain flattened", (unsigned char *)*h, GetHandleSize(h));
    {
        /* the header and the item array as Apple writes them (the data
           block's order and its pad bytes are its own business: each item
           says where its data is) */
        static const unsigned char head[] = {
            0x00,0x01,0x00,0x00, 0x40,0x00,0x00,0x00, 0x00,0x00,0x00,0x03,
            'A','B','C','D', 0,0,0,1, 0x40,0,0,0, 0,0,0,0,
            'T','E','S','T', 0,0,0,2, 0x40,0,0x01,0x02 };
        static const unsigned char third[] = { 'T','E','S','T', 0,0,0,3, 0x40,0,0,0 };
        unsigned char *f = (unsigned char *)*h;
        check("flattened: header and items", GetHandleSize(h) == 80 && !memcmp(f, head, sizeof head)
              && !memcmp(f + 44, third, sizeof third) && f[63] == 0x10, "size", GetHandleSize(h));
        c2 = NewCollection();
        err = UnflattenCollectionFromHdl(c2, h);
        expect("flattened and back", items(c2), "ABCD/1:0:40000000 TEST/2:3:40000102 TEST/3:4:40000000");
        size = 3;
        memset(buf, 0, sizeof buf);
        GetCollectionItem(c2, 'TEST', 2, &size, buf);
        check("flattened and back: the data", !memcmp(buf, "foo", 3), "size", size);
        DisposeCollection(c2);
        c2 = NewCollection();
    }
    DisposeHandle(h);
    DisposeCollection(c3);

    c3 = GetNewCollection(12345);
    printf("INFO GetNewCollection of a missing resource: %d\n", c3 != NULL);

    DisposeCollection(c);
    DisposeCollection(c2);
    printf("DONE %d\n---\n", failures);
    return 0;
}
