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
