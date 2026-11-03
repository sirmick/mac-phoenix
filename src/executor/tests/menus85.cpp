/* MacPhoenix: the Mac OS 8.5 menu calls (AppearanceLib's command IDs and
   MenuEvent, MenusLib's EnableMenuItem and DisableMenuItem) and the Text
   Services Manager's document calls, as REALbasic's runtime makes them
   (GameBox Classic on 8.6), and a junk control handle given to
   SetControlFontStyle; the bevel button's and image well's calls and
   GetPixRowBytes that the runtime's plugins call. */
#include "gtest/gtest.h"
#include "compat.h"

#include <MenuMgr.h>
#include <TextServices.h>
#include <EventMgr.h>
#include <ControlMgr.h>
#include <ControlMgr8.h>
#include <WindowMgr.h>
#include <CQuickDraw.h>

using namespace Executor;

TEST(Menus85, EnableAndDisableMenuItem)
{
    MenuHandle m = NewMenu(9301, (StringPtr) "\4Test");
    AppendMenu(m, (StringPtr) "\5A;B;C");
    C_DisableMenuItem(m, 2);
    EXPECT_TRUE(C_IsMenuItemEnabled(m, 1));
    EXPECT_FALSE(C_IsMenuItemEnabled(m, 2));
    C_EnableMenuItem(m, 2);
    EXPECT_TRUE(C_IsMenuItemEnabled(m, 2));
    C_DisableMenuItem(m, 40); /* past 31: no bit of its own, nothing changes */
    EXPECT_TRUE(C_IsMenuItemEnabled(m, 1));
    C_DisableMenuItem(m, 0);  /* the whole menu */
    EXPECT_FALSE(C_IsMenuItemEnabled(m, 1));
    DisposeMenu(m);
}

TEST(Menus85, CommandIDs)
{
    MenuHandle m = NewMenu(9302, (StringPtr) "\4Test");
    AppendMenu(m, (StringPtr) "\3A;B");
    GUEST<UInt32> id = 1;
    EXPECT_EQ(noErr, C_GetMenuItemCommandID(m, 2, &id));
    EXPECT_EQ(0u, (UInt32)id);
    EXPECT_EQ(noErr, C_SetMenuItemCommandID(m, 2, 0x07654321));
    EXPECT_EQ(noErr, C_GetMenuItemCommandID(m, 2, &id));
    EXPECT_EQ(0x07654321u, (UInt32)id);
    EXPECT_EQ(paramErr, C_SetMenuItemCommandID(m, 3, 1));
    DisposeMenu(m);
}

TEST(Menus85, MenuEventIsZeroWithoutCommandKey)
{
    EventRecord e = {};
    e.what = keyDown;
    e.message = 'q';
    EXPECT_EQ(0u, C_MenuEvent(&e));
    e.what = mouseDown;
    e.modifiers = cmdKey;
    EXPECT_EQ(0u, C_MenuEvent(&e));
}

TEST(TSM, Documents)
{
    GUEST<OSType> kinds[1] = { "tsvc"_4 };
    GUEST<TSMDocumentID> doc;
    ASSERT_EQ(noErr, NewTSMDocument(1, kinds, &doc, 1234));
    EXPECT_NE(nullptr, (TSMDocumentID)doc);
    EXPECT_EQ(noErr, ActivateTSMDocument(doc));
    EXPECT_EQ(noErr, FixTSMDocument(doc));
    EXPECT_EQ(noErr, UseInputWindow(doc, true));
    EXPECT_EQ(noErr, DeactivateTSMDocument(doc));
    EventRecord e = {};
    e.what = keyDown;
    EXPECT_FALSE(TSMEvent(&e));
    EXPECT_FALSE(TSMMenuSelect(0x00010001));
    EXPECT_EQ(noErr, DeleteTSMDocument(doc));
    EXPECT_NE(noErr, DeleteTSMDocument(doc));
    EXPECT_NE(noErr, ActivateTSMDocument(doc));
}

TEST(Controls8, SetControlFontStyleRefusesWhatIsNotAControl)
{
    ControlFontStyleRec style = {};
    style.flags = kControlUseFontMask;
    style.font = 2002;
    EXPECT_EQ(paramErr, SetControlFontStyle((ControlHandle)SYN68K_TO_US(0xCCCC), &style));
}

/* The bevel button's and image well's AppearanceLib calls pass through
   their control data tags: on a push button they answer what
   SetControlData and GetControlData do there; nil content is paramErr. */
TEST(Controls8, BevelButtonCallsGoThroughControlData)
{
    Rect wr = { 50, 50, 250, 350 };
    WindowPtr w = NewCWindow(nullptr, &wr, (StringPtr) "", false, documentProc,
                             (WindowPtr)-1, false, 0);
    Rect cr = { 10, 10, 30, 90 };
    ControlHandle c = NewControl(w, &cr, (StringPtr) "\2OK", false, 0, 0, 1, pushButProc, 0);
    ASSERT_NE(nullptr, c);
    ControlButtonContentInfo info = {};
    GUEST<SInt16> align = 0;
    OSErr set = C_SetControlData(c, kControlEntireControl, kControlBevelButtonTextAlignTag,
                                 sizeof align, &align);
    OSErr get = C_GetControlData(c, kControlEntireControl, kControlBevelButtonContentTag,
                                 sizeof info, &info, nullptr);
    EXPECT_NE(noErr, set);
    EXPECT_NE(noErr, get);
    EXPECT_EQ(paramErr, C_GetBevelButtonContentInfo(c, nullptr));
    EXPECT_EQ(paramErr, C_SetImageWellContentInfo(c, nullptr));
    EXPECT_EQ(get, C_GetBevelButtonContentInfo(c, &info));
    EXPECT_EQ(set, C_SetImageWellContentInfo(c, &info));
    EXPECT_EQ(set, C_SetBevelButtonTextAlignment(c, kControlBevelButtonAlignTextFlushLeft, 4));
    EXPECT_EQ(set, C_SetBevelButtonGraphicAlignment(c, kControlBevelButtonAlignLeft, 2, 3));
    DisposeWindow(w);
}

/* GetPixRowBytes (InterfaceLib 8.5) is the row bytes without the two flag
   bits; nil is 0. */
TEST(QuickDraw85, GetPixRowBytes)
{
    GWorldPtr gw = nullptr;
    Rect r = { 0, 0, 10, 33 };
    ASSERT_EQ(noErr, NewGWorld(PTR(gw), 8, &r, nullptr, nullptr, 0));
    PixMapHandle pm = GetGWorldPixMap(gw);
    EXPECT_NE(0, (*pm)->rowBytes & 0x8000);
    EXPECT_EQ((*pm)->rowBytes & 0x3FFF, C_GetPixRowBytes(pm));
    EXPECT_GE(C_GetPixRowBytes(pm), 33);
    EXPECT_EQ(0, C_GetPixRowBytes(nullptr));
    DisposeGWorld(gw);
}
