/* intl: GetIntlResourceTable on the System's own 'itl2' and 'itl4',
   differential (tools/difftest/run.py intl): for the system script and
   Roman, each table code (word select, word wrap, number parts, untoken,
   white space): the error, which resource the handle is (the 'itl2' or
   'itl4' GetIntlResource answers), the offset and length, and the table's
   bytes; what GetIntlResource answers for 0 to 4. Microsoft Works 4.0b's text engine takes a nil word-break table
   for "not enough memory"; the gtest (ResourceTest.IntlWordBreakTables)
   builds its 'itl2' at the offsets Executor reads, so only Apple's code on
   Apple's tables can say they are the right ones. */
#include "probe.h"
#include <Script.h>
#include <TextUtils.h>
#include <Resources.h>

/* Which resource a handle is: its type and ID (GetResInfo). */
static void out_res(const char *key, Handle h)
{
    short id = -1;
    ResType type = 0;
    Str255 name;
    if(!h)
    {
        out(key, "(nil)");
        return;
    }
    GetResInfo(h, &id, &type, name);
    if(ResError())
        out(key, "not a resource (%d)", ResError());
    else
        out(key, "'%c%c%c%c' %d", (char)(type >> 24), (char)(type >> 16), (char)(type >> 8),
            (char)type, id);
}

static void table(const char *script_name, short script, short code)
{
    static const char *names[] = { "wordSelect", "wordWrap", "numberParts", "unToken",
                                   "whiteSpace" };
    Handle h = (Handle)0xA5A5A5A5;
    long offset = (long)0xA5A5A5A5, length = (long)0xA5A5A5A5;
    char k[64];

    GetIntlResourceTable(script, code, &h, &offset, &length);
    sprintf(k, "%s.%s.err", script_name, names[code]);
    out(k, "%d", ResError());
    sprintf(k, "%s.%s.handle", script_name, names[code]);
    if(h == (Handle)0xA5A5A5A5)
        out(k, "(unset)");
    else if(!h)
        out(k, "(nil)");
    else if(h == GetIntlResource(2))
        out(k, "itl2");
    else if(h == GetIntlResource(4))
        out(k, "itl4");
    else
        out(k, "other");
    sprintf(k, "%s.%s.resource", script_name, names[code]);
    if(h != (Handle)0xA5A5A5A5)
        out_res(k, h);
    sprintf(k, "%s.%s.offset", script_name, names[code]);
    out(k, "%ld", offset);
    sprintf(k, "%s.%s.length", script_name, names[code]);
    out(k, "%ld", length);
    if(h && h != (Handle)0xA5A5A5A5 && *h && offset > 0 && length > 0
       && offset + length <= GetHandleSize(h))
    {
        sprintf(k, "%s.%s.data", script_name, names[code]);
        out_hex(k, *h + offset, length);
    }
}

int main(void)
{
    short code;

    probe_open("intl");
    /* GetIntlResource: the system script's 'itl0' 'itl1' 'itl2' 'itl4'. */
    if(SECTION("getintl"))
        for(code = 0; code <= 4; code++)
        {
            char k[32];
            sprintf(k, "GetIntlResource.%d", code);
            out_res(k, GetIntlResource(code));
        }
    if(SECTION("system"))
        for(code = smWordSelectTable; code <= smWhiteSpaceList; code++)
            table("system", smSystemScript, code);
    if(SECTION("roman"))
        for(code = smWordSelectTable; code <= smWhiteSpaceList; code++)
            table("roman", smRoman, code);
    probe_close();
    return 0;
}
