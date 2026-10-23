/*****************************************************************************
 *                                                                           *
 *                                 MenuMgr.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     INTEGER, Handle, LONGINT, Str255, ConstStringPtr,      *
 *                    ResType, CharParameter, StringPtr, Boolean, Byte,      *
 *                    OSErr, Rect                                            *
 *     QuickDraw.h    RGBColor                                               *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "QuickDraw.h"

#define MODULE_NAME MenuMgr
#include <base/api-module.h>


namespace Executor {

enum {noMark = 0,
};

enum {mDrawMsg = 0,
mChooseMsg = 1,
mSizeMsg = 2,
mPopUpRect = 3,
};

enum {textMenuProc = 0,
};

struct MenuInfo{GUEST_STRUCT;GUEST<INTEGER> menuID;GUEST<INTEGER> menuWidth;GUEST<INTEGER> menuHeight;GUEST<Handle> menuProc;GUEST<LONGINT> enableFlags;GUEST<Str255> menuData;};static_assert(sizeof(MenuInfo) == 270);void describeStruct(const MenuInfo&, std::ostream&);

typedef MenuInfo *MenuPtr;

typedef GUEST<MenuPtr> *MenuHandle;

struct MCEntry{GUEST_STRUCT;GUEST<INTEGER> mctID;GUEST<INTEGER> mctItem;RGBColor mctRGB1;RGBColor mctRGB2;RGBColor mctRGB3;RGBColor mctRGB4;GUEST<INTEGER> mctReserved;};static_assert(sizeof(MCEntry) == 30);void describeStruct(const MCEntry&, std::ostream&);

typedef MCEntry *MCEntryPtr;

typedef MCEntry MCTable[1];

typedef MCEntry *MCTablePtr;

typedef GUEST<MCTablePtr> *MCTableHandle;

// MenuMgr IMV-249 (true);
const LowMemGlobal<INTEGER > TopMenuItem{ 0xA0A };
inline INTEGER  LMGetTopMenuItem() { return LM(TopMenuItem); }
inline void LMSetTopMenuItem(INTEGER val) { LM(TopMenuItem) = val; }
NOTRAP_FUNCTION2(LMGetTopMenuItem);
NOTRAP_FUNCTION2(LMSetTopMenuItem);


// MenuMgr IMV-249 (true);
const LowMemGlobal<INTEGER > AtMenuBottom{ 0xA0C };
inline INTEGER  LMGetAtMenuBottom() { return LM(AtMenuBottom); }
inline void LMSetAtMenuBottom(INTEGER val) { LM(AtMenuBottom) = val; }
NOTRAP_FUNCTION2(LMGetAtMenuBottom);
NOTRAP_FUNCTION2(LMSetAtMenuBottom);


// MenuMgr IMI-346 (true);
const LowMemGlobal<Handle > MenuList{ 0xA1C };
inline Handle  LMGetMenuList() { return LM(MenuList); }
inline void LMSetMenuList(Handle val) { LM(MenuList) = val; }
NOTRAP_FUNCTION2(LMGetMenuList);
NOTRAP_FUNCTION2(LMSetMenuList);


// MacPhoenix (name is a guess): 7.5.5's menu list of the system menus,
// Help and Application, shared by every process (not in the
// per-process low-memory ranges). Apple's Help Manager looks the
// system's Help menu up in it to tell it from an application's copy
// (menu/sysmenu.cpp).
const LowMemGlobal<Handle > SystemMenuList{ 0x286 };
inline Handle  LMGetSystemMenuList() { return LM(SystemMenuList); }
inline void LMSetSystemMenuList(Handle val) { LM(SystemMenuList) = val; }
NOTRAP_FUNCTION2(LMGetSystemMenuList);
NOTRAP_FUNCTION2(LMSetSystemMenuList);


// MenuMgr IMI-356 (true);
const LowMemGlobal<INTEGER > MBarEnable{ 0xA20 };
inline INTEGER  LMGetMBarEnable() { return LM(MBarEnable); }
inline void LMSetMBarEnable(INTEGER val) { LM(MBarEnable) = val; }
NOTRAP_FUNCTION2(LMGetMBarEnable);
NOTRAP_FUNCTION2(LMSetMBarEnable);


// MenuMgr IMI-361 (true);
const LowMemGlobal<INTEGER > MenuFlash{ 0xA24 };
inline INTEGER  LMGetMenuFlash() { return LM(MenuFlash); }
inline void LMSetMenuFlash(INTEGER val) { LM(MenuFlash) = val; }
NOTRAP_FUNCTION2(LMGetMenuFlash);
NOTRAP_FUNCTION2(LMSetMenuFlash);


// MacPhoenix: the bits under a pulled-down menu, non-nil while one is
// down (7.5.5; Executor's own menus keep theirs elsewhere, so it is
// nil). Apple's Help Manager shows no window balloons while it is set.
const LowMemGlobal<Handle > SavedHandle{ 0xA28 };
inline Handle  LMGetSavedHandle() { return LM(SavedHandle); }
inline void LMSetSavedHandle(Handle val) { LM(SavedHandle) = val; }
NOTRAP_FUNCTION2(LMGetSavedHandle);
NOTRAP_FUNCTION2(LMSetSavedHandle);


// MenuMgr IMI-357 (true);
const LowMemGlobal<INTEGER > TheMenu{ 0xA26 };
inline INTEGER  LMGetTheMenu() { return LM(TheMenu); }
inline void LMSetTheMenu(INTEGER val) { LM(TheMenu) = val; }
NOTRAP_FUNCTION2(LMGetTheMenu);
NOTRAP_FUNCTION2(LMSetTheMenu);


// MenuMgr IMI-356 (true);
const LowMemGlobal<ProcPtr > MBarHook{ 0xA2C };
inline ProcPtr  LMGetMBarHook() { return LM(MBarHook); }
inline void LMSetMBarHook(ProcPtr val) { LM(MBarHook) = val; }
NOTRAP_FUNCTION2(LMGetMBarHook);
NOTRAP_FUNCTION2(LMSetMBarHook);


// MenuMgr IMI-356 (true);
const LowMemGlobal<ProcPtr > MenuHook{ 0xA30 };
inline ProcPtr  LMGetMenuHook() { return LM(MenuHook); }
inline void LMSetMenuHook(ProcPtr val) { LM(MenuHook) = val; }
NOTRAP_FUNCTION2(LMGetMenuHook);
NOTRAP_FUNCTION2(LMSetMenuHook);


// MenuMgr IMV-249 (true);
const LowMemGlobal<LONGINT > MenuDisable{ 0xB54 };
inline LONGINT  LMGetMenuDisable() { return LM(MenuDisable); }
inline void LMSetMenuDisable(LONGINT val) { LM(MenuDisable) = val; }
NOTRAP_FUNCTION2(LMGetMenuDisable);
NOTRAP_FUNCTION2(LMSetMenuDisable);


// MenuMgr Private.a (true);
const LowMemGlobal<Handle > MBDFHndl{ 0xB58 };
inline Handle  LMGetMBDFHndl() { return LM(MBDFHndl); }
inline void LMSetMBDFHndl(Handle val) { LM(MBDFHndl) = val; }
NOTRAP_FUNCTION2(LMGetMBDFHndl);
NOTRAP_FUNCTION2(LMSetMBDFHndl);


// MenuMgr Private.a (true);
const LowMemGlobal<Handle > MBSaveLoc{ 0xB5C };
inline Handle  LMGetMBSaveLoc() { return LM(MBSaveLoc); }
inline void LMSetMBSaveLoc(Handle val) { LM(MBSaveLoc) = val; }
NOTRAP_FUNCTION2(LMGetMBSaveLoc);
NOTRAP_FUNCTION2(LMSetMBSaveLoc);


// MenuMgr IMV-253 (true);
const LowMemGlobal<INTEGER > MBarHeight{ 0xBAA };
inline INTEGER  LMGetMBarHeight() { return LM(MBarHeight); }
inline void LMSetMBarHeight(INTEGER val) { LM(MBarHeight) = val; }
NOTRAP_FUNCTION2(LMGetMBarHeight);
NOTRAP_FUNCTION2(LMSetMBarHeight);


// QuickDraw IMV-242 (true);
const LowMemGlobal<MCTableHandle > MenuCInfo{ 0xD50 };
inline MCTableHandle  LMGetMenuCInfo() { return LM(MenuCInfo); }
inline void LMSetMenuCInfo(MCTableHandle val) { LM(MenuCInfo) = val; }
NOTRAP_FUNCTION2(LMGetMenuCInfo);
NOTRAP_FUNCTION2(LMSetMenuCInfo);


void C_DrawMenuBar();PASCAL_TRAP(DrawMenuBar, 0xA937);


void C_ClearMenuBar();PASCAL_TRAP(ClearMenuBar, 0xA934);


void C_InitMenus();PASCAL_TRAP(InitMenus, 0xA930);


MenuHandle C_NewMenu(INTEGER mid, ConstStringPtr str);PASCAL_TRAP(NewMenu, 0xA931);


void C_CalcMenuSize(MenuHandle mh);PASCAL_TRAP(CalcMenuSize, 0xA948);


MenuHandle C_GetMenu(INTEGER rid);PASCAL_TRAP(GetMenu, 0xA9BF);


void C_DisposeMenu(MenuHandle mh);PASCAL_TRAP(DisposeMenu, 0xA932);


void C_AppendMenu(MenuHandle mh, ConstStringPtr str);PASCAL_TRAP(AppendMenu, 0xA933);


void C_AppendResMenu(MenuHandle mh, ResType restype);PASCAL_TRAP(AppendResMenu, 0xA94D);


void C_DeleteMenuItem(MenuHandle mh, INTEGER item);PASCAL_TRAP(DeleteMenuItem, 0xA952);


void C_InsertResMenu(MenuHandle mh, ResType restype, INTEGER after);PASCAL_TRAP(InsertResMenu, 0xA951);


void C_InsertMenuItem(MenuHandle mh, ConstStringPtr str, INTEGER after);PASCAL_TRAP(InsertMenuItem, 0xA826);


void C_InsertMenu(MenuHandle mh, INTEGER before);PASCAL_TRAP(InsertMenu, 0xA935);


void C_DeleteMenu(INTEGER mid);PASCAL_TRAP(DeleteMenu, 0xA936);


Handle C_GetNewMBar(INTEGER mbarid);PASCAL_TRAP(GetNewMBar, 0xA9C0);


Handle C_GetMenuBar();PASCAL_TRAP(GetMenuBar, 0xA93B);


void C_SetMenuBar(Handle ml);PASCAL_TRAP(SetMenuBar, 0xA93C);


void C_HiliteMenu(INTEGER mid);PASCAL_TRAP(HiliteMenu, 0xA938);


LONGINT C_MenuSelect(Point p);PASCAL_TRAP(MenuSelect, 0xA93D);


void C_FlashMenuBar(INTEGER mid);PASCAL_TRAP(FlashMenuBar, 0xA94C);


LONGINT C_MenuKey(CharParameter thec);PASCAL_TRAP(MenuKey, 0xA93E);


void C_SetMenuItemText(MenuHandle mh, INTEGER item, ConstStringPtr str);PASCAL_TRAP(SetMenuItemText, 0xA947);


void C_GetMenuItemText(MenuHandle mh, INTEGER item, StringPtr str);PASCAL_TRAP(GetMenuItemText, 0xA946);


void C_DisableItem(MenuHandle mh, INTEGER item);PASCAL_TRAP(DisableItem, 0xA93A);


void C_EnableItem(MenuHandle mh, INTEGER item);PASCAL_TRAP(EnableItem, 0xA939);


void C_CheckItem(MenuHandle mh, INTEGER item, Boolean cflag);PASCAL_TRAP(CheckItem, 0xA945);


void C_SetItemMark(MenuHandle mh, INTEGER item, CharParameter mark);PASCAL_TRAP(SetItemMark, 0xA944);


void C_GetItemMark(MenuHandle mh, INTEGER item, GUEST<INTEGER> *markp);PASCAL_TRAP(GetItemMark, 0xA943);


void C_SetItemIcon(MenuHandle mh, INTEGER item, Byte icon);PASCAL_TRAP(SetItemIcon, 0xA940);


void C_GetItemIcon(MenuHandle mh, INTEGER item, GUEST<INTEGER> *iconp);PASCAL_TRAP(GetItemIcon, 0xA93F);


void C_SetItemStyle(MenuHandle mh, INTEGER item, INTEGER style);PASCAL_TRAP(SetItemStyle, 0xA942);


void C_GetItemStyle(MenuHandle mh, INTEGER item, GUEST<INTEGER> *stylep);PASCAL_TRAP(GetItemStyle, 0xA941);


INTEGER C_CountMItems(MenuHandle mh);PASCAL_TRAP(CountMItems, 0xA950);


MenuHandle C_GetMenuHandle(INTEGER mid);PASCAL_TRAP(GetMenuHandle, 0xA949);


void C_SetMenuFlash(INTEGER i);PASCAL_TRAP(SetMenuFlash, 0xA94A);


void C_InitProcMenu(INTEGER mbid);PASCAL_TRAP(InitProcMenu, 0xA808);


LONGINT C_MenuChoice();PASCAL_TRAP(MenuChoice, 0xAA66);


void C_GetItemCmd(MenuHandle mh, INTEGER item, GUEST<CharParameter> *cmdp);PASCAL_TRAP(GetItemCmd, 0xA84E);


void C_SetItemCmd(MenuHandle mh, INTEGER item, CharParameter cmd);PASCAL_TRAP(SetItemCmd, 0xA84F);


LONGINT C_PopUpMenuSelect(MenuHandle mh, INTEGER top, INTEGER left, INTEGER item);PASCAL_TRAP(PopUpMenuSelect, 0xA80B);


void C_DeleteMCEntries(INTEGER , INTEGER );PASCAL_TRAP(DeleteMCEntries, 0xAA60);


MCTableHandle C_GetMCInfo();PASCAL_TRAP(GetMCInfo, 0xAA61);


void C_SetMCInfo(MCTableHandle );PASCAL_TRAP(SetMCInfo, 0xAA62);


void C_DisposeMCInfo(MCTableHandle );PASCAL_TRAP(DisposeMCInfo, 0xAA63);


MCEntryPtr C_GetMCEntry(INTEGER , INTEGER );PASCAL_TRAP(GetMCEntry, 0xAA64);


void C_SetMCEntries(INTEGER , MCTablePtr );PASCAL_TRAP(SetMCEntries, 0xAA65);


void C_InvalMenuBar();PASCAL_TRAP(InvalMenuBar, 0xA81D);


typedef MenuHandle MenuRef;

void CheckMenuItem(MenuHandle mh, INTEGER item, Boolean cflag);



INTEGER C_GetMBarHeight();NOTRAP_FUNCTION(GetMBarHeight);


// 7.5.5's dispatcher (through lowmem $7B0) covers selectors -6..1 and
// only D0's low byte selects; D0's high byte is the parameter size.
// -4..-1 are MBDF message 14, menu title rectangles.
DISPATCHER_TRAP(MenuDispatch, 0xA825, D0<0xFF>);


void C_InsertFontResMenu(MenuHandle mh, INTEGER after, INTEGER scriptFilter);PASCAL_SUBTRAP(InsertFontResMenu, 0xA825, 0x0400, MenuDispatch);


void C_InsertIntlResMenu(MenuHandle mh, ResType theType, INTEGER after, INTEGER scriptFilter);PASCAL_SUBTRAP(InsertIntlResMenu, 0xA825, 0x0601, MenuDispatch);


// MenuDispatch -5 on 7.5.5 (name is a guess).  Calls the MBDF with
// message 15 (param1 = just << 8 | font byte, param2 = msg): the bar
// is cleared and msg drawn across it in bold.  Sets $B20 bit 6 so the
// next MenuSelect/MenuKey redraws the titles first.  Finder uses it.
OSErr C_DrawMenuBarMessage(ConstStringPtr msg, int16_t font, int16_t just);PASCAL_SUBTRAP(DrawMenuBarMessage, 0xA825, 0x08FB, MenuDispatch);


// MenuDispatch -6 on 7.5.5 (name is a guess): *result is true when
// menuID is in the system menu range -20480..-16385.
OSErr C_IsSystemMenu(INTEGER menuID, Boolean *result);PASCAL_SUBTRAP(IsSystemMenu, 0xA825, 0x03FA, MenuDispatch);


// MenuDispatch -4 on 7.5.5 (name is a guess): MBDF message 14 with
// parameter -2, the rectangle from the first system menu's title (Help)
// to the last's (Application); empty when there is none.  The OSErr is
// written over the rectangle's top word, as Apple's does.  Date & Time
// ends its menu-bar clock one pixel left of it.
OSErr C_GetSystemMenuTitlesRect(Rect *rect);PASCAL_SUBTRAP(GetSystemMenuTitlesRect, 0xA825, 0x04FC, MenuDispatch);


// MenuDispatch -3 on 7.5.5 (name is a guess): MBDF message 14 with
// parameter -1, the rectangle from the first title to lastRight.
OSErr C_GetAppMenuTitlesRect(Rect *rect);PASCAL_SUBTRAP(GetAppMenuTitlesRect, 0xA825, 0x04FD, MenuDispatch);


// MenuDispatch -2 on 7.5.5 (name is a guess): MBDF message 14 with
// parameter 0, the whole menu bar.
OSErr C_GetMenuBarRect(Rect *rect);PASCAL_SUBTRAP(GetMenuBarRect, 0xA825, 0x04FE, MenuDispatch);


// MenuDispatch -1 on 7.5.5 (name is a guess): MBDF message 14 with the
// menu's MenuList offset, its title rectangle; paramErr if menuID is
// not in the bar.
OSErr C_GetMenuTitleRect(INTEGER menuID, Rect *rect);PASCAL_SUBTRAP(GetMenuTitleRect, 0xA825, 0x06FF, MenuDispatch);


} /* namespace Executor */