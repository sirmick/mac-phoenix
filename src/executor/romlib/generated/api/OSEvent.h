/*****************************************************************************
 *                                                                           *
 *                                 OSEvent.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    QElemPtr, INTEGER, LONGINT, OSErr, Boolean, QHdrPtr,    *
 *                   Ptr                                                     *
 *     EventMgr.h    EventRecord                                             *
 *     PPC.h         PPCPortRec, LocationNameRec                             *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "EventMgr.h"
#include "PPC.h"

#define MODULE_NAME OSEvent
#include <base/api-module.h>


namespace Executor {

enum {evtNotEnb = 1,
};

struct EvQEl{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> evtQWhat;GUEST<LONGINT> evtQMessage;GUEST<LONGINT> evtQWhen;GUEST<Point> evtQWhere;GUEST<INTEGER> evtQModifiers;};static_assert(sizeof(EvQEl) == 22);void describeStruct(const EvQEl&, std::ostream&);

typedef EvQEl *EvQElPtr;

enum {osEvt = 15,
SUSPENDRESUMEBITS = 16777216,
SUSPEND = 0 << 0,
RESUME = 1 << 0,
CONVERTCLIPBOARD = 1 << 1,
mouseMovedMessage = 250,
};

typedef enum {SZreserved0 = 1 << 15,
SZacceptSuspendResumeEvents = 1 << 14,
SZreserved1 = 1 << 13,
SZcanBackground = 1 << 12,
SZdoesActivateOnFGSwitch = 1 << 11,
SZonlyBackground = 1 << 10,
SZgetFrontClicks = 1 << 9,
SZAcceptAppDiedEvents = 1 << 8,
SZis32BitCompatible = 1 << 7,
SZisHighLevelEventAware = 1 << 6,
SZlocalAndRemoveHLEvents = 1 << 5,
SZisStationeryAware = 1 << 4,
SZuseTextEditServices = 1 << 3,
SZreserved2 = 1 << 2,
SZreserved3 = 1 << 1,
SZreserved4 = 1 << 0,
}SZ_t;

struct SIZEResource{GUEST_STRUCT;GUEST<int16_t> size_flags;GUEST<int32_t> preferred_size;GUEST<int32_t> minimum_size;};void describeStruct(const SIZEResource&, std::ostream&);

BEGIN_EXECUTOR_ONLY
/* executor-only struct */
typedef struct size_info
{
    int16_t size_flags;
    int32_t preferred_size;
    int32_t minimum_size;
    bool size_resource_present_p;
    bool application_p;
} size_info_t;

extern size_info_t size_info;
END_EXECUTOR_ONLY


// OSEvent SysEqu.a (true-b);
const LowMemGlobal<INTEGER > monkeylives{ 0x100 };
inline INTEGER  LMGetmonkeylives() { return LM(monkeylives); }
inline void LMSetmonkeylives(INTEGER val) { LM(monkeylives) = val; }
NOTRAP_FUNCTION2(LMGetmonkeylives);
NOTRAP_FUNCTION2(LMSetmonkeylives);


// OSEvent IMII-70 (true);
const LowMemGlobal<INTEGER > SysEvtMask{ 0x144 };
inline INTEGER  LMGetSysEvtMask() { return LM(SysEvtMask); }
inline void LMSetSysEvtMask(INTEGER val) { LM(SysEvtMask) = val; }
NOTRAP_FUNCTION2(LMGetSysEvtMask);
NOTRAP_FUNCTION2(LMSetSysEvtMask);


// OSEvent IMII-71 (true);
const LowMemGlobal<QHdr > EventQueue{ 0x14A };
inline void LMGetEventQueue(QHdr *val) { *val = LM(EventQueue); }
inline void LMSetEventQueue(const QHdr *val) { LM(EventQueue) = *val; }
NOTRAP_FUNCTION2(LMGetEventQueue);
NOTRAP_FUNCTION2(LMSetEventQueue);


// OSEvent IMI-260 (true);
const LowMemGlobal<ULONGINT > Ticks{ 0x16A };
inline ULONGINT  LMGetTicks() { return LM(Ticks); }
inline void LMSetTicks(ULONGINT val) { LM(Ticks) = val; }
NOTRAP_FUNCTION2(LMGetTicks);
NOTRAP_FUNCTION2(LMSetTicks);


// EventMgr PegLeg (True-b);
const LowMemGlobal<Byte > MBState{ 0x172 };
inline Byte  LMGetMBState() { return LM(MBState); }
inline void LMSetMBState(Byte val) { LM(MBState) = val; }
NOTRAP_FUNCTION2(LMGetMBState);
NOTRAP_FUNCTION2(LMSetMBState);


// QuickDraw PegLeg (True-b);
const LowMemGlobal<Point > MTemp{ 0x828 };
inline Point  LMGetMTemp() { return LM(MTemp); }
inline void LMSetMTemp(Point val) { LM(MTemp) = val; }
NOTRAP_FUNCTION2(LMGetMTemp);
NOTRAP_FUNCTION2(LMSetMTemp);


// QuickDraw Vamp (true);
const LowMemGlobal<Point > MouseLocation{ 0x82C };
inline Point  LMGetMouseLocation() { return LM(MouseLocation); }
inline void LMSetMouseLocation(Point val) { LM(MouseLocation) = val; }
NOTRAP_FUNCTION2(LMGetMouseLocation);
NOTRAP_FUNCTION2(LMSetMouseLocation);


// QuickDraw MacAttack (true);
const LowMemGlobal<Point > MouseLocation2{ 0x830 };
inline Point  LMGetMouseLocation2() { return LM(MouseLocation2); }
inline void LMSetMouseLocation2(Point val) { LM(MouseLocation2) = val; }
NOTRAP_FUNCTION2(LMGetMouseLocation2);
NOTRAP_FUNCTION2(LMSetMouseLocation2);


BEGIN_EXECUTOR_ONLY
namespace callconv
{
struct D0Minus1Boolean
{
    operator bool() { return EM_D0 == 0; }

    static void set(bool b)
    {
        EM_D0 = b ? 0 : -1;
    }

    template<class T>
    static void afterCall(T) {}

};
}
END_EXECUTOR_ONLY


OSErr PPostEvent(INTEGER evcode, LONGINT evmsg, GUEST<EvQElPtr> *qelp);

OSErr PostEvent(INTEGER evcode, LONGINT evmsg);

void FlushEvents(INTEGER evmask, INTEGER stopmask);REGISTER_TRAP2(FlushEvents, 0xA032, void(D0LowWord, D0HighWord), ClearD0);


Boolean GetOSEvent(INTEGER evmask, EventRecord *eventp);REGISTER_TRAP2(GetOSEvent, 0xA031, D0Minus1Boolean(D0, A0));


Boolean OSEventAvail(INTEGER evmask, EventRecord *eventp);REGISTER_TRAP2(OSEventAvail, 0xA030, D0Minus1Boolean(D0, A0));


void SetEventMask(INTEGER evmask);

QHdrPtr GetEvQHdr();

EvQEl* geteventelem();

struct TargetID{GUEST_STRUCT;GUEST<int32_t> sessionID;PPCPortRec name;LocationNameRec location;PPCPortRec recvrName;};static_assert(sizeof(TargetID) == 252);void describeStruct(const TargetID&, std::ostream&);

typedef TargetID TargetIDPtr;

struct HighLevelEventMsg{GUEST_STRUCT;GUEST<int16_t> HighLevelEventMsgHeaderlength;GUEST<int16_t> version;GUEST<int32_t> reserved1;EventRecord theMsgEvent;GUEST<int32_t> userRefCon;GUEST<int32_t> postingOptions;GUEST<int32_t> msgLength;};static_assert(sizeof(HighLevelEventMsg) == 36);void describeStruct(const HighLevelEventMsg&, std::ostream&);

typedef HighLevelEventMsg *HighLevelEventMsgPtr;

using GetSpecificFilterUPP = UPP<Boolean(Ptr , HighLevelEventMsgPtr , TargetID *)>;


enum {noOutstandingHLE = -608, // MacPhoenix: was -607, swapped with bufferIsSmall (Universal
// Interfaces MacErrors.h: bufferIsSmall -607, noOutstandingHLE -608).
bufferIsSmall = -607,
};

EXTERN_DISPATCHER_TRAP(OSDispatch, 0xA88F, StackW);
OSErr C_AcceptHighLevelEvent(TargetID *sender_id_return, GUEST<int32_t> *refcon_return, Ptr msg_buf, GUEST<int32_t> *msg_length_return);PASCAL_SUBTRAP(AcceptHighLevelEvent, 0xA88F, 0x0033, OSDispatch);


Boolean C_GetSpecificHighLevelEvent(GetSpecificFilterUPP fn, Ptr data, GUEST<OSErr> *err_return);PASCAL_SUBTRAP(GetSpecificHighLevelEvent, 0xA88F, 0x0045, OSDispatch);


OSErr C_PostHighLevelEvent(EventRecord *evt, Ptr receiver_id, int32_t refcon, Ptr msg_buf, int32_t msg_length, int32_t post_options);PASCAL_SUBTRAP(PostHighLevelEvent, 0xA88F, 0x0034, OSDispatch);


} /* namespace Executor */