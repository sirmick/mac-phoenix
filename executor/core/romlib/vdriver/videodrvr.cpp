/* MacPhoenix: the screen's video driver, ".Display".

   The main GDevice had no driver (gdRefNum 0). Games set the screen's
   colours through the video driver, by Control(gdRefNum, cscSetEntries) or
   by jumping into the driver through its DCE: Warcraft II showed its
   colours through the system palette without one. So the main device gets
   a driver in the unit table, a 68k DRVR in the System heap whose entry
   points jump into the routines here. The driver's colour table is the
   hardware's: Color Manager changes reach it through gd_update_colors,
   and cscSetEntries changes it without touching the GDevice's table, as
   on a Mac. Video driver calls: Designing Cards and Drivers, ch. 9.

   Colours reach the screen through the driver's gamma table, as on a Mac:
   at first the one Apple's 7.5.5 installs at boot on the ROM machine
   (cscSetGamma with no table makes it linear). Without it Executor's
   screen was darker than the ROM machine's in 256 colours: Marathon's
   world was near black. */

#include <base/common.h>
#include <DeviceMgr.h>
#include <VDriver.h>
#include <MemoryMgr.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <quickdraw/cquick.h>
#include <vdriver/vdriver.h>
#include <vdriver/videodrvr.h>
#include <rsys/device.h>
#include <mman/mman.h>
#include <rsys/displaymode.h>
#include <base/functions.impl.h>
#include <base/traps.impl.h>

#include <cmath>

using namespace Executor;

namespace
{
enum { kModelessConnect = 8, kAllModesValid = 0, kAllModesSafe = 1 };

enum { firstVidMode = 0x80 };

struct VDSetEntryRecord { GUEST_STRUCT;
    GUEST<Ptr> csTable; GUEST<int16_t> csStart, csCount; };
static_assert(sizeof(VDSetEntryRecord) == 8);

struct VDPageInfo { GUEST_STRUCT;
    GUEST<int16_t> csMode; GUEST<int32_t> csData; GUEST<int16_t> csPage; GUEST<Ptr> csBaseAddr; };
static_assert(sizeof(VDPageInfo) == 12);

struct GammaTblHeader { GUEST_STRUCT;
    GUEST<int16_t> gVersion, gType, gFormulaSize, gChanCnt, gDataCnt, gDataWidth; };
static_assert(sizeof(GammaTblHeader) == 12);

vdriver_color_t hw_clut[256];

/* The standard gamma (one channel, 256 8-bit entries): a correction of
   1.45, out = floor(255 * (in / 255)^(1 / 1.45)). It is the table 7.5.5
   installs at boot on the ROM machine, entry for entry. */
uint8_t mac_std_gamma[256];

void make_std_gamma()
{
    for(int i = 0; i < 256; i++)
        mac_std_gamma[i] = (uint8_t)std::floor(255.0 * std::pow(i / 255.0, 1.0 / 1.45));
}

uint8_t gamma_rgb[3][256];          /* per channel */
Ptr gamma_table;                    /* the GammaTbl cscGetGamma answers (System heap) */

void push_colors()
{
    vdriver_color_t out[256];
    auto g = [](int ch, uint16_t v) { uint16_t b = gamma_rgb[ch][v >> 8]; return (uint16_t)(b << 8 | b); };
    for(int i = 0; i < 256; i++)
        out[i] = { g(0, hw_clut[i].red), g(1, hw_clut[i].green), g(2, hw_clut[i].blue) };
    vdriver->setColors(256, out);
}

/* Installs a GammaTbl (nil: linear); the driver keeps its own copy. */
OSErr set_gamma(Ptr table)
{
    std::vector<uint8_t> copy;
    if(!table)
    {
        for(int c = 0; c < 3; c++)
            for(int i = 0; i < 256; i++)
                gamma_rgb[c][i] = i;
    }
    else
    {
        GammaTblHeader *h = (GammaTblHeader *)table;
        int chans = h->gChanCnt, count = h->gDataCnt, width = h->gDataWidth;
        if(h->gVersion || h->gType || (chans != 1 && chans != 3) || width < 1 || width > 8
           || count != (1 << width))
            return paramErr;
        uint8_t *data = (uint8_t *)table + sizeof(GammaTblHeader) + h->gFormulaSize;
        for(int c = 0; c < 3; c++)
            for(int i = 0; i < 256; i++)
                gamma_rgb[c][i] = data[(chans == 3 ? c * count : 0) + (i >> (8 - width))];
    }

    /* the copy cscGetGamma answers with: header, then one channel per colour */
    if(!gamma_table)
    {
        TheZoneGuard guard(LM(SysZone));
        gamma_table = NewPtrClear(sizeof(GammaTblHeader) + 3 * 256);
    }
    if(gamma_table)
    {
        /* one channel when the three are the same, as 8.6's driver
           answers with the standard gamma (difftest sys video) */
        bool one = !memcmp(gamma_rgb[0], gamma_rgb[1], 256) && !memcmp(gamma_rgb[0], gamma_rgb[2], 256);
        GammaTblHeader *h = (GammaTblHeader *)gamma_table;
        h->gChanCnt = one ? 1 : 3;
        h->gDataCnt = 256;
        h->gDataWidth = 8;
        memcpy(gamma_table + sizeof(GammaTblHeader), gamma_rgb, one ? 256 : sizeof gamma_rgb);
    }
    return noErr;
}

/* The depth's mode, numbered from 0x80 over the depths the screen has
   (as HasDepth and the Display Manager number them). */
int16_t current_mode()
{
    int16_t mode = firstVidMode;
    for(int d : { 1, 2, 4, 8, 16, 32 })
        if(vdriver->isAcceptableMode(0, 0, d, false))
        {
            if(d == vdriver->bpp())
                return mode;
            mode++;
        }
    return firstVidMode;
}

Ptr screen_base()
{
    return PIXMAP_BASEADDR(GD_PMAP(LM(MainDevice)));
}

/* MacPhoenix: the resolutions are the Display Manager's mode list
   (quickdraw/displays.cpp, docs/executor/DisplayModes.md). */
enum : uint32_t
{
    kDisplayModeIDCurrent = 0,
    kDisplayModeIDFindFirstResolution = 0xFFFFFFFE,
    kDisplayModeIDNoMoreResolutions = 0xFFFFFFFD,
};

bool resolution(uint32_t id, DisplayResolution *out)
{
    if(id == kDisplayModeIDCurrent)
        id = ROMlib_dm_current_resolution();
    for(const DisplayResolution &r : ROMlib_dm_resolutions())
        if(r.id == id)
        {
            *out = r;
            return true;
        }
    return false;
}

/* The depth of a depth mode (from 0x80 over the depths the screen has). */
int mode_depth(int mode)
{
    int m = firstVidMode;
    for(int d : { 1, 2, 4, 8, 16, 32 })
        if(vdriver->isAcceptableMode(0, 0, d, false) && m++ == mode)
            return d;
    return 0;
}

int max_mode()
{
    int m = firstVidMode - 1;
    for(int d : { 1, 2, 4, 8, 16, 32 })
        if(vdriver->isAcceptableMode(0, 0, d, false))
            m++;
    return m;
}

struct VDVideoParametersInfoRec { GUEST_STRUCT;
    GUEST<uint32_t> csDisplayModeID; GUEST<uint16_t> csDepthMode; GUEST<Ptr> csVPBlockPtr;
    GUEST<uint32_t> csPageCount, csDeviceType, csDepthFlags; };

struct VPBlock { GUEST_STRUCT;
    GUEST<int32_t> vpBaseOffset; GUEST<int16_t> vpRowBytes; Rect vpBounds;
    GUEST<int16_t> vpVersion, vpPackType; GUEST<int32_t> vpPackSize, vpHRes, vpVRes;
    GUEST<int16_t> vpPixelType, vpPixelSize, vpCmpCount, vpCmpSize; GUEST<int32_t> vpPlaneBytes; };
static_assert(sizeof(VPBlock) == 42);

/* The record a video call's csParam points at. */
template<class T>
T *param(ParmBlkPtr pb)
{
    return (T *)(Ptr)*(GUEST<Ptr> *)pb->cntrlParam.csParam;
}

OSErr set_entries(ParmBlkPtr pb)
{
    VDSetEntryRecord *r = param<VDSetEntryRecord>(pb);
    ColorSpec *table = (ColorSpec *)(Ptr)r->csTable;
    int start = r->csStart, count = r->csCount;
    if(!table || count < 0)
        return controlErr;
    for(int i = 0; i <= count; i++)
    {
        int index = start == -1 ? (int)table[i].value : start + i;
        if(index < 0 || index > 255)
            return controlErr;
        hw_clut[index] = { table[i].rgb.red, table[i].rgb.green, table[i].rgb.blue };
    }
    push_colors();
    return noErr;
}

OSErr get_entries(ParmBlkPtr pb)
{
    VDSetEntryRecord *r = param<VDSetEntryRecord>(pb);
    ColorSpec *table = (ColorSpec *)(Ptr)r->csTable;
    int start = r->csStart, count = r->csCount;
    if(!table || count < 0)
        return statusErr;
    for(int i = 0; i <= count; i++)
    {
        int index = start == -1 ? (int)table[i].value : start + i;
        if(index < 0 || index > 255)
            return statusErr;
        if(start != -1)
            table[i].value = index;
        table[i].rgb.red = hw_clut[index].red;
        table[i].rgb.green = hw_clut[index].green;
        table[i].rgb.blue = hw_clut[index].blue;
    }
    return noErr;
}
}

static OSErr C_ROMlib_videoopen(ParmBlkPtr pb, DCtlPtr dce) { return noErr; }
REGISTER_FUNCTION_PTR(ROMlib_videoopen, D0(A0,A1));
static OSErr C_ROMlib_videoclose(ParmBlkPtr pb, DCtlPtr dce) { return noErr; }
REGISTER_FUNCTION_PTR(ROMlib_videoclose, D0(A0,A1));
static OSErr C_ROMlib_videoprime(ParmBlkPtr pb, DCtlPtr dce) { return controlErr; }
REGISTER_FUNCTION_PTR(ROMlib_videoprime, D0(A0,A1));

static OSErr C_ROMlib_videoctl(ParmBlkPtr pb, DCtlPtr dce)
{
    switch(pb->cntrlParam.csCode)
    {
        case cscSetEntries:
        case cscDirectSetEntries:
            return set_entries(pb);
        case cscSetGamma:
        {
            /* MacPhoenix: csParam holds a pointer to a VDGammaRecord, as
               for the other video calls (difftest sys video) */
            VDGammaRecord *r = param<VDGammaRecord>(pb);
            if(!r)
                return paramErr;
            OSErr err = set_gamma(r->csGTable);
            if(err == noErr)
                push_colors();
            return err;
        }
        case cscReset:
        case cscKillIO:
        case cscGrayPage:
        case cscSetGray:
        case cscSetInterrupt:
        case cscSetDefaultMode:
        case cscSavePreferredConfiguration:
            return noErr;
        case cscSetMode:                    /* the depth it has already */
            if(param<VDPageInfo>(pb)->csMode != current_mode())
                return controlErr;
            param<VDPageInfo>(pb)->csBaseAddr = screen_base();
            return noErr;
        case cscSwitchMode:
        {
            /* MacPhoenix: another resolution through the Display
               Manager's change (the driver and the GDevice are one here) */
            VDSwitchInfoRec *sw = param<VDSwitchInfoRec>(pb);
            DisplayResolution r;
            if(!resolution(sw->csData, &r))
                return controlErr;
            if(r.id != ROMlib_dm_current_resolution() && !ROMlib_dm_set_screen_size(r.width, r.height, "driver"))
                return controlErr;
            if(sw->csMode != current_mode()
               && (!mode_depth(sw->csMode) || SetDepth(LM(MainDevice), sw->csMode, 0, 0) != noErr))
                return controlErr;
            sw->csBaseAddr = screen_base();
            return noErr;
        }
        default:
            return controlErr;
    }
}
REGISTER_FUNCTION_PTR(ROMlib_videoctl, D0(A0,A1));

static OSErr C_ROMlib_videostatus(ParmBlkPtr pb, DCtlPtr dce)
{
    switch(pb->cntrlParam.csCode)
    {
        case cscGetEntries:
            return get_entries(pb);
        case cscGetMode:
        {
            VDPageInfo *p = param<VDPageInfo>(pb);
            p->csMode = current_mode();
            p->csPage = 0;
            p->csBaseAddr = screen_base();
            return noErr;
        }
        case cscGetPages:
            param<VDPageInfo>(pb)->csPage = 1;
            return noErr;
        case cscGetBaseAddr:
            param<VDPageInfo>(pb)->csBaseAddr = screen_base();
            return noErr;
        case cscGetGray:
        case cscGetInterrupt:
            *(uint8_t *)param<uint8_t>(pb) = 0;     /* colour; interrupts on */
            return noErr;
        case cscGetGamma:
        {
            /* MacPhoenix: into the VDGammaRecord csParam points to; was
               written over csParam itself (Slithereens' fade read its
               record's stale pointer, then failed with -7004) */
            VDGammaRecord *r = param<VDGammaRecord>(pb);
            if(!r || !gamma_table)
                return statusErr;
            r->csGTable = gamma_table;
            return noErr;
        }
        case cscGetDefaultMode:
            *(uint8_t *)param<uint8_t>(pb) = current_mode();
            return noErr;
        /* the preferred configuration is the one it is in: Apple's
           Monitors control panel leaves a screen out of its arrangement
           when the driver has none */
        case cscGetPreferredConfiguration:
        case cscGetCurMode:
        {
            VDSwitchInfoRec *s = param<VDSwitchInfoRec>(pb);
            s->csMode = current_mode();
            s->csData = ROMlib_dm_current_resolution();
            s->csPage = 0;
            s->csBaseAddr = screen_base();
            return noErr;
        }
        /* MacPhoenix: the resolutions, one after another (the Monitors
           control panel's list), each one's parameters and timing */
        case cscGetNextResolution:
        {
            VDResolutionInfoRec *r = param<VDResolutionInfoRec>(pb);
            std::vector<DisplayResolution> all = ROMlib_dm_resolutions();
            uint32_t prev = r->csPreviousDisplayModeID;
            size_t next = 0;
            if(prev != kDisplayModeIDFindFirstResolution)
            {
                /* kDisplayModeIDCurrent: the current one itself; else
                   the one after prev */
                uint32_t id = prev == kDisplayModeIDCurrent ? ROMlib_dm_current_resolution() : prev;
                while(next < all.size() && all[next].id != id)
                    next++;
                if(next == all.size())
                    return paramErr;
                if(prev != kDisplayModeIDCurrent)
                    next++;
            }
            if(next >= all.size())
            {
                r->csDisplayModeID = kDisplayModeIDNoMoreResolutions;
                return noErr;
            }
            r->csDisplayModeID = all[next].id;
            r->csHorizontalPixels = all[next].width;
            r->csVerticalLines = all[next].height;
            r->csRefreshRate = 67 << 16;
            r->csMaxDepthMode = max_mode();
            r->csResolutionFlags = 0;
            r->csReserved = 0;
            return noErr;
        }
        case cscGetVideoParameters:
        {
            VDVideoParametersInfoRec *v = param<VDVideoParametersInfoRec>(pb);
            DisplayResolution r;
            int d = mode_depth(v->csDepthMode);
            VPBlock *vp = (VPBlock *)(Ptr)v->csVPBlockPtr;
            if(!resolution(v->csDisplayModeID, &r) || !d || !vp)
                return paramErr;
            memset((void *)vp, 0, sizeof *vp);
            vp->vpRowBytes = (r.width * d + 31) / 32 * 4;
            vp->vpBounds = Rect{ 0, 0, (INTEGER)r.height, (INTEGER)r.width };
            vp->vpHRes = vp->vpVRes = 72 << 16;
            vp->vpPixelType = d > 8 ? RGBDirect : 0;
            vp->vpPixelSize = d;
            vp->vpCmpCount = d > 8 ? 3 : 1;
            vp->vpCmpSize = d == 16 ? 5 : d == 32 ? 8 : d;
            v->csPageCount = 1;
            v->csDeviceType = d > 8 ? directType : clutType;
            return noErr;
        }
        case cscGetModeTiming:
        {
            VDTimingInfoRec *t = param<VDTimingInfoRec>(pb);
            DisplayResolution r;
            if(!resolution(t->csTimingMode, &r))
                return paramErr;
            t->csTimingFormat = "decl"_4;
            t->csTimingData = 130;              /* timingApple_640x480_67hz, as the list says */
            /* valid, safe, (default, shown now: the current one) */
            t->csTimingFlags = r.id == ROMlib_dm_current_resolution() ? 0xF : 0x3;
            return noErr;
        }
        /* a screen that takes any of the list's modes (the ROM machine's
           answer: the Monitors control panel lists them all for it) */
        case cscGetConnection:
        {
            VDDisplayConnectInfoRec *c = param<VDDisplayConnectInfoRec>(pb);
            c->csDisplayType = kModelessConnect;
            c->csConnectTaggedType = 0;
            c->csConnectTaggedData = 0;
            c->csConnectFlags = (1 << kAllModesValid) | (1 << kAllModesSafe);
            c->csDisplayComponent = 0;
            c->csConnectReserved = 0;
            return noErr;
        }
        case cscGetModeBaseAddress:
        {
            VDBaseAddressInfoRec *b = param<VDBaseAddressInfoRec>(pb);
            if((uint32_t)b->csDevData != ROMlib_dm_current_resolution())
                return statusErr;
            b->csDevBase = US_TO_SYN68K(screen_base());
            return noErr;
        }
        default:
            return statusErr;
    }
}
REGISTER_FUNCTION_PTR(ROMlib_videostatus, D0(A0,A1));

void Executor::ROMlib_video_set_colors(int n, const vdriver_color_t *colors)
{
    std::copy(colors, colors + std::min(n, 256), hw_clut);
    push_colors();
}

/* A DRVR in the System heap (pointer-based, as DrvrInstall leaves one):
   the header, the name, then a JMP to each routine. */
INTEGER Executor::ROMlib_install_video_driver()
{
    static INTEGER refnum = 0;
    if(refnum)
        return refnum;

    /* The standard gamma, as the ROM machine's boot installs it. */
    {
        TheZoneGuard guard(LM(SysZone));
        Ptr t = NewPtrClear(sizeof(GammaTblHeader) + 256);
        if(t)
        {
            GammaTblHeader *h = (GammaTblHeader *)t;
            h->gChanCnt = 1;
            h->gDataCnt = 256;
            h->gDataWidth = 8;
            make_std_gamma();
            memcpy(t + sizeof(GammaTblHeader), mac_std_gamma, 256);
            set_gamma(t);
            DisposePtr(t);
        }
    }
    int unit = ROMlib_free_unit(64);
    if(unit < 0)
        return 0;

    static const unsigned char name[] = "\010.Display";
    enum { header = 18 };
    int code = (header + 1 + name[0] + 1) & ~1;
    TheZoneGuard guard(LM(SysZone));
    Ptr p = NewPtrClear(code + 5 * 6);
    DCtlHandle h = (DCtlHandle)NewHandleClear(sizeof(DCtlEntry));
    if(!p || !h)
        return 0;

    ramdriverptr d = (ramdriverptr)p;
    d->drvrFlags = (1 << 14) | (1 << 11) | (1 << 10);  /* dNeedLock, dStatEnable, dCtlEnable */
    memcpy(&d->drvrName, name, name[0] + 1);
    ProcPtr routines[5] = { (ProcPtr)&ROMlib_videoopen, (ProcPtr)&ROMlib_videoprime,
                            (ProcPtr)&ROMlib_videoctl, (ProcPtr)&ROMlib_videostatus,
                            (ProcPtr)&ROMlib_videoclose };
    GUEST<INTEGER> *offsets[5] = { &d->drvrOpen, &d->drvrPrime, &d->drvrCtl,
                                   &d->drvrStatus, &d->drvrClose };
    for(int i = 0; i < 5; i++)
    {
        int at = code + i * 6;
        *offsets[i] = at;
        GUEST<uint16_t> *jmp = (GUEST<uint16_t> *)(p + at);
        uint32_t target = US_TO_SYN68K(routines[i]);
        jmp[0] = 0x4EF9;                            /* JMP abs.l */
        jmp[1] = target >> 16;
        jmp[2] = target & 0xFFFF;
    }

    (*h)->dCtlDriver = (umacdriverptr)p;
    (*h)->dCtlFlags = d->drvrFlags | DRIVEROPENBIT;
    refnum = -(unit + 1);
    (*h)->dCtlRefNum = refnum;
    LM(UTableBase)[unit] = h;
    return refnum;
}
