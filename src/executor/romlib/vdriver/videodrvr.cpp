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
#include <MemoryMgr.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <quickdraw/cquick.h>
#include <vdriver/vdriver.h>
#include <vdriver/videodrvr.h>
#include <rsys/device.h>
#include <mman/mman.h>
#include <base/functions.impl.h>
#include <base/traps.impl.h>

#include <cmath>

using namespace Executor;

namespace
{
enum
{
    /* Control */
    cscReset = 0, cscKillIO = 1, cscSetMode = 2, cscSetEntries = 3, cscSetGamma = 4,
    cscGrayPage = 5, cscSetGray = 6, cscSetInterrupt = 7, cscDirectSetEntries = 8,
    cscSetDefaultMode = 9, cscSwitchMode = 10, cscSavePreferredConfiguration = 16,
    /* Status */
    cscGetMode = 2, cscGetEntries = 3, cscGetPages = 4, cscGetBaseAddr = 5, cscGetGray = 6,
    cscGetInterrupt = 7, cscGetGamma = 8, cscGetDefaultMode = 9, cscGetCurMode = 10,
};

enum { firstVidMode = 0x80 };

struct VDSetEntryRecord { GUEST_STRUCT;
    GUEST<Ptr> csTable; GUEST<int16_t> csStart, csCount; };
static_assert(sizeof(VDSetEntryRecord) == 8);

struct VDPageInfo { GUEST_STRUCT;
    GUEST<int16_t> csMode; GUEST<int32_t> csData; GUEST<int16_t> csPage; GUEST<Ptr> csBaseAddr; };
static_assert(sizeof(VDPageInfo) == 12);

struct VDSwitchInfoRec { GUEST_STRUCT;
    GUEST<uint16_t> csMode; GUEST<uint32_t> csData; GUEST<uint16_t> csPage;
    GUEST<Ptr> csBaseAddr; GUEST<uint32_t> csReserved; };
static_assert(sizeof(VDSwitchInfoRec) == 16);

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
        GammaTblHeader *h = (GammaTblHeader *)gamma_table;
        h->gChanCnt = 3;
        h->gDataCnt = 256;
        h->gDataWidth = 8;
        memcpy(gamma_table + sizeof(GammaTblHeader), gamma_rgb, sizeof gamma_rgb);
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
            OSErr err = set_gamma((Ptr)*(GUEST<Ptr> *)pb->cntrlParam.csParam);
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
            if(param<VDSwitchInfoRec>(pb)->csMode != current_mode())
                return controlErr;
            param<VDSwitchInfoRec>(pb)->csBaseAddr = screen_base();
            return noErr;
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
            *(GUEST<Ptr> *)pb->cntrlParam.csParam = gamma_table;
            return gamma_table ? noErr : statusErr;
        case cscGetDefaultMode:
            *(uint8_t *)param<uint8_t>(pb) = current_mode();
            return noErr;
        case cscGetCurMode:
        {
            VDSwitchInfoRec *s = param<VDSwitchInfoRec>(pb);
            s->csMode = current_mode();
            s->csData = 0x80;                       /* the one resolution (displays.cpp) */
            s->csPage = 0;
            s->csBaseAddr = screen_base();
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
