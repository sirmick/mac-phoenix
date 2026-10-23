/*****************************************************************************
 *                                                                           *
 *                              CodeFragments.h                              *
 *                              ===============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSType, Ptr, Boolean, INTEGER, StringPtr, OSErr,        *
 *                   LONGINT, ConstStr63Param, Str255, ConstStr255Param      *
 *     FileMgr.h     FSSpecPtr                                               *
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

#define MODULE_NAME CodeFragments
#include <base/api-module.h>


namespace Executor {

BEGIN_EXECUTOR_ONLY
const std::nullptr_t kUnresolvedCFragSymbolAddress = nullptr;
END_EXECUTOR_ONLY


struct cfrg_resource_t{GUEST_STRUCT;GUEST<uint32_t> reserved0;GUEST<uint32_t> reserved1;GUEST<uint32_t> version;GUEST<uint32_t> reserved2;GUEST<uint32_t> reserved3;GUEST<uint32_t> reserved4;GUEST<uint32_t> reserved5;GUEST<int32_t> n_descripts;};static_assert(sizeof(cfrg_resource_t) == 32);void describeStruct(const cfrg_resource_t&, std::ostream&);

BEGIN_EXECUTOR_ONLY
#define CFRG_VERSION(cfrg) ((cfgr)->version)
#define CFRG_N_DESCRIPTS(cfrg) ((cfrg)->n_descripts)
END_EXECUTOR_ONLY


struct cfir_t{GUEST_STRUCT;GUEST<OSType> isa;GUEST<uint32_t> update_level;GUEST<uint32_t> current_version;GUEST<uint32_t> oldest_definition_version;GUEST<uint32_t> stack_size;GUEST<int16_t> appl_library_dir;uint8_t fragment_type;uint8_t fragment_location;GUEST<int32_t> offset_to_fragment;GUEST<int32_t> fragment_length;GUEST<uint32_t> reserved0;GUEST<uint32_t> reserved1;GUEST<uint16_t> cfir_length;uint8_t name[1];};static_assert(sizeof(cfir_t) == 44);void describeStruct(const cfir_t&, std::ostream&);

BEGIN_EXECUTOR_ONLY
#define CFIR_ISA(cfir) ((cfir)->isa)
#define CFIR_TYPE(cfir) ((cfir)->fragment_type)
#define CFIR_LOCATION(cfir) ((cfir)->fragment_location)
#define CFIR_LENGTH(cfir) ((cfir)->cfir_length)
#define CFIR_OFFSET_TO_FRAGMENT(cfir) ((cfir)->offset_to_fragment)
#define CFIR_FRAGMENT_LENGTH(cfir) ((cfir)->fragment_length)
#define CFIR_NAME(cfir) ((cfir)->name)
END_EXECUTOR_ONLY


enum {kImportLibraryCFrag,
kApplicationCFrag,
kDropInAdditionCFrag,
kStubLibraryCFrag,
kWeakStubLibraryCFrag,
};

enum {kWholeFork = 0,
};

enum {kInMem,
kOnDiskFlat,
kOnDiskSegmented,
};

enum {kPowerPCArch = "pwpc"_4,
kMotorola68KArch = "m68k"_4,
};

enum {kLoadLib = 1, // deprecated
kReferenceCFrag = 1,
kFindLib = 2,
kLoadNewCopy = 5,
};

typedef uint32_t LoadFlags;

struct MemFragment{GUEST_STRUCT;GUEST<Ptr> address;GUEST<uint32_t> length;Boolean inPlace;uint8_t reservedA;GUEST<uint16_t> reservedB;};static_assert(sizeof(MemFragment) == 12);void describeStruct(const MemFragment&, std::ostream&);

struct DiskFragment{GUEST_STRUCT;GUEST<FSSpecPtr> fileSpec;GUEST<uint32_t> offset;GUEST<uint32_t> length;};static_assert(sizeof(DiskFragment) == 12);void describeStruct(const DiskFragment&, std::ostream&);

struct SegmentedFragment{GUEST_STRUCT;GUEST<FSSpecPtr> fileSpec;GUEST<OSType> rsrcType;GUEST<INTEGER> rsrcID;GUEST<uint16_t> reservedA;};static_assert(sizeof(SegmentedFragment) == 12);void describeStruct(const SegmentedFragment&, std::ostream&);

struct FragmentLocator{GUEST_STRUCT;GUEST<uint32_t> where;union{MemFragment inMem;DiskFragment onDisk;SegmentedFragment inSegs;} u;};static_assert(sizeof(FragmentLocator) == 16);void describeStruct(const FragmentLocator&, std::ostream&);

struct InitBlock{GUEST_STRUCT;GUEST<uint32_t> contextID;GUEST<uint32_t> closureID;GUEST<uint32_t> connectionID;FragmentLocator fragLocator;GUEST<StringPtr> libName;GUEST<uint32_t> reserved4;};static_assert(sizeof(InitBlock) == 36);void describeStruct(const InitBlock&, std::ostream&);

struct CFragConnection;

typedef CFragConnection *ConnectionID;

struct CFragClosure;

typedef CFragClosure *CFragClosureID;

DISPATCHER_TRAP(CodeFragmentDispatch, 0xAA5A, StackW);


OSErr C_GetDiskFragment(FSSpecPtr fsp, LONGINT offset, LONGINT length, ConstStr63Param fragname, LoadFlags flags, GUEST<ConnectionID> *connp, GUEST<Ptr> *mainAddrp, Str255 errname);PASCAL_SUBTRAP(GetDiskFragment, 0xAA5A, 0x0002, CodeFragmentDispatch);


typedef uint8_t SymClass;

OSErr C_FindSymbol(ConnectionID connID, ConstStr255Param symName, GUEST<Ptr> *symAddr, SymClass *symClass);PASCAL_SUBTRAP(FindSymbol, 0xAA5A, 0x0005, CodeFragmentDispatch);


OSErr C_CloseConnection(GUEST<ConnectionID> *cidp);PASCAL_SUBTRAP(CloseConnection, 0xAA5A, 0x0004, CodeFragmentDispatch);


OSErr C_GetSharedLibrary(ConstStr63Param library, OSType arch, LoadFlags loadflags, GUEST<ConnectionID> *cidp, GUEST<Ptr> *mainaddrp, Str255 errName);PASCAL_SUBTRAP(GetSharedLibrary, 0xAA5A, 0x0001, CodeFragmentDispatch);


OSErr C_GetMemFragment(void *addr, uint32_t length, ConstStr63Param fragname, LoadFlags flags, GUEST<ConnectionID> *connp, GUEST<Ptr> *mainAddrp, Str255 errname);PASCAL_SUBTRAP(GetMemFragment, 0xAA5A, 0x0003, CodeFragmentDispatch);


OSErr C_CountSymbols(ConnectionID id, GUEST<LONGINT> *countp);PASCAL_SUBTRAP(CountSymbols, 0xAA5A, 0x0006, CodeFragmentDispatch);


OSErr C_GetIndSymbol(ConnectionID id, LONGINT index, Str255 name, GUEST<Ptr> *addrp, SymClass *classp);PASCAL_SUBTRAP(GetIndSymbol, 0xAA5A, 0x0007, CodeFragmentDispatch);


} /* namespace Executor */