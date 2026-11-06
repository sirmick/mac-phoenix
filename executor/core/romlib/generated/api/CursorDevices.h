/*****************************************************************************
 *                                                                           *
 *                              CursorDevices.h                              *
 *                              ===============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSType, Fixed, UInt8, OSErr                             *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME CursorDevices
#include <base/api-module.h>


namespace Executor {

struct CursorData;

struct CursorDevice{GUEST_STRUCT;GUEST<CursorDevice*> nextCursorDevice;GUEST<CursorData*> whichCursor;GUEST<int32_t> refCon;GUEST<int32_t> unused;GUEST<OSType> devID;GUEST<Fixed> resolution;UInt8 devClass;UInt8 cntButtons;UInt8 filler1;UInt8 buttons;UInt8 buttonOp[8];GUEST<uint32_t> buttonTicks[8];GUEST<int32_t> buttonData[8];GUEST<uint32_t> doubleClickTime;GUEST<Fixed> acceleration;GUEST<short> privateFields[15];};static_assert(sizeof(CursorDevice) == 138);void describeStruct(const CursorDevice&, std::ostream&);

typedef CursorDevice *CursorDevicePtr;

typedef short ButtonOpcode;

enum {kButtonNoOp = 0,
kButtonSingleClick = 1,
kButtonDoubleClick = 2,
kButtonClickLock = 3,
kButtonCustom = 6,
};

enum {kDeviceClassAbsolute = 0,
kDeviceClassMouse = 1,
kDeviceClassTrackball = 2,
kDeviceClassTrackPad = 3,
kDeviceClass3D = 6,
};

DISPATCHER_TRAP(CursorDeviceDispatch, 0xAADB, D0W);


OSErr C_CursorDeviceMove(CursorDevicePtr ourDevice, int32_t deltaX, int32_t deltaY);PASCAL_SUBTRAP(CursorDeviceMove, 0xAADB, 0x0000, CursorDeviceDispatch);


OSErr C_CursorDeviceMoveTo(CursorDevicePtr ourDevice, int32_t absX, int32_t absY);PASCAL_SUBTRAP(CursorDeviceMoveTo, 0xAADB, 0x0001, CursorDeviceDispatch);


OSErr C_CursorDeviceFlush(CursorDevicePtr ourDevice);PASCAL_SUBTRAP(CursorDeviceFlush, 0xAADB, 0x0002, CursorDeviceDispatch);


OSErr C_CursorDeviceButtons(CursorDevicePtr ourDevice, short buttons);PASCAL_SUBTRAP(CursorDeviceButtons, 0xAADB, 0x0003, CursorDeviceDispatch);


OSErr C_CursorDeviceButtonDown(CursorDevicePtr ourDevice);PASCAL_SUBTRAP(CursorDeviceButtonDown, 0xAADB, 0x0004, CursorDeviceDispatch);


OSErr C_CursorDeviceButtonUp(CursorDevicePtr ourDevice);PASCAL_SUBTRAP(CursorDeviceButtonUp, 0xAADB, 0x0005, CursorDeviceDispatch);


OSErr C_CursorDeviceButtonOp(CursorDevicePtr ourDevice, short buttonNumber, ButtonOpcode opcode, int32_t data);PASCAL_SUBTRAP(CursorDeviceButtonOp, 0xAADB, 0x0006, CursorDeviceDispatch);


OSErr C_CursorDeviceSetButtons(CursorDevicePtr ourDevice, short numberOfButtons);PASCAL_SUBTRAP(CursorDeviceSetButtons, 0xAADB, 0x0007, CursorDeviceDispatch);


OSErr C_CursorDeviceSetAcceleration(CursorDevicePtr ourDevice, Fixed acceleration);PASCAL_SUBTRAP(CursorDeviceSetAcceleration, 0xAADB, 0x0008, CursorDeviceDispatch);


OSErr C_CursorDeviceDoubleTime(CursorDevicePtr ourDevice, int32_t durationTicks);PASCAL_SUBTRAP(CursorDeviceDoubleTime, 0xAADB, 0x0009, CursorDeviceDispatch);


OSErr C_CursorDeviceUnitsPerInch(CursorDevicePtr ourDevice, Fixed resolution);PASCAL_SUBTRAP(CursorDeviceUnitsPerInch, 0xAADB, 0x000A, CursorDeviceDispatch);


OSErr C_CursorDeviceNextDevice(GUEST<CursorDevicePtr> *ourDevice);PASCAL_SUBTRAP(CursorDeviceNextDevice, 0xAADB, 0x000B, CursorDeviceDispatch);


OSErr C_CursorDeviceNewDevice(GUEST<CursorDevicePtr> *ourDevice);PASCAL_SUBTRAP(CursorDeviceNewDevice, 0xAADB, 0x000C, CursorDeviceDispatch);


OSErr C_CursorDeviceDisposeDevice(CursorDevicePtr ourDevice);PASCAL_SUBTRAP(CursorDeviceDisposeDevice, 0xAADB, 0x000D, CursorDeviceDispatch);


OSErr C_CursorDeviceMove(CursorDevicePtr ourDevice, int32_t deltaX, int32_t deltaY);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevMove, C_CursorDeviceMove);


OSErr C_CursorDeviceMoveTo(CursorDevicePtr ourDevice, int32_t absX, int32_t absY);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevMoveTo, C_CursorDeviceMoveTo);


OSErr C_CursorDeviceFlush(CursorDevicePtr ourDevice);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevFlush, C_CursorDeviceFlush);


OSErr C_CursorDeviceButtons(CursorDevicePtr ourDevice, short buttons);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevButtons, C_CursorDeviceButtons);


OSErr C_CursorDeviceButtonDown(CursorDevicePtr ourDevice);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevButtonDown, C_CursorDeviceButtonDown);


OSErr C_CursorDeviceButtonUp(CursorDevicePtr ourDevice);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevButtonUp, C_CursorDeviceButtonUp);


OSErr C_CursorDeviceButtonOp(CursorDevicePtr ourDevice, short buttonNumber, ButtonOpcode opcode, int32_t data);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevButtonOp, C_CursorDeviceButtonOp);


OSErr C_CursorDeviceSetButtons(CursorDevicePtr ourDevice, short numberOfButtons);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevSetButtons, C_CursorDeviceSetButtons);


OSErr C_CursorDeviceSetAcceleration(CursorDevicePtr ourDevice, Fixed acceleration);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevSetAcceleration, C_CursorDeviceSetAcceleration);


OSErr C_CursorDeviceDoubleTime(CursorDevicePtr ourDevice, int32_t durationTicks);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevDoubleTime, C_CursorDeviceDoubleTime);


OSErr C_CursorDeviceUnitsPerInch(CursorDevicePtr ourDevice, Fixed resolution);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevUnitsPerInch, C_CursorDeviceUnitsPerInch);


OSErr C_CursorDeviceNextDevice(GUEST<CursorDevicePtr> *ourDevice);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevNextDevice, C_CursorDeviceNextDevice);


OSErr C_CursorDeviceNewDevice(GUEST<CursorDevicePtr> *ourDevice);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevNewDevice, C_CursorDeviceNewDevice);


OSErr C_CursorDeviceDisposeDevice(CursorDevicePtr ourDevice);LIBRARY_FUNCTION(CursorDevicesLib, CrsrDevDisposeDevice, C_CursorDeviceDisposeDevice);


OSErr C_CursorDeviceMove(CursorDevicePtr ourDevice, int32_t deltaX, int32_t deltaY);LIBRARY_FUNCTION(InterfaceLib, CrsrDevMove, C_CursorDeviceMove);


OSErr C_CursorDeviceMoveTo(CursorDevicePtr ourDevice, int32_t absX, int32_t absY);LIBRARY_FUNCTION(InterfaceLib, CrsrDevMoveTo, C_CursorDeviceMoveTo);


OSErr C_CursorDeviceFlush(CursorDevicePtr ourDevice);LIBRARY_FUNCTION(InterfaceLib, CrsrDevFlush, C_CursorDeviceFlush);


OSErr C_CursorDeviceButtons(CursorDevicePtr ourDevice, short buttons);LIBRARY_FUNCTION(InterfaceLib, CrsrDevButtons, C_CursorDeviceButtons);


OSErr C_CursorDeviceButtonDown(CursorDevicePtr ourDevice);LIBRARY_FUNCTION(InterfaceLib, CrsrDevButtonDown, C_CursorDeviceButtonDown);


OSErr C_CursorDeviceButtonUp(CursorDevicePtr ourDevice);LIBRARY_FUNCTION(InterfaceLib, CrsrDevButtonUp, C_CursorDeviceButtonUp);


OSErr C_CursorDeviceButtonOp(CursorDevicePtr ourDevice, short buttonNumber, ButtonOpcode opcode, int32_t data);LIBRARY_FUNCTION(InterfaceLib, CrsrDevButtonOp, C_CursorDeviceButtonOp);


OSErr C_CursorDeviceSetButtons(CursorDevicePtr ourDevice, short numberOfButtons);LIBRARY_FUNCTION(InterfaceLib, CrsrDevSetButtons, C_CursorDeviceSetButtons);


OSErr C_CursorDeviceSetAcceleration(CursorDevicePtr ourDevice, Fixed acceleration);LIBRARY_FUNCTION(InterfaceLib, CrsrDevSetAcceleration, C_CursorDeviceSetAcceleration);


OSErr C_CursorDeviceDoubleTime(CursorDevicePtr ourDevice, int32_t durationTicks);LIBRARY_FUNCTION(InterfaceLib, CrsrDevDoubleTime, C_CursorDeviceDoubleTime);


OSErr C_CursorDeviceUnitsPerInch(CursorDevicePtr ourDevice, Fixed resolution);LIBRARY_FUNCTION(InterfaceLib, CrsrDevUnitsPerInch, C_CursorDeviceUnitsPerInch);


OSErr C_CursorDeviceNextDevice(GUEST<CursorDevicePtr> *ourDevice);LIBRARY_FUNCTION(InterfaceLib, CrsrDevNextDevice, C_CursorDeviceNextDevice);


OSErr C_CursorDeviceNewDevice(GUEST<CursorDevicePtr> *ourDevice);LIBRARY_FUNCTION(InterfaceLib, CrsrDevNewDevice, C_CursorDeviceNewDevice);


OSErr C_CursorDeviceDisposeDevice(CursorDevicePtr ourDevice);LIBRARY_FUNCTION(InterfaceLib, CrsrDevDisposeDevice, C_CursorDeviceDisposeDevice);


} /* namespace Executor */