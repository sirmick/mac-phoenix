/* QTCodecProbe: Apple's QuickTime components without QuickTime's INIT
   (route B in docs/executor/QuickTime.md). An application: the components
   by type (Executor registers QuickTime(TM)'s at startup; without them it
   registers the file itself), then the 'raw ' and 'rle ' decompressors
   driven through the codec component interface as the Image Compression
   Manager would (GetCodecInfo, PreDecompress, BandDecompress into a
   GWorld), not through the ICM. One line per step, then "---", to
   Host:<suite.cfg's name> (run by tests/test_guest_suite.sh), else
   qtprobe.txt next to it; the decoded pixels are printed so the run can
   be compared with a ROM machine's.

     tools/mpw/mpw build tools/qt/probe
     tests/test_guest_suite.sh --app tools/qt/probe/build/QTCodecProbe.bin \
         --results qtprobe.txt --backend executor --os-version 7.6.1 */
#include <Types.h>
#include <Memory.h>
#include <Files.h>
#include <Folders.h>
#include <Resources.h>
#include <Components.h>
#include <Gestalt.h>
#include <OSUtils.h>
#include <Traps.h>
#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* The codec calls, as Universal Interfaces' ImageCodec.h glue (written
   out here: MPW 3.4.2's headers predate some of the structures). */
typedef struct {
    long flags;
    short wantedPixelSize, extendWidth, extendHeight, bandMin, bandInc, pad;
    unsigned long time;
} Caps;

typedef struct {
    long idSize;
    OSType cType;
    long resvd1;
    short resvd2, dataRefIndex, version, revisionLevel;
    long vendor, temporalQuality, spatialQuality;
    short width, height;
    Fixed hRes, vRes;
    long dataSize;
    short frameCount;
    unsigned char name[32];
    short depth, clutID;
} ImageDesc;

typedef struct { long proc, refcon; } ProcRec;

typedef struct {
    long sequenceID;
    Handle imageDescription;
    Ptr data;
    long bufferSize, frameNumber, startLine, stopLine, conditionFlags;
    short callerFlags;
    Caps *capabilities;
    ProcRec progress, completion, dataProc;
    CGrafPtr port;
    PixMap dstPixMap;
    BitMapPtr maskBits;
    PixMapPtr mattePixMap;
    Rect srcRect;
    void *matrix;
    long accuracy;
    short transferMode;
    void *frameTime;
    long reserved[1];
    char matrixFlags, matrixType;
    Rect dstRect;
    unsigned short majorSeed, minorSeed;
    void *sourceData;
    RgnHandle maskRegion;
    OSType **wantedTypes;
    long floodMethod, floodValue;
    short preferredOffscreenPixelSize;
    char spare[64];
} DecompParams;

typedef struct { Fixed m[3][3]; } Matrix;

QDGlobals qd;
static FILE *out;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fflush(out);
}
#define printf say

pascal ComponentResult CodecGetCodecInfo(ComponentInstance ci, void *info)
    = {0x2F3C, 0x0004, 0x0000, 0x7000, 0xA82A};
pascal ComponentResult CodecPreDecompress(ComponentInstance ci, DecompParams *p)
    = {0x2F3C, 0x0004, 0x0005, 0x7000, 0xA82A};
pascal ComponentResult CodecBandDecompress(ComponentInstance ci, DecompParams *p)
    = {0x2F3C, 0x0004, 0x0006, 0x7000, 0xA82A};

static long Call(ComponentInstance ci, short what, void *p1)
{
    if(what == 0)
        return CodecGetCodecInfo(ci, p1);
    if(what == 5)
        return CodecPreDecompress(ci, (DecompParams *)p1);
    return CodecBandDecompress(ci, (DecompParams *)p1);
}

static void typestr(OSType t, char *s)
{
    memcpy(s, &t, 4);
    s[4] = 0;
}

static void pstr(const unsigned char *p, char *s)
{
    memcpy(s, p + 1, p[0]);
    s[p[0]] = 0;
}

static long Count(OSType type, OSType sub)
{
    ComponentDescription d;
    memset(&d, 0, sizeof d);
    d.componentType = type;
    d.componentSubType = sub;
    return CountComponents(&d);
}

static void ListType(OSType type, int show)
{
    ComponentDescription d, info;
    Component c = 0;
    Handle name = NewHandle(0);
    char t[5], st[5], n[256];
    long k = 0;
    memset(&d, 0, sizeof d);
    d.componentType = type;
    typestr(type, t);
    while((c = FindNextComponent(c, &d)) != 0)
    {
        k++;
        if(show && GetComponentInfo(c, &info, name, 0, 0) == noErr)
        {
            typestr(info.componentSubType, st);
            HLock(name);
            if(GetHandleSize(name) > 0)
                pstr((unsigned char *)*name, n);
            else
                n[0] = 0;
            HUnlock(name);
            printf("  %s '%s' %s\n", t, st, n);
        }
    }
    printf("components '%s': %ld\n", t, k);
    DisposeHandle(name);
}

/* Decode one frame with codec SUB into a 32-bit GWorld w x h; print the
   pixels. */
static void Decode(OSType sub, short depth, Ptr data, long size, short w, short h)
{
    ComponentDescription d;
    Component c;
    ComponentInstance ci;
    ComponentResult r;
    GWorldPtr gw;
    PixMapHandle pm;
    Rect rc;
    Handle idh;
    ImageDesc *id;
    DecompParams p;
    Caps caps;
    Matrix m;
    char st[5], info[128];
    short y, x;
    typestr(sub, st);
    memset(&d, 0, sizeof d);
    d.componentType = 'imdc';
    d.componentSubType = sub;
    c = FindNextComponent(0, &d);
    if(!c)
    {
        printf("decode '%s': no decompressor\n", st);
        return;
    }
    ci = OpenComponent(c);
    printf("decode '%s': OpenComponent %s\n", st, ci ? "ok" : "failed");
    if(!ci)
        return;
    memset(info, 0, sizeof info);
    r = Call(ci, 0, info);
    printf("decode '%s': GetCodecInfo %ld\n", st, (long)r);

    SetRect(&rc, 0, 0, w, h);
    if(NewGWorld(&gw, 32, &rc, 0, 0, 0) != noErr)
    {
        printf("decode '%s': NewGWorld failed\n", st);
        CloseComponent(ci);
        return;
    }
    pm = GetGWorldPixMap(gw);
    LockPixels(pm);
    {   /* fill with a known colour, so untouched pixels show */
        CGrafPtr port;
        GDHandle gd;
        GetGWorld(&port, &gd);
        SetGWorld(gw, 0);
        PaintRect(&rc);
        SetGWorld(port, gd);
    }

    idh = NewHandleClear(sizeof(ImageDesc));
    HLock(idh);
    id = (ImageDesc *)*idh;
    id->idSize = sizeof(ImageDesc);
    id->cType = sub;
    id->width = w;
    id->height = h;
    id->hRes = id->vRes = 72L << 16;
    id->dataSize = size;
    id->frameCount = 1;
    id->depth = depth;
    id->clutID = -1;
    HUnlock(idh);

    memset(&m, 0, sizeof m);
    m.m[0][0] = m.m[1][1] = 1L << 16;
    m.m[2][2] = 1L << 30;
    memset(&caps, 0, sizeof caps);
    memset(&p, 0, sizeof p);
    p.imageDescription = idh;
    p.data = data;
    p.bufferSize = size;
    p.frameNumber = 1;
    p.startLine = 0;
    p.stopLine = h;
    p.conditionFlags = 0x0FFF;       /* first/last band, first frame, new everything */
    p.capabilities = &caps;
    p.port = (CGrafPtr)gw;
    p.dstPixMap = **pm;
    p.srcRect = rc;
    p.matrix = &m;
    p.accuracy = 0x200;              /* codecNormalQuality */
    p.transferMode = srcCopy;
    p.dstRect = rc;
    r = Call(ci, 5, &p);
    printf("decode '%s': PreDecompress %ld (flags %08lX wantedPixelSize %d band %d/%d)\n",
           st, (long)r, caps.flags, caps.wantedPixelSize, caps.bandMin, caps.bandInc);
    if(r == noErr)
    {
        p.conditionFlags = 0x0007;
        r = Call(ci, 6, &p);
        printf("decode '%s': BandDecompress %ld\n", st, (long)r);
        for(y = 0; y < h; y++)
        {
            unsigned long *row = (unsigned long *)(GetPixBaseAddr(pm) + (long)y * ((**pm).rowBytes & 0x3FFF));
            printf("decode '%s': row %d:", st, y);
            for(x = 0; x < w; x++)
                printf(" %08lX", row[x] & 0x00FFFFFF);
            printf("\n");
        }
    }
    CloseComponent(ci);
    DisposeHandle(idh);
    UnlockPixels(pm);
    DisposeGWorld(gw);
}

int main(void)
{
    long v;
    short vRef, ref;
    long dirID;
    FSSpec spec;
    OSErr e;
    long n;
    FILE *cfg;
    char results_name[64] = "qtprobe.txt", path[80];
    static unsigned long raw[2][4] = {
        {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF},
        {0x00123456, 0x00654321, 0x00000000, 0x00808080}};
    /* 'rle ' (Animation), depth 24: per line a skip byte (1 = none), then
       codes: -n = the next pixel n times, n = n pixels, -1 = end of line. */
    static unsigned char rle[] = {
        0, 0, 0, 0,                      /* chunk size, set below */
        0, 0,                            /* header: the whole frame */
        1, 0xFC, 0xFF, 0x00, 0x00, 0xFF, /* line 0: red x4 */
        1, 4, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x20, 0x30, 0xFF};
    rle[3] = sizeof rle;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    InitCursor();
    /* run by tests/test_guest_suite.sh: Host:suite.cfg names the file */
    if((cfg = fopen("Host:suite.cfg", "r")) != NULL)
    {
        if(fgets(results_name, sizeof results_name, cfg))
            results_name[strcspn(results_name, "\r\n")] = 0;
        fclose(cfg);
        sprintf(path, "Host:%s", results_name);
    }
    else
        strcpy(path, results_name);
    out = fopen(path, "w");
    if(!out)
        return 1;

    e = Gestalt('qtim', &v);
    printf("Gestalt 'qtim' %d %08lX\n", (int)e, e ? 0L : v);
    e = Gestalt('icmp', &v);
    printf("Gestalt 'icmp' %d %08lX\n", (int)e, e ? 0L : v);
    e = Gestalt('cpnt', &v);
    printf("Gestalt 'cpnt' %d %08lX\n", (int)e, e ? 0L : v);
    n = Count('imdc', 0);
    printf("before: imdc %ld\n", n);

    /* Executor registers QuickTime's components at startup, without its
       INIT (extensions.cpp); a Mac's INIT does. Otherwise: the file in
       the Extensions folder, or the boot image's (Executor's System
       Folder lacks a policy-off extension). */
    if(n == 0)
    {
        e = FindFolder(kOnSystemDisk, kExtensionFolderType, false, &vRef, &dirID);
        if(e == noErr)
            e = FSMakeFSSpec(vRef, dirID, "\pQuickTime\252", &spec);
        printf("QuickTime file in the Extensions folder: %d\n", (int)e);
        if(e != noErr)
        {
            e = FSMakeFSSpec(0, 0, "\pMacintosh HD:System Folder:Extensions:QuickTime\252", &spec);
            printf("QuickTime file on Macintosh HD: %d\n", (int)e);
        }
        if(e != noErr)
            goto done;
        ref = FSpOpenResFile(&spec, fsRdPerm);
        printf("FSpOpenResFile %d (ResError %d)\n", (int)ref, (int)ResError());
        if(ref == -1)
            goto done;
        n = RegisterComponentResourceFile(ref, 1);
        printf("RegisterComponentResourceFile %ld\n", n);
    }
    printf("components: imdc %ld imco %ld mhlr %ld dhlr %ld clok %ld play %ld tune %ld sdev %ld eat %ld\n",
           Count('imdc', 0), Count('imco', 0), Count('mhlr', 0), Count('dhlr', 0), Count('clok', 0),
           Count('play', 0), Count('tune', 0), Count('sdev', 0), Count('eat ', 0));
    ListType('imdc', 1);
    Decode('raw ', 32, (Ptr)raw, sizeof raw, 4, 2);
    Decode('rle ', 24, (Ptr)rle, sizeof rle, 4, 2);
done:
    printf("done\n---\n");
    fclose(out);
    return 0;
}
