/* QTICM: the Image Compression Manager on a frame of its own, no movie:
   codec information, GetMaxCompressionSize, CompressImage and
   DecompressImage for Apple's codecs, every result CRC'd; CompressPicture's
   pictures (their bytes around the compressed data) drawn by DrawPicture. A ROM machine's
   run with QuickTime is the reference Executor's own ICM is compared with
   (run_qticm.sh). One "PASS name value" line per check, to Host:<suite.cfg's
   name> (default qticm_results.txt), then "---".

     tools/mpw/mpw build tests/guest/qticm -o tests/guest/qticm */
#include <Types.h>
#include <Memory.h>
#include <Gestalt.h>
#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <ImageCompression.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;
static FILE *out;
static unsigned long crc_table[256];

static void put(const char *name, const char *value)
{
    fprintf(out, "PASS %s %s\n", name, value);
    fflush(out);
}

static void fail(const char *name, const char *value)
{
    fprintf(out, "FAIL %s %s\n", name, value);
    fflush(out);
}

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

static unsigned long crc_bytes(unsigned long crc, const unsigned char *p, long n)
{
    while(n-- > 0)
        crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc;
}

/* the pixels of a 32-bit pixmap, alpha left out */
static unsigned long pixmap_crc(PixMapHandle pm)
{
    unsigned long crc = 0xFFFFFFFFUL;
    long rb = (**pm).rowBytes & 0x3FFF;
    Rect r = (**pm).bounds;
    Ptr base;
    int x, y;

    LockPixels(pm);
    base = GetPixBaseAddr(pm);
    for(y = 0; y < r.bottom - r.top; y++)
    {
        unsigned char *p = (unsigned char *)base + y * rb;
        for(x = 0; x < r.right - r.left; x++, p += 4)
            crc = crc_bytes(crc, p + 1, 3);
    }
    return crc ^ 0xFFFFFFFFUL;
}

static char *fcc(OSType t, char *s)
{
    memcpy(s, &t, 4);
    s[4] = 0;
    return s;
}

/* a 32-bit GWorld w x h: gradients and a few hard edges */
static GWorldPtr make_source(short w, short h)
{
    GWorldPtr gw;
    PixMapHandle pm;
    Rect r;
    long rb;
    int x, y;

    SetRect(&r, 0, 0, w, h);
    if(NewGWorld(&gw, 32, &r, NULL, NULL, 0) != noErr)
        return NULL;
    pm = GetGWorldPixMap(gw);
    LockPixels(pm);
    rb = (**pm).rowBytes & 0x3FFF;
    for(y = 0; y < h; y++)
    {
        unsigned long *row = (unsigned long *)(GetPixBaseAddr(pm) + y * rb);
        for(x = 0; x < w; x++)
        {
            unsigned long r8 = (x * 255) / (w - 1), g8 = (y * 255) / (h - 1), b8 = ((x / 8 + y / 8) & 1) ? 0xC0 : 0x20;
            if(x >= w / 2 && y < h / 3)
                r8 = g8 = b8 = 0xFF;
            row[x] = (r8 << 16) | (g8 << 8) | b8;
        }
    }
    return gw;
}

static void test_codec_info(OSType t)
{
    CodecInfo info;
    char s[8], name[64], line[256];
    OSErr err;

    memset(&info, 0, sizeof info);
    err = GetCodecInfo(&info, t, NULL);
    sprintf(name, "info.%s", fcc(t, s));
    if(err != noErr)
    {
        sprintf(line, "GetCodecInfo %d", err);
        fail(name, line);
        return;
    }
    memcpy(s, &info.typeName[1], 0);
    sprintf(line, "'%.*s' v%d r%d vendor=%08lX dflags=%08lX cflags=%08lX format=%08lX acc=%d/%d level=%d",
            (int)info.typeName[0], (char *)&info.typeName[1], info.version, info.revisionLevel, info.vendor,
            info.decompressFlags, info.compressFlags, info.formatFlags, info.compressionAccuracy,
            info.decompressionAccuracy, info.compressionLevel);
    put(name, line);
}

static void test_round_trip(GWorldPtr src, OSType t, short dst_depth, const char *tag)
{
    PixMapHandle pm = GetGWorldPixMap(src);
    Rect r = src->portRect, half;
    GWorldPtr dst, dst2;
    long max = 0;
    ImageDescriptionHandle desc;
    Ptr data;
    OSErr err;
    char s[8], name[64], line[256];
    ImageDescription *d;

    sprintf(name, "icm.%s.%s", fcc(t, s), tag);
    err = GetMaxCompressionSize(pm, &r, 0, codecNormalQuality, t, anyCodec, &max);
    if(err != noErr)
    {
        sprintf(line, "GetMaxCompressionSize %d", err);
        fail(name, line);
        return;
    }
    desc = (ImageDescriptionHandle)NewHandle(sizeof(ImageDescription));
    data = NewPtr(max);
    err = CompressImage(pm, &r, codecNormalQuality, t, desc, data);
    if(err != noErr)
    {
        sprintf(line, "max=%ld CompressImage %d", max, err);
        fail(name, line);
        DisposePtr(data);
        DisposeHandle((Handle)desc);
        return;
    }
    HLock((Handle)desc);
    d = *desc;
    sprintf(line, "max=%ld idSize=%ld size=%ld data=%08lX v%d r%d vendor=%08lX tq=%ld sq=%ld %dx%d res=%08lX/%08lX "
                  "frames=%d '%.*s' depth=%d clut=%d",
            max, d->idSize, d->dataSize, crc_bytes(0xFFFFFFFFUL, (unsigned char *)data, d->dataSize) ^ 0xFFFFFFFFUL,
            d->version, d->revisionLevel, d->vendor, d->temporalQuality, d->spatialQuality, d->width, d->height,
            d->hRes, d->vRes, d->frameCount, (int)d->name[0], (char *)&d->name[1], d->depth, d->clutID);
    HUnlock((Handle)desc);
    sprintf(name, "compress.%s.%s", fcc(t, s), tag);
    put(name, line);

    if(NewGWorld(&dst, dst_depth, &r, NULL, NULL, 0) == noErr)
    {
        CGrafPtr port;
        GDHandle dev;
        GWorldPtr show;

        GetGWorld(&port, &dev);
        SetGWorld(dst, NULL);
        EraseRect(&r);
        err = DecompressImage(data, desc, GetGWorldPixMap(dst), &r, &r, srcCopy, NULL);
        SetGWorld(port, dev);
        /* the result as 32-bit pixels */
        show = dst;
        if(dst_depth != 32 && NewGWorld(&show, 32, &r, NULL, NULL, 0) == noErr)
        {
            LockPixels(GetGWorldPixMap(show));
            LockPixels(GetGWorldPixMap(dst));
            SetGWorld(show, NULL);
            CopyBits((BitMap *)*GetGWorldPixMap(dst), (BitMap *)*GetGWorldPixMap(show), &r, &r, srcCopy, NULL);
            SetGWorld(port, dev);
        }
        sprintf(name, "decompress.%s.%s", fcc(t, s), tag);
        if(err != noErr)
        {
            sprintf(line, "DecompressImage %d", err);
            fail(name, line);
        }
        else
        {
            sprintf(line, "crc=%08lX", pixmap_crc(GetGWorldPixMap(show)));
            put(name, line);
        }
        if(show != dst)
            DisposeGWorld(show);

        /* into the bottom-right quarter of a cleared 32-bit world:
           half the size, through a matrix */
        if(NewGWorld(&dst2, 32, &r, NULL, NULL, 0) == noErr)
        {
            SetGWorld(dst2, NULL);
            EraseRect(&r);
            SetRect(&half, r.right / 2, r.bottom / 2, r.right, r.bottom);
            err = DecompressImage(data, desc, GetGWorldPixMap(dst2), &r, &half, srcCopy, NULL);
            SetGWorld(port, dev);
            sprintf(name, "decompress.%s.%s.half", fcc(t, s), tag);
            if(err != noErr)
            {
                sprintf(line, "DecompressImage %d", err);
                fail(name, line);
            }
            else
            {
                sprintf(line, "crc=%08lX", pixmap_crc(GetGWorldPixMap(dst2)));
                put(name, line);
            }
            DisposeGWorld(dst2);
        }
        DisposeGWorld(dst);
    }
    DisposePtr(data);
    DisposeHandle((Handle)desc);
}

static const OSType codecs[] = { 'raw ', 'rle ', 'rpza', 'cvid', 'jpeg', 'smc ' };

/* n bytes from p as hex, at most 160 of them */
static void hex(char *out, const unsigned char *p, long n)
{
    static const char digits[] = "0123456789ABCDEF";
    if(n > 160)
        n = 160;
    while(n-- > 0)
    {
        *out++ = digits[*p >> 4];
        *out++ = digits[*p++ & 15];
    }
    *out = 0;
}

/* A picture of src (one CopyBits), CompressPicture'd: its bytes around the
   compressed data (header and opcode fields before it, what follows it),
   then drawn whole and at half size into cleared 32-bit worlds. */
static void test_picture(GWorldPtr src, OSType t, const char *tag)
{
    PixMapHandle pm = GetGWorldPixMap(src);
    Rect r = src->portRect, half;
    CGrafPtr port;
    GDHandle dev;
    PicHandle pic, cpic;
    GWorldPtr dst;
    OSErr err;
    long size, i, data_at = -1, data_size = 0;
    unsigned char *b;
    char s[8], name[64], line[400];

    GetGWorld(&port, &dev);
    SetGWorld(src, NULL);
    ClipRect(&r);
    pic = OpenPicture(&r);
    CopyBits((BitMap *)*pm, (BitMap *)*pm, &r, &r, srcCopy, NULL);
    ClosePicture();
    SetGWorld(port, dev);
    HLock((Handle)pic);
    sprintf(line, "size=%ld crc=%08lX", GetHandleSize((Handle)pic),
            crc_bytes(0xFFFFFFFFUL, (unsigned char *)*pic, GetHandleSize((Handle)pic)) ^ 0xFFFFFFFFUL);
    HUnlock((Handle)pic);
    sprintf(name, "pict.%s.source", tag);
    put(name, line);
    cpic = (PicHandle)NewHandle(0);
    err = CompressPicture(pic, cpic, codecNormalQuality, t);
    sprintf(name, "pict.%s", tag);
    if(err != noErr)
    {
        sprintf(line, "CompressPicture %d", err);
        fail(name, line);
        KillPicture(pic);
        DisposeHandle((Handle)cpic);
        return;
    }
    size = GetHandleSize((Handle)cpic);
    HLock((Handle)cpic);
    b = (unsigned char *)*cpic;
    /* the image description: 'idSize' 86 then the codec type */
    for(i = 0; i + 8 <= size; i++)
        if(b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 0 && b[i + 3] == 86 && memcmp(b + i + 4, &t, 4) == 0)
        {
            data_size = ((long)b[i + 44] << 24) | ((long)b[i + 45] << 16) | ((long)b[i + 46] << 8) | b[i + 47];
            data_at = i + 86;
            break;
        }
    sprintf(line, "size=%ld crc=%08lX desc_at=%ld data=%ld", size,
            crc_bytes(0xFFFFFFFFUL, b, size) ^ 0xFFFFFFFFUL, data_at - 86, data_size);
    sprintf(name, "pict.%s.compress", tag);
    put(name, line);
    hex(line, b, data_at > 0 ? data_at : size);
    sprintf(name, "pict.%s.head", tag);
    put(name, line);
    if(data_at > 0 && data_at + data_size <= size)
    {
        hex(line, b + data_at + data_size, size - data_at - data_size);
        sprintf(name, "pict.%s.tail", tag);
        put(name, line);
    }
    HUnlock((Handle)cpic);

    if(NewGWorld(&dst, 32, &r, NULL, NULL, 0) == noErr)
    {
        SetGWorld(dst, NULL);
        EraseRect(&r);
        DrawPicture(cpic, &r);
        err = QDError();
        SetGWorld(port, dev);
        sprintf(name, "pict.%s.draw", tag);
        sprintf(line, "err=%d crc=%08lX", err, pixmap_crc(GetGWorldPixMap(dst)));
        put(name, line);
        SetGWorld(dst, NULL);
        EraseRect(&r);
        SetRect(&half, r.right / 4, r.bottom / 4, r.right / 4 + r.right / 2, r.bottom / 4 + r.bottom / 2);
        DrawPicture(cpic, &half);
        SetGWorld(port, dev);
        sprintf(name, "pict.%s.draw.half", tag);
        sprintf(line, "crc=%08lX", pixmap_crc(GetGWorldPixMap(dst)));
        put(name, line);
        DisposeGWorld(dst);
    }
    KillPicture(pic);
    DisposeHandle((Handle)cpic);
    (void)fcc(t, s);
}

int main(void)
{
    char results_name[64] = "qticm_results.txt";
    char path[80], line[64];
    FILE *cfg;
    long v;
    GWorldPtr src;
    int i;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    MaxApplZone();
    crc_init();

    if((cfg = fopen("Host:suite.cfg", "r")) != NULL)
    {
        if(fgets(results_name, sizeof results_name, cfg))
            results_name[strcspn(results_name, "\r\n")] = 0;
        fclose(cfg);
    }
    sprintf(path, "Host:%s", results_name);
    if((out = fopen(path, "w")) == NULL)
        return 1;
    if(Gestalt('icmp', &v) != noErr)
        fprintf(out, "# no Gestalt 'icmp'\n");
    v = 0;
    CodecManagerVersion(&v);
    sprintf(line, "%ld", v);
    put("CodecManagerVersion", line);

    for(i = 0; i < sizeof codecs / sizeof codecs[0]; i++)
        test_codec_info(codecs[i]);
    src = make_source(64, 48);
    if(src == NULL)
        fail("source", "NewGWorld");
    else
    {
        sprintf(line, "crc=%08lX", pixmap_crc(GetGWorldPixMap(src)));
        put("source", line);
        for(i = 0; i < sizeof codecs / sizeof codecs[0]; i++)
        {
            test_round_trip(src, codecs[i], 32, "32");
            test_round_trip(src, codecs[i], 16, "16");
        }
        test_picture(src, 'raw ', "raw");
        test_picture(src, 'jpeg', "jpeg");
        test_picture(src, 'rle ', "rle");
    }
    fprintf(out, "---\n");
    fclose(out);
    return 0;
}
