/* qttest.c — QTTest: QuickTime from an application, one result line per
 * check ("PASS|FAIL|SKIP name detail"), written to Host:qt_results.txt
 * (the first line of Host:suite.cfg names another), "---" at the end.
 *
 * The details are what QuickTime answered: versions, component lists,
 * movie headers, CRC-32s of decoded frames, compressed sizes. The ROM
 * machines' answers are the references (expected_*.txt); a check FAILs
 * here only when a call returns an error or a result is impossible.
 * Lines starting "#" are notes (timings), not compared.
 *
 * The movies are tests/lib/qtmovie.py's, put on Host: by the test
 * (TEXT files there: QTTest makes them 'MooV' first). QTTest also makes
 * movies in Apple's codecs from raw32.mov's frames (CompressSequence,
 * AddMediaSample, FlattenMovieData) as Host:made-<codec>.mov, decodes them
 * and leaves them for the host to keep (~/storage/qt).
 *
 * Built by tools/mpw/mpw build tests/guest/qt (build.mpw): fat, 68k and
 * PowerPC code. */

#include <Components.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <Folders.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <ImageCompression.h>
#include <Memory.h>
#include <Menus.h>
#include <FixMath.h>
#include <Movies.h>
#include <QDOffscreen.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Script.h>
#include <Windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;

static FILE *results;
static int passed, failed, skipped;

static void vout(const char *status, const char *name, const char *fmt, va_list ap)
{
    fprintf(results, "%s %s", status, name);
    if(fmt && *fmt)
    {
        fputc(' ', results);
        vfprintf(results, fmt, ap);
    }
    fputc('\n', results);
    fflush(results);
    if(status[0] == 'P')
        passed++;
    else if(status[0] == 'F')
        failed++;
    else if(status[0] == 'S')
        skipped++;
}

static void PASS(const char *name, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vout("PASS", name, fmt, ap);
    va_end(ap);
}

static void FAIL(const char *name, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vout("FAIL", name, fmt, ap);
    va_end(ap);
}

static void note(const char *fmt, ...)
{
    va_list ap;

    fputs("# ", results);
    va_start(ap, fmt);
    vfprintf(results, fmt, ap);
    va_end(ap);
    fputc('\n', results);
    fflush(results);
}

/* A four-character code for a detail ('raw ') or a test name (raw_). */
static const char *fcc(OSType t, int for_name)
{
    static char buf[4][8];
    static int which;
    char *b = buf[which++ & 3];
    int i;

    for(i = 0; i < 4; i++)
    {
        unsigned char c = (unsigned char)(t >> (24 - 8 * i));
        if(c < 0x20 || c >= 0x7F)
            c = '.';
        if(for_name && c == ' ')
            c = '_';
        b[i] = c;
    }
    b[4] = 0;
    return b;
}

static const char *pstr(const unsigned char *p)
{
    static char buf[2][256];
    static int which;
    char *b = buf[which++ & 1];

    memcpy(b, p + 1, p[0]);
    b[p[0]] = 0;
    return b;
}

static void idle_ticks(long ticks)
{
    EventRecord ev;
    unsigned long end = TickCount() + ticks;

    while(TickCount() < end)
        WaitNextEvent(everyEvent, &ev, 1, NULL);
}

/* --- CRC-32 of a 32-bit GWorld's R, G, B bytes (tests/lib/qtmovie.py) --- */

static unsigned long crc_table[256];

static void crc_init(void)
{
    unsigned long c;
    int n, k;

    for(n = 0; n < 256; n++)
    {
        c = n;
        for(k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static unsigned long pixmap_crc(PixMapHandle pm, const Rect *r)
{
    unsigned long crc = 0xFFFFFFFFUL;
    long rb = (**pm).rowBytes & 0x3FFF;
    Ptr base;
    int x, y, i;

    LockPixels(pm);
    base = GetPixBaseAddr(pm);
    for(y = r->top - (**pm).bounds.top; y < r->bottom - (**pm).bounds.top; y++)
    {
        unsigned char *p = (unsigned char *)base + y * rb + (r->left - (**pm).bounds.left) * 4;
        for(x = r->left; x < r->right; x++, p += 4)
            for(i = 1; i < 4; i++)
                crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    UnlockPixels(pm);
    return crc ^ 0xFFFFFFFFUL;
}

/* Pixels of the rectangle unlike its top-left pixel. */
static long pixmap_varied(PixMapHandle pm, const Rect *r)
{
    long rb = (**pm).rowBytes & 0x3FFF, n = 0;
    unsigned long first, *p;
    Ptr base;
    int x, y;

    LockPixels(pm);
    base = GetPixBaseAddr(pm);
    first = *(unsigned long *)(base + (r->top - (**pm).bounds.top) * rb + (r->left - (**pm).bounds.left) * 4) & 0xFFFFFF;
    for(y = r->top - (**pm).bounds.top; y < r->bottom - (**pm).bounds.top; y++)
    {
        p = (unsigned long *)(base + y * rb + (r->left - (**pm).bounds.left) * 4);
        for(x = r->left; x < r->right; x++, p++)
            if((*p & 0xFFFFFF) != first)
                n++;
    }
    UnlockPixels(pm);
    return n;
}

static void erase_gworld(GWorldPtr gw)
{
    CGrafPtr port;
    GDHandle dev;

    GetGWorld(&port, &dev);
    SetGWorld(gw, NULL);
    BackColor(whiteColor);
    ForeColor(blackColor);
    EraseRect(&gw->portRect);
    SetGWorld(port, dev);
}

/* --- the movies -------------------------------------------------------- */

typedef struct
{
    const char *file;           /* on Host: */
    const char *name;           /* in test names */
    int frames;                 /* video frames; 0: none */
    long frame_dur;             /* in the movie's time scale */
} MovieInfo;

static const MovieInfo movies[] = {
    { "raw32.mov", "raw32", 10, 60 },
    { "raw16.mov", "raw16", 8, 6 },
    { "rle32.mov", "rle32", 10, 60 },
    { "rle16.mov", "rle16", 6, 250 },
    { "tone16.mov", "tone16", 0, 0 },
    { "tone8.mov", "tone8", 0, 0 },
    { "av.mov", "av", 20, 60 },
};

static void host_spec(const char *file, FSSpec *spec, OSErr *err)
{
    Str255 path;

    sprintf((char *)path + 1, "Host:%s", file);
    path[0] = strlen((char *)path + 1);
    *err = FSMakeFSSpec(0, 0, path, spec);
}

/* A file in the boot disk's Temporary Items. QuickTime makes and flattens
 * movies there: FlattenMovieData onto the ROM machines' Host volume (ExtFS)
 * writes a 705-byte stub and stops, with no error. */
static void temp_spec(const char *file, FSSpec *spec, OSErr *err)
{
    Str255 name;
    short vref;
    long dir;

    *err = FindFolder(kOnSystemDisk, kTemporaryFolderType, kCreateFolder, &vref, &dir);
    if(*err != noErr)
        return;
    strcpy((char *)name + 1, file);
    name[0] = strlen(file);
    *err = FSMakeFSSpec(vref, dir, name, spec);
}

/* The data fork of from, as Host:file ('MooV'), for the host to keep. */
static OSErr copy_to_host(const FSSpec *from, const char *file)
{
    static char buf[16384];
    FSSpec to;
    OSErr err;
    short in, out;
    long n;

    host_spec(file, &to, &err);
    if(err != noErr && err != fnfErr)
        return err;
    FSpDelete(&to);
    if((err = FSpCreate(&to, 'TVOD', 'MooV', smSystemScript)) != noErr)
        return err;
    if((err = FSpOpenDF(from, fsRdPerm, &in)) != noErr)
        return err;
    if((err = FSpOpenDF(&to, fsRdWrPerm, &out)) != noErr)
    {
        FSClose(in);
        return err;
    }
    do
    {
        n = sizeof buf;
        err = FSRead(in, &n, buf);
        if(n > 0 && FSWrite(out, &n, buf) != noErr)
            break;
    } while(err == noErr);
    FSClose(in);
    FSClose(out);
    return err == eofErr ? noErr : err;
}

static Movie open_movie(const char *file, const char *name)
{
    char test[64];
    FSSpec spec;
    FInfo fi;
    OSErr err;
    short ref, resId = 0;
    Movie m = NULL;

    sprintf(test, "movie.%s.open", name);
    host_spec(file, &spec, &err);
    if(err != noErr)
    {
        FAIL(test, "FSMakeFSSpec %d", err);
        return NULL;
    }
    if(FSpGetFInfo(&spec, &fi) == noErr && fi.fdType != 'MooV')
    {
        fi.fdType = 'MooV';
        fi.fdCreator = 'TVOD';
        FSpSetFInfo(&spec, &fi);
    }
    err = OpenMovieFile(&spec, &ref, fsRdPerm);
    if(err != noErr)
    {
        FAIL(test, "OpenMovieFile %d", err);
        return NULL;
    }
    err = NewMovieFromFile(&m, ref, &resId, NULL, newMovieActive, NULL);
    CloseMovieFile(ref);
    if(err != noErr || m == NULL)
    {
        FAIL(test, "NewMovieFromFile %d", err);
        return NULL;
    }
    PASS(test, "resId=%d", resId);
    return m;
}

static void movie_info(Movie m, const char *name)
{
    char test[64], detail[256];
    long i, n;

    sprintf(test, "movie.%s.info", name);
    n = GetMovieTrackCount(m);
    sprintf(detail, "scale=%ld duration=%ld tracks=%ld", GetMovieTimeScale(m), GetMovieDuration(m), n);
    for(i = 1; i <= n; i++)
    {
        Track t = GetMovieIndTrack(m, i);
        Media md = GetTrackMedia(t);
        OSType type = 0;
        Handle h = NewHandle(0);

        GetMediaHandlerDescription(md, &type, NULL, NULL);
        sprintf(detail + strlen(detail), " [%s scale=%ld duration=%ld samples=%ld", fcc(type, 0),
                GetMediaTimeScale(md), GetMediaDuration(md), GetMediaSampleCount(md));
        GetMediaSampleDescription(md, 1, (SampleDescriptionHandle)h);
        if(GetHandleSize(h) >= sizeof(SampleDescription))
        {
            SampleDescriptionPtr sd = (SampleDescriptionPtr)*h;
            sprintf(detail + strlen(detail), " '%s'", fcc(sd->dataFormat, 0));
            if(type == VideoMediaType && GetHandleSize(h) >= sizeof(ImageDescription))
            {
                ImageDescriptionPtr id = (ImageDescriptionPtr)*h;
                sprintf(detail + strlen(detail), " %dx%d depth=%d", id->width, id->height, id->depth);
            }
            else if(type == SoundMediaType && GetHandleSize(h) >= sizeof(SoundDescription))
            {
                SoundDescriptionPtr snd = (SoundDescriptionPtr)*h;
                sprintf(detail + strlen(detail), " ch=%d bits=%d rate=%lu", snd->numChannels, snd->sampleSize,
                        (unsigned long)snd->sampleRate >> 16);
            }
        }
        strcat(detail, "]");
        DisposeHandle(h);
    }
    PASS(test, "%s", detail);
}

/* Every frame at its middle, drawn into a 32-bit GWorld. */
static void movie_frames(Movie m, const char *name, int frames, long frame_dur)
{
    char test[64];
    Rect box;
    GWorldPtr gw;
    OSErr err;
    int k, pass;

    GetMovieBox(m, &box);
    OffsetRect(&box, -box.left, -box.top);
    SetMovieBox(m, &box);
    sprintf(test, "frame.%s.gworld", name);
    err = NewGWorld(&gw, 32, &box, NULL, NULL, 0);
    if(err != noErr)
    {
        FAIL(test, "NewGWorld %d", err);
        return;
    }
    SetMovieGWorld(m, gw, NULL);
    /* Decoders at their best, not what time allows; and every frame drawn
     * once before the pass that counts: Photo-JPEG's first two frames
     * otherwise come out one of two ways from run to run. */
    SetMoviePlayHints(m, hintsHighQuality, hintsHighQuality);
    for(pass = 0; pass < 2; pass++)
        for(k = 0; k < frames; k++)
        {
            sprintf(test, "frame.%s.%02d", name, k);
            erase_gworld(gw);
            SetMovieTimeValue(m, k * frame_dur + frame_dur / 2);
            UpdateMovie(m);
            MoviesTask(m, 0);
            err = GetMoviesError();
            if(pass == 0)
                continue;
            if(err != noErr)
                FAIL(test, "MoviesTask %d", err);
            else
                PASS(test, "crc=%08lX", pixmap_crc(GetGWorldPixMap(gw), &box));
        }
    SetMovieGWorld(m, (CGrafPtr)qd.thePort, NULL);
    DisposeGWorld(gw);
}

/* Played to its end at rate 1: wall time against the duration. */
static void movie_play(Movie m, const char *test)
{
    long duration = GetMovieDuration(m), scale = GetMovieTimeScale(m);
    long want = duration * 60 / scale, ticks, t0;
    Rect box;
    GWorldPtr gw = NULL;

    GetMovieBox(m, &box);
    if(!EmptyRect(&box) && NewGWorld(&gw, 32, &box, NULL, NULL, 0) == noErr)
        SetMovieGWorld(m, gw, NULL);
    GoToBeginningOfMovie(m);
    PrerollMovie(m, 0, fixed1);
    t0 = TickCount();
    StartMovie(m);
    while(!IsMovieDone(m) && TickCount() - t0 < want * 4 + 300)
        MoviesTask(m, 0);
    ticks = TickCount() - t0;
    StopMovie(m);
    note("%s: %ld ticks for %ld", test, ticks, want);
    if(!IsMovieDone(m))
        FAIL(test, "not done");
    else if(ticks < want * 3 / 4)
        FAIL(test, "done too soon");    /* the sound clock racing: no sound output */
    else if(ticks > want * 2 + 30)
        FAIL(test, "done too late");
    else
        PASS(test, "done in time");
    if(gw)
    {
        SetMovieGWorld(m, (CGrafPtr)qd.thePort, NULL);
        DisposeGWorld(gw);
    }
}

/* --- Gestalt, components ----------------------------------------------- */

static int test_gestalt(void)
{
    long v;
    OSErr err;

    err = Gestalt('qtim', &v);
    if(err != noErr)
    {
        FAIL("gestalt.qtim", "Gestalt %d", err);
        return 0;
    }
    PASS("gestalt.qtim", "%08lX", v);
    err = Gestalt('icmp', &v);
    if(err != noErr)
        FAIL("gestalt.icmp", "Gestalt %d", err);
    else
        PASS("gestalt.icmp", "%08lX", v);
    return 1;
}

static void test_components(void)
{
    static const OSType types[] = { 'imdc', 'imco', 'mhlr', 'dhlr', 'clok', 'play', 'sdev', 'eat ' };
    ComponentDescription want, cd;
    Component c;
    Handle name = NewHandle(0);
    char test[64];
    int i, k;

    for(i = 0; i < sizeof types / sizeof types[0]; i++)
    {
        memset(&want, 0, sizeof want);
        want.componentType = types[i];
        sprintf(test, "comp.%s", fcc(types[i], 1));
        PASS(test, "count=%ld", CountComponents(&want));
        for(c = NULL, k = 0; (c = FindNextComponent(c, &want)) != NULL; k++)
        {
            SetHandleSize(name, 0);
            if(GetComponentInfo(c, &cd, name, NULL, NULL) != noErr)
                continue;
            HLock(name);
            sprintf(test, "comp.%s.%02d", fcc(types[i], 1), k);
            PASS(test, "'%s' '%s' flags=%08lX %s", fcc(cd.componentSubType, 0), fcc(cd.componentManufacturer, 0),
                 cd.componentFlags, GetHandleSize(name) ? pstr((unsigned char *)*name) : "");
            HUnlock(name);
        }
    }
    DisposeHandle(name);
}

/* --- the movie controller ---------------------------------------------- */

static void test_controller(Movie m)
{
    Rect wr, box, br;
    WindowPtr w;
    MovieController mc;
    GWorldPtr gw;
    TimeValue t1, ta, tb;
    long end;

    GoToBeginningOfMovie(m);
    GetMovieBox(m, &box);
    OffsetRect(&box, -box.left, -box.top);
    SetMovieBox(m, &box);
    SetRect(&wr, 60, 80, 60 + box.right + 40, 80 + box.bottom + 60);
    w = NewCWindow(NULL, &wr, "\pQTTest", true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(w);
    SetMovieGWorld(m, (CGrafPtr)w, NULL);
    mc = NewMovieController(m, &box, mcTopLeftMovie);
    if(mc == NULL)
    {
        FAIL("mc.new", "NewMovieController %d", GetMoviesError());
        DisposeWindow(w);
        return;
    }
    PASS("mc.new", "");
    MCGetControllerBoundsRect(mc, &br);
    PASS("mc.bounds", "%d %d %d %d", br.top, br.left, br.bottom, br.right);
    MCDraw(mc, w);
    idle_ticks(10);
    MCIdle(mc);
    MCDraw(mc, w);
    if(NewGWorld(&gw, 32, &br, NULL, NULL, 0) == noErr)
    {
        Rect bar = br;
        long varied;

        bar.top = box.bottom;
        CopyBits(&w->portBits, (BitMap *)*GetGWorldPixMap(gw), &br, &br, srcCopy, NULL);
        varied = pixmap_varied(GetGWorldPixMap(gw), &bar);
        if(varied == 0)
            FAIL("mc.draw", "the bar is blank");
        else
            PASS("mc.draw", "bar drawn");
        note("mc.draw: bar %d..%d, %ld pixels unlike its corner, crc %08lX", bar.top, bar.bottom, varied,
             pixmap_crc(GetGWorldPixMap(gw), &bar));
        DisposeGWorld(gw);
    }
    MCDoAction(mc, mcActionPlay, (void *)fixed1);
    for(end = TickCount() + 40; TickCount() < end; )
        MCIdle(mc);
    t1 = GetMovieTime(m, NULL);
    if(t1 > 0)
        PASS("mc.play", "time advances");
    else
        FAIL("mc.play", "time %ld", t1);
    MCDoAction(mc, mcActionPlay, (void *)0);
    MCIdle(mc);
    ta = GetMovieTime(m, NULL);
    for(end = TickCount() + 20; TickCount() < end; )
        MCIdle(mc);
    tb = GetMovieTime(m, NULL);
    if(ta == tb && GetMovieRate(m) == 0)
        PASS("mc.stop", "time stops");
    else
        FAIL("mc.stop", "time %ld then %ld, rate %08lX", ta, tb, GetMovieRate(m));
    note("mc: played to %ld, stopped at %ld", t1, ta);
    DisposeMovieController(mc);
    SetMovieGWorld(m, (CGrafPtr)qd.thePort, NULL);
    DisposeWindow(w);
}

/* --- the Image Compression Manager --------------------------------------- */

static const OSType codecs[] = { 'raw ', 'rle ', 'rpza', 'cvid', 'jpeg', 'smc ' };

/* src: a 32-bit GWorld with a frame of raw32.mov. */
static void test_icm(GWorldPtr src)
{
    PixMapHandle pm = GetGWorldPixMap(src);
    Rect r = src->portRect;
    GWorldPtr dst;
    char test[64];
    int i;

    if(NewGWorld(&dst, 32, &r, NULL, NULL, 0) != noErr)
    {
        FAIL("icm.gworld", "NewGWorld");
        return;
    }
    LockPixels(pm);
    LockPixels(GetGWorldPixMap(dst));
    PASS("icm.source", "crc=%08lX", pixmap_crc(pm, &r));
    for(i = 0; i < sizeof codecs / sizeof codecs[0]; i++)
    {
        long max = 0;
        OSErr err;
        ImageDescriptionHandle desc;
        Ptr data;

        sprintf(test, "icm.%s", fcc(codecs[i], 1));
        err = GetMaxCompressionSize(pm, &r, 0, codecNormalQuality, codecs[i], anyCodec, &max);
        if(err != noErr)
        {
            FAIL(test, "GetMaxCompressionSize %d", err);
            continue;
        }
        desc = (ImageDescriptionHandle)NewHandle(sizeof(ImageDescription));
        data = NewPtr(max);
        if(data == NULL)
        {
            FAIL(test, "NewPtr(%ld)", max);
            DisposeHandle((Handle)desc);
            continue;
        }
        err = CompressImage(pm, &r, codecNormalQuality, codecs[i], desc, data);
        if(err != noErr)
            FAIL(test, "max=%ld CompressImage %d", max, err);
        else
        {
            CGrafPtr port;
            GDHandle dev;

            erase_gworld(dst);
            GetGWorld(&port, &dev);
            SetGWorld(dst, NULL);
            err = DecompressImage(data, desc, GetGWorldPixMap(dst), &r, &r, srcCopy, NULL);
            SetGWorld(port, dev);
            if(err != noErr)
                FAIL(test, "max=%ld size=%ld DecompressImage %d", max, (**desc).dataSize, err);
            else
                PASS(test, "max=%ld size=%ld depth=%d crc=%08lX", max, (**desc).dataSize, (**desc).depth,
                     pixmap_crc(GetGWorldPixMap(dst), &r));
        }
        DisposePtr(data);
        DisposeHandle((Handle)desc);
    }

    /* A PICT of the frame, JPEG-compressed, drawn. */
    {
        CGrafPtr port;
        GDHandle dev;
        PicHandle pic, jpic;
        OSErr err;

        GetGWorld(&port, &dev);
        SetGWorld(src, NULL);
        ClipRect(&r);
        pic = OpenPicture(&r);
        CopyBits((BitMap *)*pm, (BitMap *)*pm, &r, &r, srcCopy, NULL);
        ClosePicture();
        SetGWorld(port, dev);
        jpic = (PicHandle)NewHandle(0);
        err = CompressPicture(pic, jpic, codecNormalQuality, 'jpeg');
        if(err != noErr)
            FAIL("pict.jpeg.compress", "CompressPicture %d", err);
        else
        {
            PASS("pict.jpeg.compress", "pict=%ld jpeg=%ld", GetHandleSize((Handle)pic), GetHandleSize((Handle)jpic));
            erase_gworld(dst);
            SetGWorld(dst, NULL);
            DrawPicture(jpic, &r);
            err = QDError();
            SetGWorld(port, dev);
            if(err != noErr)
                FAIL("pict.jpeg.draw", "DrawPicture %d", err);
            else
                PASS("pict.jpeg.draw", "crc=%08lX", pixmap_crc(GetGWorldPixMap(dst), &r));
        }
        KillPicture(pic);
        DisposeHandle((Handle)jpic);
    }
    UnlockPixels(pm);
    DisposeGWorld(dst);
}

/* --- movies in Apple's codecs, made from raw32.mov ----------------------- */

/* Each at the depth its compressor works in (icm.* above says). */
/* Each at the depth its compressor works in (icm.* above says); Photo-JPEG
 * has no temporal compression (asked for it, the ICM finds no codec). */
static const struct { OSType codec; short depth; short temporal; } made_codecs[] = {
    { 'cvid', 24, 1 }, { 'rpza', 16, 1 }, { 'rle ', 32, 1 }, { 'jpeg', 24, 0 }, { 'smc ', 8, 1 }
};

/* A movie of raw32's frames in codec c, then flattened into
 * Host:made-<codec>.mov (data fork only); read back and decoded. */
static void make_movie(Movie raw, OSType c, short depth, short temporal)
{
    char test[64], name[16], file[64], tmp[64], *p;
    int made = 0;
    FSSpec spec, flat;
    OSErr err;
    short resRef;
    Movie nm = NULL, fm;
    Track track;
    Media media;
    Rect box;
    GWorldPtr gw;
    PixMapHandle pm;
    ImageSequence seq = 0;
    ImageDescriptionHandle desc;
    Handle data;
    long max = 0, size;
    unsigned char similarity;
    int k;

    strcpy(name, "made-");
    strcat(name, fcc(c, 1));
    if((p = strchr(name, '_')) != NULL)
        *p = 0;                         /* made-rle, not made-rle_ */
    sprintf(file, "%s.mov", name);
    sprintf(tmp, "tmp-%s", file);
    sprintf(test, "make.%s", fcc(c, 1));

    GetMovieBox(raw, &box);
    OffsetRect(&box, -box.left, -box.top);
    if(NewGWorld(&gw, 32, &box, NULL, NULL, 0) != noErr)
    {
        FAIL(test, "NewGWorld");
        return;
    }
    pm = GetGWorldPixMap(gw);
    LockPixels(pm);
    SetMovieGWorld(raw, gw, NULL);

    temp_spec(tmp, &spec, &err);
    if(err != noErr && err != fnfErr)
    {
        FAIL(test, "FSMakeFSSpec %d", err);
        goto done;
    }
    err = CreateMovieFile(&spec, 'TVOD', smSystemScript, createMovieFileDeleteCurFile, &resRef, &nm);
    if(err != noErr)
    {
        FAIL(test, "CreateMovieFile %d", err);
        goto done;
    }
    track = NewMovieTrack(nm, (long)box.right << 16, (long)box.bottom << 16, 0);
    media = NewTrackMedia(track, VideoMediaType, 600, NULL, 0);
    desc = (ImageDescriptionHandle)NewHandle(sizeof(ImageDescription));
    err = GetMaxCompressionSize(pm, &box, 0, codecNormalQuality, c, anyCodec, &max);
    data = NewHandle(max);
    if(err != noErr || data == NULL || media == NULL)
    {
        FAIL(test, "GetMaxCompressionSize %d (track %d)", err, GetMoviesError());
        goto close;
    }
    HLock(data);
    BeginMediaEdits(media);
    for(k = 0; k < 10; k++)
    {
        erase_gworld(gw);
        SetMovieTimeValue(raw, k * 60 + 30);
        UpdateMovie(raw);
        MoviesTask(raw, 0);
        if(k == 0)
            err = CompressSequenceBegin(&seq, pm, NULL, &box, NULL, depth, c, anyCodec, codecNormalQuality,
                                        temporal ? codecNormalQuality : 0, temporal ? 5 : 0, NULL,
                                        temporal ? codecFlagUpdatePrevious : 0, desc);
        if(err != noErr)
        {
            FAIL(test, "CompressSequenceBegin %d", err);
            break;
        }
        similarity = 0;                 /* a key frame unless the codec says otherwise */
        err = CompressSequenceFrame(seq, pm, &box, temporal ? codecFlagUpdatePrevious : 0, *data, &size, &similarity, NULL);
        if(err != noErr)
        {
            FAIL(test, "CompressSequenceFrame %d at %d", err, k);
            break;
        }
        err = AddMediaSample(media, data, 0, size, 60, (SampleDescriptionHandle)desc, 1,
                             temporal && similarity ? mediaSampleNotSync : 0, NULL);
        if(err != noErr)
        {
            FAIL(test, "AddMediaSample %d at %d", err, k);
            break;
        }
    }
    if(seq)
        CDSequenceEnd(seq);
    EndMediaEdits(media);
    if(k < 10)
        goto close;
    err = InsertMediaIntoTrack(track, 0, 0, GetMediaDuration(media), fixed1);
    if(err != noErr)
    {
        FAIL(test, "InsertMediaIntoTrack %d", err);
        goto close;
    }
    temp_spec(file, &flat, &err);
    fm = FlattenMovieData(nm, flattenAddMovieToDataFork, &flat, 'TVOD', smSystemScript, createMovieFileDeleteCurFile);
    err = GetMoviesError();
    if(err != noErr)
    {
        FAIL(test, "FlattenMovieData %d", err);
        goto close;
    }
    if(fm)
        DisposeMovie(fm);
    if((err = copy_to_host(&flat, file)) != noErr)
    {
        FAIL(test, "copying %s to Host: %d", file, err);
        goto close;
    }
    FSpDelete(&flat);
    PASS(test, "%s samples=%ld flattened=%s", file, GetMediaSampleCount(media), fm ? "movie" : "nil");
    made = 1;

close:
    DisposeHandle(data);
    DisposeHandle((Handle)desc);
    DisposeMovie(nm);
    CloseMovieFile(resRef);
    FSpDelete(&spec);
done:
    SetMovieGWorld(raw, (CGrafPtr)qd.thePort, NULL);
    UnlockPixels(pm);
    DisposeGWorld(gw);

    if(made)
    {
        Movie back;

        if((back = open_movie(file, name)) != NULL)
        {
            movie_info(back, name);
            movie_frames(back, name, 10, 60);
            DisposeMovie(back);
        }
    }
}

/* raw32.mov flattened again by QuickTime: the same movie. */
static void test_flatten(Movie raw)
{
    FSSpec flat;
    OSErr err;
    Movie fm;

    temp_spec("flat-raw32.mov", &flat, &err);
    fm = FlattenMovieData(raw, flattenAddMovieToDataFork, &flat, 'TVOD', smSystemScript, createMovieFileDeleteCurFile);
    err = GetMoviesError();
    if(err != noErr)
    {
        FAIL("flatten.raw32", "FlattenMovieData %d", err);
        return;
    }
    if(fm)
        DisposeMovie(fm);
    if((err = copy_to_host(&flat, "flat-raw32.mov")) != noErr)
    {
        FAIL("flatten.raw32", "copying to Host: %d", err);
        return;
    }
    FSpDelete(&flat);
    PASS("flatten.raw32", "returned %s", fm ? "a movie" : "nil");
    if((fm = open_movie("flat-raw32.mov", "flat-raw32")) != NULL)
    {
        movie_info(fm, "flat-raw32");
        movie_frames(fm, "flat-raw32", 10, 60);
        DisposeMovie(fm);
    }
}

/* ------------------------------------------------------------------------- */

int main(void)
{
    char results_name[64] = "qt_results.txt";
    char path[80];
    FILE *cfg;
    Movie m[sizeof movies / sizeof movies[0]];
    OSErr err;
    int i;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    MaxApplZone();
    idle_ticks(30);                     /* come to the front */
    crc_init();

    if((cfg = fopen("Host:suite.cfg", "r")) != NULL)
    {
        if(fgets(results_name, sizeof results_name, cfg))
            results_name[strcspn(results_name, "\r\n")] = 0;
        fclose(cfg);
    }
    sprintf(path, "Host:%s", results_name);
    if((results = fopen(path, "w")) == NULL)
        return 1;
    #if defined(powerc) || defined(__powerc)
    note("QTTest, PowerPC code");
#else
    note("QTTest, 68k code");
#endif

    if(!test_gestalt())
        goto end;
    err = EnterMovies();
    if(err != noErr)
    {
        FAIL("enter_movies", "%d", err);
        goto end;
    }
    PASS("enter_movies", "");
    test_components();

    for(i = 0; i < sizeof movies / sizeof movies[0]; i++)
    {
        m[i] = open_movie(movies[i].file, movies[i].name);
        if(m[i] == NULL)
            continue;
        movie_info(m[i], movies[i].name);
        if(movies[i].frames)
            movie_frames(m[i], movies[i].name, movies[i].frames, movies[i].frame_dur);
    }

    if(m[0])
    {
        Rect box;
        GWorldPtr gw;

        test_flatten(m[0]);
        GetMovieBox(m[0], &box);
        OffsetRect(&box, -box.left, -box.top);
        if(NewGWorld(&gw, 32, &box, NULL, NULL, 0) == noErr)
        {
            SetMovieGWorld(m[0], gw, NULL);
            SetMovieTimeValue(m[0], 3 * 60 + 30);
            UpdateMovie(m[0]);
            MoviesTask(m[0], 0);
            SetMovieGWorld(m[0], (CGrafPtr)qd.thePort, NULL);
            test_icm(gw);
            DisposeGWorld(gw);
        }
        for(i = 0; i < sizeof made_codecs / sizeof made_codecs[0]; i++)
            make_movie(m[0], made_codecs[i].codec, made_codecs[i].depth, made_codecs[i].temporal);
    }

    /* Sound last but one, in this order (the host looks for the tones in
     * --audio-dump): 1000 Hz 2 s, 440 Hz 1 s, then av's 1000 Hz 2 s. */
    note("sound: start");
    if(m[4])
        movie_play(m[4], "play.tone16");
    if(m[5])
        movie_play(m[5], "play.tone8");
    if(m[6])
        movie_play(m[6], "play.av");
    note("sound: end");
    if(m[6])
        test_controller(m[6]);

    for(i = 0; i < sizeof movies / sizeof movies[0]; i++)
        if(m[i])
            DisposeMovie(m[i]);
    ExitMovies();

end:
    fprintf(results, "---\n%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    fclose(results);
    return 0;
}
