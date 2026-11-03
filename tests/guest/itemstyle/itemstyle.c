/* itemstyle.c — what GetItemStyle ($A941) writes, from 68k code through
 * the trap: Pascal's VAR Style is a word there, the style in its low byte,
 * as AppleWorks 5 reads it (its glue passes a word and keeps the low byte
 * to match command keys whose Shift it keeps in style bit $80). PowerPC
 * code through InterfaceLib gets one byte (tests/guest/difftest menu, on
 * 8.6). Results to Host:itemstyle_test.txt. */

#include <ConditionalMacros.h>
#include <Fonts.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <stdio.h>

QDGlobals qd;

static FILE *out;
static int passes, fails;

/* the trap with a word for its VAR, as Pascal callers pass it */
pascal void GetItemStyleWord(MenuHandle m, short item, short *style) = 0xA941;

static void check(const char *name, unsigned got, unsigned want)
{
    if(got == want)
    {
        fprintf(out, "PASS %s\n", name);
        passes++;
    }
    else
    {
        fprintf(out, "FAIL %s got %04x, want %04x\n", name, got, want);
        fails++;
    }
}

int main(void)
{
    MenuHandle m;
    short word;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();

    if((out = fopen("Host:itemstyle_test.txt", "w")) == NULL)
        return 1;

    m = NewMenu(200, "\pProbe");
    AppendMenu(m, "\pPlain;Bold<B;Shifted");
    SetItemStyle(m, 3, 0x80);

    word = (short)0xAAAA;
    GetItemStyleWord(m, 1, &word);
    check("plain", (unsigned short)word, 0x0000);
    word = (short)0xAAAA;
    GetItemStyleWord(m, 2, &word);
    check("bold", (unsigned short)word, 0x0001);
    word = (short)0xAAAA;
    GetItemStyleWord(m, 3, &word);
    check("bit_80", (unsigned short)word, 0x0080);

    DisposeMenu(m);
    fprintf(out, "---\n%d passed, %d failed, 0 skipped\n", passes, fails);
    fclose(out);
    return 0;
}
