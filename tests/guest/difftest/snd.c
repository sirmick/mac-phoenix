/* snd: GetSoundHeaderOffset (Sound Manager 3.0, InterfaceLib 8.6),
   differential (tools/difftest/run.py snd): what it answers for 'snd '
   handles of each shape, built by hand: format 1 with 0, 1 and 2 modifiers,
   format 2, bufferCmd and soundCmd with and without the data offset flag,
   the sound command after others, an offset that isn't where the header
   is, no sound command, no commands, an unknown format, an empty handle.
   Ares (Ambrosia) plays its sounds from what it answers for format 1
   resources with one bufferCmd: Executor answered badFormat to every one. */
#include "probe.h"
#include <Sound.h>

typedef pascal OSErr (*GetOffset)(Handle, long *);

static GetOffset get_offset;
static unsigned char buf[256];
static int len;

static void w16(int v) { buf[len++] = v >> 8; buf[len++] = v; }
static void w32(long v) { w16((int)(v >> 16)); w16((int)v); }
static void cmd(int c, int p1, long p2) { w16(c); w16(p1); w32(p2); }
/* a standard sound header and four samples */
static void header(void)
{
    w32(0);                 /* samplePtr: the samples follow */
    w32(4);                 /* length */
    w32(0x56220000);        /* 22050 Hz */
    w32(0);                 /* loopStart */
    w32(0);                 /* loopEnd */
    buf[len++] = 0;         /* encode: stdSH */
    buf[len++] = 60;        /* baseFrequency */
    w32(0x80808080);
}

static void ask(const char *key)
{
    Handle h;
    long offset;
    OSErr err;
    char k[64];
    if(PtrToHand(buf, &h, len) != noErr)
    {
        sprintf(k, "%s.handle", key);
        out(k, "failed");
        return;
    }
    memset(&offset, SENTINEL, sizeof offset);
    err = get_offset(h, &offset);
    sprintf(k, "%s.err", key);
    out(k, "%d", err);
    sprintf(k, "%s.offset", key);
    out_hex(k, &offset, sizeof offset);
    DisposeHandle(h);
}

/* format 1, n modifiers (sampledSynth, init 0xA0), as Ares's: the header
   offset is the next len after the commands */
static void format1(int mods)
{
    int i;
    len = 0;
    w16(1);
    w16(mods);
    for(i = 0; i < mods; i++)
    {
        w16(sampledSynth);
        w32(0xA0);
    }
}

int main(void)
{
    probe_open("snd");
    get_offset = (GetOffset)probe_bind("InterfaceLib", "GetSoundHeaderOffset");
    if(get_offset && SECTION("format1"))
    {
        format1(1); w16(1); cmd(0x8000 | bufferCmd, 0, 20); header();
        ask("f1.mods1.buffer");                 /* Ares's 'snd ' 510 */
        format1(0); w16(1); cmd(0x8000 | bufferCmd, 0, 14); header();
        ask("f1.mods0.buffer");
        format1(2); w16(1); cmd(0x8000 | bufferCmd, 0, 26); header();
        ask("f1.mods2.buffer");
        format1(1); w16(1); cmd(0x8000 | soundCmd, 0, 20); header();
        ask("f1.sound");
        format1(1); w16(1); cmd(bufferCmd, 0, 20); header();
        ask("f1.buffer.noflag");
        format1(1); w16(1); cmd(soundCmd, 0, 20); header();
        ask("f1.sound.noflag");
        format1(1); w16(3); cmd(ampCmd, 128, 0); cmd(quietCmd, 0, 0); cmd(0x8000 | bufferCmd, 0, 36); header();
        ask("f1.third");
        format1(1); w16(2); cmd(0x8000 | soundCmd, 0, 28); cmd(0x8000 | bufferCmd, 0, 28); header();
        ask("f1.sound.then.buffer");
        format1(1); w16(1); cmd(0x8000 | bufferCmd, 0, 100); header();
        ask("f1.offset.elsewhere");             /* the header is at 20 */
        format1(1); w16(1); cmd(ampCmd, 128, 0); header();
        ask("f1.nosound");
        format1(1); w16(0); header();
        ask("f1.nocommands");
    }
    if(get_offset && SECTION("format2"))
    {
        len = 0; w16(2); w16(0); w16(1); cmd(0x8000 | bufferCmd, 0, 14); header();
        ask("f2.buffer");
        len = 0; w16(2); w16(0); w16(1); cmd(0x8000 | soundCmd, 0, 14); header();
        ask("f2.sound");
        len = 0; w16(2); w16(3); w16(1); cmd(0x8000 | bufferCmd, 0, 14); header();
        ask("f2.refcount3");
        len = 0; w16(2); w16(0); w16(1); cmd(soundCmd, 0, 14); header();
        ask("f2.sound.noflag");
    }
    if(get_offset && SECTION("odd"))
    {
        len = 0; w16(3); w16(0); w16(1); cmd(0x8000 | bufferCmd, 0, 14); header();
        ask("format3");
        len = 0; w16(0); w16(0); w16(1); cmd(0x8000 | bufferCmd, 0, 14); header();
        ask("format0");
    }
    if(get_offset && SECTION("short"))
    {
        len = 0; w16(1);
        ask("f1.short");
    }
    if(get_offset && SECTION("empty"))
    {
        Handle h = NewEmptyHandle();
        long offset;
        memset(&offset, SENTINEL, sizeof offset);
        out("empty.err", "%d", get_offset(h, &offset));
        out_hex("empty.offset", &offset, sizeof offset);
        DisposeHandle(h);
    }
    probe_close();
    return 0;
}
