/*****************************************************************************
 *                                                                           *
 *                                ProcessMgr.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     INTEGER, LONGINT, StringPtr, OSType, Ptr, OSErr,       *
 *                    Boolean, Handle, ConstStringPtr                        *
 *     FileMgr.h      FSSpec, FSSpecPtr                                      *
 *     WindowMgr.h    WindowPtr                                              *
 *     PPC.h          PPCPortPtr                                             *
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
#include "PPC.h"

#define MODULE_NAME ProcessMgr
#include <base/api-module.h>


namespace Executor {

typedef INTEGER LaunchFlags;

struct ProcessSerialNumber{GUEST_STRUCT;GUEST<uint32_t> highLongOfPSN;GUEST<uint32_t> lowLongOfPSN;};static_assert(sizeof(ProcessSerialNumber) == 8);void describeStruct(const ProcessSerialNumber&, std::ostream&);

struct ROMlib_AppParameters_t{GUEST_STRUCT;GUEST<uint32_t> magic;GUEST<INTEGER> n_fsspec;FSSpec fsspec[0];};void describeStruct(const ROMlib_AppParameters_t&, std::ostream&);

// chosen from /dev/random
enum {APP_PARAMS_MAGIC = 3594734107,
};

typedef ROMlib_AppParameters_t *AppParametersPtr;

struct LaunchParamBlockRec{GUEST_STRUCT;GUEST<LONGINT> reserved1;GUEST<INTEGER> reserved2;GUEST<INTEGER> launchBlockID;GUEST<LONGINT> launchEPBLength;GUEST<INTEGER> launchFileFlags;GUEST<LaunchFlags> launchControlFlags;GUEST<FSSpecPtr> launchAppSpec;ProcessSerialNumber launchProcessSN;GUEST<LONGINT> launchPreferredSize;GUEST<LONGINT> launchMinimumSize;GUEST<LONGINT> launchAvailableSize;GUEST<AppParametersPtr> launchAppParameters;};static_assert(sizeof(LaunchParamBlockRec) == 44);void describeStruct(const LaunchParamBlockRec&, std::ostream&);

enum {extendedBlock = 19523,
};

enum {extendedBlockLen = sizeof(LaunchParamBlockRec) - 12,
};

enum {launchContinue = 16384,
launchNoFileFlags = 2048,
};

struct ProcessInfoRec{GUEST_STRUCT;GUEST<uint32_t> processInfoLength;GUEST<StringPtr> processName;ProcessSerialNumber processNumber;GUEST<uint32_t> processType;GUEST<OSType> processSignature;GUEST<uint32_t> processMode;GUEST<Ptr> processLocation;GUEST<uint32_t> processSize;GUEST<uint32_t> processFreeMem;ProcessSerialNumber processLauncher;GUEST<uint32_t> processLaunchDate;GUEST<uint32_t> processActiveTime;GUEST<FSSpecPtr> processAppSpec;};static_assert(sizeof(ProcessInfoRec) == 60);void describeStruct(const ProcessInfoRec&, std::ostream&);

typedef ProcessInfoRec *ProcessInfoPtr;

// flags for the `processMode' field of the `ProcessInformationRec'
//    record
enum {modeDeskAccessory = 131072,
modeMultiLaunch = 65536,
modeNeedSuspendResume = 16384,
modeCanBackground = 4096,
modeDoesActivateOnFGSwitch = 2048,
modeOnlyBackground = 1024,
modeGetFrontClicks = 512,
modeGetAppDiedMsg = 256,
mode32BitCompatible = 128,
modeHighLevelEventAware = 64,
modeLocalAndRemoteHLEvents = 32,
modeStationeryAware = 16,
modeUseTextEditServices = 8,
};

enum {kNoProcess = 0,
kSystemProcess = 1,
kCurrentProcess = 2,
};

enum {procNotFound = -600,
};

BEGIN_EXECUTOR_ONLY
extern void process_create(bool desk_accessory_p,
                           uint32_t type, uint32_t signature);
END_EXECUTOR_ONLY


// The first caller (Finder) takes the Process Manager's desk layer: a window-shaped port whose visRgn is the desktop (GrayRgn), and gets protocolErr after that. Finder adopts its regions for its desktop window and SysErrors(41) unless this returns noErr.
EXTERN_DISPATCHER_TRAP(OSDispatch, 0xA88F, StackW);
OSErr C_TakeDesktopLayer(GUEST<GUEST<WindowPtr>> *layer);PASCAL_SUBTRAP(TakeDesktopLayer, 0xA88F, 0x0014, OSDispatch);


// Apple's links {next, proc, refCon, current PSN} into a Process Manager list (PM globals -$136); Finder registers one of its routines.
OSErr C_AddProcessCallback(Ptr proc, int32_t refCon);PASCAL_SUBTRAP(AddProcessCallback, 0xA88F, 0x0042, OSDispatch);


// Undoes AddProcessCallback (name is a guess): Finder passes the same proc and refCon as it did to $42 when it quits (restart, shut down).
OSErr C_RemoveProcessCallback(Ptr proc, int32_t refCon);PASCAL_SUBTRAP(RemoveProcessCallback, 0xA88F, 0x0043, OSDispatch);


// Either pointer may be nil. Apple's fills them from HeapDispatch $19 and $18 (Process Manager heap); which one is the total free and which the largest block is our best reading. Finder checks memory with it.
OSErr C_GetTempMemInfo(GUEST<GUEST<int32_t>> *freeBytes, GUEST<GUEST<int32_t>> *maxBlock);PASCAL_SUBTRAP(GetTempMemInfo, 0xA88F, 0x0047, OSDispatch);


// Stores a long in the current process's record (+$15C in Apple's); Finder passes its drag-hook record (see DragDispatch $C301).
OSErr C_SetProcessDragHooks(int32_t hooks);PASCAL_SUBTRAP(SetProcessDragHooks, 0xA88F, 0x0055, OSDispatch);


// MacPhoenix (name is a guess): 7.5.5's OSDispatch $64, the window
// under a point among the Text Services Manager's floating input
// windows (TextServicesDispatch 23 when the TSM is present, else 0).
// Apple's Help Manager asks before it shows a window's balloon. None
// here (textservices.cpp).
INTEGER C_FindInputWindow(Point pt, GUEST<WindowPtr> *window);PASCAL_SUBTRAP(FindInputWindow, 0xA88F, 0x0064, OSDispatch);


// General Controls' driver passes its "Show Desktop when in background" setting, inverted (name is a guess; learned.yaml).  Apple's stores the byte in its globals and, when it turns on while another process is in front, hides the desk owner's layer.  Executor only keeps the flag.
OSErr C_SetHideDesktopInBackground(Boolean hide);PASCAL_SUBTRAP(SetHideDesktopInBackground, 0xA88F, 0x005E, OSDispatch);


// Finder feeds the Apple Menu Items folder to the Process Manager, which draws the Apple menu (22 bytes of params; System 'scod' -16463 +3020). name must be non-empty and key nonzero, else paramErr. The entry keeps the name behind two placeholder bytes (the icon's room), the suite, the key, sortGroup (passed to the sort comparison), the low bytes of flagA/flagB and a copy of info. Names other than name, iconSuite and key are guesses.
OSErr C_AddAppleMenuItem(StringPtr name, int16_t sortGroup, int16_t flagA, Handle iconSuite, int16_t flagB, StringPtr info, int32_t key);PASCAL_SUBTRAP(AddAppleMenuItem, 0xA88F, 0x0031, OSDispatch);


// Removes the caller's items with that key (0: all of them).
OSErr C_RemoveAppleMenuItems(int32_t key);PASCAL_SUBTRAP(RemoveAppleMenuItems, 0xA88F, 0x0032, OSDispatch);


OSErr C_GetCurrentProcess(ProcessSerialNumber *serial_number);PASCAL_SUBTRAP(GetCurrentProcess, 0xA88F, 0x0037, OSDispatch);


OSErr C_GetNextProcess(ProcessSerialNumber *serial_number);PASCAL_SUBTRAP(GetNextProcess, 0xA88F, 0x0038, OSDispatch);


OSErr C_GetProcessInformation(ProcessSerialNumber *serial_number, ProcessInfoPtr info);PASCAL_SUBTRAP(GetProcessInformation, 0xA88F, 0x003A, OSDispatch);


OSErr C_SameProcess(ProcessSerialNumber *serial_number0, ProcessSerialNumber *serial_number1, Boolean *same_out);PASCAL_SUBTRAP(SameProcess, 0xA88F, 0x003D, OSDispatch);


// The inline pushes a long -1 after the PSN pointer (Processes.h: moveq #-1,d0; move.l d0,-(sp)); dummy is that long. Apple's (scod -16463, RAM $29E94 on the reference boot) takes anything else as a layer or window and answers the process owning it (procNotFound if none): the Drag Manager finds a drop target's process that way.
OSErr C_GetFrontProcess(ProcessSerialNumber *serial_number, int32_t dummy);PASCAL_SUBTRAP(GetFrontProcess, 0xA88F, 0x0039, OSDispatch);


// MacPhoenix (name is a guess; Apple's at RAM $2874A). The layer whose window is under the point, searched from the root (FindWindowInLayer with the root current), nil if none. The Drag Manager's FinderTrackDrag starts from it; the System's GetNextEvent patch routes clicks with it.
WindowPtr C_GetLayerAtPoint(Point pt);PASCAL_SUBTRAP(GetLayerAtPoint, 0xA88F, 0x000F, OSDispatch);


// MacPhoenix (name is a guess; Apple's at RAM $2A386 counts up a word in its globals, $1A counts it down). No parameters, no result. The System's StillDown and WaitMouseUp patches bracket every poll with the pair, and the Drag Manager its tracking loop: no process switch while held.
void C_HoldProcessSwitching();PASCAL_SUBTRAP(HoldProcessSwitching, 0xA88F, 0x0019, OSDispatch);


void C_ReleaseProcessSwitching();PASCAL_SUBTRAP(ReleaseProcessSwitching, 0xA88F, 0x001A, OSDispatch);


// MacPhoenix (name is a guess; Apple's at RAM $2A78C, the call itself at $2B582). Runs a Pascal routine in another process: its world (A5, low memory, stack) switched in, paramSize bytes of parameters pushed as a stack image, the routine's word result returned. With paramSize of 4 or less, params is the image itself (a long pushed by value); otherwise it points at the image. procNotFound for the current process. The Drag Manager runs a drop target's tracking and receive handlers through it.
OSErr C_CallProcessRoutine(ProcessSerialNumber *psn, ProcPtr routine, int32_t params, int16_t paramSize);PASCAL_SUBTRAP(CallProcessRoutine, 0xA88F, 0x0053, OSDispatch);


// MacPhoenix (name is a guess; Apple's at RAM $2A2E0). The process that took the desktop layer (TakeDesktopLayer: Finder), -603 if none. The Drag Manager's drop target for a point on the desktop.
OSErr C_GetDesktopProcess(ProcessSerialNumber *psn);PASCAL_SUBTRAP(GetDesktopProcess, 0xA88F, 0x0056, OSDispatch);


// MacPhoenix (name is a guess; Apple's at RAM $28BB0). The process owning a window, procNotFound if none.
OSErr C_GetWindowProcess(WindowPtr window, ProcessSerialNumber *psn);PASCAL_SUBTRAP(GetWindowProcess, 0xA88F, 0x0062, OSDispatch);


OSErr C_SetFrontProcess(ProcessSerialNumber *serial_number);PASCAL_SUBTRAP(SetFrontProcess, 0xA88F, 0x003B, OSDispatch);


OSErr C_WakeUpProcess(ProcessSerialNumber *serial_number);PASCAL_SUBTRAP(WakeUpProcess, 0xA88F, 0x003C, OSDispatch);


OSErr C_GetProcessSerialNumberFromPortName(PPCPortPtr port_name, ProcessSerialNumber *serial_number);PASCAL_SUBTRAP(GetProcessSerialNumberFromPortName, 0xA88F, 0x0035, OSDispatch);


OSErr C_LaunchDeskAccessory(const FSSpec *pFileSpec, ConstStringPtr pDAName);PASCAL_SUBTRAP(LaunchDeskAccessory, 0xA88F, 0x0036, OSDispatch);


OSErr C_BeginSystemMode();PASCAL_SUBTRAP(BeginSystemMode, 0xA88F, 0x0040, OSDispatch);


OSErr C_EndSystemMode();PASCAL_SUBTRAP(EndSystemMode, 0xA88F, 0x0041, OSDispatch);


OSErr C_GetPortNameFromProcessSerialNumber(PPCPortPtr port_name, ProcessSerialNumber *serial_number);PASCAL_SUBTRAP(GetPortNameFromProcessSerialNumber, 0xA88F, 0x0046, OSDispatch);


OSErr LaunchApplication(LaunchParamBlockRec *params);REGISTER_TRAP2(LaunchApplication, 0xA9F2, D0(A0));


BEGIN_EXECUTOR_ONLY
extern OSErr NewLaunch(ConstStringPtr appl, INTEGER vrefnum,
                       LaunchParamBlockRec *lpbp);
END_EXECUTOR_ONLY


} /* namespace Executor */