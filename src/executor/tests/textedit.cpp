#include "gtest/gtest.h"

#include "compat.h"
#ifdef EXECUTOR
#include <TextEdit.h>
#include <WindowMgr.h>
#include <QuickDraw.h>
#include <FontMgr.h>
#else
#include <TextEdit.h>
#include <Windows.h>
#endif

#include <cstring>

/* MacPhoenix: what Apple's Help Manager does to size a balloon: a TE record
   whose destination and view rectangles it narrows and widens, TECalText,
   then TEGetHeight of every line; the height must follow the width. */
struct TextEditWrap : public testing::Test
{
    WindowPtr window;
    TEHandle te = nullptr;

    TextEditWrap()
    {
        Rect r = { 50, 50, 250, 450 };
        window = NewWindow(nullptr, &r, (StringPtr) "", false, 0, (WindowPtr)-1, false, 0);
        SetPort(window);
        TextFont(0);
        TextSize(0);
    }

    ~TextEditWrap()
    {
        if(te)
            TEDispose(te);
        DisposeWindow(window);
    }

    int linesAt(int width, int height)
    {
        Rect r = { 0, 0, (short)height, (short)width };
        (*te)->destRect = r;
        (*te)->viewRect = r;
        TECalText(te);
        return (*te)->nLines;
    }
};

TEST_F(TextEditWrap, HeightFollowsWidth)
{
    Rect r = { 0, 0, 20, 20 };
    te = TENew(&r, &r);
    const char *text = "Creates a new folder, called \"untitled folder,\" which appears in the active window or on the desktop.";
    TESetText((Ptr)text, strlen(text), te);

    int lineHeight = (*te)->lineHeight;
    EXPECT_GT(lineHeight, 0);
    int previous = 1 << 30;
    for(int width : { 20, 40, 80, 160, 320, 640 })
    {
        int n = linesAt(width, 20);
        int32_t h = TEGetHeight(0x7FF, 0, te);
        printf("width %3d: %d lines, TEGetHeight %d\n", width, n, (int)h);
        EXPECT_LE(n, previous);
        EXPECT_EQ(h, n * lineHeight);
        /* the Help Manager's way of asking for every line */
        EXPECT_EQ(TEGetHeight(0, 0x7FF, te), h);
        previous = n;
    }
    EXPECT_LE(linesAt(640, 20), 2);
    EXPECT_GT(linesAt(40, 20), 5);
}

/* The Help Manager's sizing loop itself (PACK 14 +1a80..+1be0): the
   height candidate starts from the text's total width scaled to the
   golden ratio, grows a line at a time, and the width follows; it stops
   when the wrapped text fits the candidate. 7.5.5 does this in a handful
   of iterations. */
TEST_F(TextEditWrap, BalloonSizingLoop)
{
    GrafPort port;
    OpenPort(&port);
    TextFont(0);
    TextSize(0);
    Rect r = { 0, 0, 20, 20 };
    te = TENew(&r, &r);
    const char *text = "Creates a new folder, called \"untitled folder,\" which appears in the active window or on the desktop.";
    TESetText((Ptr)text, strlen(text), te);
    FontInfo fi;
    GetFontInfo(&fi);
    int lineHeight = fi.ascent + fi.descent + fi.leading;
    int total = TextWidth((Ptr)text, 0, strlen(text));
    int d7 = (int)((int64_t)total * 1000 / 1272);
    if(d7 % lineHeight)
        d7 += lineHeight - d7 % lineHeight;
    int iterations = 0, h = 0, width = 0;
    for(; iterations < 200; iterations++)
    {
        width = (int)((int64_t)d7 * 3236 / 2000);
        Rect dr = { 0, 0, (short)d7, (short)width };
        (*te)->destRect = dr;
        (*te)->viewRect = dr;
        TECalText(te);
        h = TEGetHeight(0x7FF, 0, te);
        if(h <= d7)
            break;
        d7 += lineHeight;
    }
    printf("line height %d, total width %d, %d iterations: candidate %d, width %d, %d lines (%d)\n",
           lineHeight, total, iterations, d7, width, (*te)->nLines, h);
    EXPECT_LT(iterations, 10);
    ClosePort(&port);
    SetPort(window);
}
