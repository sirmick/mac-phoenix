/* cdevtest.c — CdevTest, a control panel ('cdev' -4064, a 68k code resource)
 * that writes every message it is sent to Host:cdevtest.log, one line each:
 * "NAME msg=M item=I value=V" (V: unset, 0, h for a handle). nulDev and
 * cursorDev are written when the one sent changes; closeDev's line counts them
 * ("nul=N cursor=N"), initDev's ROM85 and HWCfgFlags ("rom85=N hwcfg=N") and where the panel
 * is ("win=L,T R,B hit=X,Y"); each line whether the panel's file is the
 * current resource file and its dialog the port ("res=panel port=panel");
 * hitDev leaves the System file current, as Text does: the shell makes
 * the panel's file current again for the next message. (Left with another
 * port, the G3's shell doesn't set the port again.)
 *
 * Its 'STR ' -4064 says when to answer an error: pairs of a message (hex
 * digit) and what to answer the first time it comes, M cdevMemErr, R
 * cdevResErr, G cdevGenErr, Z 0 (macDev: can't run). The variants
 * (cdevtest.r, build.mpw) differ in that string, the 'mach' masks and a
 * 'CURS' -4064, so the same code measures what the Finder (the G3's 9.0.4)
 * and Executor's control panel shell (cdevshell.cpp) do.
 */
#include <Types.h>
#include <Memory.h>
#include <Resources.h>
#include <Files.h>
#include <Dialogs.h>
#include <Events.h>
#include <TextUtils.h>
#include <Quickdraw.h>
#include <Devices.h>

#define cursorDev 14

typedef struct
{
    long nul_count;
    long cursor_count;
    short last_idle;    /* nulDev or cursorDev, the last written */
} Storage;

static void append(unsigned char *line, const unsigned char *s);
static void append_c(unsigned char *line, const char *s);
static void append_num(unsigned char *line, long n);
static void log_line(unsigned char *line);
static char planned(short message);
static void log_where(unsigned char *line, DialogPtr dialog);

/* The entry point: first in the code resource. */
pascal long main(short message, short item, short numItems, short CPanelID,
                 EventRecord *theEvent, long cdevValue, DialogPtr CPDialog)
{
    Str255 line;
    Storage **st = NULL;
    char act;

    if(cdevValue != cdevUnset && cdevValue != 0 && cdevValue != 1 && cdevValue != -1
       && message != macDev && message != initDev)
        st = (Storage **)cdevValue;

    if(st && (message == nulDev || message == cursorDev))
    {
        if(message == nulDev)
            (*st)->nul_count++;
        else
            (*st)->cursor_count++;
        if((*st)->last_idle == message)
            goto act;
        (*st)->last_idle = message;
    }

    line[0] = 0;
    {
        Handle nh = Get1Resource('STR ', -4065);
        if(nh && *nh)
            append(line, (unsigned char *)*nh);
        else
            append_c(line, "CdevTest");
    }
    append_c(line, " msg=");
    append_num(line, message);
    append_c(line, " item=");
    append_num(line, item);
    append_c(line, " value=");
    if(cdevValue == cdevUnset)
        append_c(line, "unset");
    else if(cdevValue == 0 || cdevValue == 1 || cdevValue == -1)
        append_num(line, cdevValue);
    else
        append_c(line, "h");
    /* the panel's file the current resource file (Get1Resource looks there
       only) and its dialog the port, as IM 8-26 says the Finder has them */
    {
        GrafPtr port;
        GetPort(&port);
        append_c(line, Get1Resource('nrct', -4064) ? " res=panel" : " res=other");
        append_c(line, !CPDialog ? "" : port == CPDialog ? " port=panel" : " port=other");
    }
    if(message == initDev)
    {
        /* what 'mach' is checked against (low memory: ROM85, HWCfgFlags) */
        append_c(line, " rom85=");
        append_num(line, *(unsigned short *)0x28E);
        append_c(line, " hwcfg=");
        append_num(line, *(unsigned short *)0xB22);
        log_where(line, CPDialog);
    }
    if(st && message == closeDev)
    {
        append_c(line, " nul=");
        append_num(line, (*st)->nul_count);
        append_c(line, " cursor=");
        append_num(line, (*st)->cursor_count);
    }
    log_line(line);

act:
    act = planned(message);
    if(message == hitDev)
    {
        /* as Text does (UseResFile(0) in a message, not undone): the System
           file current when it returns */
        UseResFile(0);
    }
    if(message == macDev)
        return act == 'Z' ? 0 : 1;
    if(message == initDev && !act)
        return (long)NewHandleClear(sizeof(Storage));
    if(act)
    {
        if(st)
            DisposeHandle((Handle)st);
        return act == 'M' ? cdevMemErr : act == 'R' ? cdevResErr : cdevGenErr;
    }
    if(message == closeDev)
    {
        if(st)
            DisposeHandle((Handle)st);
        return cdevUnset;
    }
    return cdevValue;
}

static void append(unsigned char *line, const unsigned char *s)
{
    short i;
    for(i = 1; i <= s[0] && line[0] < 250; i++)
        line[++line[0]] = s[i];
}

static void append_c(unsigned char *line, const char *s)
{
    while(*s && line[0] < 250)
        line[++line[0]] = *s++;
}

static void append_num(unsigned char *line, long n)
{
    Str255 num;
    NumToString(n, num);
    append(line, num);
}

static void log_line(unsigned char *line)
{
    short ref;
    long count;
    OSErr err;
    /* a code resource has no globals: the string is in the code (SC -b) */
    ConstStr255Param path = "\pHost:cdevtest.log";

    line[++line[0]] = '\r';
    err = FSOpen(path, 0, &ref);
    if(err == fnfErr)
    {
        Create(path, 0, 'ttxt', 'TEXT');
        err = FSOpen(path, 0, &ref);
    }
    if(err != noErr)
        return;
    SetFPos(ref, fsFromLEOF, 0);
    count = line[0];
    FSWrite(ref, &count, line + 1);
    FSClose(ref);
    FlushVol(NULL, 0);
}

/* What 'STR ' -4064 says to answer for this message, 0 for nothing. */
static char planned(short message)
{
    Handle h = Get1Resource('STR ', -4064);
    unsigned char *s;
    short i;
    char digit = message < 10 ? '0' + message : 'A' + message - 10;
    if(!h || !*h)
        return 0;
    s = (unsigned char *)*h;
    for(i = 1; i + 1 <= s[0]; i += 2)
        if(s[i] == digit)
            return s[i + 1];
    return 0;
}

static void append_point(unsigned char *line, Point pt)
{
    append_num(line, pt.h);
    append_c(line, ",");
    append_num(line, pt.v);
}

/* Where the panel is on the screen, for the test to point and click
   without /api/ui: " win=L,T R,B hit=X,Y" (the Hit button's centre). */
static void log_where(unsigned char *line, DialogPtr dialog)
{
    Rect r;
    Point a, b;
    short type;
    Handle h;
    GrafPtr save;
    if(!dialog)
        return;
    GetPort(&save);
    SetPort(dialog);
    r = dialog->portRect;
    a.h = r.left, a.v = r.top, b.h = r.right, b.v = r.bottom;
    LocalToGlobal(&a);
    LocalToGlobal(&b);
    append_c(line, " win=");
    append_point(line, a);
    append_c(line, " ");
    append_point(line, b);
    GetDialogItem(dialog, 1, &type, &h, &r);
    a.h = (r.left + r.right) / 2, a.v = (r.top + r.bottom) / 2;
    LocalToGlobal(&a);
    append_c(line, " hit=");
    append_point(line, a);
    SetPort(save);
}
