/* MacPhoenix: 8.5's FontManager library (FontMgr.yaml), Executor's own.

   Apple's library is a Font Manager of its own: loaded beside Executor's,
   it replaced WidthListHand with its handle and Executor's FMSwapFont then
   trimmed it under the library (docs/executor/MacOS8-9.md). So its exports
   are answered here, from Executor's Font Manager, and Apple's library
   never runs. Behaviour from a G3 with 8.6 (the text probe's "fm"
   section). */

#include <base/common.h>
#include <FontMgr.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <quickdraw/cquick.h>

#include <algorithm>

using namespace Executor;

/* The Appearance control panel's "smooth fonts" switch. Executor draws no
   anti-aliased text; the setting is kept and answered. Off, from 12 point,
   on a fresh 8.6; a minimum size of 0 leaves the minimum as it was. */
static bool aa_enabled = false;
static int16_t aa_min_size = 12;

OSStatus Executor::C_SetAntiAliasedTextEnabled(Boolean iEnable, SInt16 iMinFontSize)
{
    aa_enabled = iEnable;
    if(iMinFontSize > 0)
        aa_min_size = iMinFontSize;
    return noErr;
}

Boolean Executor::C_IsAntiAliasedTextEnabled(GUEST<SInt16> *oMinFontSize)
{
    if(oMinFontSize)
        *oMinFontSize = aa_min_size;
    return aa_enabled;
}

/* The bounds of the ink the text makes drawn in the current port's font,
   size and face with the pen at (0, 0) (the pen itself is ignored): an
   empty rectangle for none. Found by drawing it, so it is what Executor's
   QuickDraw draws. */
void Executor::C_QDTextBounds(INTEGER byteCount, const void *textAddr, Rect *bounds)
{
    *bounds = Rect{ 0, 0, 0, 0 };
    GrafPtr port = qdGlobals().thePort;
    if(!port || byteCount <= 0 || !textAddr)
        return;

    INTEGER font = PORT_TX_FONT(port), size = PORT_TX_SIZE(port);
    Style face = PORT_TX_FACE(port);
    FontInfo fi;
    GetFontInfo(&fi);
    int width = TextWidth((Ptr)textAddr, 0, byteCount);
    /* room for overhangs: italic, outline, shadow, a glyph past its advance */
    int pad = std::max<int>(size ? size : 12, fi.widMax) + 8;
    Rect r;
    r.left = -pad;
    r.right = std::max(width, 0) + pad;
    r.top = -(fi.ascent + fi.leading + pad);
    r.bottom = fi.descent + pad;

    GUEST<CGrafPtr> save_port;
    GUEST<GDHandle> save_device;
    GetGWorld(&save_port, &save_device);
    GUEST<GWorldPtr> gw_x;
    if(NewGWorld(&gw_x, 1, &r, nullptr, nullptr, 0) != noErr || !gw_x)
        return;
    GWorldPtr gw = gw_x;
    SetGWorld(gw, nullptr);
    PixMapHandle pm = GetGWorldPixMap(gw);
    LockPixels(pm);
    EraseRect(&r);
    TextFont(font);
    TextSize(size);
    TextFace(face);
    TextMode(srcOr);
    MoveTo(0, 0);
    DrawText((Ptr)textAddr, 0, byteCount);

    const uint8_t *base = (const uint8_t *)GetPixBaseAddr(pm);
    int rowbytes = PIXMAP_ROWBYTES(pm);
    int w = r.right - r.left, h = r.bottom - r.top;
    int top = h, bottom = -1, left = w, right = -1;
    for(int y = 0; y < h; y++)
    {
        const uint8_t *row = base + y * rowbytes;
        for(int x = 0; x < w; x++)
            if(row[x >> 3] & (0x80 >> (x & 7)))
            {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
    }
    UnlockPixels(pm);
    SetGWorld(save_port, save_device);
    DisposeGWorld(gw);

    if(bottom >= 0)
    {
        bounds->top = r.top + top;
        bounds->left = r.left + left;
        bounds->bottom = r.top + bottom + 1;
        bounds->right = r.left + right + 1;
    }
}

/* GetFontInfo for a font, size and style, the current port unchanged. */
OSErr Executor::C_FetchFontInfo(SInt16 fontID, SInt16 fontSize, SInt16 fontStyle,
                                FontInfo *info)
{
    GrafPtr port = qdGlobals().thePort;
    if(!port || !info)
        return paramErr;
    INTEGER font = PORT_TX_FONT(port), size = PORT_TX_SIZE(port);
    Style face = PORT_TX_FACE(port);
    PORT_TX_FONT(port) = fontID;
    PORT_TX_SIZE(port) = fontSize;
    PORT_TX_FACE(port) = fontStyle;
    GetFontInfo(info);
    PORT_TX_FONT(port) = font;
    PORT_TX_SIZE(port) = size;
    PORT_TX_FACE(port) = face;
    return noErr;
}
