/* text: the Font Manager and QuickDraw's text measuring, differential
   (tools/difftest/run.py text): FMSwapFont's output for fonts, sizes and
   styles, GetFontInfo, FontMetrics, character and string widths,
   MeasureText, TruncString, font names and numbers. */
#include "probe.h"
#include <Fonts.h>
#include <TextUtils.h>
#include <Script.h>

#define SET(x) memset(&(x), SENTINEL, sizeof(x))

static const char *names[] = { "Chicago", "Charcoal", "Geneva", "Monaco", "Times", "Helvetica", "Courier", "New York", "Palatino", "Symbol" };
static const short sizes[] = { 9, 10, 12, 14, 18, 24 };
static const Style styles[] = { 0, bold, italic, underline, outline, shadow, condense, extend, bold | italic };
static const char *sample = "The quick brown fox jumps over the lazy dog 0123456789!";

int main(void)
{
    int f, z, y, c;
    char k[96];
    probe_open("text");
    if(SECTION("names"))
        for(f = 0; f < (int)(sizeof names / sizeof names[0]); f++)
        {
            Str255 n, back;
            short num;
            n[0] = strlen(names[f]);
            memcpy(n + 1, names[f], n[0]);
            SET(num);
            GetFNum(n, &num);
            sprintf(k, "%s", names[f]);
            out(k, "%d", num);
            SET(back);
            GetFontName(num, back);
            sprintf(k, "%s.back", names[f]);
            out_str(k, back);
            sprintf(k, "%s.RealFont12", names[f]);
            out(k, "%d", RealFont(num, 12));
            sprintf(k, "%s.FontToScript", names[f]);
            out(k, "%d", FontToScript(num));
        }
    /* 8.5's FontManager library: anti-aliasing switch, ink bounds, font
       info without the port */
    if(SECTION("fm"))
    {
        typedef OSStatus (*SetAAProc)(Boolean, SInt16);
        typedef Boolean (*IsAAProc)(SInt16 *);
        typedef void (*BoundsProc)(short, const void *, Rect *);
        typedef OSErr (*FetchProc)(SInt16, SInt16, SInt16, FontInfo *);
        SetAAProc setAA = (SetAAProc)probe_bind("FontManager", "SetAntiAliasedTextEnabled");
        IsAAProc isAA = (IsAAProc)probe_bind("FontManager", "IsAntiAliasedTextEnabled");
        BoundsProc bounds = (BoundsProc)probe_bind("FontManager", "QDTextBounds");
        FetchProc fetch = (FetchProc)probe_bind("FontManager", "FetchFontInfo");
        static const char *texts[] = { "Hello", "gjpqy", "ii", " ", "" };
        static const struct { short font, size; Style face; } fonts[] = {
            { 3, 9, 0 }, { 0, 12, 0 }, { 20, 24, italic }, { 4, 10, bold }, { 999, 12, 0 }
        };
        if(isAA && setAA)
        {
            short min, was_min;
            Boolean was;
            SET(was_min);
            was = isAA(&was_min);
            out("aa.initial", "%d min %d", was, was_min);
            out("aa.set_on9", "%d", (int)setAA(true, 9));
            SET(min);
            out("aa.on9", "%d min %d", isAA(&min), min);
            out("aa.set_off12", "%d", (int)setAA(false, 12));
            SET(min);
            out("aa.off12", "%d min %d", isAA(&min), min);
            out("aa.set_on0", "%d", (int)setAA(true, 0));
            SET(min);
            out("aa.on0", "%d min %d", isAA(&min), min);
            setAA(was, was_min);
        }
        if(bounds)
            for(f = 0; f < 4; f++)
            {
                TextFont(fonts[f].font); TextSize(fonts[f].size); TextFace(fonts[f].face);
                for(c = 0; c < (int)(sizeof texts / sizeof texts[0]); c++)
                {
                    Rect r;
                    MoveTo(f ? 10 : 0, f ? 20 : 0);
                    SET(r);
                    bounds(strlen(texts[c]), texts[c], &r);
                    sprintf(k, "bounds.%d.%d.%d.%s", fonts[f].font, fonts[f].size, fonts[f].face, texts[c]);
                    out_rect(k, &r);
                }
            }
        TextFont(0); TextSize(0); TextFace(0);
        if(fetch)
            for(f = 0; f < (int)(sizeof fonts / sizeof fonts[0]); f++)
            {
                FontInfo fi;
                OSErr err;
                SET(fi);
                err = fetch(fonts[f].font, fonts[f].size, fonts[f].face, &fi);
                sprintf(k, "fetch.%d.%d.%d", fonts[f].font, fonts[f].size, fonts[f].face);
                out(k, "%d: %d %d %d %d", err, fi.ascent, fi.descent, fi.widMax, fi.leading);
            }
        {
            FontInfo fi;
            GetFontInfo(&fi);
            out("fetch.port", "font %d size %d face %d: %d %d", qd.thePort->txFont, qd.thePort->txSize,
                qd.thePort->txFace, fi.ascent, fi.widMax);
        }
    }
    for(f = 0; f < (int)(sizeof names / sizeof names[0]); f++)
    {
        Str255 n;
        short num;
        char sec[32];
        n[0] = strlen(names[f]);
        memcpy(n + 1, names[f], n[0]);
        GetFNum(n, &num);
        sprintf(sec, "font-%s", names[f]);
        for(c = 0; sec[c]; c++)
            if(sec[c] == ' ')
                sec[c] = '_';
        if(!SECTION(sec))
            continue;
        for(z = 0; z < (int)(sizeof sizes / sizeof sizes[0]); z++)
            for(y = 0; y < (int)(sizeof styles / sizeof styles[0]); y++)
            {
                FMInput in;
                FMOutPtr o;
                FontInfo fi;
                FMetricRec fm;
                short widths[256];
                unsigned long crc;
                Str255 t;
                sprintf(k, "%d.%d", sizes[z], styles[y]);
                memset(&in, 0, sizeof in);
                in.family = num; in.size = sizes[z]; in.face = styles[y]; in.needBits = false;
                in.numer.h = in.numer.v = in.denom.h = in.denom.v = 1;
                o = FMSwapFont(&in);
                {
                    char kk[96];
                    sprintf(kk, "%s.swap", k);
                    out(kk, "err %d asc %d desc %d widMax %d lead %d bold %d ital %d ul %d %d %d shadow %d extra %d numer %d/%d denom %d/%d",
                        o->errNum, o->ascent, o->descent, o->widMax, o->leading, o->bold,
                        o->italic, o->ulOffset, o->ulShadow, o->ulThick, o->shadow,
                        o->extra, o->numer.h, o->numer.v, o->denom.h, o->denom.v);
                    sprintf(kk, "%s.swap.font", k);
                    out(kk, "%s", o->fontHandle ? "set" : "nil");
                }
                TextFont(num); TextSize(sizes[z]); TextFace(styles[y]);
                SET(fi);
                GetFontInfo(&fi);
                {
                    char kk[96];
                    sprintf(kk, "%s.info", k);
                    out(kk, "%d %d %d %d", fi.ascent, fi.descent, fi.widMax, fi.leading);
                    SET(fm);
                    FontMetrics(&fm);
                    sprintf(kk, "%s.metrics", k);
                    out(kk, "%08lx %08lx %08lx %08lx", (long)fm.ascent, (long)fm.descent, (long)fm.leading, (long)fm.widMax);
                    for(c = 0; c < 256; c++)
                        widths[c] = CharWidth(c);
                    crc = probe_crc32((unsigned char *)widths, sizeof widths, 0);
                    sprintf(kk, "%s.charwidths", k);
                    out(kk, "%08lx w(A)=%d w(i)=%d w(W)=%d", crc, widths['A'], widths['i'], widths['W']);
                    /* every width: runs of "w*n" (plain), of "+d*n" against
                       plain (the other styles) */
                    {
                        static short plain[256];
                        char list[1400];
                        int len = 0, run;
                        if(styles[y] == 0)
                            memcpy(plain, widths, sizeof plain);
                        for(c = 0; c < 256 && len < (int)sizeof list - 16; c += run)
                        {
                            int v = styles[y] ? widths[c] - plain[c] : widths[c];
                            for(run = 1; c + run < 256 && (styles[y] ? widths[c + run] - plain[c + run] : widths[c + run]) == v; run++)
                                ;
                            len += sprintf(list + len, run > 1 ? (styles[y] ? " %+d*%d" : " %d*%d") : (styles[y] ? " %+d" : " %d"), v, run);
                        }
                        sprintf(kk, "%s.widths", k);
                        out(kk, "%s", list + 1);
                    }
                    t[0] = strlen(sample);
                    memcpy(t + 1, sample, t[0]);
                    sprintf(kk, "%s.StringWidth", k);
                    out(kk, "%d", StringWidth(t));
                    {
                        short offs[64];
                        MeasureText(20, (Ptr)sample, (Ptr)offs);
                        sprintf(kk, "%s.MeasureText", k);
                        out(kk, "%d %d %d", offs[1], offs[10], offs[20]);
                    }
                    for(c = 40; c <= 160; c += 60)
                    {
                        Str255 tt;
                        short res;
                        memcpy(tt, t, t[0] + 1);
                        res = TruncString(c, tt, truncEnd);
                        sprintf(kk, "%s.trunc%d", k, c);
                        out(kk, "%d len %d w %d", res, tt[0], StringWidth(tt));
                    }
                }
            }
        TextFont(0); TextSize(0); TextFace(0);
    }
    probe_close();
    return 0;
}
