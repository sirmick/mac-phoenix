/* textutil: the Text Utilities' case and diacritical calls on raw text,
   differential (tools/difftest/run.py textutil): every MacRoman byte
   through LowerText, StripText, UpperText and StripUpperText (InterfaceLib's
   exports of the _LwrText trap family) and through the Script Manager's
   LowercaseText, StripDiacritics, UppercaseText and UppercaseStripDiacritics
   for the system script and for Roman. Serial MReader's PowerPC build
   (a REALbasic program) calls LowerText and jumped to 0 on Executor. */
#include "probe.h"
#include <Script.h>

typedef pascal void (*Text2)(Ptr, short);
typedef pascal void (*Text3)(Ptr, short, short);

static void all_bytes(unsigned char *b)
{
    int i;
    for(i = 0; i < 256; i++)
        b[i] = (unsigned char)i;
}

/* In quarters: out_hex shows 64 bytes in full. */
static void show(const char *name, const unsigned char *b)
{
    char k[64];
    int q;
    for(q = 0; q < 4; q++)
    {
        sprintf(k, "%s.%02x", name, q * 64);
        out_hex(k, b + q * 64, 64);
    }
}

int main(void)
{
    static const char *two[] = { "LowerText", "StripText", "UpperText", "StripUpperText" };
    static const char *three[] = { "LowercaseText", "StripDiacritics", "UppercaseText",
                                   "UppercaseStripDiacritics" };
    unsigned char b[256];
    char k[64];
    int i;

    probe_open("textutil");
    if(SECTION("traps"))
        for(i = 0; i < 4; i++)
        {
            Text2 f = (Text2)probe_bind("InterfaceLib", two[i]);
            if(!f)
                continue;
            all_bytes(b);
            f((Ptr)b, 256);
            show(two[i], b);
            all_bytes(b);
            f((Ptr)b + 65, 10); /* "ABCDEFGHIJ": only those */
            sprintf(k, "%s.part", two[i]);
            out_hex(k, b + 60, 20);
        }
    if(SECTION("script"))
        for(i = 0; i < 4; i++)
        {
            Text3 f = (Text3)probe_bind("InterfaceLib", three[i]);
            if(!f)
                continue;
            all_bytes(b);
            f((Ptr)b, 256, smSystemScript);
            sprintf(k, "%s.system", three[i]);
            show(k, b);
            all_bytes(b);
            f((Ptr)b, 256, smRoman);
            sprintf(k, "%s.roman", three[i]);
            show(k, b);
        }
    probe_close();
    return 0;
}
