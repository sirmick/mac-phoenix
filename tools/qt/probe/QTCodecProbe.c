/* QTCodecProbe: can Executor use Apple's QuickTime components without
   QuickTime's INIT (route B in docs/executor/QuickTime.md)? An application:
   registers the components of QuickTime(TM) in the Extensions folder with
   RegisterComponentResourceFile, lists them by type, then opens the 'raw '
   and 'rle ' decompressors and drives them through the codec component
   interface as the Image Compression Manager would (PreDecompress,
   BandDecompress into a GWorld), with no ICM: the ICM is part of the INIT.
   One line per step, to qtprobe.txt next to the application; the pixels
   decoded are printed so the run can be compared with a ROM machine's.

     tools/mpw/mpw build tools/qt/probe
     mac-phoenix --backend executor --executor-system 7.6.1.img \
         --executor-app tools/qt/probe/build/QTCodecProbe.bin */
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

/* The same calls made as the Component Manager makes them to a component
   with cmpFastDispatch set in its flags (QuickTime's codecs): D0 the
   {flags, paramSize, what} long, the caller's parameters left where they
   are with the instance replaced by its storage, the entry point called. */
pascal long FastCall(Handle storage, void *p1, long header, ProcPtr entry)
    = {0x205F, 0x201F, 0x4E90};       /* movea.l (sp)+,a0; move.l (sp)+,d0; jsr (a0) */

/* HeapDispatch ($A0A4) selectors 0-5, as the 7.6.1 System's 'gpch' 667
   has them: HLock, HUnlock, HPurge, HNoPurge, HGetState, HSetState (A1)
   on the handle in A0. QuickTime calls them throughout; Executor stops
   at them, so the probe puts this in the trap table while it runs. */
static unsigned short heapDispatch[] = {
    0x4A40, 0x6716, 0x5340, 0x6716, 0x5340, 0x6716, 0x5340, 0x6716,
    0x5340, 0x6716, 0x5340, 0x6716, 0x4E75,
    0xA029, 0x4E75, 0xA02A, 0x4E75, 0xA049, 0x4E75, 0xA04A, 0x4E75,
    0xA069, 0x4E75, 0x2009, 0xA06A, 0x4E75};

/* The Image Compression Manager ($AAA3) is part of QuickTime's INIT: a
   stub that answers CodecManagerVersion (selector 0) and, for any other
   selector, makes Executor log it (an OS trap it lacks, with D0 the
   selector) and quits: the next ICM call a codec needs, one per run. */
static unsigned short icmStub[] = {
    0x4A40, 0x660E,                  /* tst.w d0; bne.s other */
    0x205F, 0x225F,                  /* movea.l (sp)+,a0; movea.l (sp)+,a1 */
    0x22BC, 0x0011, 0x0000,          /* move.l #$00110000,(a1) */
    0x4257, 0x4ED0,                  /* clr.w (sp); jmp (a0) */
    0xA0FE, 0xA9F4};                 /* other: unimplemented trap; ExitToShell */

static Boolean fast;                 /* make the calls ourselves, as above */
static ProcPtr entry;
static Boolean icm = true;           /* patch the ICM stub too */
static Handle storage;

static long Call(ComponentInstance ci, short what, void *p1)
{
    if(!fast)
    {
        if(what == 0)
            return CodecGetCodecInfo(ci, p1);
        if(what == 5)
            return CodecPreDecompress(ci, (DecompParams *)p1);
        return CodecBandDecompress(ci, (DecompParams *)p1);
    }
    return FastCall(storage, p1, (4L << 16) | (unsigned short)what, entry);
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
    printf("decode '%s'%s: OpenComponent %s\n", st, fast ? " (fast dispatch)" : "", ci ? "ok" : "failed");
    if(!ci)
        return;
    if(fast)
    {   /* the component's code, as its 'thng' names it; open it again
           the fast way, so it sets its storage on the instance */
        ComponentDescription cd;
        Handle th = NewHandle(0);
        long r2;
        GetComponentInfo(c, &cd, 0, 0, 0);
        r2 = GetComponentRefcon(c);
        {
            Handle code = 0;
            short i;
            for(i = 1; i <= Count1Resources('thng') && !code; i++)
            {
                Handle t = Get1IndResource('thng', i);
                if(t && ((long *)*t)[1] == sub && ((long *)*t)[0] == 'imdc')
                    code = Get1Resource(((long *)*t)[5], ((short *)*t)[12]);
            }
            if(!code)
            {
                printf("decode '%s': no code resource\n", st);
                return;
            }
            HLockHi(code);
            entry = (ProcPtr)StripAddress(*code);
        }
        storage = 0;
        r2 = FastCall(0, (void *)ci, (4L << 16) | 0xFFFF, entry);   /* kComponentOpenSelect */
        storage = GetComponentInstanceStorage(ci);
        printf("decode '%s': open (fast) %ld, storage %s\n", st, r2, storage ? "set" : "none");
        DisposeHandle(th);
    }
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
    out = fopen("qtprobe.txt", "w");
    if(!out)
        return 1;

    printf("Gestalt 'qtim' %d", (int)Gestalt('qtim', &v));
    printf(" %08lX\n", v);
    v = 0;
    printf("Gestalt 'icmp' %d %08lX\n", (int)Gestalt('icmp', &v), v);
    v = 0;
    printf("Gestalt 'cpnt' %d %08lX\n", (int)Gestalt('cpnt', &v), v);
    printf("before: imdc %ld imco %ld mhlr %ld clok %ld play %ld\n",
           Count('imdc', 0), Count('imco', 0), Count('mhlr', 0), Count('clok', 0), Count('play', 0));

    /* The Extensions folder; on Executor, where a policy-off extension
       isn't copied into its System Folder, the boot image's. */
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
        return 1;
    ref = FSpOpenResFile(&spec, fsRdPerm);
    printf("FSpOpenResFile %d (ResError %d)\n", (int)ref, (int)ResError());
    if(ref == -1)
        return 1;
    n = RegisterComponentResourceFile(ref, 1);
    printf("RegisterComponentResourceFile %ld\n", n);
    printf("after: imdc %ld imco %ld mhlr %ld dhlr %ld clok %ld play %ld tune %ld sdev %ld eat %ld\n",
           Count('imdc', 0), Count('imco', 0), Count('mhlr', 0), Count('dhlr', 0), Count('clok', 0),
           Count('play', 0), Count('tune', 0), Count('sdev', 0), Count('eat ', 0));
    ListType('imdc', 1);
    Decode('raw ', 32, (Ptr)raw, sizeof raw, 4, 2);
    Decode('rle ', 24, (Ptr)rle, sizeof rle, 4, 2);
    fast = true;
    UseResFile(ref);
    {
        UniversalProcPtr old = NGetTrapAddress(0xA0A4, OSTrap);
        UniversalProcPtr oldICM = NGetTrapAddress(0xAAA3, ToolTrap);
        NSetTrapAddress((UniversalProcPtr)heapDispatch, 0xA0A4, OSTrap);
        printf("HeapDispatch 0-5 patched by the probe\n");
        if(icm)
        {
            NSetTrapAddress((UniversalProcPtr)icmStub, 0xAAA3, ToolTrap);
            printf("ICM stub ($AAA3) patched by the probe\n");
        }
        Decode('raw ', 32, (Ptr)raw, sizeof raw, 4, 2);
        Decode('rle ', 24, (Ptr)rle, sizeof rle, 4, 2);
        NSetTrapAddress(old, 0xA0A4, OSTrap);
        NSetTrapAddress(oldICM, 0xAAA3, ToolTrap);
    }
    printf("without the HeapDispatch patch:\n");
    Decode('raw ', 32, (Ptr)raw, sizeof raw, 4, 2);
    Decode('rle ', 24, (Ptr)rle, sizeof rle, 4, 2);
    printf("done\n");
    fclose(out);
    return 0;
}
