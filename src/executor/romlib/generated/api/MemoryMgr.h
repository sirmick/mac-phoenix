/*****************************************************************************
 *                                                                           *
 *                                MemoryMgr.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    LONGINT, Size, Ptr, INTEGER, Handle, OSErr,             *
 *                   SignedByte                                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME MemoryMgr
#include <base/api-module.h>


namespace Executor {

enum {memFullErr = -108,
memLockedErr = -117,
memPurErr = -112,
memWZErr = -111,
};

enum {nilHandleErr = -109,
};

enum {memROZErr = -99,
memAdrErr = -110,
memAZErr = -113,
memPCErr = -114,
memBCErr = -115,
memSCErr = -116,
};

using GrowZoneUPP = UPP<LONGINT(Size )>;


struct block_header_t;

struct Zone{GUEST_STRUCT;GUEST<Ptr> bkLim;GUEST<Ptr> purgePtr;GUEST<Ptr> hFstFree;GUEST<LONGINT> zcbFree;GUEST<GrowZoneUPP> gzProc;GUEST<INTEGER> moreMast;GUEST<INTEGER> flags;GUEST<INTEGER> cntRel;GUEST<INTEGER> maxRel;GUEST<INTEGER> cntNRel;GUEST<INTEGER> maxNRel;GUEST<INTEGER> cntEmpty;GUEST<INTEGER> cntHandles;GUEST<LONGINT> minCBFree;GUEST<ProcPtr> purgeProc;GUEST<Ptr> sparePtr;GUEST<block_header_t*> allocPtr;GUEST<INTEGER> heapData;};static_assert(sizeof(Zone) == 54);void describeStruct(const Zone&, std::ostream&);

typedef Zone *THz;

// MemoryMgr IMII-19 (true);
const LowMemGlobal<Ptr > MemTop{ 0x108 };
inline Ptr  LMGetMemTop() { return LM(MemTop); }
inline void LMSetMemTop(Ptr val) { LM(MemTop) = val; }
NOTRAP_FUNCTION2(LMGetMemTop);
NOTRAP_FUNCTION2(LMSetMemTop);


// MemoryMgr IMII-19 (true-b);
const LowMemGlobal<Ptr > BufPtr{ 0x10C };
inline Ptr  LMGetBufPtr() { return LM(BufPtr); }
inline void LMSetBufPtr(Ptr val) { LM(BufPtr) = val; }
NOTRAP_FUNCTION2(LMGetBufPtr);
NOTRAP_FUNCTION2(LMSetBufPtr);


// MemoryMgr IMII-19 (true);
const LowMemGlobal<Ptr > HeapEnd{ 0x114 };
inline Ptr  LMGetHeapEnd() { return LM(HeapEnd); }
inline void LMSetHeapEnd(Ptr val) { LM(HeapEnd) = val; }
NOTRAP_FUNCTION2(LMGetHeapEnd);
NOTRAP_FUNCTION2(LMSetHeapEnd);


// MemoryMgr IMII-31 (true);
const LowMemGlobal<THz > TheZone{ 0x118 };
inline THz  LMGetTheZone() { return LM(TheZone); }
inline void LMSetTheZone(THz val) { LM(TheZone) = val; }
NOTRAP_FUNCTION2(LMGetTheZone);
NOTRAP_FUNCTION2(LMSetTheZone);


// MemoryMgr IMII-19 (true);
const LowMemGlobal<Ptr > ApplLimit{ 0x130 };
inline Ptr  LMGetApplLimit() { return LM(ApplLimit); }
inline void LMSetApplLimit(Ptr val) { LM(ApplLimit) = val; }
NOTRAP_FUNCTION2(LMGetApplLimit);
NOTRAP_FUNCTION2(LMSetApplLimit);


// MemoryMgr IMIV-80 (true);
const LowMemGlobal<INTEGER > MemErr{ 0x220 };
inline INTEGER  LMGetMemErr() { return LM(MemErr); }
inline void LMSetMemErr(INTEGER val) { LM(MemErr) = val; }
NOTRAP_FUNCTION2(LMGetMemErr);
NOTRAP_FUNCTION2(LMSetMemErr);


// MemoryMgr IMII-19 (true);
const LowMemGlobal<THz > SysZone{ 0x2A6 };
inline THz  LMGetSysZone() { return LM(SysZone); }
inline void LMSetSysZone(THz val) { LM(SysZone) = val; }
NOTRAP_FUNCTION2(LMGetSysZone);
NOTRAP_FUNCTION2(LMSetSysZone);


// MemoryMgr IMII-19 (true);
const LowMemGlobal<THz > ApplZone{ 0x2AA };
inline THz  LMGetApplZone() { return LM(ApplZone); }
inline void LMSetApplZone(THz val) { LM(ApplZone) = val; }
NOTRAP_FUNCTION2(LMGetApplZone);
NOTRAP_FUNCTION2(LMSetApplZone);


// MemoryMgr IMIV-236 (true-b);
const LowMemGlobal<Ptr > ROMBase{ 0x2AE };
inline Ptr  LMGetROMBase() { return LM(ROMBase); }
inline void LMSetROMBase(Ptr val) { LM(ROMBase) = val; }
NOTRAP_FUNCTION2(LMGetROMBase);
NOTRAP_FUNCTION2(LMSetROMBase);


// MemoryMgr IMI-87 (false);
const LowMemGlobal<Ptr > RAMBase{ 0x2B2 };
inline Ptr  LMGetRAMBase() { return LM(RAMBase); }
inline void LMSetRAMBase(Ptr val) { LM(RAMBase) = val; }
NOTRAP_FUNCTION2(LMGetRAMBase);
NOTRAP_FUNCTION2(LMSetRAMBase);


// MemoryMgr SysEqu.a (true-b);
const LowMemGlobal<Ptr > heapcheck{ 0x316 };
inline Ptr  LMGetheapcheck() { return LM(heapcheck); }
inline void LMSetheapcheck(Ptr val) { LM(heapcheck) = val; }
NOTRAP_FUNCTION2(LMGetheapcheck);
NOTRAP_FUNCTION2(LMSetheapcheck);


// MemoryMgr IMI-85 (true);
const LowMemGlobal<LONGINT > Lo3Bytes{ 0x31A };
inline LONGINT  LMGetLo3Bytes() { return LM(Lo3Bytes); }
inline void LMSetLo3Bytes(LONGINT val) { LM(Lo3Bytes) = val; }
NOTRAP_FUNCTION2(LMGetLo3Bytes);
NOTRAP_FUNCTION2(LMSetLo3Bytes);


// MemoryMgr IMII-17 (true-b);
const LowMemGlobal<LONGINT > MinStack{ 0x31E };
inline LONGINT  LMGetMinStack() { return LM(MinStack); }
inline void LMSetMinStack(LONGINT val) { LM(MinStack) = val; }
NOTRAP_FUNCTION2(LMGetMinStack);
NOTRAP_FUNCTION2(LMSetMinStack);


// MemoryMgr IMII-17 (true-b);
const LowMemGlobal<LONGINT > DefltStack{ 0x322 };
inline LONGINT  LMGetDefltStack() { return LM(DefltStack); }
inline void LMSetDefltStack(LONGINT val) { LM(DefltStack) = val; }
NOTRAP_FUNCTION2(LMGetDefltStack);
NOTRAP_FUNCTION2(LMSetDefltStack);


// MemoryMgr IMI-43 (true);
const LowMemGlobal<Handle > GZRootHnd{ 0x328 };
inline Handle  LMGetGZRootHnd() { return LM(GZRootHnd); }
inline void LMSetGZRootHnd(Handle val) { LM(GZRootHnd) = val; }
NOTRAP_FUNCTION2(LMGetGZRootHnd);
NOTRAP_FUNCTION2(LMSetGZRootHnd);


// MemoryMgr LowMem.h (false);
const LowMemGlobal<Handle > GZMoveHnd{ 0x330 };
inline Handle  LMGetGZMoveHnd() { return LM(GZMoveHnd); }
inline void LMSetGZMoveHnd(Handle val) { LM(GZMoveHnd) = val; }
NOTRAP_FUNCTION2(LMGetGZMoveHnd);
NOTRAP_FUNCTION2(LMSetGZMoveHnd);


// MemoryMgr ThinkC (true-b);
const LowMemGlobal<ProcPtr > IAZNotify{ 0x33C };
inline ProcPtr  LMGetIAZNotify() { return LM(IAZNotify); }
inline void LMSetIAZNotify(ProcPtr val) { LM(IAZNotify) = val; }
NOTRAP_FUNCTION2(LMGetIAZNotify);
NOTRAP_FUNCTION2(LMSetIAZNotify);


// MemoryMgr IMI-95 (true);
const LowMemGlobal<Ptr > CurrentA5{ 0x904 };
inline Ptr  LMGetCurrentA5() { return LM(CurrentA5); }
inline void LMSetCurrentA5(Ptr val) { LM(CurrentA5) = val; }
NOTRAP_FUNCTION2(LMGetCurrentA5);
NOTRAP_FUNCTION2(LMSetCurrentA5);


// MemoryMgr IMII-19 (true-b);
const LowMemGlobal<Ptr > CurStackBase{ 0x908 };
inline Ptr  LMGetCurStackBase() { return LM(CurStackBase); }
inline void LMSetCurStackBase(Ptr val) { LM(CurStackBase) = val; }
NOTRAP_FUNCTION2(LMGetCurStackBase);
NOTRAP_FUNCTION2(LMSetCurStackBase);


// MemoryMgr IMI-85 (true);
const LowMemGlobal<Byte [20]> Scratch20{ 0x1E4 };


// MemoryMgr IMI-85 (true);
const LowMemGlobal<Byte [8]> ToolScratch{ 0x9CE };


// MemoryMgr IMI-85 (true);
const LowMemGlobal<Byte [8]> Scratch8{ 0x9FA };


// MemoryMgr IMI-85 (true);
const LowMemGlobal<LONGINT > OneOne{ 0xA02 };
inline LONGINT  LMGetOneOne() { return LM(OneOne); }
inline void LMSetOneOne(LONGINT val) { LM(OneOne) = val; }
NOTRAP_FUNCTION2(LMGetOneOne);
NOTRAP_FUNCTION2(LMSetOneOne);


// MemoryMgr IMI-85 (true);
const LowMemGlobal<LONGINT > MinusOne{ 0xA06 };
inline LONGINT  LMGetMinusOne() { return LM(MinusOne); }
inline void LMSetMinusOne(LONGINT val) { LM(MinusOne) = val; }
NOTRAP_FUNCTION2(LMGetMinusOne);
NOTRAP_FUNCTION2(LMSetMinusOne);


// MemoryMgr IMI-85 (true);
const LowMemGlobal<Byte [12]> ApplScratch{ 0xA78 };


BEGIN_EXECUTOR_ONLY
/* traps which can have a `sys' or `clear' bit set */
#define SYSBIT (1 << 10)
#define CLRBIT (1 << 9)

namespace callconv
{
    template<typename Loc> struct ReturnMemErr
    {
        syn68k_addr_t afterwards(syn68k_addr_t ret)
        {
            Loc::set(LM(MemErr));
            return ret;
        }
    };
    template<typename Loc> struct ReturnMemErrConditional
    {
        syn68k_addr_t afterwards(syn68k_addr_t ret)
        {
            auto err = LM(MemErr);
            if(err < 0)
                Loc::set(err);
            return ret;
        }
    };
}
END_EXECUTOR_ONLY


Handle _NewEmptyHandle_flags(bool sys_p);REGISTER_FLAG_TRAP(_NewEmptyHandle_flags, NewEmptyHandle, NewEmptyHandleSys, 0xA166, Handle(), A0(TrapBit<SYSBIT>), callconv::ReturnMemErr<D0>);


Handle _NewHandle_flags(Size size, bool sys_p, bool clear_p);REGISTER_2FLAG_TRAP(_NewHandle_flags, NewHandle, NewHandleClear, NewHandleSys, NewHandleSysClear, 0xA122, Handle(Size ), A0(D0, TrapBit<SYSBIT>, TrapBit<CLRBIT>), callconv::ReturnMemErr<D0>);


Handle _RecoverHandle_flags(Ptr p, bool sys_p);REGISTER_FLAG_TRAP(_RecoverHandle_flags, RecoverHandle, RecoverHandleSys, 0xA128, Handle(Ptr ), A0(A0, TrapBit<SYSBIT>), callconv::ReturnMemErr<D0>);


Ptr _NewPtr_flags(Size size, bool sys_p, bool clear_p);REGISTER_2FLAG_TRAP(_NewPtr_flags, NewPtr, NewPtrClear, NewPtrSys, NewPtrSysClear, 0xA11E, Ptr(Size ), A0(D0, TrapBit<SYSBIT>, TrapBit<CLRBIT>), callconv::ReturnMemErr<D0>);


int32_t _FreeMem_flags(bool sys_p);REGISTER_FLAG_TRAP(_FreeMem_flags, FreeMem, FreeMemSys, 0xA01C, int32_t(), D0(TrapBit<SYSBIT>));


Size _MaxMem_flags(GUEST<Size> *growp, bool sys_p);REGISTER_FLAG_TRAP(_MaxMem_flags, MaxMem, MaxMemSys, 0xA11D, Size(GUEST<Size> *), D0(Out<Size, A0>, TrapBit<SYSBIT>));


Size _CompactMem_flags(Size sizeneeded, bool sys_p);REGISTER_FLAG_TRAP(_CompactMem_flags, CompactMem, CompactMemSys, 0xA04C, Size(Size ), D0(D0, TrapBit<SYSBIT>));


void _ResrvMem_flags(Size needed, bool sys_p);REGISTER_FLAG_TRAP(_ResrvMem_flags, ReserveMem, ReserveMemSys, 0xA040, void(Size ), void(D0, TrapBit<SYSBIT>), callconv::ReturnMemErr<D0>);


void _PurgeMem_flags(Size needed, bool sys_p);REGISTER_FLAG_TRAP(_PurgeMem_flags, PurgeMem, PurgeMemSys, 0xA04D, void(Size ), void(D0, TrapBit<SYSBIT>), callconv::ReturnMemErr<D0>);


Size _MaxBlock_flags(bool sys_p);REGISTER_FLAG_TRAP(_MaxBlock_flags, MaxBlock, MaxBlockSys, 0xA061, Size(), D0(TrapBit<SYSBIT>));


void _PurgeSpace_flags(GUEST<Size> *totalp, GUEST<Size> *contigp, bool sys_p);REGISTER_FLAG_TRAP(_PurgeSpace_flags, PurgeSpace, PurgeSpaceSys, 0xA062, void(GUEST<Size> *, GUEST<Size> *), void(Out<Size, D0>, Out<Size, A0>, TrapBit<SYSBIT>));


OSErr C_MemError();NOTRAP_FUNCTION(MemError);


SignedByte HGetState(Handle h);REGISTER_TRAP2(HGetState, 0xA069, D0(A0));


void HSetState(Handle h, SignedByte flags);REGISTER_TRAP2(HSetState, 0xA06A, void(A0, D0), ReturnMemErr<D0>);


void HLock(Handle h);REGISTER_TRAP2(HLock, 0xA029, void(A0), ReturnMemErr<D0>);


void HUnlock(Handle h);REGISTER_TRAP2(HUnlock, 0xA02A, void(A0), ReturnMemErr<D0>);


void HPurge(Handle h);REGISTER_TRAP2(HPurge, 0xA049, void(A0), ReturnMemErr<D0>);


void HNoPurge(Handle h);REGISTER_TRAP2(HNoPurge, 0xA04A, void(A0), ReturnMemErr<D0>);


void HSetRBit(Handle h);REGISTER_TRAP2(HSetRBit, 0xA067, void(A0), ReturnMemErr<D0>);


void HClrRBit(Handle h);REGISTER_TRAP2(HClrRBit, 0xA068, void(A0), ReturnMemErr<D0>);


void InitApplZone();REGISTER_TRAP2(InitApplZone, 0xA02C, void(), ReturnMemErr<D0>);


void SetApplBase(Ptr newbase);REGISTER_TRAP2(SetApplBase, 0xA057, void(A0), ReturnMemErr<D0>);


void MoreMasters();REGISTER_TRAP2(MoreMasters, 0xA036, void(), ReturnMemErr<D0>);


void InitZone(GrowZoneUPP pGrowZone, int16_t cMoreMasters, Ptr limitPtr, THz startPtr);NOTRAP_FUNCTION2(InitZone);


THz GetZone();REGISTER_TRAP2(GetZone, 0xA11A, A0(), ReturnMemErr<D0>);


void SetZone(THz hz);REGISTER_TRAP2(SetZone, 0xA01B, void(A0), ReturnMemErr<D0>);


void DisposeHandle(Handle h);REGISTER_TRAP2(DisposeHandle, 0xA023, void(A0), ReturnMemErr<D0>);


Size GetHandleSize(Handle h);REGISTER_TRAP2(GetHandleSize, 0xA025, D0(A0), ReturnMemErrConditional<D0>);


void SetHandleSize(Handle h, Size newsize);REGISTER_TRAP2(SetHandleSize, 0xA024, void(A0, D0), ReturnMemErr<D0>);


THz HandleZone(Handle h);REGISTER_TRAP2(HandleZone, 0xA126, A0(A0), ReturnMemErr<D0>);


void ReallocateHandle(Handle h, Size size);REGISTER_TRAP2(ReallocateHandle, 0xA027, void(A0, D0), ReturnMemErr<D0>);


void DisposePtr(Ptr p);REGISTER_TRAP2(DisposePtr, 0xA01F, void(A0), ReturnMemErr<D0>);


Size GetPtrSize(Ptr p);REGISTER_TRAP2(GetPtrSize, 0xA021, D0(A0), ReturnMemErrConditional<D0>);


void SetPtrSize(Ptr p, Size newsize);REGISTER_TRAP2(SetPtrSize, 0xA020, void(A0, D0), ReturnMemErr<D0>);


THz PtrZone(Ptr p);REGISTER_TRAP2(PtrZone, 0xA148, A0(A0), ReturnMemErr<D0>);


void _BlockMove_flags(const void *src, void *dst, Size cnt, bool flush_p);REGISTER_FLAG_TRAP(_BlockMove_flags, BlockMove, BlockMoveData, 0xA02E, void(const void *, void *, Size ), void(A0, A1, D0, TrapBit<0x200>), ReturnMemErr<D0>);


void MaxApplZone();REGISTER_TRAP2(MaxApplZone, 0xA063, void(), ReturnMemErr<D0>);


void MoveHHi(Handle h);REGISTER_TRAP2(MoveHHi, 0xA064, void(A0), ReturnMemErr<D0>);


void SetApplLimit(Ptr newlimit);REGISTER_TRAP2(SetApplLimit, 0xA02D, void(A0), ReturnMemErr<D0>);


void SetGrowZone(GrowZoneUPP newgz);REGISTER_TRAP2(SetGrowZone, 0xA04B, void(A0), ReturnMemErr<D0>);


void EmptyHandle(Handle h);REGISTER_TRAP2(EmptyHandle, 0xA02B, void(A0), ReturnMemErr<D0>);


void C_HLockHi(Handle h);NOTRAP_FUNCTION(HLockHi);


THz C_SystemZone();NOTRAP_FUNCTION(SystemZone);


THz C_ApplicationZone();NOTRAP_FUNCTION(ApplicationZone);


Size StackSpace();REGISTER_TRAP2(StackSpace, 0xA065, D0());


Ptr C_GetApplLimit();NOTRAP_FUNCTION(GetApplLimit);


Ptr C_TopMem();NOTRAP_FUNCTION(TopMem);


Handle C_GZSaveHnd();NOTRAP_FUNCTION(GZSaveHnd);


DISPATCHER_TRAP(OSDispatch, 0xA88F, StackW);


// temporary memory functions; see tempmem.c
int32_t C_TempFreeMem();PASCAL_SUBTRAP(TempFreeMem, 0xA88F, 0x0018, OSDispatch);


Size C_TempMaxMem(GUEST<Size> *grow);PASCAL_SUBTRAP(TempMaxMem, 0xA88F, 0x0015, OSDispatch);


Ptr C_TempTopMem();PASCAL_SUBTRAP(TempTopMem, 0xA88F, 0x0016, OSDispatch);


Handle C_TempNewHandle(Size logical_size, GUEST<OSErr> *result_code);PASCAL_SUBTRAP(TempNewHandle, 0xA88F, 0x001D, OSDispatch);


void C_TempHLock(Handle h, GUEST<OSErr> *result_code);PASCAL_SUBTRAP(TempHLock, 0xA88F, 0x001E, OSDispatch);


void C_TempHUnlock(Handle h, GUEST<OSErr> *result_code);PASCAL_SUBTRAP(TempHUnlock, 0xA88F, 0x001F, OSDispatch);


void C_TempDisposeHandle(Handle h, GUEST<OSErr> *result_code);PASCAL_SUBTRAP(TempDisposeHandle, 0xA88F, 0x0020, OSDispatch);


// MacPhoenix: the virtual memory calls. There is no virtual memory, so
// holding and locking succeed and do nothing, as on a Mac with it off
// (Warcraft II holds its sound buffers).
DISPATCHER_TRAP(MemoryDispatch, 0xA05C, D0W);


OSErr C_HoldMemory(void *address, uint32_t count);REGISTER_SUBTRAP(HoldMemory, 0xA05C, 0x0000, MemoryDispatch, D0(A0, A1));


OSErr C_UnholdMemory(void *address, uint32_t count);REGISTER_SUBTRAP(UnholdMemory, 0xA05C, 0x0001, MemoryDispatch, D0(A0, A1));


OSErr C_LockMemory(void *address, uint32_t count);REGISTER_SUBTRAP(LockMemory, 0xA05C, 0x0002, MemoryDispatch, D0(A0, A1));


OSErr C_UnlockMemory(void *address, uint32_t count);REGISTER_SUBTRAP(UnlockMemory, 0xA05C, 0x0003, MemoryDispatch, D0(A0, A1));


OSErr C_LockMemoryContiguous(void *address, uint32_t count);REGISTER_SUBTRAP(LockMemoryContiguous, 0xA05C, 0x0004, MemoryDispatch, D0(A0, A1));


} /* namespace Executor */