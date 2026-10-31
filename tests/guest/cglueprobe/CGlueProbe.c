/* CGlueProbe: InterfaceLib's lowercase C routines (numtostring,
   getindstring, ptinrect, ...) against their Pascal routines on the same
   machine: each answer the C routine gives must be the Pascal one's, its
   strings C strings, and its input strings left as they were. Run on the
   G3 (the reference) and on Executor, whose routines are declared with
   `cglue:` in the definitions (multiversal/executor.rb, declare_cglue).
   Results in Host:cglue_results.txt (tests/test_guest_suite.sh). */
#include <Types.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Resources.h>
#include <Files.h>
#include <ToolUtils.h>
#include <TextUtils.h>
#include <OSUtils.h>
#include <Packages.h>
#include <StandardFile.h>
#include <stdio.h>
#include <string.h>

/* The C routines: InterfaceLib's (Universal Interfaces, CGLUESUPPORTED). */
void numtostring(long n, char *s);
void stringtonum(const char *s, long *n);
void getindstring(char *s, short id, short index);
Boolean equalstring(const char *a, const char *b, Boolean cs, Boolean ds);
short relstring(const char *a, const char *b, Boolean cs, Boolean ds);
short iucompstring(const char *a, const char *b);
void upperstring(char *s, Boolean ds);
void getfnum(const char *name, short *num);
void getfontname(short num, char *name);
Boolean ptinrect(const Point *p, const Rect *r);
void addpt(const Point *src, Point *dst);
Boolean equalpt(const Point *a, const Point *b);
void pt2rect(const Point *a, const Point *b, Rect *r);
void iudatestring(long date, DateForm form, char *s);
void iutimestring(long date, Boolean secs, char *s);
OSErr getvol(char *name, short *vref);
WindowPtr newwindow(void *storage, const Rect *r, const char *title, Boolean vis, short proc,
                    WindowPtr behind, Boolean goaway, long refcon);
void getwtitle(WindowPtr w, char *title);
void setwtitle(WindowPtr w, const char *title);
short stringwidth(const char *s);
void drawstring(const char *s);
MenuHandle newmenu(short id, const char *title);
void appendmenu(MenuHandle m, const char *items);
void getmenuitemtext(MenuHandle m, short item, char *s);
void setmenuitemtext(MenuHandle m, short item, const char *s);
Handle getnamedresource(ResType type, const char *name);
void getresinfo(Handle h, short *id, ResType *type, char *name);
void sfputfile(Point *where, const char *prompt, const char *name, DlgHookUPP hook, SFReply *reply);

static int fails;

static void same_str(const char *what, const char *c, Str255 ps)
{
    char want[256];
    memcpy(want, ps + 1, ps[0]);
    want[ps[0]] = 0;
    if(strcmp(c, want) == 0)
        printf("PASS %s: \"%s\"\n", what, c);
    else
        printf("FAIL %s: \"%s\" (Pascal \"%s\")\n", what, c, want), fails++;
}

static void same_num(const char *what, long c, long want)
{
    if(c == want)
        printf("PASS %s: %ld\n", what, c);
    else
        printf("FAIL %s: %ld (Pascal %ld)\n", what, c, want), fails++;
}

static void unchanged(const char *what, const char *s, const char *was)
{
    if(strcmp(s, was) == 0)
        printf("PASS %s: input \"%s\" as it was\n", what, s);
    else
        printf("FAIL %s: input now \"%s\", was \"%s\"\n", what, s, was), fails++;
}

static void dump_name(const char *what, SFReply *r)
{
    int i;
    printf("INFO %s: good %d fName bytes", what, r->good);
    for(i = 0; i < 12; i++)
        printf(" %02x", r->fName[i]);
    printf("\n");
}

int main(void)
{
    char buf[256], in[64];
    Str255 ps;
    long a, b;
    short sa, sb;
    Point p1 = { 10, 20 }, p2 = { 80, 5 }, pc;
    Rect r = { 0, 0, 50, 50 }, r1, r2;
    unsigned long now;
    WindowPtr w;
    MenuHandle m;
    Handle h;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    freopen("Host:cglue_results.txt", "w", stdout);

    numtostring(-1234, buf);
    NumToString(-1234, ps);
    same_str("numtostring", buf, ps);
    strcpy(in, "-5678");
    stringtonum(in, &a);
    StringToNum("\p-5678", &b);
    same_num("stringtonum", a, b);
    unchanged("stringtonum", in, "-5678");

    getindstring(buf, 128, 2);
    GetIndString(ps, 128, 2);
    same_str("getindstring", buf, ps);

    same_num("equalstring", equalstring("Abc", "abc", false, false),
             EqualString("\pAbc", "\pabc", false, false));
    same_num("equalstring casesig", equalstring("Abc", "abc", true, false),
             EqualString("\pAbc", "\pabc", true, false));
    same_num("relstring", relstring("abc", "abd", true, true), RelString("\pabc", "\pabd", true, true));
    same_num("iucompstring", iucompstring("abc", "ABD"), IUCompString("\pabc", "\pABD"));
    strcpy(buf, "upper case");
    upperstring(buf, false);
    memcpy(ps, "\pupper case", 11);
    UpperString(ps, false);
    same_str("upperstring", buf, ps);

    strcpy(in, "Geneva");
    getfnum(in, &sa);
    GetFNum("\pGeneva", &sb);
    same_num("getfnum", sa, sb);
    unchanged("getfnum", in, "Geneva");
    getfontname(4, buf);
    GetFontName(4, ps);
    same_str("getfontname", buf, ps);

    same_num("ptinrect", ptinrect(&p1, &r), PtInRect(p1, &r));
    same_num("ptinrect outside", ptinrect(&p2, &r), PtInRect(p2, &r));
    pc = p2;
    addpt(&p1, &pc);
    same_num("addpt", *(long *)&pc, ((long)(p1.v + p2.v) << 16) | (unsigned short)(p1.h + p2.h));
    same_num("equalpt", equalpt(&p1, &p2), EqualPt(p1, p2));
    pt2rect(&p1, &p2, &r1);
    Pt2Rect(p1, p2, &r2);
    same_num("pt2rect", memcmp(&r1, &r2, sizeof r1), 0);

    GetDateTime(&now);
    iudatestring(now, shortDate, buf);
    IUDateString(now, shortDate, ps);
    same_str("iudatestring", buf, ps);
    iutimestring(now, true, buf);
    IUTimeString(now, true, ps);
    same_str("iutimestring", buf, ps);
    getvol(buf, &sa);
    GetVol(ps, &sb);
    same_str("getvol", buf, ps);
    same_num("getvol vRefNum", sa, sb);

    SetRect(&r1, 40, 60, 240, 160);
    w = newwindow(NULL, &r1, "CGlue", true, documentProc, (WindowPtr)-1, false, 0);
    getwtitle(w, buf);
    GetWTitle(w, ps);
    same_str("getwtitle", buf, ps);
    same_str("newwindow title", "CGlue", ps);
    setwtitle(w, "Renamed");
    GetWTitle(w, ps);
    same_str("setwtitle", "Renamed", ps);
    SetPort(w);
    same_num("stringwidth", stringwidth("Hello"), StringWidth("\pHello"));
    MoveTo(10, 20);
    drawstring("Hello");
    GetPen(&pc);
    same_num("drawstring pen", pc.h - 10, StringWidth("\pHello"));

    m = newmenu(200, "Probe");
    appendmenu(m, "One;Two");
    getmenuitemtext(m, 2, buf);
    GetMenuItemText(m, 2, ps);
    same_str("getmenuitemtext", buf, ps);
    setmenuitemtext(m, 1, "First");
    GetMenuItemText(m, 1, ps);
    same_str("setmenuitemtext", "First", ps);
    same_num("CountMItems", CountMItems(m), 2);

    h = getnamedresource('STR ', "CGlue named");
    same_num("getnamedresource", (long)h, (long)GetNamedResource('STR ', "\pCGlue named"));
    if(h)
    {
        ResType t;
        getresinfo(h, &sa, &t, buf);
        GetResInfo(h, &sb, &t, ps);
        same_str("getresinfo name", buf, ps);
        same_num("getresinfo id", sa, sb);
    }
    /* HOpenResFile with an empty name (a directory's): the name is the
       caller's constant, left as it was (MPW Shell's is one byte at the end
       of a heap block). */
    {
        unsigned char name[16];
        CInfoPBRec pb;
        memset(name, 0xA5, sizeof name);
        name[0] = 0;
        memset(&pb, 0, sizeof pb);
        pb.dirInfo.ioNamePtr = NULL;
        pb.dirInfo.ioFDirIndex = -1;
        pb.dirInfo.ioDrDirID = 0;
        PBGetCatInfoSync(&pb);                  /* the default directory */
        sa = HOpenResFile(0, pb.dirInfo.ioDrDirID, name, fsRdPerm);
        printf("INFO HOpenResFile empty name: %d, ResError %d\n", sa, ResError());
        same_num("HOpenResFile name bytes", name[0] == 0 && name[1] == 0xA5 && name[15] == 0xA5, 1);
    }
    fflush(stdout);

    /* sfputfile: the prompt and name C strings, the reply's fName a
       Pascal string, as on the G3 (8.6). The test presses Return (Save). */
    {
        SFReply reply;
        Point where = { 80, 80 };
        memset(&reply, 0, sizeof reply);
        printf("WAITING sfputfile\n");
        fflush(stdout);
        sfputfile(&where, "Save as:", "cglue-probe", NULL, &reply);
        dump_name("sfputfile", &reply);
        same_num("sfputfile good", reply.good, 1);
        same_num("sfputfile fName Pascal", memcmp(reply.fName, "\pcglue-probe", 12), 0);
    }

    printf("DONE %d\n---\n", fails);
    return 0;
}
