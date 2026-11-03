/* MacPhoenix: the Mac OS 8.5 menu calls (AppearanceLib's command IDs and
   MenuEvent, MenusLib's EnableMenuItem and DisableMenuItem) and the Text
   Services Manager's document calls, as REALbasic's runtime makes them
   (GameBox Classic on 8.6), and a junk control handle given to
   SetControlFontStyle. */
#include "gtest/gtest.h"

#include <MenuMgr.h>
#include <TextServices.h>
#include <EventMgr.h>
#include <ControlMgr.h>
#include <ControlMgr8.h>
#include <WindowMgr.h>

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
