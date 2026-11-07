/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in FontMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <ResourceMgr.h>
#include <CQuickDraw.h>
#include <QuickDraw.h>
#include <MemoryMgr.h>
#include <FontMgr.h>
#include <ToolboxUtil.h>
#include <OSUtil.h>
#include <quickdraw/cquick.h>
#include <quickdraw/font.h>
#include <res/resource.h>
#include <quickdraw/sfnt.h>
#include <prefs/prefs.h>

#include <vector>
#include <rsys/cfm.h>

using namespace Executor;

#define MAXTABLES 12

static FHandle at_least_one_fond_entry(INTEGER family);

static void
reset_myfmi(void)
{
    LM(CurFMInput).family = -1;
    LM(CurFMInput).size = -1;
    LM(CurFMInput).face = -1;
}

void Executor::C_InitFonts() /* IMI-222 */
{
    static Boolean beenhere = false;
    GUEST<THz> saveZone;

    ROMlib_cfm_toolbox_library("FontManager");

    if(!beenhere)
    {
        saveZone = LM(TheZone);
        LM(TheZone) = LM(SysZone);
        SetResLoad(true);
        LM(ROMFont0) = GetResource("FONT"_4, FONTRESID(systemFont, 12));
        LM(WidthListHand) = NewHandle(MAXTABLES * sizeof(GUEST<Handle>));
        memset(*LM(WidthListHand), 0, MAXTABLES * sizeof(GUEST<Handle>));
        LM(TheZone) = saveZone;
        beenhere = true;
    }
    LM(ApFontID) = LM(SPFont) + 1;
    /* the system font is the Roman script's: its extended 'itlb' says
       Charcoal on 8.x (itlbSysFond, 36), Chicago before */
    LM(SysFontSiz) = 12;
    LM(SysFontFam) = 0;
    if(Handle itlb = GetResource("itlb"_4, 0))
    {
        if(!*itlb)
            LoadResource(itlb);
        if(*itlb && GetHandleSize(itlb) >= 40)
        {
            INTEGER fond = *(GUEST<INTEGER> *)(*itlb + 36), size = *(GUEST<INTEGER> *)(*itlb + 38);
            if(fond && at_least_one_fond_entry(fond))
            {
                LM(SysFontFam) = fond;
                if(size > 0)
                    LM(SysFontSiz) = size;
            }
        }
    }
#if 0
    /* 95-09-20: PAUP uses a built in font that has fried width tables.
       The bad tables won't hurt us if LM(FractEnable) is not set, so I ran
       a test program on the Mac and found that LM(FractEnable) wasn't set
       at the beginning of the program or even after InitFonts was called.
       This suggests that the hack for WordPerfect is wrong.  Perhaps
       there's some bit in the Size resource that controls this thing,
       perhpas the WordPerfect hack was always fried.  Yahoo. */

    LM(FractEnable) = 0xff;	/* new mod for WordPerfect */
#endif
    LM(FScaleDisable) = 0;
    reset_myfmi();
}

static void
invalidate_all_widths(void)
{
    int i, n_entries;
    GUEST<Handle> *hp;
    Handle wlh;

    wlh = LM(WidthListHand);
    HLock(wlh);
    n_entries = GetHandleSize(wlh) / sizeof(GUEST<Handle>);
    hp = (GUEST<Handle> *)*wlh;
    for(i = 0; i < n_entries; ++i)
    {
        DisposeHandle(hp[i]);
        hp[i] = nullptr;
    }
    HUnlock(wlh);
}

void
Executor::ROMlib_shutdown_font_manager(void)
{
    invalidate_all_widths();
}

void Executor::C_GetFontName(INTEGER fnum, StringPtr fnam) /* IMI-223 */
{
    Handle h;
    GUEST<INTEGER> i;
    GUEST<ResType> rest;

    if(fnum == systemFont)
        fnum = LM(SysFontFam);
    else if(fnum == applFont)
        fnum = LM(ApFontID);
    SetResLoad(false);
    h = GetResource("FONT"_4, FONTRESID(fnum, 0));
    if(!h)
        h = GetResource("NFNT"_4, FONTRESID(fnum, 0));
    if(!h)
        h = GetResource("FOND"_4, fnum);
    GetResInfo(h, &i, &rest, fnam);
    if(ResError())
        *fnam = 0;
    SetResLoad(true);
}

void Executor::C_GetFNum(ConstStringPtr fnam, GUEST<INTEGER> *fnum) /* IMI-223 */
{
    Handle h;
    GUEST<ResType> rest;
    Boolean shift;

    SetResLoad(false);

    h = GetNamedResource("FOND"_4, fnam);
    if(h)
        shift = false;
    else
    {
        shift = true;
        h = GetNamedResource("FONT"_4, fnam);
        if(!h)
            h = GetNamedResource("NFNT"_4, fnam);
    }

    GetResInfo(h, fnum, &rest, nullptr);
    if(ResError())
        *fnum = 0;
    else if(shift)
        *fnum = *(GUEST<uint16_t> *)fnum >> 7;
    SetResLoad(true);
}

void Executor::C_SetFontLock(Boolean lflag) /* IMI-223 */
{
    INTEGER attrs;

    attrs = GetResAttrs(LM(ROMlib_fmo).fontHandle);
    if(lflag)
    {
        LoadResource(LM(ROMlib_fmo).fontHandle);
        SetResAttrs(LM(ROMlib_fmo).fontHandle, attrs & ~resPurgeable);
    }
    else
    { /* TODO: is the next line necessary */
        SetResAttrs(LM(ROMlib_fmo).fontHandle, attrs | resPurgeable);
    }
}

typedef signed char ctrip[3];
typedef struct
{
    INTEGER dpiv, dpih;
    ctrip boldt, italt, nat, outt, shadt, condt, extt, undert;
} fchartstr;

static fchartstr ftstr = {
    80, 80, /* dpiv, dpih */
    {
        0, 1, 1,
    }, /* boldt */
    {
        1, 8, 0,
    }, /* italt */
    {
        0, 0, 0,
    }, /* Not Used */
    {
        5, 1, 1,
    }, /* outt */
    {
        5, 2, 2,
    }, /* shadt */
    {
        0, 0, -1,
    }, /* condt */
    {
        0, 0, 1,
    }, /* extt */
    {
        1, 1, 1,
    }, /* undert */
};

static void mungfmo(ctrip, FMOutput *);
static Boolean widthlistmatch(FMInput *);
static int countones(unsigned short);
static GUEST<INTEGER> *findfondwidths();
static void buildtable(INTEGER);
static void findclosestfont(INTEGER family, INTEGER size, INTEGER *lesserp,
                            INTEGER *greaterp);
static void findclosestfond(FHandle fh, INTEGER size, INTEGER *powerof2p,
                            INTEGER *lesserp, INTEGER *greaterp);
static INTEGER closestface();
static void newwidthtable(FMInput *fmip);

static void mungfmo(ctrip cp, FMOutput *fmop)
{
    Byte *p;
    INTEGER i;

    p = &(fmop->bold);
    i = cp[1];
    if(i & 0x80)
        i |= 0xFF00;
    *(p + cp[0]) += i;
    i = cp[2];
    if(i & 0x80)
        i |= 0xFF00;
    fmop->extra = fmop->extra + i;
}

#define MAXTABLES 12

/* MacPhoenix: a listed table that isn't in memory (its handle or master
   pointer outside every window: THINK Pascal 4.5's Run leaves one) is
   taken out of the list rather than read; in the tab reading it is a wasm
   out-of-bounds access */
static bool width_entry_mapped(GUEST<WHandle> *whp, int i)
{
    WHandle h = *whp;
    /* By guest address: a host pointer made from junk can land in another
       window's memory (natively the RAM is right below the 1 GB pool) */
    bool mapped = syn68k_is_guest_addr_mapped(whp->raw_host_order(), sizeof(GUEST<WPtr>));
    if(mapped
       && (!*h || syn68k_is_guest_addr_mapped(GUEST<WPtr>(*h).raw_host_order(), sizeof(WidthTable))))
        return *h != nullptr;
    fprintf(stderr, "[bad handle] width list entry %d: handle $%08X %s $%08X\n", i,
            whp->raw_host_order(), mapped ? "with master pointer" : "outside memory",
            mapped ? GUEST<WPtr>(*h).raw_host_order() : 0u);
    *whp = nullptr;
    return false;
}

static Boolean widthlistmatch(FMInput *fmip)
{

    GUEST<WHandle> *whp, *ewhp;

    for(whp = *WIDTHLISTHAND, ewhp = whp + MAXTABLES; whp != ewhp; whp++)
    {
        if(*whp && (LONGINT)(*whp).raw() != (LONGINT)-1
           && width_entry_mapped(whp, whp - *WIDTHLISTHAND))
        {
            LM(WidthPtr) = *(*whp);
            if(WIDTHPTR->aFID == fmip->family && WIDTHPTR->aSize == fmip->size && (char)WIDTHPTR->aFace == (char)fmip->face && WIDTHPTR->device == fmip->device && WIDTHPTR->inNumer.h == fmip->numer.h && WIDTHPTR->inNumer.v == fmip->numer.v && WIDTHPTR->inDenom.h == fmip->denom.h && WIDTHPTR->inDenom.v == fmip->denom.v && WIDTHPTR->fSize != -1 && WIDTHPTR->fSize != 0 && !WIDTHPTR->usedFam == !LM(FractEnable))
            {
                LM(WidthTabHandle) = guest_cast<WidthTableHandle>(*whp);
                HLock((Handle)LM(WidthTabHandle));
                return true;
            }
        }
    }
    return false;
}

/* MacPhoenix: is WidthTabHandle still one of the listed tables (WidthPtr
   then where it is now)? WidthTabHandle and the last request
   (CurFMInput) are switched per process (7.5.5's 'lmem' -16458), but the
   list is shared, so another process may have disposed of this process's
   table to make room for one of its own: the request still matches, the
   handle is gone. */
static bool width_table_listed()
{
    if(!LM(WidthListHand) || !LM(WidthTabHandle))
        return false;
    GUEST<WHandle> *whp = *WIDTHLISTHAND;
    for(int i = 0; i < MAXTABLES; i++)
        if(whp[i] && whp[i].raw() == LM(WidthTabHandle).raw())
        {
            if(!width_entry_mapped(&whp[i], i))
                return false;
            LM(WidthPtr) = *(whp[i]);
            return true;
        }
    return false;
}

static int countones(unsigned short i)
{
    int retval;
    static int counts[] = {
        0, /* 0 0 0 0 */
        1, /* 0 0 0 1 */
        1, /* 0 0 1 0 */
        2, /* 0 0 1 1 */
        1, /* 0 1 0 0 */
        2, /* 0 1 0 1 */
        2, /* 0 1 1 0 */
        3, /* 0 1 1 1 */
        1, /* 1 0 0 0 */
        2, /* 1 0 0 1 */
        2, /* 1 0 1 0 */
        3, /* 1 0 1 1 */
        2, /* 1 1 0 0 */
        3, /* 1 1 0 1 */
        3, /* 1 1 1 0 */
        4, /* 1 1 1 1 */
    };

    retval = counts[i & 0xF];
    i >>= 4;
    retval += counts[i & 0xF];
    i >>= 4;
    retval += counts[i & 0xF];
    i >>= 4;
    return retval + counts[i & 0xF];
}

static INTEGER nhappybits(unsigned short want, unsigned short have)
{
    return (have & ~want) ? -1 : countones(have);
}

#define WIDTHBIT (1 << 1)

static GUEST<INTEGER> *findfondwidths()
{
    GUEST<INTEGER> *retval, *numentriesminusone;
    LONGINT offset;
    INTEGER bitsmatched, newbits;
    INTEGER want;
    INTEGER tabsize;
    widentry_t *widp;
    INTEGER i;

    retval = 0;
    if((*(FHandle)LM(LastFOND))->ffFamID == WIDTHPTR->aFID && (offset = (*(FHandle)LM(LastFOND))->ffWTabOff))
    {
        bitsmatched = -1;
        want = WIDTHPTR->aFace;
        /*
 * NOTE: we add 3 to lastchar - firstchar to include the missing character
 *	 entry and what appears to be a zero entry located thereafter.
 */
        tabsize = ((*(FHandle)LM(LastFOND))->ffLastChar - (*(FHandle)LM(LastFOND))->ffFirstChar + 3)
                * sizeof(INTEGER)
            + sizeof(INTEGER);
        numentriesminusone = (GUEST<INTEGER> *)((char *)&(*(FHandle)LM(LastFOND))->ffFlags + offset);
        i = *numentriesminusone + 1;
        widp = (widentry_t *)(numentriesminusone + 1);
        for(; --i >= 0; widp = (widentry_t *)((char *)widp + tabsize))
        {
            newbits = nhappybits(want, widp->style);
            if(newbits > bitsmatched)
            {
                retval = widp->table;
                bitsmatched = newbits;
            }
        }
    }

    return retval;
}

#define FIXED(n) ((LONGINT)(n) << 16)
#define FIXED8(n) ((LONGINT)((unsigned short)n) << 8)
#define FIXED4(n) (((LONGINT)((unsigned short)n) << 4) * (WIDTHPTR->aSize))

Fixed
Executor::font_width_expand(Fixed width, Fixed fixed_extra, Fixed hOutputInverse)
{
    Fixed retval;

    retval = FixMul(width + fixed_extra, hOutputInverse);
    return retval;
}

typedef enum { FontFract,
               FontInt,
               FondFract } howtobuild_t;

static void buildtabdata(howtobuild_t howtobuild, INTEGER extra,
                         GUEST<INTEGER> *fondwidthtable)
{
    INTEGER c, firstchar, lastchar;
    GUEST<INTEGER> *widp, width;
    GUEST<Fixed> *p, *ep;
    Fixed misswidth, hOutputInverse, fixed_extra;
    FontRec *fp;

    fp = *(GUEST<FontRec *> *)WIDTHPTR->tabFont;
    firstchar = fp->firstChar;
    lastchar = fp->lastChar;

    switch(howtobuild)
    {
        case FontFract:
            WIDTHPTR->usedFam = 0; /* don't really know what this should be */
            widp = &fp->owTLoc + fp->owTLoc + (lastchar - firstchar + 2);
            fixed_extra = FIXED(extra);
            misswidth = FIXED8(widp[lastchar - firstchar + 1]) + fixed_extra;

            /* NOTE: this assumes missing characters have the missing width in the
	 table.  If this is wrong we need to look for missing characters
	 explictly */

            for(c = 0, p = WIDTHPTR->tabData, ep = p + 256; p < ep; c++)
            {
                if(c < firstchar || c > lastchar)
                    *p++ = misswidth;
                else
                    *p++ = FIXED8(*widp++) + fixed_extra;
            }
            break;
        case FontInt:
        {
            /* (bitmap strikes only: an outline's glyph of no width
               widens with the style) */
            bool outline = ROMlib_outline_strike(WIDTHPTR->tabFont);
            WIDTHPTR->usedFam = 0;
            widp = &fp->owTLoc + fp->owTLoc;
            misswidth = FIXED((widp[lastchar - firstchar + 1] & 0xFF) + extra);
            for(c = 0, p = WIDTHPTR->tabData, ep = p + 256; p < ep; c++)
            {
                if(c < firstchar || c > lastchar)
                    *p++ = misswidth;
                else if((width = *widp++) == -1)
                    *p++ = misswidth;
                /* 8.x: a glyph of no width stays so whatever the style,
                   and a carriage return has none (text_helper too) */
                else if(system_version >= 0x800 && ((!(width & 0xFF) && !outline) || c == '\r'))
                    *p++ = 0;
                else
                    *p++ = FIXED(std::max((width & 0xFF) + extra, 0));
            }
            break;
        }
        case FondFract:
            WIDTHPTR->usedFam = -1;
            hOutputInverse = FixRatio(1 << 8, WIDTHPTR->hOutput);
            widp = fondwidthtable;
            fixed_extra = FIXED(extra);
            misswidth = FIXED4(widp[lastchar - firstchar + 1]);
            misswidth = font_width_expand(misswidth, fixed_extra, hOutputInverse);
            for(c = 0, p = WIDTHPTR->tabData, ep = p + 256; p < ep; c++)
            {
                if(c < firstchar || c > lastchar)
                    *p++ = misswidth;
                else
                {
                    *p++ = FixMul(FIXED4(*widp) + fixed_extra,
                                     hOutputInverse);
                    widp++;
                }
            }
            break;
    }
    WIDTHPTR->tabData[(unsigned)' '] = WIDTHPTR->tabData[(unsigned)' '] + WIDTHPTR->sExtra;
}

static void buildtable(INTEGER extra)
{
    howtobuild_t howtobuild;
    GUEST<INTEGER> *fondwidthtable;
    Fixed tempfix;
    INTEGER numerh, numerv, denomh, denomv;

#if !defined(LETGCCWAIL)
    fondwidthtable = 0;
#endif
    if(!LM(FractEnable))
        howtobuild = FontInt;
    else if((*(GUEST<FontRec *> *)WIDTHPTR->tabFont)->fontType & WIDTHBIT)
    {
        howtobuild = FontFract;
        extra = 0;
    }
    /*
 * NOTE: The sleazy hack for checking for geneva below.  This is because we
 *	 used to use a System file that didn't have widths for geneva and
 *	 everything worked fine.  Now we have widths for geneva, but they
 *	 appear to be too wide.  I don't have time to figure out what's
 *	 going on here, but since geneva is a toy font, I'm not too concerned
 *	 right now.  The trouble that the new sizes cause is in the Word 5.1
 *	 ribbon control labels.
 */
    else if(WIDTHPTR->aFID != kFontIDGeneva && (fondwidthtable = findfondwidths()))
    {
        howtobuild = FondFract;
        extra = 0;
    }
    else
        howtobuild = FontInt;
    numerh = WIDTHPTR->inNumer.h;
    numerv = WIDTHPTR->inNumer.v;
    denomh = WIDTHPTR->inDenom.h;
    denomv = WIDTHPTR->inDenom.v;
    if(WIDTHPTR->fSize == WIDTHPTR->aSize)
    {
        WIDTHPTR->hOutput = FixRatio(numerh, denomh) >> 8;
        WIDTHPTR->vOutput = FixRatio(numerv, denomv) >> 8;
        WIDTHPTR->hFactor = WIDTHPTR->vFactor = 256;
    }
    else if(WIDTHPTR->fSize < WIDTHPTR->aSize && LM(FScaleDisable))
    {
        WIDTHPTR->hOutput = FixRatio(numerh, denomh) >> 8;
        WIDTHPTR->vOutput = FixRatio(numerv, denomv) >> 8;
        WIDTHPTR->hFactor = WIDTHPTR->vFactor = FixRatio(WIDTHPTR->aSize,
                                                            WIDTHPTR->fSize)
                                                   >> 8;
    }
    else
    {
        tempfix = FixRatio(WIDTHPTR->aSize, WIDTHPTR->fSize);
        WIDTHPTR->hOutput = FixMul(tempfix, FixRatio(numerh, denomh)) >> 8;
        WIDTHPTR->vOutput = FixMul(tempfix, FixRatio(numerv, denomv)) >> 8;
        WIDTHPTR->hFactor = WIDTHPTR->vFactor = 256;
    }
#if 0 /* WTF is going on here? */
    WIDTHPTR->style = extra;
#endif

    buildtabdata(howtobuild, extra, fondwidthtable);
}

static void findclosestfont(INTEGER family, INTEGER size, INTEGER *lesserp,
                            INTEGER *greaterp)
{
    INTEGER i, lesser, greater, newsize, nres;
    GUEST<ResType> rest;
    GUEST<INTEGER> id_s;
    Handle h;

    lesser = 0;
    greater = 32767;
    SetResLoad(false);
    nres = CountResources("FONT"_4); /* how about NFNT? */
    for(i = 1; i <= nres; i++)
    {
        h = GetIndResource("FONT"_4, i);
        GetResInfo(h, &id_s, &rest, (StringPtr)0);
        INTEGER id = id_s.get();
        if(((unsigned short)id >> 7) == family)
        { // ### WTF ENDIAN
            if((newsize = id & ((1 << 7) - 1)) <= size)
            {
                if(newsize > lesser)
                    lesser = newsize;
            }
            else
            {
                if(newsize < greater)
                    greater = newsize;
            }
        }
    }
    SetResLoad(true);
    *lesserp = lesser;
    *greaterp = greater == 32767 ? 0 : greater;
}

static void findclosestfond(FHandle fh, INTEGER size, INTEGER *powerof2p,
                            INTEGER *lesserp, INTEGER *greaterp)
{
    fatabentry *p, *ep;
    INTEGER newsize;
    INTEGER powerof2, lesser, greater;
    GUEST<INTEGER> *ip;

    powerof2 = 0;
    lesser = 0;
    greater = 32767;
    ip = &(*fh)->ffVersion + 1;
    for(p = (fatabentry *)(ip + 1), ep = p + *ip + 1; p < ep; p++)
    {
        newsize = p->size;
        if(!newsize) /* an outline font ('sfnt') */
            continue;
        if(newsize < size)
        {
            if(newsize == size / 2)
                powerof2 = newsize; /* lower priority than  *2, but */
            /* we know these entries are ordered */
            if(newsize > lesser)
                lesser = newsize;
        }
        else if(newsize == size)
        {
            powerof2 = newsize;
            /*-->*/ break;
        }
        else
        {
            if(newsize == size * 2)
                powerof2 = newsize;
            if(newsize < greater)
                greater = newsize;
        }
    }
    *powerof2p = powerof2;
    *lesserp = lesser;
    *greaterp = greater == 32767 ? 0 : greater;
}


/* MacPhoenix: a family's FONDs in every open file, top of the chain
   first. 8.x's System file has FONDs of its own for Chicago, Geneva and
   Monaco (their 9 and 12 point strikes); the Fonts folder's suitcases have
   the same families with more sizes and the outlines. */
template<typename F>
static FHandle find_fond(INTEGER family, F want)
{
    FHandle found = nullptr;
    INTEGER saved = LM(CurMap);
    resmaphand map;
    WALKMAPTOP(map)
        UseResFile((*map)->resfn);
        FHandle h = (FHandle)Get1Resource("FOND"_4, family);
        if(!h)
            continue;
        if(!*h)
            LoadResource((Handle)h);
        if(*h && want(h))
        {
            found = h;
            break;
        }
    EWALKMAP()
    LM(CurMap) = saved;
    return found;
}

/* the font association table */
static int fond_entries(FHandle fh, fatabentry **first)
{
    GUEST<INTEGER> *ip = &(*fh)->ffVersion + 1;
    *first = (fatabentry *)(ip + 1);
    return *ip + 1;
}

static bool fond_has_size(FHandle fh, INTEGER size)
{
    fatabentry *p;
    for(int n = fond_entries(fh, &p); n-- > 0; p++)
        if(p->size == size)
            return true;
    return false;
}

/* the FOND with a strike of SIZE whose style has most of FACE and nothing
   else (8.6 takes Geneva 9 italic from the suitcase's italic strike, not
   the System file's plain one) */
static FHandle best_fond_with_size(INTEGER family, INTEGER size, INTEGER face)
{
    FHandle best = nullptr;
    int best_bits = -2;
    find_fond(family, [&](FHandle fh) {
        fatabentry *p;
        for(int n = fond_entries(fh, &p); n-- > 0; p++)
            if(p->size == size)
            {
                int bits = (p->style & ~face) ? -1 : countones(p->style);
                if(bits > best_bits)
                {
                    best = fh;
                    best_bits = bits;
                }
            }
        return false; /* all of them */
    });
    return best;
}

/* the family's outline ('sfnt', size 0 in the table) for FACE: the one
   whose style has most of FACE and nothing else */
static Handle fond_outline(FHandle fh, INTEGER face, INTEGER *stylep)
{
    Handle found = nullptr;
    int best = -1;
    fatabentry *p;
    int n = fond_entries(fh, &p);
    for(int i = 0; i < n; i++)
    {
        fond_entries(fh, &p); /* GetResource may have moved it */
        INTEGER size = p[i].size, style = p[i].style, id = p[i].fontresid;
        if(size || (style & ~face) || countones(style) <= best)
            continue;
        /* (whether it's there: outline_strike loads the one it uses) */
        Boolean resload = LM(ResLoad);
        SetResLoad(false);
        Handle h = GetResource("sfnt"_4, id);
        SetResLoad(resload);
        if(h)
        {
            found = h;
            best = countones(style);
            *stylep = style;
        }
        if(!*fh)
            LoadResource((Handle)fh);
        if(!*fh)
            break;
    }
    return found;
}

/* Strikes made from outlines (sfnt.cpp), one per outline and size. When
   there are too many, the oldest go and the width tables made from them
   stop matching (fSize 0: rebuilt when next asked for). */
struct OutlineStrike
{
    Handle sfnt;
    INTEGER ppem;
    Handle strike;
    SfntMetrics metrics;
};
static std::vector<OutlineStrike> outline_strikes;
enum
{
    MAX_OUTLINE_STRIKES = 48
};

static void retire_outline_strikes(size_t n)
{
    Handle wlh = LM(WidthListHand);
    for(size_t i = 0; i < n && i < outline_strikes.size(); i++)
    {
        Handle strike = outline_strikes[i].strike;
        if(wlh)
        {
            GUEST<WHandle> *hp = *WIDTHLISTHAND;
            int n_entries = GetHandleSize(wlh) / sizeof(GUEST<Handle>);
            for(int j = 0; j < n_entries; j++)
            {
                if(!hp[j] || (LONGINT)hp[j].raw() == (LONGINT)-1
                   || !width_entry_mapped(&hp[j], j))
                    continue;
                WPtr wp = *hp[j];
                if(wp && wp->tabFont == strike && wp != (WPtr)LM(WidthPtr))
                    wp->fSize = 0;
            }
        }
        if(LM(ROMlib_fmo).fontHandle == strike)
            reset_myfmi();
        DisposeHandle(strike);
    }
    outline_strikes.erase(outline_strikes.begin(), outline_strikes.begin() + std::min(n, outline_strikes.size()));
}

static Handle outline_strike(Handle sfnt, INTEGER ppem)
{
    for(const OutlineStrike &o : outline_strikes)
        if(o.sfnt == sfnt && o.ppem == ppem)
            return o.strike;
    LoadResource(sfnt);
    if(!*sfnt)
        return nullptr;
    if(outline_strikes.size() >= MAX_OUTLINE_STRIKES)
        retire_outline_strikes(MAX_OUTLINE_STRIKES / 4);
    SfntMetrics m;
    Handle h = ROMlib_sfnt_strike(sfnt, ppem, &m);
    if(h)
        outline_strikes.push_back({ sfnt, ppem, h, m });
    /* the strike is what's used: the outline (70K for Charcoal) may go */
    HPurge(sfnt);
    return h;
}

static const SfntMetrics *outline_metrics(Handle strike)
{
    for(const OutlineStrike &o : outline_strikes)
        if(o.strike == strike)
            return &o.metrics;
    return nullptr;
}

bool Executor::ROMlib_outline_strike(Handle strike)
{
    return outline_metrics(strike) != nullptr;
}

/* The width table's font from FOND's outline at the size (8.x answers
   sizes it has no strike of from the outline); a size whose strike won't
   fit an NFNT is drawn from a smaller one, scaled. */
static bool use_outline(FHandle fond, INTEGER family, FMInput *fmip)
{
    INTEGER style = 0;
    Handle sfnt = fond_outline(fond, fmip->face, &style);
    if(!sfnt)
        return false;
    for(int ppem = std::min<int>(fmip->size, 255); ppem >= 4; ppem = ppem * 3 / 4)
        if(Handle strike = outline_strike(sfnt, ppem))
        {
            WIDTHPTR->fHand = (Handle)fond;
            WIDTHPTR->fID = family;
            WIDTHPTR->fSize = ppem;
            WIDTHPTR->face = style;
            WIDTHPTR->tabFont = strike;
            LM(LastFOND) = (FamRecHandle)fond;
            return true;
        }
    return false;
}

static bool fond_has_outline(FHandle fh)
{
    INTEGER style;
    return fond_outline(fh, 0x7F, &style) != nullptr;
}

Handle Executor::ROMlib_family_outline(INTEGER family, INTEGER face, INTEGER *stylep)
{
    FHandle fh = find_fond(family, [](FHandle fh) { return fond_has_outline(fh); });
    return fh ? fond_outline(fh, face, stylep) : nullptr;
}

Boolean Executor::C_RealFont(INTEGER fnum, INTEGER sz) /* IMI-223 */
{
    Handle h;
    int retval;

    SetResLoad(false);
    h = GetResource("FONT"_4, FONTRESID(fnum, sz));
    if(!h)
        h = GetResource("NFNT"_4, FONTRESID(fnum, sz));
    retval = !!h;
    SetResLoad(true);
    /* a strike of the size in any of the family's FONDs, or an outline
       (any size) */
    if(!retval)
        retval = find_fond(fnum, [&](FHandle fh) { return fond_has_size(fh, sz) || fond_has_outline(fh); }) != nullptr;
    return retval;
}

static INTEGER closestface();

/* MacPhoenix: the System file's FOND points Chicago 12 at FONT 12, which a
   real Mac has in ROM. With no ROM, look for the same family's FOND in the
   other open files (the Fonts folder suitcases, chained below the System
   file) and take the face from there. */
static Handle font_from_other_fonds(INTEGER family, FHandle first)
{
    Handle found = nullptr;
    INTEGER saved = LM(CurMap);
    resmaphand map;
    WALKMAPTOP(map)
        UseResFile((*map)->resfn);
        Handle h = Get1Resource("FOND"_4, family);
        if(!h || h == (Handle)first)
            continue;
        LM(LastFOND) = (FamRecHandle)h;
        INTEGER resid = closestface();
        if(!(found = GetResource("NFNT"_4, resid)))
            found = GetResource("FONT"_4, resid);
        if(found)
        {
            WIDTHPTR->fHand = h;
            break;
        }
    EWALKMAP()
    if(!found)
        LM(LastFOND) = (FamRecHandle)first;
    LM(CurMap) = saved;
    return found;
}

static INTEGER closestface() /* no args, uses WIDTHPTR */
{
    fatabentry *p, *ep, *bestp;
    INTEGER size;
    GUEST<INTEGER> *ip;
    INTEGER nmatch, want, newmatch;

    bestp = 0;
    size = WIDTHPTR->fSize;
    ip = &(*(FHandle)LM(LastFOND))->ffVersion + 1;
    nmatch = -1;
    want = WIDTHPTR->aFace;
    for(p = (fatabentry *)(ip + 1), ep = p + *ip + 1;
        p < ep && p->size <= size; p++)
    {
        if(!p->size) /* an outline font ('sfnt') */
            continue;
        if(!bestp)
            bestp = p; /* pick something */
        if((p->size == size) && !(p->style & ~want))
        {
            if((newmatch = countones(p->style & want)) >= nmatch)
            {
                /* note, IMIV is vague on precedence, so I choose the
			 last entry, though all I need is some comparison
			 function for bestp->style and p->style */
                nmatch = newmatch;
                bestp = p;
            }
        }
    }
    if(!bestp)
        return 0;
    WIDTHPTR->face = bestp->style;
    return bestp->fontresid;
}

static FHandle
at_least_one_fond_entry(INTEGER family)
{
    FHandle retval;

    retval = (FHandle)GetResource("FOND"_4, family);
    if(retval)
    {
        fatabentry *p;
        LoadResource((Handle)retval);
        if(fond_entries(retval, &p) <= 0)
            retval = 0;
    }
    return retval;
}

/*
 * TODO:  NFNT below
 */

#define AVAILABLE(x) (WIDTHPTR->fSize = (x), WIDTHPTR->tabFont = GetResource("FONT"_4, FONTRESID(family, (x))))

static void newwidthtable(FMInput *fmip)
{
    FHandle fh;
    INTEGER lesser, greater, wanted_family, family, fontresid, powerof2;
    LONGINT todelete;
    GUEST<THz> savezone;
    bool tried_app_font;
    int n_tried_sys_font;

    savezone = LM(TheZone);
    LM(TheZone) = LM(SysZone);
    LM(WidthTabHandle) = (WidthTableHandle)NewHandle((Size)sizeof(WidthTable));
    LM(TheZone) = savezone;
    /* MacPhoenix: no memory for one more: the oldest listed table, taken
       out of the list (re-entered at the front below) */
    if(!LM(WidthTabHandle))
    {
        GUEST<WHandle> *whp = *WIDTHLISTHAND;
        int n = GetHandleSize((Handle)LM(WidthListHand)) / sizeof(GUEST<Handle>);
        for(int i = n - 1; i >= 0 && !LM(WidthTabHandle); i--)
            if(whp[i] && whp[i].raw() != (uint32_t)-1 && width_entry_mapped(&whp[i], i))
            {
                LM(WidthTabHandle) = guest_cast<WidthTableHandle>(whp[i]);
                Munger((Handle)LM(WidthListHand), i * sizeof(GUEST<Handle>), nullptr,
                       sizeof(GUEST<Handle>), (Ptr) "", 0);
            }
        /* none to reuse: no table this time (WidthPtr stays as it was)
           rather than a nil handle listed and locked */
        if(!LM(WidthTabHandle))
            return;
    }
    Munger((Handle)LM(WidthListHand), (LONGINT)0, (Ptr)0, 0,
           (Ptr)&LM(WidthTabHandle), sizeof(LM(WidthTabHandle)));

    /*
 * NOTE: the code below is a bit confusing.  What is going on is we always want
 *       to put the new entry in the front, but if we've grown too large then
 *	 we try to find a (Handle) 0 or a (Handle) -1) to delete.  If we find
 *	 neither we delete the last entry.
 */

    if(GetHandleSize((Handle)LM(WidthListHand)) > (int)sizeof(GUEST<Handle>) * MAXTABLES)
    {
        todelete = 0;
        if(Munger((Handle)LM(WidthListHand), (LONGINT)0, (Ptr)&todelete,
                  sizeof(todelete), (Ptr) "", 0)
           < 0)
        {
            todelete = -1;
            if(Munger((Handle)LM(WidthListHand), (LONGINT)0, (Ptr)&todelete,
                      sizeof(todelete), (Ptr) "", 0)
               < 0)
            {
                DisposeHandle((Handle)(*WIDTHLISTHAND)[MAXTABLES]);
                SetHandleSize(LM(WidthListHand),
                              (Size)sizeof(GUEST<Handle>) * MAXTABLES);
            }
        }
    }
    HLock((Handle)LM(WidthTabHandle));
    LM(WidthPtr) = *LM(WidthTabHandle);

    WIDTHPTR->sExtra = PORT_SP_EXTRA(qdGlobals().thePort);
    WIDTHPTR->inNumer = fmip->numer;
    WIDTHPTR->inDenom = fmip->denom;
    WIDTHPTR->aFID = fmip->family;
    WIDTHPTR->aFace = fmip->face;
    WIDTHPTR->aSize = fmip->size;
    WIDTHPTR->device = fmip->device;
    WIDTHPTR->tabSize = 0; /* I don't know what this field is, but it */
    /* appears to have flags in it */

    SetResLoad(true);
    fh = 0;
    wanted_family = fmip->family;
    tried_app_font = false;
    n_tried_sys_font = 0;
    if((fh = at_least_one_fond_entry(wanted_family))
       || GetResource("FONT"_4, FONTRESID(wanted_family, fmip->size))
       || GetResource("FONT"_4, FONTRESID(wanted_family, 0)))
        family = wanted_family;
    else if((fh = at_least_one_fond_entry(LM(ApFontID)))
            || GetResource("FONT"_4, FONTRESID(LM(ApFontID), 0)))
    {
        family = LM(ApFontID);
        tried_app_font = true;
    }
    else
    {
        fh = at_least_one_fond_entry(LM(SysFontFam));
        family = LM(SysFontFam);
        tried_app_font = true;
        ++n_tried_sys_font;
    }
    WIDTHPTR->tabFont = 0;
    while(!WIDTHPTR->tabFont && n_tried_sys_font < 2)
    {
        WIDTHPTR->fHand = (Handle)fh;
        WIDTHPTR->fID = family;
        if(fh)
        {
            /* 8.x: the strike of the size from whichever of the family's
               FONDs has one, else the outline, else the nearest strike
               scaled (OutlinePreferred: the outline first) */
            INTEGER want = fmip->size;
            FHandle exact = best_fond_with_size(family, want, fmip->face);
            if(!exact || C_GetOutlinePreferred())
            {
                FHandle outline = find_fond(family, [](FHandle h) { return fond_has_outline(h); });
                if(outline && use_outline(outline, family, fmip))
                    break;
            }
            if(exact)
                WIDTHPTR->fHand = (Handle)(fh = exact);
            LM(LastFOND) = (FamRecHandle)fh;

            findclosestfond(fh, fmip->size, &powerof2, &lesser, &greater);
            if(powerof2 == fmip->size)
                WIDTHPTR->fSize = powerof2;
            else
            {
                if(LM(FScaleDisable))
                    WIDTHPTR->fSize = lesser ? lesser : greater;
                else
                {
                    if(powerof2)
                        WIDTHPTR->fSize = powerof2;
                    else
                    {
                        if(!greater || (lesser && fmip->size - lesser <= greater - fmip->size))
                            WIDTHPTR->fSize = lesser;
                        else
                            WIDTHPTR->fSize = greater;
                    }
                }
            }
            if(!WIDTHPTR->fSize)
                fprintf(stderr, "fh:  fSize = 0\n");
            fontresid = closestface();
            if(!(WIDTHPTR->tabFont = GetResource("NFNT"_4, fontresid)))
                WIDTHPTR->tabFont = GetResource("FONT"_4, fontresid);
            if(!WIDTHPTR->tabFont)
                WIDTHPTR->tabFont = GetResource("FONT"_4,
                                                   FONTRESID(family, WIDTHPTR->fSize));
            if(!WIDTHPTR->tabFont)
                WIDTHPTR->tabFont = font_from_other_fonds(family, fh);
            if(!WIDTHPTR->tabFont)
                warning_unexpected("");
        }
        if(!WIDTHPTR->tabFont)
        {
            WIDTHPTR->face = 0;

            if(!AVAILABLE(fmip->size))
            {
                if(LM(FScaleDisable))
                {
                    findclosestfont(family, fmip->size, &lesser, &greater);
                    WIDTHPTR->fSize = lesser ? lesser : greater;
                    WIDTHPTR->tabFont = GetResource("FONT"_4,
                                                       FONTRESID(family, WIDTHPTR->fSize));
                }
                else
                {
                    if(!AVAILABLE(fmip->size * 2) && !AVAILABLE(fmip->size / 2))
                    {
                        findclosestfont(family, fmip->size, &lesser, &greater);
                        if(lesser && (fmip->size - lesser <= greater - fmip->size))
                            WIDTHPTR->fSize = lesser;
                        else
                            WIDTHPTR->fSize = greater;
                        WIDTHPTR->tabFont = GetResource("FONT"_4,
                                                           FONTRESID(family, WIDTHPTR->fSize));
                    }
                }
            }
        }
        if(!WIDTHPTR->tabFont)
        {
            if(!tried_app_font)
            {
                tried_app_font = true;
                family = LM(ApFontID);
                fh = at_least_one_fond_entry(family);
            }
            else if(n_tried_sys_font < 2)
            {
                ++n_tried_sys_font;
                family = LM(SysFontFam);
                fh = at_least_one_fond_entry(family);
            }
        }
    }
    LoadResource(WIDTHPTR->tabFont);
    if(!WIDTHPTR->fSize)
        warning_unexpected("!fh:  fSize = 0, family = %d\n",
                           (LONGINT)fmip->family);
}

FMOutPtr Executor::C_FMSwapFont(FMInput *fmip) /* IMI-223 */
{
    Style style;
    FontRec *fp;
    GUEST<INTEGER> savesize, savefamily;
    Boolean needtobuild;

    savesize = fmip->size;
    if(fmip->size <= 0)
        fmip->size = LM(SysFontSiz);

    if(fmip->size <= 0)
        fmip->size = 12;

    if(fmip->denom.h == fmip->numer.h)
    {
        fmip->denom.h = 256;
        fmip->numer.h = 256;
    }
    if(fmip->denom.v == fmip->numer.v)
    {
        fmip->denom.v = 256;
        fmip->numer.v = 256;
    }

    if(fmip->family == applFont)
    {
        savefamily = fmip->family;
        fmip->family = LM(ApFontID);
    }
    else
        savefamily = fmip->family;

    if(fmip->family == 0)
    {
        savefamily = 0;
        fmip->family = LM(SysFontFam);
    }

    if(!width_table_listed() || fmip->family != LM(CurFMInput).family || fmip->size != LM(CurFMInput).size || fmip->face != LM(CurFMInput).face || fmip->device != LM(CurFMInput).device || fmip->numer.h != LM(CurFMInput).numer.h || fmip->numer.v != LM(CurFMInput).numer.v || fmip->denom.h != LM(CurFMInput).denom.h || fmip->denom.v != LM(CurFMInput).denom.v || WIDTHPTR->fSize == -1 || WIDTHPTR->fSize == 0 || (fmip->needBits && !LM(CurFMInput).needBits))
    {
        if((needtobuild = !widthlistmatch(fmip)))
            newwidthtable(fmip);

        LM(CurFMInput) = *fmip;
        LM(ROMlib_fmo).errNum = 0;
        LM(ROMlib_fmo).bold = 0;
        LM(ROMlib_fmo).italic = 0;
        LM(ROMlib_fmo).ulOffset = 0;
        LM(ROMlib_fmo).ulShadow = 0;
        LM(ROMlib_fmo).ulThick = 0;
        LM(ROMlib_fmo).shadow = 0;
        LM(ROMlib_fmo).extra = 0;
        /* MacPhoenix: no strike of any family in memory (all of them
           missing, or no room to load one): nothing to scale from, no
           metrics, no widths; text measures 0 and draws nothing
           (ROMlib_fmo_has_font) rather than reading a nil font */
        LM(ROMlib_fmo).fontHandle = WIDTHPTR->tabFont;
        if(LM(ROMlib_fmo).fontHandle)
            LoadResource(LM(ROMlib_fmo).fontHandle);
        bool have_font = ROMlib_fmo_has_font(&LM(ROMlib_fmo));
        INTEGER fsize = WIDTHPTR->fSize > 0 ? (INTEGER)WIDTHPTR->fSize : (INTEGER)fmip->size;
        LM(ROMlib_fmo).numer.h = 
            (LONGINT)fmip->numer.h * 256 * fmip->size / fmip->denom.h / fsize;
        LM(ROMlib_fmo).numer.v = 
            (LONGINT)fmip->numer.v * 256 * fmip->size / fmip->denom.v / fsize;
        LM(ROMlib_fmo).denom.h = 256;
        LM(ROMlib_fmo).denom.v = 256;
        fp = have_font ? (FontRec *)*((LM(ROMlib_fmo).fontHandle)) : nullptr;
        style = fmip->face ^ WIDTHPTR->face;
        if(style & (int)bold)
            mungfmo(ftstr.boldt, &LM(ROMlib_fmo));
        if(style & (int)italic)
            mungfmo(ftstr.italt, &LM(ROMlib_fmo));
        if(style & (int)outline)
            mungfmo(ftstr.outt, &LM(ROMlib_fmo));
        if(style & (int)shadow)
            mungfmo(ftstr.shadt, &LM(ROMlib_fmo));
        if(style & (int)condense)
            mungfmo(ftstr.condt, &LM(ROMlib_fmo));
        if(style & (int)extend)
            mungfmo(ftstr.extt, &LM(ROMlib_fmo));
        if(style & (int)underline)
        {
            LM(ROMlib_fmo).ulOffset += ftstr.undert[0];
            LM(ROMlib_fmo).ulShadow += ftstr.undert[1];
            LM(ROMlib_fmo).ulThick += ftstr.undert[2];
        }
        /* A FOND whose flags say so (bit 12 set and 13 clear, or bit 13
           clear with FractEnable) gives the styles' extra widths in its
           style property table, in 1/4096 em (8.6: Chicago 12 shadowed
           is 1 wider, not 2) */
        {
            FHandle fond = (FHandle)WIDTHPTR->fHand;
            if(fond && *fond)
            {
                INTEGER flags = (*fond)->ffFlags;
                if(!(flags & 0x2000) && ((flags & 0x1000) || LM(FractEnable)))
                {
                    /* an entry of an em or more counts its sign and low 12
                       bits (8.6: Palatino's condensed, $8155, is -341:
                       -1 at 9-18 points, -2 at 24) */
                    LONGINT sum = 0;
                    for(int i = 0; i < 7; i++)
                        if(style & (1 << i))
                        {
                            INTEGER prop = (*fond)->ffProperty[i + 1];
                            if(prop <= -4096 || prop >= 4096)
                                prop = prop < 0 ? -(prop & 0x0FFF) : prop & 0x0FFF;
                            sum += (LONGINT)prop * fmip->size;
                        }
                    LM(ROMlib_fmo).extra = sum >= 0 ? (sum + 2048) / 4096 : -((-sum + 2048) / 4096);
                }
            }
        }
        if(!have_font)
        {
            /* fSize stays 0: built again at the next call, when there
               may be room. Unscaled, no widths. */
            memset(WIDTHPTR->tabData, 0, sizeof WIDTHPTR->tabData);
            WIDTHPTR->hOutput = WIDTHPTR->vOutput = 256;
            WIDTHPTR->hFactor = WIDTHPTR->vFactor = 256;
        }
        else if(needtobuild)
            buildtable(LM(ROMlib_fmo).extra);
        /* the strike's own metrics, as 8.6 answers them: the style is in
           bold/shadow/extra (ROMlib_styled_font_info adds it) */
        if(!have_font)
        {
            LM(ROMlib_fmo).ascent = 0;
            LM(ROMlib_fmo).descent = 0;
            LM(ROMlib_fmo).widMax = 0;
            LM(ROMlib_fmo).leading = 0;
        }
        else if(const SfntMetrics *m = outline_metrics(WIDTHPTR->tabFont))
        {
            /* the outline's, not the strike's (which holds every glyph) */
            LM(ROMlib_fmo).ascent = m->ascent;
            LM(ROMlib_fmo).descent = m->descent;
            LM(ROMlib_fmo).widMax = m->widMax;
            LM(ROMlib_fmo).leading = m->leading;
            LM(ROMlib_fmo).italic = 0; /* the outline is slanted */
        }
        else
        {
            LM(ROMlib_fmo).ascent = fp->ascent;
            LM(ROMlib_fmo).descent = fp->descent;
            LM(ROMlib_fmo).widMax = fp->widMax;
            LM(ROMlib_fmo).leading = fp->leading;
        }
    }
    else
    {
        if(LM(ROMlib_fmo).fontHandle)
            LoadResource(LM(ROMlib_fmo).fontHandle);
        LM(WidthPtr) = *LM(WidthTabHandle);
    }
    fmip->size = savesize;
    fmip->family = savefamily;
    HUnlock((Handle)LM(WidthTabHandle));
    LM(LastFOND) = guest_cast<FamRecHandle>(WIDTHPTR->fHand);
    return &LM(ROMlib_fmo);
}

/* MacPhoenix: FMSwapFont's answer has a strike in memory to draw with */
bool Executor::ROMlib_fmo_has_font(const FMOutput *fmop)
{
    Handle h = fmop->fontHandle;
    return h && *h;
}

/* GetFontInfo's metrics for FMSwapFont's answer: shadow adds a line
   above and its width below, every style's extra widens widMax (condense
   narrows it); measured on 8.6 */
void Executor::ROMlib_styled_font_info(const FMOutput *fmop, FontInfo *fip)
{
    fip->ascent = fmop->ascent + !!fmop->shadow;
    fip->descent = fmop->descent + fmop->shadow;
    fip->widMax = fmop->widMax + fmop->extra;
    fip->leading = fmop->leading;
}

#define SCALE(x) FixRatio(x *fmop->numer.v, fmop->denom.v)

void Executor::C_FontMetrics(FMetricRec *metrp) /* IMIV-32 */
{
    FMInput fmi;
    FMOutPtr fmop;

    fmi.family = PORT_TX_FONT(qdGlobals().thePort);
    fmi.size = PORT_TX_SIZE(qdGlobals().thePort);
    fmi.face = PORT_TX_FACE(qdGlobals().thePort);
    fmi.needBits = false;
    fmi.device = PORT_DEVICE(qdGlobals().thePort);
    fmi.numer.v = 1;
    fmi.numer.h = 1;
    fmi.denom.v = 1;
    fmi.denom.h = 1;

    fmop = FMSwapFont(&fmi);

    /* TODO:  check out qdGlobals().thePort->device and use the FOND stuff if not
	      going to the screen */

    FontInfo fi;
    ROMlib_styled_font_info(fmop, &fi);
    metrp->ascent = SCALE(fi.ascent);
    metrp->descent = SCALE(fi.descent);
    metrp->leading = SCALE(fi.leading);
    metrp->widMax = SCALE(fi.widMax);
    metrp->wTabHandle = guest_cast<Handle>(LM(WidthTabHandle));
}

void Executor::C_SetFScaleDisable(Boolean disable) /* IMIV-32 */
{
    if(LM(FScaleDisable) != (Byte)disable)
    {
        LM(FScaleDisable) = disable;
        invalidate_all_widths();
        reset_myfmi();
    }
}

void Executor::C_SetFractEnable(Boolean enable) /* IMIV-32 */
{
    LM(FractEnable) = enable;
}
