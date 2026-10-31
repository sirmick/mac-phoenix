/* listcell.c — LNextCell at the end of a list: what it returns and where
 * it leaves the cell. AppleWorks 5 counts a list's selected cells with
 * LGetSelect and LNextCell and stops when LGetSelect finds no more
 * (its Define Database Fields dialog). Results to Host:listcell_test.txt. */

#include <ConditionalMacros.h>
#include <Fonts.h>
#include <Lists.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <stdio.h>

QDGlobals qd;

static FILE *out;
static int passes, fails;

static void check(const char *name, Boolean ret, Cell c, Boolean want_ret, short want_h, short want_v)
{
    if(ret == want_ret && c.h == want_h && c.v == want_v)
    {
        fprintf(out, "PASS %s\n", name);
        passes++;
    }
    else
    {
        fprintf(out, "FAIL %s got %d (%d,%d), want %d (%d,%d)\n", name, ret, c.h, c.v,
                want_ret, want_h, want_v);
        fails++;
    }
}

static void next(const char *name, ListHandle l, Boolean hnext, Boolean vnext, short h, short v,
                 Boolean want_ret, short want_h, short want_v)
{
    Cell c;
    Boolean ret;

    c.h = h;
    c.v = v;
    ret = LNextCell(hnext, vnext, &c, l);
    check(name, ret, c, want_ret, want_h, want_v);
}

int main(void)
{
    WindowPtr w;
    Rect r = { 40, 40, 200, 240 }, view = { 0, 0, 100, 150 }, bounds;
    Point size = { 0, 0 };
    ListHandle l;
    Cell c;
    Boolean ret;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();

    if((out = fopen("Host:listcell_test.txt", "w")) == NULL)
        return 1;

    w = NewWindow(NULL, &r, "\pList", false, documentProc, (WindowPtr)-1, false, 0);
    SetPort(w);

    SetRect(&bounds, 0, 0, 2, 2);               /* two columns, two rows */
    l = LNew(&view, &bounds, size, 0, w, false, false, false, false);
    next("middle", l, true, true, 0, 0, true, 1, 0);
    next("row_end", l, true, true, 1, 0, true, 0, 1);
    next("last_both", l, true, true, 1, 1, false, 0, 2);
    next("last_h_only", l, true, false, 1, 1, false, 0, 1);
    next("last_v_only", l, false, true, 1, 1, false, 1, 2);
    LDispose(l);

    SetRect(&bounds, 0, 0, 1, 1);               /* one cell, selected */
    l = LNew(&view, &bounds, size, 0, w, false, false, false, false);
    c.h = c.v = 0;
    LSetSelect(true, c, l);
    ret = LGetSelect(true, &c, l);
    check("select_found", ret, c, true, 0, 0);
    ret = LNextCell(true, true, &c, l);
    check("one_cell_next", ret, c, false, 0, 1);
    ret = LGetSelect(true, &c, l);
    check("select_after_end", ret, c, false, 0, 1);
    LDispose(l);

    DisposeWindow(w);
    fprintf(out, "---\n%d passed, %d failed, 0 skipped\n", passes, fails);
    fclose(out);
    return 0;
}
