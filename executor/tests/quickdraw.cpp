#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <CQuickDraw.h>
#include <FontMgr.h>
#include <MemoryMgr.h>
#include <base/common.h>
#include <quickdraw/font.h>
#include <DeviceMgr.h>
#include <WindowMgr.h>
#include <quickdraw/cquick.h>
#include <OSUtil.h>
#else
#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <Windows.h>
#include <Devices.h>
#include <Palettes.h>
#include <OSUtils.h>
#endif

#include <bitset>
#include <vector>

struct OffscreenPort
{
    GrafPort port;
    Rect r;
    GUEST<GrafPtr> savedPort;

    OffscreenPort(int width = 2, int height = 2)
    {
        GetPort(&savedPort);
        OpenPort(&port);

        short rowBytes = (width + 31) / 31 * 4;
        port.portBits.rowBytes = rowBytes;
        port.portBits.baseAddr = NewPtrClear(rowBytes * height);
        SetRect(&r, 0,0,width,height);
        port.portBits.bounds = r;

        set();
    }

    ~OffscreenPort()
    {
        // ClosePort does not change the current port, so put the previous one
        // back first; otherwise we leave a closed port current.
        SetPort(savedPort);
        ClosePort(&port);
    }

    void set()
    {
        SetPort(&port);
    }

    uint8_t& data(int row, int offset)
    {
        return reinterpret_cast<uint8_t&>(
            port.portBits.baseAddr[row * port.portBits.rowBytes + offset]);
    }
};

struct OffscreenWorld
{
    GWorldPtr world = nullptr;
    Rect r;
    GUEST<CGrafPtr> savedPort;
    GUEST<GDHandle> savedGDevice;

    OffscreenWorld(int depth, int height = 2, int width = 2)
    {
        GetGWorld(&savedPort, &savedGDevice);
        SetRect(&r,0,0,2,2);
        NewGWorld(PTR(world), depth, &r, nullptr, nullptr, 0);
        LockPixels(world->portPixMap);
        set();
    }

    ~OffscreenWorld()
    {
        // SetGWorld changed the current port and device; restore them before
        // disposing the world so we don't leave the disposed world current.
        SetGWorld(savedPort, savedGDevice);
        DisposeGWorld(world);
    }

    void set()
    {
        SetGWorld(world, nullptr);
    }

    uint8_t& data(int row, int offset)
    {
        Ptr baseAddr = (*world->portPixMap)->baseAddr;
        uint8_t *data = (uint8_t*)baseAddr;
        short rowBytes = (*world->portPixMap)->rowBytes;
        rowBytes &= 0x3FFF;

        return data[row * rowBytes + offset];
    }

    uint16_t data16(int row, int offset)
    {
        return data(row, offset*2) * 256 + data(row, offset*2+1);
    }
    uint32_t data32(int row, int offset)
    {
        return data16(row, offset*2) * 65536 + data16(row, offset*2+1);
    }
};


TEST(QuickDraw, BWLine1)
{
    OffscreenPort port;

    EraseRect(&port.r);
    MoveTo(0,0);
    LineTo(2,2);

    EXPECT_EQ(0x80, port.data(0,0));
    EXPECT_EQ(0x40, port.data(1,0));
}

TEST(QuickDraw, GWorld8)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world(8);

    EraseRect(&world.r);
    MoveTo(0,0);
    LineTo(2,2);
    
    EXPECT_EQ(255, world.data(0,0));
    EXPECT_EQ(0, world.data(0,1));
    EXPECT_EQ(0, world.data(1,0));
    EXPECT_EQ(255, world.data(1,1));
}

TEST(QuickDraw, GWorld16)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world(16);

    EraseRect(&world.r);
    MoveTo(0,0);
    LineTo(2,2);
    
    EXPECT_EQ(0, world.data16(0,0));
    EXPECT_EQ(0x7FFF, world.data16(0,1));
}

TEST(QuickDraw, GWorld32)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world(32);

    EraseRect(&world.r);
    MoveTo(0,0);
    LineTo(2,2);
    
    EXPECT_EQ(0, world.data32(0,0));
    EXPECT_EQ(0x00FFFFFF, world.data32(0,1));
}

TEST(QuickDraw, BasicQDColorBW)
{
    OffscreenPort port;

    FillRect(&port.r, &qd.gray);
    EXPECT_EQ(0x80, port.data(0,0));
    EXPECT_EQ(0x40, port.data(1,0));
    
    ForeColor(whiteColor);
    BackColor(redColor);
    FillRect(&port.r, &qd.gray);

    EXPECT_EQ(0x40, port.data(0,0));
    EXPECT_EQ(0x80, port.data(1,0));

    EXPECT_EQ(whiteColor, port.port.fgColor);
    EXPECT_EQ(redColor, port.port.bkColor);
}

TEST(QuickDraw, BasicQDColor32)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world(32);


    FillRect(&world.r, &qd.gray);
    EXPECT_EQ(0x00000000, world.data32(0,0));
    EXPECT_EQ(0x00FFFFFF, world.data32(0,1));
    EXPECT_EQ(0x00FFFFFF, world.data32(1,0));
    EXPECT_EQ(0x00000000, world.data32(1,1));
    
    ForeColor(whiteColor);
    BackColor(redColor);

    const uint32_t rgbRed = 0xDD0806;

    EXPECT_EQ(0x00FFFFFF, world.world->fgColor);
    EXPECT_EQ(rgbRed, world.world->bkColor);

    FillRect(&world.r, &qd.gray);

    EXPECT_EQ(0x00FFFFFF, world.data32(0,0));
    EXPECT_EQ(rgbRed, world.data32(0,1));
    EXPECT_EQ(rgbRed, world.data32(1,0));
    EXPECT_EQ(0x00FFFFFF, world.data32(1,1));
} 

TEST(QuickDraw, GrayPattern32)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world(32);

    FillRect(&world.r, &qd.gray);
    EXPECT_EQ(0x00000000, world.data32(0,0));
    EXPECT_EQ(0x00FFFFFF, world.data32(0,1));
    EXPECT_EQ(0x00FFFFFF, world.data32(1,0));
    EXPECT_EQ(0x00000000, world.data32(1,1));
}

TEST(QuickDraw, GrayPattern8)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world(8);

    FillRect(&world.r, &qd.gray);
    EXPECT_EQ(0xFF, world.data(0,0));
    EXPECT_EQ(0x00, world.data(0,1));
    EXPECT_EQ(0x00, world.data(1,0));
    EXPECT_EQ(0xFF, world.data(1,1));
}

TEST(QuickDraw, GrayPattern1)
{
    OffscreenPort port;

    FillRect(&port.r, &qd.gray);
    EXPECT_EQ(0x80, port.data(0,0));
    EXPECT_EQ(0x40, port.data(1,0));
}

TEST(QuickDraw, BlackPattern32)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world(32);

    FillRect(&world.r, &qd.black);
    EXPECT_EQ(0x00000000, world.data32(0,0));
    EXPECT_EQ(0x00000000, world.data32(0,1));
    EXPECT_EQ(0x00000000, world.data32(1,0));
    EXPECT_EQ(0x00000000, world.data32(1,1));
}

TEST(QuickDraw, CopyBit8)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world1(8);

    world1.data(0,0) = 1;
    world1.data(0,1) = 2;
    world1.data(1,0) = 3;
    world1.data(1,1) = 4;
    OffscreenWorld world2(8);
    EraseRect(&world2.r);

    RgnHandle rgn1 = NewRgn();
    SetRectRgn(rgn1, 0,0,2,1);
    RgnHandle rgn2 = NewRgn();
    SetRectRgn(rgn2, 0,0,1,2);
    UnionRgn(rgn1, rgn2, rgn1);
    DisposeRgn(rgn2);

    CopyBits(
        &GrafPtr(world1.world)->portBits,
        &GrafPtr(world2.world)->portBits,
        &world1.r, &world2.r, srcCopy, rgn1);

    DisposeRgn(rgn1);

    EXPECT_EQ(1, world2.data(0,0));
    EXPECT_EQ(2, world2.data(0,1));
    EXPECT_EQ(3, world2.data(1,0));
    EXPECT_EQ(0, world2.data(1,1));
}

TEST(QuickDraw, CopyBit32)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world1(32);

    world1.data(0,3) = 1;
    world1.data(0,7) = 2;
    world1.data(1,3) = 3;
    world1.data(1,7) = 4;
    OffscreenWorld world2(32);
    EraseRect(&world2.r);

    RgnHandle rgn1 = NewRgn();
    SetRectRgn(rgn1, 0,0,2,1);
    RgnHandle rgn2 = NewRgn();
    SetRectRgn(rgn2, 0,0,1,2);
    UnionRgn(rgn1, rgn2, rgn1);
    DisposeRgn(rgn2);

    CopyBits(
        &GrafPtr(world1.world)->portBits,
        &GrafPtr(world2.world)->portBits,
        &world1.r, &world2.r, srcCopy, rgn1);

    DisposeRgn(rgn1);

    EXPECT_EQ(1, world2.data(0,3));
    EXPECT_EQ(2, world2.data(0,7));
    EXPECT_EQ(3, world2.data(1,3));
    EXPECT_EQ(255, world2.data(1,7));
}

TEST(QuickDraw, CopyMask8)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenPort mask;

    mask.data(0,0) = 0xC0;
    mask.data(1,0) = 0x80;
    
    OffscreenWorld world1(8);

    world1.data(0,0) = 1;
    world1.data(0,1) = 2;
    world1.data(1,0) = 3;
    world1.data(1,1) = 4;
    OffscreenWorld world2(8);
    EraseRect(&world2.r);

    CopyMask(
        &GrafPtr(world1.world)->portBits,
        &mask.port.portBits,
        &GrafPtr(world2.world)->portBits,
        &world1.r, &mask.r, &world2.r);


    EXPECT_EQ(1, world2.data(0,0));
    EXPECT_EQ(2, world2.data(0,1));
    EXPECT_EQ(3, world2.data(1,0));
    EXPECT_EQ(0, world2.data(1,1));
}

TEST(QuickDraw, CopyMask32)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenPort mask;

    mask.data(0,0) = 0xC0;
    mask.data(1,0) = 0x80;
    
    OffscreenWorld world1(32);

    world1.data(0,3) = 1;
    world1.data(0,7) = 2;
    world1.data(1,3) = 3;
    world1.data(1,7) = 4;
    OffscreenWorld world2(32);
    EraseRect(&world2.r);

    CopyMask(
        &GrafPtr(world1.world)->portBits,
        &mask.port.portBits,
        &GrafPtr(world2.world)->portBits,
        &world1.r, &mask.r, &world2.r);


    EXPECT_EQ(1, world2.data(0,3));
    EXPECT_EQ(2, world2.data(0,7));
    EXPECT_EQ(3, world2.data(1,3));
    EXPECT_EQ(255, world2.data(1,7));
}


TEST(QuickDraw, Hilite8)
{
    if(!hasDeepGWorlds())
        return;
    /* a highlight colour that is neither black nor white (a Mac's
       default is black); a port takes it when it is opened */
    RGBColor save_hilite, hilite = { 0x9999, 0xCCCC, 0xCCCC };
    LMGetHiliteRGB(&save_hilite);
    LMSetHiliteRGB(&hilite);
    OffscreenWorld world(8);

    Rect upper = world.r;
    upper.bottom /= 2;

    FillRect(&world.r, &qd.black);
    FillRect(&upper, &qd.white);

    EXPECT_EQ(0x00, world.data(0,0));
    EXPECT_EQ(0x00, world.data(0,1));
    EXPECT_EQ(0xFF, world.data(1,0));
    EXPECT_EQ(0xFF, world.data(1,1));

    LMSetHiliteMode(LMGetHiliteMode() & ~(1<<7));
    InvertRect(&world.r);

    EXPECT_NE(0x00, world.data(0,0));
    EXPECT_NE(0x00, world.data(0,1));
    EXPECT_EQ(0xFF, world.data(1,0));
    EXPECT_EQ(0xFF, world.data(1,1));

    LMSetHiliteMode(LMGetHiliteMode() & ~(1<<7));
    InvertRect(&world.r);

    EXPECT_EQ(0x00, world.data(0,0));
    EXPECT_EQ(0x00, world.data(0,1));
    EXPECT_EQ(0xFF, world.data(1,0));
    EXPECT_EQ(0xFF, world.data(1,1));

    LMSetHiliteRGB(&save_hilite);
}


TEST(QuickDraw, Hilite32)
{
    if(!hasDeepGWorlds())
        return;
    /* a highlight colour that is neither black nor white (a Mac's
       default is black); a port takes it when it is opened */
    RGBColor save_hilite, hilite = { 0x9999, 0xCCCC, 0xCCCC };
    LMGetHiliteRGB(&save_hilite);
    LMSetHiliteRGB(&hilite);
    OffscreenWorld world(32);

    Rect upper = world.r;
    upper.bottom /= 2;

    FillRect(&world.r, &qd.black);
    FillRect(&upper, &qd.white);

    EXPECT_EQ(0x00FFFFFF, world.data32(0,0));
    EXPECT_EQ(0x00FFFFFF, world.data32(0,1));
    EXPECT_EQ(0x00000000, world.data32(1,0));
    EXPECT_EQ(0x00000000, world.data32(1,1));

    LMSetHiliteMode(LMGetHiliteMode() & ~(1<<7));
    InvertRect(&world.r);

    EXPECT_NE(0x00FFFFFF, world.data32(0,0));
    EXPECT_NE(0x00FFFFFF, world.data32(0,1));
    EXPECT_EQ(0x00000000, world.data32(1,0));
    EXPECT_EQ(0x00000000, world.data32(1,1));

    LMSetHiliteMode(LMGetHiliteMode() & ~(1<<7));
    InvertRect(&world.r);

    EXPECT_EQ(0x00FFFFFF, world.data32(0,0));
    EXPECT_EQ(0x00FFFFFF, world.data32(0,1));
    EXPECT_EQ(0x00000000, world.data32(1,0));
    EXPECT_EQ(0x00000000, world.data32(1,1));

    LMSetHiliteRGB(&save_hilite);
}


TEST(QuickDraw, UnionRect)
{
    Rect r1,r2,r;
    
    r1 = {100,200,1000,2000};
    r2 = {1,2,10,20};
    UnionRect(&r1, &r2, &r);

    EXPECT_EQ(1, r.top);
    EXPECT_EQ(2, r.left);
    EXPECT_EQ(1000, r.bottom);
    EXPECT_EQ(2000, r.right);

    r1 = {1000,2000,100,200};
    r2 = {1,2,10,20};
    UnionRect(&r1, &r2, &r);

    EXPECT_EQ(1, r.top);
    EXPECT_EQ(2, r.left);
    EXPECT_EQ(100, r.bottom);
    EXPECT_EQ(200, r.right);

    r1 = {1,2,10,20};
    r2 = {0,0,0,0};
    UnionRect(&r1, &r2, &r);

    EXPECT_EQ(0, r.top);
    EXPECT_EQ(0, r.left);
    EXPECT_EQ(10, r.bottom);
    EXPECT_EQ(20, r.right);
}

TEST(QuickDraw, PinRect)
{
    auto pin = [](Rect& r, Point p) -> Point {
        auto l = PinRect(&r, p);
        return { (int16_t)(l >> 16), (int16_t)l };
    };
    Rect r = { 10,20,30,40 };
    Point p;

    p = pin(r, {15,25});
    EXPECT_EQ(15, p.v);
    EXPECT_EQ(25, p.h);

    p = pin(r, {30,40});
    EXPECT_EQ(29, p.v);
    EXPECT_EQ(39, p.h);

    p = pin(r, {35,45});
    EXPECT_EQ(29, p.v);
    EXPECT_EQ(39, p.h);
}

TEST(QuickDraw, InvalidCloseRgn)
{
    OffscreenPort port;

    RgnHandle rgn = NewRgn();
    CloseRgn(rgn);      // CloseRgn without OpenRgn is invalid, but doesn't crash on real Macs.
    DisposeRgn(rgn);
}

TEST(QuickDraw, PaintCircle)
{
    using whlist = std::initializer_list<std::pair<int,int>>;
    for(auto [w,h] : whlist{
        {16,16},{15,15},{15,16},{16,15},{4,4},{3,3},{3,10},{10,3},

        // {9,4},{8,5},{90,47} // real macOS doesn't quite agree with my theory for these
        
        })
    {
        std::ostringstream str;
        str << "oval " << w << "x" << h;
        SCOPED_TRACE(str.str());

        OffscreenPort port(w, h);

        PaintOval(&port.r);

        int count = 0;
        for(int row = 0; row < port.r.bottom; row++)
            for(int offset = 0; offset < (port.r.right+7)/8; offset++)
                count += std::bitset<8>(port.data(row,offset)).count();
        
        int perfect = 0;
        for(int y = 0; y < port.r.bottom; y++)
        {
            float yy = (y + 0.5f) * 2.0f / port.r.bottom - 1.0f;

                
            int whitepixels = 0;
            int mask = 0x80;
            int offset = 0;
            while(offset < (port.r.right+7)/8 && (port.data(y,offset) & mask) == 0)
            {
                whitepixels++;
                mask >>= 1;
                if(!mask)
                {
                    mask = 0x80;
                    offset++;
                }
            }

            int expectedWhitePixels = 0;

                // takes a while on an ancient mac
            /*for(int x = 0; x < port.r.right; x++)
            {
                float xx = (x + 0.5f) * 2.0f / port.r.right - 1.0f;
                
                if(xx*xx + yy*yy < 1.0f)
                    expectedWhitePixels ++;
                else
                    break;
            }*/

                // binary search makes 8MHz feel fast.
            if(float xx = 0.5f * 2.0f / port.r.right - 1.0f;
                xx*xx + yy*yy >= 1.0f)
            {
                int a = 0;
                int b = port.r.right/2;

                while(b - a > 1)
                {
                    int m = (a + b) / 2;

                    if(float xx = (m + 0.5f) * 2.0f / port.r.right - 1.0f;
                        xx*xx + yy*yy < 1.0f)
                        b = m;
                    else
                        a = m;
                }
                expectedWhitePixels = b;
            }

            perfect += port.r.right - 2 * expectedWhitePixels;
            EXPECT_EQ(expectedWhitePixels, whitepixels);
        }
        EXPECT_EQ(perfect, count);
    }
}


TEST(QuickDraw, FrameCircle1)
{
    OffscreenPort port(32, 32);

    FrameOval(&port.r);

    int count = 0;
    for(int row = 0; row < port.r.bottom; row++)
        for(int offset = 0; offset < port.r.right/8; offset++)
            count += std::bitset<8>(port.data(row,offset)).count();
    
    EXPECT_EQ(96, count);
}

TEST(QuickDraw, FrameCircle2)
{
    OffscreenPort port(32, 32);

    PenSize(3, 3);
    FrameOval(&port.r);

    Rect r2 = port.r;
    InsetRect(&r2, 3, 3);
    InvertOval(&r2);

    InvertOval(&port.r);

    int count = 0;
    for(int row = 0; row < port.r.bottom; row++)
        for(int offset = 0; offset < port.r.right/8; offset++)
            count += std::bitset<8>(port.data(row,offset)).count();
    
    EXPECT_EQ(0, count);
}

TEST(QuickDraw, FrameRoundRect)
{
    OffscreenPort port(32, 32);

    PenSize(3, 3);
    FrameRoundRect(&port.r, 16, 16);

    Rect r2 = port.r;
    InsetRect(&r2, 3, 3);
    InvertRoundRect(&r2, 10, 10);

    InvertRoundRect(&port.r, 16, 16);

    int count = 0;
    for(int row = 0; row < port.r.bottom; row++)
        for(int offset = 0; offset < port.r.right/8; offset++)
            count += std::bitset<8>(port.data(row,offset)).count();
    
    EXPECT_EQ(0, count);
}

TEST(QuickDraw, FrameArc1)
{
    OffscreenPort port(32, 32);

    FrameArc(&port.r, 45, 90);
    
    int count = 0;
    for(int row = 0; row < port.r.bottom; row++)
    {
        int rowcount = 0;
        for(int offset = 0; offset < port.r.right/8; offset++)
            rowcount += std::bitset<8>(port.data(row,offset)).count();

        count += rowcount;
        
        EXPECT_LE(rowcount, 2);
    }
    
    EXPECT_LT(port.r.bottom / 2, count);
}


TEST(QuickDraw, PaintRoundRect_TooBig)
{
    OffscreenPort port(32, 24);

    PaintRoundRect(&port.r, 64, 64);

    InvertOval(&port.r);

    int count = 0;
    for(int row = 0; row < port.r.bottom; row++)
        for(int offset = 0; offset < port.r.right/8; offset++)
            count += std::bitset<8>(port.data(row,offset)).count();
    
    EXPECT_EQ(0, count);
}



TEST(QuickDraw, PaintRoundRect_TooSmall)
{
    OffscreenPort port(32, 1);

    PaintRoundRect(&port.r, 5, 5);

    InvertOval(&port.r);

    int count = 0;
    for(int row = 0; row < port.r.bottom; row++)
        for(int offset = 0; offset < port.r.right/8; offset++)
            count += std::bitset<8>(port.data(row,offset)).count();
    
    EXPECT_EQ(0, count);
}

#ifdef EXECUTOR
/* useTempMem: the pixels go where TempNewHandle puts things (8.6: outside
   the application heap; the 8.6 Finder's window offscreens filled its
   1 MB partition when they went in it). */
TEST(QuickDraw, GWorldUseTempMemPixels)
{
    if(!hasDeepGWorlds())
        return;
    GUEST<OSErr> err;
    Handle temp = TempNewHandle(16, &err);
    ASSERT_NE(nullptr, temp);
    THz temp_zone = HandleZone(temp);
    DisposeHandle(temp);

    GWorldPtr gw = nullptr;
    Rect r = { 0, 0, 218, 404 };
    ASSERT_EQ(noErr, NewGWorld(PTR(gw), 32, &r, nullptr, nullptr, useTempMem));
    ASSERT_NE(nullptr, gw);
    PixMapHandle pm = GetGWorldPixMap(gw);
    Handle pixels = (Handle)(*pm)->baseAddr;
    EXPECT_EQ(temp_zone, HandleZone(pixels));
    /* locked, unlocked, locked again (the 8.6 Finder): the pixels found
       again though they aren't in the current zone */
    for(int i = 0; i < 2; i++)
    {
        EXPECT_TRUE(LockPixels(pm));
        EXPECT_EQ(*pixels, GetPixBaseAddr(pm));
        UnlockPixels(pm);
        EXPECT_EQ((Ptr)pixels, (Ptr)(*pm)->baseAddr);
    }
    LockPixels(pm);
    DisposeGWorld(gw);
}

/* A GWorld no heap has room for: memFullErr and no GWorld, as on 8.6
   (not one whose pixels are an empty handle, drawn into all the same) */
TEST(QuickDraw, GWorldTooBig)
{
    if(!hasDeepGWorlds())
        return;
    Rect r = { 0, 0, 8000, 8000 };
    for(GWorldFlags flags : { (GWorldFlags)0, (GWorldFlags)useTempMem })
    {
        GWorldPtr gw = (GWorldPtr)-1;
        EXPECT_EQ(memFullErr, NewGWorld(PTR(gw), 32, &r, nullptr, nullptr, flags));
        EXPECT_EQ(nullptr, gw);
    }
}

/* No family's strike to be found (the port's, the application font and
   the system font all missing; also what no room to load one comes to):
   text measures 0 and has no metrics, rather than FMSwapFont dividing by
   a size of 0 and reading a nil font (the 8.6 Finder in the tab). */
TEST(QuickDraw, TextWithNoFont)
{
    OffscreenPort port(32, 16);
    INTEGER sysfam = LM(SysFontFam), apfont = LM(ApFontID);
    LM(SysFontFam) = 32001;
    LM(ApFontID) = 32002;
    TextFont(32003);
    TextSize(12);
    EXPECT_EQ(0, StringWidth((ConstStringPtr) "\003abc"));
    FontInfo fi;
    GetFontInfo(&fi);
    EXPECT_EQ(0, fi.ascent);
    EXPECT_EQ(0, fi.descent);
    MoveTo(2, 12);
    DrawString((ConstStringPtr) "\003abc");
    EXPECT_EQ(2, port.port.pnLoc.h);
    LM(SysFontFam) = sysfam;
    LM(ApFontID) = apfont;
    TextFont(0);
    EXPECT_LT(0, StringWidth((ConstStringPtr) "\003abc"));
}

/* A width table in the list whose master pointer is outside memory (THINK
   Pascal 4.5's Run leaves one): skipped and taken out of the list, not read
   (in the tab reading it is a wasm out-of-bounds access, the renderer gone). */
TEST(QuickDraw, WidthListBadEntry)
{
    OffscreenPort port(32, 16);
    TextFont(0);
    TextSize(12);
    StringWidth((ConstStringPtr) "\003abc");     /* a table listed */
    ASSERT_TRUE(LM(WidthListHand));
    GUEST<WHandle> *list = *WIDTHLISTHAND;
    int slot = 0;
    while(slot < 12 && list[slot])
        slot++;
    ASSERT_LT(slot, 12);
    Handle bad = NewHandle(4);
    GUEST<Ptr> saved = *bad;
    GUEST<Ptr> outside;
    outside.raw_host_order(0x0FF81004);
    *bad = outside;
    list[slot] = guest_cast<WHandle>(bad);
    TextSize(13);                                 /* not the table it has */
    EXPECT_LT(0, StringWidth((ConstStringPtr) "\003abc"));
    for(int i = 0; i < 12; i++)
        EXPECT_NE((WHandle)bad, (WHandle)list[i]);
    *bad = saved;
    DisposeHandle(bad);
    TextSize(12);
}
#endif

/* Random as the Quadra's ROM answers it (a probe on the ROM machine): a
   seed with its top bit set counts as its low 31 bits plus one, 0 and
   2^31-1 stay 0. Photoshop 4.0 decodes its personalization with it. */
TEST(QuickDraw, RandomAsTheROM)
{
    static const struct { uint32_t seed; short r1; uint32_t s1; short r4; uint32_t s4; } cases[] = {
        { 0x00000000, 0, 0x00000000, 0, 0x00000000 },
        { 0x00000001, 16807, 0x000041a7, 3114, 0x3ab50c2a },
        { 0x00bc614e, 18498, 0x4f984842, 1366, 0x6bf40556 },
        { 0x7ffffffe, -16808, 0x7fffbe58, -3115, 0x454af3d5 },
        { 0x7fffffff, 0, 0x00000000, 0, 0x00000000 },
        { 0x80000001, -31922, 0x0000834e, 6228, 0x756a1854 },
        { 0xfffffffb, 15114, 0x7fff3b0a, -9344, 0x4fe0db80 },
        { 0x3fffffff, -8404, 0x3fffdf2c, 31210, 0x62a579ea },
        { 0xdeadbeef, -20705, 0x60d2af1f, 14484, 0x38913894 },
    };
    int32_t saved = qd.randSeed;
    for(auto &c : cases)
    {
        qd.randSeed = (int32_t)c.seed;
        EXPECT_EQ(c.r1, Random()) << std::hex << c.seed;
        EXPECT_EQ(c.s1, (uint32_t)qd.randSeed) << std::hex << c.seed;
        Random();
        Random();
        EXPECT_EQ(c.r4, Random()) << std::hex << c.seed;
        EXPECT_EQ(c.s4, (uint32_t)qd.randSeed) << std::hex << c.seed;
    }
    qd.randSeed = saved;
}

/* The main screen has a driver, as on a 7.6.1 Quadra (2026-11-03: gdFlags
   $B820, a DCE at its gdRefNum): noDriver clear, for every program
   (Executor once set it for Realmz). */
TEST(QuickDraw, MainDeviceHasDriver)
{
    GDHandle gd = GetMainDevice();
    ASSERT_NE(nullptr, gd);
    short flags = (*gd)->gdFlags;
    EXPECT_EQ(0, flags & (1 << 14));      // noDriver
    EXPECT_NE(0, flags & (1 << 13));      // screenDevice
    EXPECT_NE(0, flags & (1 << 11));      // mainScreen
    EXPECT_NE(nullptr, GetDCtlEntry((*gd)->gdRefNum));
}

#ifndef EXECUTOR
/* The 68k application's only (natively there is no screen device): the
   main device at 8 bits (set for the test where it isn't), restored
   after. */
struct MainDevice8
{
    GDHandle gd = GetMainDevice();
    short depth = (*(*gd)->gdPMap)->pixelSize;
    bool ok;
    MainDevice8()
    {
        if((*gd)->gdType != clutType)
            SetDepth(gd, 8, 0, 0);
        ok = (*gd)->gdType == clutType;
    }
    ~MainDevice8()
    {
        if((*(*gd)->gdPMap)->pixelSize != depth)
            SetDepth(gd, depth, 0, 0);
    }
    CTabHandle table() { return (*(*gd)->gdPMap)->pmTable; }
};

/* SetEntries gives the device's table a new seed even for an entry set to
   the colour it has (the Quadra: 40 -> 1096). */
TEST(QuickDraw, SetEntriesSameColourNewSeed)
{
    MainDevice8 dev;
    if(!dev.ok)
        GTEST_SKIP() << "no 8-bit main device";
    int32_t seed = (*dev.table())->ctSeed;
    ColorSpec cs[1];
    cs[0] = (*dev.table())->ctTable[5];
    cs[0].value = 0;
    GDHandle save = GetGDevice();
    SetGDevice(dev.gd);
    SetEntries(5, 0, cs);
    SetGDevice(save);
    EXPECT_NE(seed, (int32_t)(*dev.table())->ctSeed);
}

/* SetPalette on the front window puts its colours in the device's table
   at once, no ActivatePalette needed. */
TEST(QuickDraw, SetPaletteFrontWindowActivates)
{
    MainDevice8 dev;
    if(!dev.ok)
        GTEST_SKIP() << "no 8-bit main device";
    Rect r = { 60, 60, 160, 260 };
    WindowPtr w = NewCWindow(nullptr, &r, PSTR("P"), true, 0, (WindowPtr)-1, false, 0);
    ASSERT_NE(nullptr, w);
    SelectWindow(w);
    ASSERT_EQ(w, FrontWindow());
    PaletteHandle pal = NewPalette(1, nullptr, pmTolerant, 0);
    RGBColor odd = { 0x1234, 0x5678, 0x9ABC };
    SetEntryColor(pal, 0, &odd);
    SetPalette(w, pal, true);
    CTabHandle ct = dev.table();
    bool found = false;
    for(int i = 0; i <= (*ct)->ctSize; i++)
    {
        RGBColor c = (*ct)->ctTable[i].rgb;
        found |= c.red == odd.red && c.green == odd.green && c.blue == odd.blue;
    }
    EXPECT_TRUE(found);
    DisposeWindow(w);
    DisposePalette(pal);
}
#endif

/* Speedometer 4.0's Color QuickDraw test: a polygon recorded as a region
   (OpenRgn, LineTo, CloseRgn), inset by 1 and framed, over and over. The
   heap's free space stays as it was. */
TEST(QuickDraw, InsetFrameRgnNoLeak)
{
    OffscreenPort port(300, 300);
    auto once = [] {
        RgnHandle r = NewRgn();
        OpenRgn();
        MoveTo(150, 10);
        LineTo(290, 120);
        LineTo(240, 290);
        LineTo(60, 290);
        LineTo(10, 120);
        LineTo(150, 10);
        LineTo(150, 150);
        LineTo(20, 280);
        LineTo(150, 10);
        CloseRgn(r);
        InsetRgn(r, 1, 1);
        FrameRgn(r);
        DisposeRgn(r);
    };
    once();
    long before = FreeMem();
    for(int i = 0; i < 200; i++)
        once();
    EXPECT_GE(FreeMem(), before - 1024) << "before " << before;
}


#ifdef EXECUTOR
/* Region calls in a heap with plenty free but no large block (Speedometer
   4.0's Color QuickDraw tests: MaxBlock 6K of 225K free, behind locked
   blocks). InsetRgn needs no more than its result; a call whose result
   doesn't fit fails with QDError and leaves the region as it was; the
   palette GetNewCWindow gives each window isn't made (nil). Not on
   the Mac side: on a 7.6.1 Quadra InsetRgn hangs the machine here. */
TEST(QuickDraw, RgnOpsInFragmentedHeap)
{
    OffscreenPort port(300, 300);
    auto poly = [](RgnHandle rg, int k) {
        OpenRgn();
        MoveTo(150, 10);
        for(int i = 0; i < 40; i++)
            LineTo(10 + (i * 37 + k) % 280, 10 + (i * 53) % 280);
        LineTo(150, 10);
        CloseRgn(rg);
    };
    RgnHandle r = NewRgn(), a = NewRgn(), b = NewRgn(), c = NewRgn();
    RgnHandle a0 = NewRgn(), ri = NewRgn(), rx = NewRgn();
    poly(r, 0);
    poly(a, 5);
    poly(b, 11);
    CopyRgn(a, a0);
    CopyRgn(r, ri);
    InsetRgn(ri, 1, 1);
    XorRgn(a, b, rx);
    ASSERT_GT(GetHandleSize((Handle)rx), 8192);

    MaxApplZone();
    std::vector<Ptr> ps;
    while(MaxBlock() > 256 * 1024)
    {
        Ptr p = NewPtr(64 * 1024);
        if(!p)
            break;
        ps.push_back(p);
    }
    std::vector<Ptr> chunks;
    for(int n = 0; n < 2000 && MaxBlock() > 1024 + 64; n++)
    {
        Ptr p = NewPtr(1024);
        if(!p)
            break;
        chunks.push_back(p);
    }
    for(size_t i = 0; i < chunks.size(); i++)
        if(i % 2 == 0)
            DisposePtr(chunks[i]);
        else
            ps.push_back(chunks[i]);
    EXPECT_LT(MaxBlock(), 4096);
    EXPECT_GT(FreeMem(), 64 * 1024);

    InsetRgn(r, 1, 1);
    EXPECT_EQ(QDError(), noErr);
    EXPECT_TRUE(EqualRgn(r, ri));

    XorRgn(a, b, c);
    EXPECT_EQ(QDError(), memFullErr);
    EXPECT_TRUE(EmptyRgn(c));

    XorRgn(a, b, a);
    EXPECT_EQ(QDError(), memFullErr);
    EXPECT_TRUE(EqualRgn(a, a0));

    UnionRgn(a, b, c);
    EXPECT_EQ(QDError(), memFullErr);
    EXPECT_TRUE(EmptyRgn(c));

    CopyRgn(b, c);
    EXPECT_EQ(QDError(), memFullErr);
    EXPECT_TRUE(EmptyRgn(c));

    FrameRgn(b);
    Rect oval = { 20, 20, 280, 280 };
    FrameOval(&oval);
    PaintOval(&oval);
    /* a 256-colour palette (4112 bytes) doesn't fit: nil */
    EXPECT_EQ(NewPalette(256, nullptr, pmTolerant, 0), nullptr);

    for(Ptr p : ps)
        DisposePtr(p);

    XorRgn(a, b, a);
    EXPECT_EQ(QDError(), noErr);
    EXPECT_TRUE(EqualRgn(a, rx));

    DisposeRgn(r);
    DisposeRgn(a);
    DisposeRgn(b);
    DisposeRgn(c);
    DisposeRgn(a0);
    DisposeRgn(ri);
    DisposeRgn(rx);
}
#endif

/* A picture that changes the pen, fill and background pixel patterns:
   drawing it leaves the heap as it was (Executor kept every pattern the
   picture replaced; Speedometer 4.0's heap filled up with them). A
   version 2 PICT made by hand: OpenPicture doesn't record these opcodes
   in Executor. */
TEST(QuickDraw, DrawPictureWithPixPatsNoLeak)
{
    if(!hasDeepGWorlds())
        return;
    OffscreenWorld world(8);
    std::vector<unsigned char> b;
    auto w = [&b](unsigned v) { b.push_back(v >> 8); b.push_back(v & 0xFF); };
    auto rect = [&w](int t, int l, int bo, int r) { w(t); w(l); w(bo); w(r); };
    w(0);                       /* picSize, set below */
    rect(0, 0, 40, 40);         /* picFrame */
    w(0x0011); w(0x02FF);       /* version 2 */
    w(0x0C00);                  /* header: -2, 72 dpi, the frame */
    w(0xFFFE); w(0);
    w(0x0048); w(0); w(0x0048); w(0);
    rect(0, 0, 40, 40);
    w(0); w(0);
    for(int i = 0; i < 6; i++)
    {
        for(unsigned op : { 0x0012u, 0x0013u, 0x0014u }) /* Bk, Pn, Fill */
        {
            w(op);
            w(2);               /* RGB pattern */
            for(int j = 0; j < 4; j++)
                w(0xAA55);      /* its 1-bit pattern */
            w(i * 10000); w(20000); w(60000 - i * 9000);
        }
        w(0x0031);              /* paintRect */
        rect(i * 6, 0, i * 6 + 5, 40);
    }
    w(0x00FF);
    b[0] = b.size() >> 8;
    b[1] = b.size() & 0xFF;
    Handle h = nullptr;
    GUEST<Handle> gh;
    ASSERT_EQ(noErr, PtrToHand((Ptr)b.data(), &gh, b.size()));
    h = gh;
    PicHandle pic = (PicHandle)h;
    Rect frame = { 0, 0, 40, 40 };

    DrawPicture(pic, &frame);
    long before = FreeMem();
    for(int i = 0; i < 50; i++)
        DrawPicture(pic, &frame);
    EXPECT_GE(FreeMem(), before - 512) << "before " << before;
    DisposeHandle(h);
}

/* MapPt, MapRect, MapRgn and MapPoly as 8.6 answers on the G3
   (tests/guest/difftest desk): an empty source divides by nothing (the
   Appearance control panel's preview mapped through an empty rectangle
   and Executor stopped on a division by zero), the same sizes only
   offset, the Fixed ratio rounded, an inverted source towards zero.

   The Quadra's 7.5.5 and 7.6.1 agree only where the sizes are the same or
   the source isn't empty or inverted. Their ROM's MapPt is a DIVU.W by the
   source's height and width: an empty source onto a destination of
   another size is a zero divide exception (System Error 4, a bomb), and
   an inverted one divides unsigned ({100,100,0,0} onto dst: (0,0) gives
   (10,10), (-40,1000) (10,11), (-5,-15) (10,10)); MapPoly maps every
   point so (the triangle from an inverted source all (10,10), from the
   screen onto an empty rectangle all (5,5)). On the ROM machine the
   test runs only the cases they agree on; Executor answers as 8.6 on any
   System. */
TEST(QuickDraw, MapEmptyRects)
{
    auto map = [](Point p, Rect src, Rect dst) {
        GUEST<Point> g = p;
        MapPt(&g, &src, &dst);
        Point r = g;
        return std::make_pair((int)r.v, (int)r.h);
    };
    using P = std::pair<int, int>;
    Rect dst = { 10, 10, 58, 74 }, none = { 5, 5, 5, 5 };
#ifdef EXECUTOR
    EXPECT_EQ(P(10, 10), map({ 7, 5 }, { 0, 0, 0, 0 }, dst));
    EXPECT_EQ(P(13, 10), map({ 7, 5 }, { 0, 0, 100, 0 }, dst));
    EXPECT_EQ(P(-9, 10), map({ -40, 1000 }, { 0, 0, 100, 0 }, dst));
    EXPECT_EQ(P(10, 650), map({ -40, 1000 }, { 0, 0, 0, 100 }, dst));
#endif
    EXPECT_EQ(P(-35, 5), map({ -40, 1000 }, { 0, 0, 0, 100 }, none));
    EXPECT_EQ(P(0, -10), map({ -5, -15 }, { 0, 0, 0, 0 }, none));
#ifdef EXECUTOR
    EXPECT_EQ(P(57, 73), map({ 0, 0 }, { 100, 100, 0, 0 }, dst));
    EXPECT_EQ(P(76, -565), map({ -40, 1000 }, { 100, 100, 0, 0 }, dst));
    EXPECT_EQ(P(59, 83), map({ -5, -15 }, { 100, 100, 0, 0 }, dst));
#endif
    EXPECT_EQ(P(11, 11), map({ 7, 5 }, { 0, 0, 480, 640 }, dst));
    EXPECT_EQ(P(6, 110), map({ -40, 1000 }, { 0, 0, 480, 640 }, dst));
    EXPECT_EQ(P(9, 8), map({ -5, -15 }, { 0, 0, 480, 640 }, dst));
    EXPECT_EQ(P(12, 11), map({ 15, 5 }, { 0, 0, 480, 640 }, dst));

    Rect r = { 3, 4, 30, 40 }, src = { 0, 0, 100, 0 };
#ifdef EXECUTOR
    MapRect(&r, &src, &dst);
    EXPECT_EQ(11, r.top);
    EXPECT_EQ(10, r.left);
    EXPECT_EQ(24, r.bottom);
    EXPECT_EQ(10, r.right);
#endif

    /* a region of two rectangles: empty from an empty source or onto an
       empty destination; a rectangle's region maps as MapRect */
    RgnHandle rgn = NewRgn(), rgn2 = NewRgn();
    Rect r1 = { 3, 4, 30, 40 }, r2 = { 20, 30, 60, 90 }, screen = { 0, 0, 480, 640 };
#ifdef EXECUTOR
    for(Rect s : { Rect{ 0, 0, 0, 0 }, Rect{ 0, 0, 100, 0 }, Rect{ 0, 0, 0, 100 } })
    {
        RectRgn(rgn, &r1);
        RectRgn(rgn2, &r2);
        UnionRgn(rgn, rgn2, rgn);
        MapRgn(rgn, &s, &dst);
        EXPECT_TRUE(EmptyRgn(rgn));
    }
#endif
    RectRgn(rgn, &r1);
    RectRgn(rgn2, &r2);
    UnionRgn(rgn, rgn2, rgn);
    MapRgn(rgn, &screen, &none);
    EXPECT_TRUE(EmptyRgn(rgn));
#ifdef EXECUTOR
    RectRgn(rgn, &r1);
    MapRgn(rgn, &src, &dst);
    EXPECT_EQ(11, (*rgn)->rgnBBox.top);
    EXPECT_EQ(24, (*rgn)->rgnBBox.bottom);
#endif
    DisposeRgn(rgn);
    DisposeRgn(rgn2);

    /* a polygon stays as it is unless both rectangles have room */
    OffscreenPort port(64, 64);
    PolyHandle poly = OpenPoly();
    MoveTo(3, 4);
    LineTo(30, 4);
    LineTo(17, 40);
    LineTo(3, 4);
    ClosePoly();
#ifdef EXECUTOR
    for(Rect s : { Rect{ 0, 0, 0, 0 }, Rect{ 100, 100, 0, 0 } })
    {
        MapPoly(poly, &s, &dst);
        EXPECT_EQ(4, (*poly)->polyBBox.top);
        EXPECT_EQ(3, (*poly)->polyBBox.left);
    }
    MapPoly(poly, &screen, &none);
    EXPECT_EQ(40, (*poly)->polyBBox.bottom);
#endif
    MapPoly(poly, &screen, &dst);
    EXPECT_EQ(10, (*poly)->polyBBox.top);
    EXPECT_EQ(14, (*poly)->polyBBox.bottom);
    KillPoly(poly);
}

#ifdef EXECUTOR
#include <Iconutil.h>
#include <OSUtil.h>
#endif

/* An open item's icon (ttOpen), a full square mask: 8.x (the G3's 8.6
   Finder, an open control panel) draws the outline black and inside every
   fourth pixel black (rows 0x88/0x22) over the highlight colour; 7.x a 50%
   grey on white. Executor only: it sets the System's version and
   HiliteRGB through Executor's low memory. */
#ifdef EXECUTOR
TEST(QuickDraw, OpenIconLook)
{
    if(!hasDeepGWorlds())
        return;
    GUEST<CGrafPtr> savedPort;
    GUEST<GDHandle> savedDevice;
    GetGWorld(&savedPort, &savedDevice);
    Rect r = { 0, 0, 32, 32 };
    GWorldPtr world = nullptr;
    ASSERT_EQ(noErr, NewGWorld(PTR(world), 32, &r, nullptr, nullptr, 0));
    LockPixels(world->portPixMap);
    SetGWorld(world, nullptr);
    auto px = [&](int y, int x) {
        uint8_t *p = (uint8_t *)(Ptr)(*world->portPixMap)->baseAddr
            + y * ((*world->portPixMap)->rowBytes & 0x3FFF) + 4 * x;
        return (uint32_t)((p[1] << 16) | (p[2] << 8) | p[3]);
    };
    Handle icn = NewHandleClear(256);
    memset(*icn + 128, 0xFF, 128); /* the mask: all of it */
    GUEST<Handle> suite;
    ASSERT_EQ(noErr, NewIconSuite(&suite));
    ASSERT_EQ(noErr, AddIconToSuite(icn, suite, "ICN#"_4));
    RGBColor saved_hilite = LM(HiliteRGB);
    INTEGER saved_version = LM(SysVersion);
    LM(HiliteRGB) = RGBColor{ 0xCCCC, 0xCCCC, 0xFFFF };

    LM(SysVersion) = 0x0860;
    EraseRect(&r);
    EXPECT_EQ(noErr, PlotIconSuite(&r, kAlignNone, ttOpen, suite));
    EXPECT_EQ(0x000000u, px(0, 0));    /* outline */
    EXPECT_EQ(0xCCCCFFu, px(1, 1));
    EXPECT_EQ(0x000000u, px(1, 2));    /* row 1: 0x22 */
    EXPECT_EQ(0x000000u, px(2, 4));    /* row 2: 0x88 */
    EXPECT_EQ(0xCCCCFFu, px(2, 2));

    LM(SysVersion) = 0x0755;
    EraseRect(&r);
    EXPECT_EQ(noErr, PlotIconSuite(&r, kAlignNone, ttOpen, suite));
    EXPECT_EQ(0xFFFFFFu, px(1, 2));    /* 50%: 0x55 on odd rows */
    EXPECT_EQ(0x000000u, px(1, 1));

    LM(SysVersion) = saved_version;
    LM(HiliteRGB) = saved_hilite;
    DisposeIconSuite(suite, true);
    SetGWorld(savedPort, savedDevice);
    DisposeGWorld(world);
}
#endif
