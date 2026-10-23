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

/* MacPhoenix: TEGetHiliteRgn (TEDispatch $F, which 7.5.5's Drag Manager
   patch adds): the selection's rectangles as TextEdit hilites them, in the
   port's coordinates; empty without a selection. */
TEST_F(TextEditWrap, HiliteRegion)
{
    Rect r = { 10, 10, 100, 300 };
    te = TENew(&r, &r);
    const char *text = "hello hello\rsecond line";
    TESetText((Ptr)text, strlen(text), te);
    int lineHeight = (*te)->lineHeight;
    ASSERT_GT(lineHeight, 0);
    ASSERT_EQ(2, (*te)->nLines);

    RgnHandle rgn = NewRgn();
    EXPECT_EQ(noErr, TEGetHiliteRgn(rgn, te));
    EXPECT_TRUE(EmptyRgn(rgn));

    /* The first word: one rectangle, the word's width on the first line. */
    TESetSelect(0, 5, te);
    EXPECT_EQ(noErr, TEGetHiliteRgn(rgn, te));
    Rect box = (*rgn)->rgnBBox;
    EXPECT_EQ(10, box.top);
    EXPECT_EQ(10 + lineHeight, box.bottom);
    EXPECT_EQ(10, box.left);
    EXPECT_EQ(10 + TextWidth((Ptr)text, 0, 5), box.right);
    Point in = { 15, 12 }, past = { 15, 200 }, below = { (int16_t)(10 + lineHeight + 2), 12 };
    EXPECT_TRUE(PtInRgn(in, rgn));
    EXPECT_FALSE(PtInRgn(past, rgn));
    EXPECT_FALSE(PtInRgn(below, rgn));

    /* Across the lines: the first line to its end, the second to the
       selection's end. */
    TESetSelect(6, 18, te);
    EXPECT_EQ(noErr, TEGetHiliteRgn(rgn, te));
    box = (*rgn)->rgnBBox;
    EXPECT_EQ(10, box.top);
    EXPECT_EQ(10 + 2 * lineHeight, box.bottom);
    Point line1_right = { 15, 290 }, line2_left = { (int16_t)(10 + lineHeight + 2), 12 }, line2_right = { (int16_t)(10 + lineHeight + 2), 290 };
    EXPECT_TRUE(PtInRgn(line1_right, rgn));
    EXPECT_TRUE(PtInRgn(line2_left, rgn));
    EXPECT_FALSE(PtInRgn(line2_right, rgn));
    Point line1_before = { 15, 12 }; /* before "hello " ends: not selected */
    EXPECT_FALSE(PtInRgn(line1_before, rgn));
    DisposeRgn(rgn);
}
