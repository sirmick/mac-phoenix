/* qtstress.c — QTStress: the Image Compression Manager's codecs opened and
 * run over and over (OpenDefaultComponent, GetMaxCompressionSize,
 * CompressImage, DecompressImage), every result against the first; for
 * finding a CPU core's faults in QuickTime's PowerPC codecs. Writes
 * Host:qtstress_results.txt (the first line of Host:suite.cfg names
 * another): a line per codec, PASS when every round agreed.
 * tools/mpw/mpw build tests/guest/qtstress -o tests/guest/qtstress */

#include <Components.h>
#include <Events.h>
#include <Fonts.h>
#include <ImageCompression.h>
#include <Memory.h>
#include <QDOffscreen.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <Movies.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;

#define ROUNDS 150

static unsigned long crc_table[256];

static unsigned long pixmap_crc(PixMapHandle pm, const Rect *r)
{
    unsigned long crc = 0xFFFFFFFFUL;
    long rb = (**pm).rowBytes & 0x3FFF;
    unsigned char *base = (unsigned char *)GetPixBaseAddr(pm);
    int x, y, i;

    for(y = 0; y < r->bottom - r->top; y++)
        for(x = 0; x < r->right - r->left; x++)
            for(i = 1; i < 4; i++)
                crc = crc_table[(crc ^ base[y * rb + x * 4 + i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}

static void paint_source(GWorldPtr gw)
{
    CGrafPtr port;
    GDHandle dev;
    RGBColor c;
    Rect r;
    int i;

    GetGWorld(&port, &dev);
    SetGWorld(gw, NULL);
    for(i = 0; i < 16; i++)
    {
        c.red = (i & 1) ? 0xFFFF : 0x2000;
        c.green = (i & 2) ? 0xC000 : 0x1000;
        c.blue = (i & 4) ? 0x8000 : 0x4000 + i * 0x0800;
        RGBForeColor(&c);
        SetRect(&r, i * 10, 0, i * 10 + 10, 120);
        PaintRect(&r);
        SetRect(&r, 0, i * 7, 160, i * 7 + 3);
        if(i & 8)
            PaintOval(&r);
    }
    ForeColor(blackColor);
    SetGWorld(port, dev);
}

int main(void)
{
    static const OSType codecs[] = { 'raw ', 'rle ', 'rpza', 'cvid', 'jpeg', 'smc ' };
    char results_name[64] = "qtstress_results.txt", path[80];
    FILE *cfg, *out;
    GWorldPtr src, dst;
    Rect r;
    unsigned long c;
    int n, k, i, round;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitCursor();
    MaxApplZone();
    for(n = 0; n < 256; n++)
    {
        c = n;
        for(k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    if((cfg = fopen("Host:suite.cfg", "r")) != NULL)
    {
        if(fgets(results_name, sizeof results_name, cfg))
            results_name[strcspn(results_name, "\r\n")] = 0;
        fclose(cfg);
    }
    sprintf(path, "Host:%s", results_name);
    if((out = fopen(path, "w")) == NULL)
        return 1;
    EnterMovies();
    SetRect(&r, 0, 0, 160, 120);
    NewGWorld(&src, 32, &r, NULL, NULL, 0);
    NewGWorld(&dst, 32, &r, NULL, NULL, 0);
    LockPixels(GetGWorldPixMap(src));
    LockPixels(GetGWorldPixMap(dst));
    paint_source(src);

    for(i = 0; i < sizeof codecs / sizeof codecs[0]; i++)
    {
        long first_max = 0, first_size = 0, opens = 0, maxes = 0, comps = 0, decs = 0, diffs = 0;
        unsigned long first_crc = 0;
        unsigned long t0 = TickCount();

        for(round = 0; round < ROUNDS; round++)
        {
            ComponentInstance ci = OpenDefaultComponent('imco', codecs[i]);
            long max = 0;
            OSErr err;
            ImageDescriptionHandle desc;
            Ptr data;
            CGrafPtr port;
            GDHandle dev;

            if(ci == NULL)
                opens++;
            else
                CloseComponent(ci);
            err = GetMaxCompressionSize(GetGWorldPixMap(src), &r, 0, codecNormalQuality, codecs[i], anyCodec, &max);
            if(err != noErr)
            {
                if(maxes++ < 3)
                    fprintf(out, "# %.4s round %d: GetMaxCompressionSize %d\n", (char *)&codecs[i], round, err);
                continue;
            }
            desc = (ImageDescriptionHandle)NewHandle(sizeof(ImageDescription));
            data = NewPtr(max);
            err = CompressImage(GetGWorldPixMap(src), &r, codecNormalQuality, codecs[i], desc, data);
            if(err != noErr)
            {
                if(comps++ < 3)
                    fprintf(out, "# %.4s round %d: CompressImage %d\n", (char *)&codecs[i], round, err);
            }
            else
            {
                GetGWorld(&port, &dev);
                SetGWorld(dst, NULL);
                EraseRect(&r);
                err = DecompressImage(data, desc, GetGWorldPixMap(dst), &r, &r, srcCopy, NULL);
                SetGWorld(port, dev);
                if(err != noErr)
                {
                    if(decs++ < 3)
                        fprintf(out, "# %.4s round %d: DecompressImage %d\n", (char *)&codecs[i], round, err);
                }
                else
                {
                    unsigned long crc = pixmap_crc(GetGWorldPixMap(dst), &r);
                    if(first_max == 0)
                    {
                        first_max = max;
                        first_size = (**desc).dataSize;
                        first_crc = crc;
                    }
                    else if(max != first_max || (**desc).dataSize != first_size || crc != first_crc)
                    {
                        if(diffs++ < 3)
                            fprintf(out, "# %.4s round %d: max %ld size %ld crc %08lX, first %ld %ld %08lX\n",
                                    (char *)&codecs[i], round, max, (**desc).dataSize, crc,
                                    first_max, first_size, first_crc);
                    }
                }
            }
            DisposePtr(data);
            DisposeHandle((Handle)desc);
            fflush(out);
        }
        fprintf(out, "%s %.4s rounds=%d open=%ld max=%ld compress=%ld decompress=%ld differ=%ld (%lu ticks)\n",
                opens + maxes + comps + decs + diffs ? "FAIL" : "PASS", (char *)&codecs[i], ROUNDS,
                opens, maxes, comps, decs, diffs, TickCount() - t0);
        fflush(out);
    }
    fprintf(out, "---\n");
    fclose(out);
    return 0;
}
