/*****************************************************************************
 *                                                                           *
 *                                 DragMgr.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h       Ptr, UInt32, OSType, INTEGER, UInt16, OSErr,         *
 *                      Boolean, Size, Rect                                  *
 *     FileMgr.h        FSSpec                                               *
 *     WindowMgr.h      WindowRef                                            *
 *     QuickDraw.h      RgnHandle, RGBColor, PixMapHandle                    *
 *     EventMgr.h       EventRecord                                          *
 *     AppleEvents.h    AEDesc                                               *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "FileMgr.h"
#include "WindowMgr.h"
#include "QuickDraw.h"
#include "EventMgr.h"
#include "AppleEvents.h"

#define MODULE_NAME DragMgr
#include <base/api-module.h>


namespace Executor {

// A drag.  Opaque: allocated by NewDrag, or handed to a handler by
// the Drag Manager.
typedef Ptr DragRef;

// One item within a drag.  Not an index: GetDragItemReferenceNumber
// turns a one-based index into one of these.
typedef UInt32 DragItemRef;

typedef OSType FlavorType;

typedef UInt32 DragAttributes;

typedef UInt32 DragBehaviors;

typedef UInt32 DragImageFlags;

typedef UInt32 FlavorFlags;

typedef INTEGER DragTrackingMessage;

typedef INTEGER DragRegionMessage;

typedef INTEGER ZoomAcceleration;

typedef DragRef DragReference;

typedef DragItemRef ItemReference;

enum {kDragHasLeftSenderWindow = 1,
kDragInsideSenderApplication = 2,
kDragInsideSenderWindow = 4,
};

enum {kDragBehaviorNone = 0,
kDragBehaviorZoomBackAnimation = 1,
};

enum {kDragRegionAndImage = 16,
};

enum {kDragStandardTranslucency = 0,
kDragDarkTranslucency = 1,
kDragDarkerTranslucency = 2,
kDragOpaqueTranslucency = 3,
};

enum {flavorSenderOnly = 1,
flavorSenderTranslated = 2,
flavorNotSaved = 4,
flavorSystemTranslated = 256,
};

enum {flavorTypeHFS = "hfs "_4,
flavorTypePromiseHFS = "phfs"_4,
flavorTypeDirectory = "diry"_4,
};

enum {kFlavorTypeClippingName = "clnm"_4,
kFlavorTypeClippingFilename = "clfn"_4,
kFlavorTypeDragToTrashOnly = "fdtt"_4,
kFlavorTypeFinderNoTrackingBehavior = "fntb"_4,
};

enum {kDragTrackingEnterHandler = 1,
kDragTrackingEnterWindow = 2,
kDragTrackingInWindow = 3,
kDragTrackingLeaveWindow = 4,
kDragTrackingLeaveHandler = 5,
};

enum {kDragRegionBegin = 1,
kDragRegionDraw = 2,
kDragRegionHide = 3,
kDragRegionIdle = 4,
kDragRegionEnd = 5,
};

enum {kZoomNoAcceleration = 0,
kZoomAccelerate = 1,
kZoomDecelerate = 2,
};

// The Drag Manager's own errors.  They live in Apple's MacErrors.h
// rather than in Drag.h, but nothing else in this repository is
// claiming that range, and a header whose calls return codes it does
// not name is half a header.
enum {badDragRefErr = -1850,
badDragItemErr = -1851,
badDragFlavorErr = -1852,
duplicateFlavorErr = -1853,
cantGetFlavorErr = -1854,
duplicateHandlerErr = -1855,
handlerNotFoundErr = -1856,
dragNotAcceptedErr = -1857,
unsupportedForPlatformErr = -1858,
noSuitableDisplaysErr = -1859,
badImageRgnErr = -1860,
badImageErr = -1861,
};

// The data behind a flavorTypeHFS flavor: an existing file, given as
// an FSSpec.  This is what the Finder puts in a drag when what is
// being dragged is a file.
struct HFSFlavor{GUEST_STRUCT;GUEST<OSType> fileType;GUEST<OSType> fileCreator;GUEST<UInt16> fdFlags;FSSpec fileSpec;};static_assert(sizeof(HFSFlavor) == 80);void describeStruct(const HFSFlavor&, std::ostream&);

struct PromiseHFSFlavor{GUEST_STRUCT;GUEST<OSType> fileType;GUEST<OSType> fileCreator;GUEST<UInt16> fdFlags;GUEST<FlavorType> promisedFlavor;};static_assert(sizeof(PromiseHFSFlavor) == 14);void describeStruct(const PromiseHFSFlavor&, std::ostream&);

using DragTrackingHandlerUPP = UPP<OSErr(DragTrackingMessage message, WindowRef theWindow, void *handlerRefCon, DragRef theDrag)>;


using DragReceiveHandlerUPP = UPP<OSErr(WindowRef theWindow, void *handlerRefCon, DragRef theDrag)>;


using DragSendDataUPP = UPP<OSErr(FlavorType theType, void *dragSendRefCon, DragItemRef theItemRef, DragRef theDrag)>;


using DragInputUPP = UPP<OSErr(GUEST<Point> *mouse, GUEST<INTEGER> *modifiers, void *dragInputRefCon, DragRef theDrag)>;


using DragDrawingUPP = UPP<OSErr(DragRegionMessage message, RgnHandle showRegion, Point showOrigin, RgnHandle hideRegion, Point hideOrigin, void *dragDrawingRefCon, DragRef theDrag)>;


DISPATCHER_TRAP(DragDispatch, 0xABED, D0W);


OSErr InstallTrackingHandler(DragTrackingHandlerUPP trackingHandler, WindowRef theWindow, void *handlerRefCon);TRAP_GLUE(DragLib, InstallTrackingHandler, DragDispatch, 0x0001);


OSErr InstallReceiveHandler(DragReceiveHandlerUPP receiveHandler, WindowRef theWindow, void *handlerRefCon);TRAP_GLUE(DragLib, InstallReceiveHandler, DragDispatch, 0x0002);


OSErr RemoveTrackingHandler(DragTrackingHandlerUPP trackingHandler, WindowRef theWindow);TRAP_GLUE(DragLib, RemoveTrackingHandler, DragDispatch, 0x0003);


OSErr RemoveReceiveHandler(DragReceiveHandlerUPP receiveHandler, WindowRef theWindow);TRAP_GLUE(DragLib, RemoveReceiveHandler, DragDispatch, 0x0004);


// MacPhoenix. Private in 7.5.5 (name is our guess). Finder registers a 7-long record (its hook routine, a window, ..., a refCon at +$10) with the System's Drag Manager at start-up, install = true, and removes it at quit (install = false). Apple's routine ('proc' -16516 routine $80) installs the globals' two trampolines as the window's tracking and receive handlers with the record {hook, refCon}; the trampolines call the hook with (message, globals+4). With Apple's Drag Manager in charge of the trap (drag.cpp) this entry is not reached; the C++ stub stays for Executor's own System.
OSErr C_SetFinderDragHooks(Ptr hooks, Boolean install);PASCAL_SUBTRAP(SetFinderDragHooks, 0xABED, 0xC301, DragDispatch);


OSErr NewDrag(GUEST<DragRef> *theDrag);TRAP_GLUE(DragLib, NewDrag, DragDispatch, 0x0005);


OSErr DisposeDrag(DragRef theDrag);TRAP_GLUE(DragLib, DisposeDrag, DragDispatch, 0x0006);


OSErr AddDragItemFlavor(DragRef theDrag, DragItemRef theItemRef, FlavorType theType, const void *dataPtr, Size dataSize, FlavorFlags theFlags);TRAP_GLUE(DragLib, AddDragItemFlavor, DragDispatch, 0x0007);


OSErr SetDragItemFlavorData(DragRef theDrag, DragItemRef theItemRef, FlavorType theType, const void *dataPtr, Size dataSize, UInt32 dataOffset);TRAP_GLUE(DragLib, SetDragItemFlavorData, DragDispatch, 0x0009);


OSErr SetDragSendProc(DragRef theDrag, DragSendDataUPP sendProc, void *dragSendRefCon);TRAP_GLUE(DragLib, SetDragSendProc, DragDispatch, 0x000A);


OSErr SetDragInputProc(DragRef theDrag, DragInputUPP inputProc, void *dragInputRefCon);TRAP_GLUE(DragLib, SetDragInputProc, DragDispatch, 0x000B);


OSErr SetDragDrawingProc(DragRef theDrag, DragDrawingUPP drawingProc, void *dragDrawingRefCon);TRAP_GLUE(DragLib, SetDragDrawingProc, DragDispatch, 0x000C);


OSErr TrackDrag(DragRef theDrag, const EventRecord *theEvent, RgnHandle theRegion);TRAP_GLUE(DragLib, TrackDrag, DragDispatch, 0x000D);


OSErr CountDragItems(DragRef theDrag, GUEST<UInt16> *numItems);TRAP_GLUE(DragLib, CountDragItems, DragDispatch, 0x000E);


OSErr GetDragItemReferenceNumber(DragRef theDrag, UInt16 index, GUEST<DragItemRef> *theItemRef);TRAP_GLUE(DragLib, GetDragItemReferenceNumber, DragDispatch, 0x000F);


OSErr CountDragItemFlavors(DragRef theDrag, DragItemRef theItemRef, GUEST<UInt16> *numFlavors);TRAP_GLUE(DragLib, CountDragItemFlavors, DragDispatch, 0x0010);


OSErr GetFlavorType(DragRef theDrag, DragItemRef theItemRef, UInt16 index, GUEST<FlavorType> *theType);TRAP_GLUE(DragLib, GetFlavorType, DragDispatch, 0x0011);


OSErr GetFlavorFlags(DragRef theDrag, DragItemRef theItemRef, FlavorType theType, GUEST<FlavorFlags> *theFlags);TRAP_GLUE(DragLib, GetFlavorFlags, DragDispatch, 0x0012);


OSErr GetFlavorDataSize(DragRef theDrag, DragItemRef theItemRef, FlavorType theType, GUEST<Size> *dataSize);TRAP_GLUE(DragLib, GetFlavorDataSize, DragDispatch, 0x0013);


OSErr GetFlavorData(DragRef theDrag, DragItemRef theItemRef, FlavorType theType, void *dataPtr, GUEST<Size> *dataSize, UInt32 dataOffset);TRAP_GLUE(DragLib, GetFlavorData, DragDispatch, 0x0014);


OSErr GetDragItemBounds(DragRef theDrag, DragItemRef theItemRef, Rect *itemBounds);TRAP_GLUE(DragLib, GetDragItemBounds, DragDispatch, 0x0015);


OSErr SetDragItemBounds(DragRef theDrag, DragItemRef theItemRef, const Rect *itemBounds);TRAP_GLUE(DragLib, SetDragItemBounds, DragDispatch, 0x0016);


OSErr GetDropLocation(DragRef theDrag, AEDesc *dropLocation);TRAP_GLUE(DragLib, GetDropLocation, DragDispatch, 0x0017);


OSErr SetDropLocation(DragRef theDrag, const AEDesc *dropLocation);TRAP_GLUE(DragLib, SetDropLocation, DragDispatch, 0x0018);


OSErr GetDragAttributes(DragRef theDrag, GUEST<DragAttributes> *flags);TRAP_GLUE(DragLib, GetDragAttributes, DragDispatch, 0x0019);


OSErr GetDragMouse(DragRef theDrag, GUEST<Point> *mouse, GUEST<Point> *globalPinnedMouse);TRAP_GLUE(DragLib, GetDragMouse, DragDispatch, 0x001A);


OSErr SetDragMouse(DragRef theDrag, Point globalPinnedMouse);TRAP_GLUE(DragLib, SetDragMouse, DragDispatch, 0x001B);


OSErr GetDragOrigin(DragRef theDrag, GUEST<Point> *globalInitialMouse);TRAP_GLUE(DragLib, GetDragOrigin, DragDispatch, 0x001C);


OSErr GetDragModifiers(DragRef theDrag, GUEST<INTEGER> *modifiers, GUEST<INTEGER> *mouseDownModifiers, GUEST<INTEGER> *mouseUpModifiers);TRAP_GLUE(DragLib, GetDragModifiers, DragDispatch, 0x001D);


OSErr ShowDragHilite(DragRef theDrag, RgnHandle hiliteFrame, Boolean inside);TRAP_GLUE(DragLib, ShowDragHilite, DragDispatch, 0x001E);


OSErr HideDragHilite(DragRef theDrag);TRAP_GLUE(DragLib, HideDragHilite, DragDispatch, 0x001F);


OSErr DragPreScroll(DragRef theDrag, INTEGER dH, INTEGER dV);TRAP_GLUE(DragLib, DragPreScroll, DragDispatch, 0x0020);


OSErr DragPostScroll(DragRef theDrag);TRAP_GLUE(DragLib, DragPostScroll, DragDispatch, 0x0021);


OSErr UpdateDragHilite(DragRef theDrag, RgnHandle updateRgn);TRAP_GLUE(DragLib, UpdateDragHilite, DragDispatch, 0x0022);


Boolean WaitMouseMoved(Point initialMouse);TRAP_GLUE(DragLib, WaitMouseMoved, DragDispatch, 0x0023);


OSErr ZoomRects(const Rect *fromRect, const Rect *toRect, INTEGER zoomSteps, ZoomAcceleration acceleration);TRAP_GLUE(DragLib, ZoomRects, DragDispatch, 0x0024);


OSErr ZoomRegion(RgnHandle region, Point zoomDistance, INTEGER zoomSteps, ZoomAcceleration acceleration);TRAP_GLUE(DragLib, ZoomRegion, DragDispatch, 0x0025);


OSErr GetDragHiliteColor(WindowRef window, RGBColor *color);TRAP_GLUE(DragLib, GetDragHiliteColor, DragDispatch, 0x0026);


OSErr SetDragImage(DragRef theDrag, PixMapHandle imagePixMap, RgnHandle imageRgn, Point imageOffsetPt, DragImageFlags theImageFlags);TRAP_GLUE(DragLib, SetDragImage, DragDispatch, 0x0027);


OSErr ChangeDragBehaviors(DragRef theDrag, DragBehaviors inBehaviorsToSet, DragBehaviors inBehaviorsToClear);TRAP_GLUE(DragLib, ChangeDragBehaviors, DragDispatch, 0x0028);


} /* namespace Executor */