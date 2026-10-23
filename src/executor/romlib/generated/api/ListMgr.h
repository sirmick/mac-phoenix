/*****************************************************************************
 *                                                                           *
 *                                 ListMgr.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     Byte, Boolean, Rect, SignedByte, LONGINT, Handle,      *
 *                    INTEGER, Ptr                                           *
 *     QuickDraw.h    GrafPtr, RgnHandle                                     *
 *     WindowMgr.h    ControlHandle, WindowPtr                               *
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
#include "WindowMgr.h"

#define MODULE_NAME ListMgr
#include <base/api-module.h>


namespace Executor {

typedef Point Cell;

typedef Byte DataArray[32001];

typedef GUEST<DataArray> *DataPtr;

typedef GUEST<DataPtr> *DataHandle;

using ListClickLoopUPP = UPP<Boolean()>;


struct ListRec{GUEST_STRUCT;Rect rView;GUEST<GrafPtr> port;GUEST<Point> indent;GUEST<Point> cellSize;Rect visible;GUEST<ControlHandle> vScroll;GUEST<ControlHandle> hScroll;SignedByte selFlags;Boolean lActive;SignedByte lReserved;SignedByte listFlags;GUEST<LONGINT> clikTime;GUEST<Point> clikLoc;GUEST<Point> mouseLoc;GUEST<ListClickLoopUPP> lClikLoop;GUEST<Cell> lastClick;GUEST<LONGINT> refCon;GUEST<Handle> listDefProc;GUEST<Handle> userHandle;Rect dataBounds;GUEST<DataHandle> cells;GUEST<INTEGER> maxIndex;GUEST<INTEGER> cellArray[1];};static_assert(sizeof(ListRec) == 88);void describeStruct(const ListRec&, std::ostream&);

typedef ListRec *ListPtr;

typedef GUEST<ListPtr> *ListHandle;

enum {lGrowBox = 32,
lMysteryFlags = 20,
lDrawingModeOff = 8,
lDoVAutoscroll = 2,
lDoHAutoscroll = 1,
};

enum {lOnlyOne = -128,
lExtendDrag = 64,
lNoDisjoint = 32,
};

enum {lNoExtend = 16,
lNoRect = 8,
lUseSense = 4,
lNoNilHilite = 2,
};

enum {lInitMsg = 0,
lDrawMsg = 1,
lHiliteMsg = 2,
lCloseMsg = 3,
};

DISPATCHER_TRAP(Pack0, 0xA9E7, StackW);


void C_LGetCellDataLocation(GUEST<INTEGER> *offsetp, GUEST<INTEGER> *lenp, Cell cell, ListHandle list);PASCAL_SUBTRAP(LGetCellDataLocation, 0xA9E7, 0x0034, Pack0);


Boolean C_LNextCell(Boolean hnext, Boolean vnext, GUEST<Cell> *cellp, ListHandle list);PASCAL_SUBTRAP(LNextCell, 0xA9E7, 0x0048, Pack0);


void C_LRect(Rect *cellrect, Cell cell, ListHandle list);PASCAL_SUBTRAP(LRect, 0xA9E7, 0x004C, Pack0);


Boolean C_LSearch(Ptr dp, INTEGER dl, Ptr proc, GUEST<Cell> *cellp, ListHandle list);PASCAL_SUBTRAP(LSearch, 0xA9E7, 0x0054, Pack0);


void C_LSize(INTEGER width, INTEGER height, ListHandle list);PASCAL_SUBTRAP(LSize, 0xA9E7, 0x0060, Pack0);


INTEGER C_LAddColumn(INTEGER count, INTEGER coln, ListHandle list);PASCAL_SUBTRAP(LAddColumn, 0xA9E7, 0x0004, Pack0);


INTEGER C_LAddRow(INTEGER count, INTEGER rown, ListHandle list);PASCAL_SUBTRAP(LAddRow, 0xA9E7, 0x0008, Pack0);


void C_LDelColumn(INTEGER count, INTEGER coln, ListHandle list);PASCAL_SUBTRAP(LDelColumn, 0xA9E7, 0x0020, Pack0);


void C_LDelRow(INTEGER count, INTEGER rown, ListHandle list);PASCAL_SUBTRAP(LDelRow, 0xA9E7, 0x0024, Pack0);


ListHandle C_LNew(const Rect *rview, const Rect *bounds, Point csize, INTEGER proc, WindowPtr wind, Boolean draw, Boolean grow, Boolean scrollh, Boolean scrollv);PASCAL_SUBTRAP(LNew, 0xA9E7, 0x0044, Pack0);


void C_LDispose(ListHandle list);PASCAL_SUBTRAP(LDispose, 0xA9E7, 0x0028, Pack0);


void C_LDraw(Cell cell, ListHandle list);PASCAL_SUBTRAP(LDraw, 0xA9E7, 0x0030, Pack0);


void C_LSetDrawingMode(Boolean draw, ListHandle list);PASCAL_SUBTRAP(LSetDrawingMode, 0xA9E7, 0x002C, Pack0);


void C_LScroll(INTEGER ncol, INTEGER nrow, ListHandle list);PASCAL_SUBTRAP(LScroll, 0xA9E7, 0x0050, Pack0);


void C_LAutoScroll(ListHandle list);PASCAL_SUBTRAP(LAutoScroll, 0xA9E7, 0x0010, Pack0);


void C_LUpdate(RgnHandle rgn, ListHandle list);PASCAL_SUBTRAP(LUpdate, 0xA9E7, 0x0064, Pack0);


void C_LActivate(Boolean act, ListHandle list);PASCAL_SUBTRAP(LActivate, 0xA9E7, 0x0000, Pack0);


Boolean C_LClick(Point pt, INTEGER mods, ListHandle list);PASCAL_SUBTRAP(LClick, 0xA9E7, 0x0018, Pack0);


LONGINT C_LLastClick(ListHandle list);PASCAL_SUBTRAP(LLastClick, 0xA9E7, 0x0040, Pack0);


void C_LSetSelect(Boolean setit, Cell cell, ListHandle list);PASCAL_SUBTRAP(LSetSelect, 0xA9E7, 0x005C, Pack0);


void C_LAddToCell(Ptr dp, INTEGER dl, Cell cell, ListHandle list);PASCAL_SUBTRAP(LAddToCell, 0xA9E7, 0x000C, Pack0);


void C_LClrCell(Cell cell, ListHandle list);PASCAL_SUBTRAP(LClrCell, 0xA9E7, 0x001C, Pack0);


void C_LGetCell(Ptr dp, GUEST<INTEGER> *dlp, Cell cell, ListHandle list);PASCAL_SUBTRAP(LGetCell, 0xA9E7, 0x0038, Pack0);


void C_LSetCell(Ptr dp, INTEGER dl, Cell cell, ListHandle list);PASCAL_SUBTRAP(LSetCell, 0xA9E7, 0x0058, Pack0);


void C_LCellSize(Point csize, ListHandle list);PASCAL_SUBTRAP(LCellSize, 0xA9E7, 0x0014, Pack0);


Boolean C_LGetSelect(Boolean next, GUEST<Cell> *cellp, ListHandle list);PASCAL_SUBTRAP(LGetSelect, 0xA9E7, 0x003C, Pack0);


} /* namespace Executor */