/*****************************************************************************
 *                                                                           *
 *                              DriverServices.h                             *
 *                              ================                             *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    SInt32, UInt32, Boolean, Str31, OSType                  *
 *     SoundMgr.h    NumVersion                                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "SoundMgr.h"

#define MODULE_NAME DriverServices
#include <base/api-module.h>


namespace Executor {

// The low 16 bits are an OSErr; the high 16 are its sign extension.
typedef SInt32 OSStatus;

typedef UInt32 OptionBits;

enum {kNilOptions = 0,
};

typedef UInt32 ByteCount;

typedef UInt32 ItemCount;

typedef void *LogicalAddress;

typedef void *PhysicalAddress;

struct OpaqueRef;

// All the kernel's ID types derive from this.  They are opaque.
typedef OpaqueRef *KernelID;

typedef KernelID AddressSpaceID;

enum {kCurrentAddressSpaceID = 0, // Mac OS provides only this one address space
};

typedef UInt32 ExecutionLevel;

enum {kTaskLevel = 0,
kSecondaryInterruptLevel = 5,
kHardwareInterruptLevel = 6,
};

// Callable at any level.  Many Driver Services calls, the memory pool
// and PrepareMemoryForIO among them, are legal only at kTaskLevel.
ExecutionLevel CurrentExecutionLevel();

// Positive values are milliseconds, negative values are negated
// microseconds, as with the Time Manager.  The two forms are
// interchangeable, so code converting a Duration has to handle both.
typedef SInt32 Duration;

enum {durationMicrosecond = -1,
durationMillisecond = 1,
durationSecond = 1000,
durationMinute = 1000 * 60,
durationHour = 1000 * 60 * 60,
durationDay = 1000 * 60 * 60 * 24,
durationForever = 2147483647,
durationImmediate = 0,
};

// Time base units.  Their length varies from machine to machine;
// convert with the routines below rather than assuming one.
struct AbsoluteTime{GUEST_STRUCT;GUEST<UInt32> hi;GUEST<UInt32> lo;};static_assert(sizeof(AbsoluteTime) == 8);void describeStruct(const AbsoluteTime&, std::ostream&);

struct Nanoseconds{GUEST_STRUCT;GUEST<UInt32> hi;GUEST<UInt32> lo;};static_assert(sizeof(Nanoseconds) == 8);void describeStruct(const Nanoseconds&, std::ostream&);

// Time since startup.  Callable at any level.
AbsoluteTime UpTime();

// Returns left minus right, and zero if that is negative, so the later
// time goes on the left: AbsoluteDeltaToDuration(UpTime(), start).
// This is the reverse of what the book's note on the routine suggests;
// the order given here is the one observed under Mac OS 9.2.2.
Duration AbsoluteDeltaToDuration(AbsoluteTime leftAbsoluteTime, AbsoluteTime rightAbsoluteTime);

AbsoluteTime DurationToAbsolute(Duration duration);

AbsoluteTime AddDurationToAbsolute(Duration duration, AbsoluteTime absoluteTime);

// Blocks, so task level only.  The delay can run long but not short.
OSStatus DelayFor(Duration expirationTime);

// Busy-waits, which makes it callable at any level.  The argument is
// the length of the delay, not a deadline: convert with
// DurationToAbsolute.  Passing UpTime() plus a delay spins for the
// machine's whole uptime.
OSStatus DelayForHardware(AbsoluteTime absoluteTime);

typedef KernelID TimerID;

using SecondaryInterruptHandler2 = UPP<OSStatus(void *p1, void *p2), callconv::CCall>;


// Runs handler as a secondary interrupt handler once expirationTime (an
// absolute time, unlike DelayForHardware's argument) is reached.  The
// handler gets p1, and the program counter at expiry as p2.  Callable
// at any level.
OSStatus SetInterruptTimer(const AbsoluteTime *expirationTime, SecondaryInterruptHandler2 handler, void *p1, GUEST<TimerID> *timer);

// Fails if the timer has already fired or been cancelled, which is how
// a caller learns that the handler has run or is about to.
OSStatus CancelTimer(TimerID timer, AbsoluteTime *timeRemaining);

// Orders accesses to noncached (memory-mapped I/O) space against the
// instructions around them.
void SynchronizeIO();

typedef KernelID InterruptSetID;

typedef SInt32 InterruptMemberNumber;

struct InterruptSetMember{GUEST_STRUCT;GUEST<InterruptSetID> setID;GUEST<InterruptMemberNumber> member;};static_assert(sizeof(InterruptSetMember) == 8);void describeStruct(const InterruptSetMember&, std::ostream&);

enum {kReturnToParentWhenComplete = 1,
kReturnToParentWhenNotComplete = 2,
};

enum {kISTChipInterruptSource = 0,
kISTOutputDMAInterruptSource = 1,
kISTInputDMAInterruptSource = 2,
kISTPropertyMemberCount = 3,
};

// The value of a device's "driver-ist" Name Registry property: the
// interrupt sources that belong to it.  A PCI card with one interrupt
// line uses only kISTChipInterruptSource.
typedef InterruptSetMember ISTProperty[kISTPropertyMemberCount];

// The Name Registry property that holds a device's ISTProperty.
#define kISTPropertyName "driver-ist"

typedef SInt32 InterruptReturnValue;

enum {kFirstMemberNumber = 1,
kIsrIsComplete = 0, // the interrupt was this device's, and has been handled
kIsrIsNotComplete = -1, // not this device's; offer it to the others sharing the line
kMemberNumberParent = -2,
};

typedef Boolean InterruptSourceState;

enum {kSourceWasEnabled = 1,
kSourceWasDisabled = 0,
};

using InterruptHandler = UPP<InterruptMemberNumber(InterruptSetMember ISTmember, void *refCon, UInt32 theIntCount), callconv::CCall>;


using InterruptEnabler = UPP<void(InterruptSetMember ISTmember, void *refCon), callconv::CCall>;


using InterruptDisabler = UPP<InterruptSourceState(InterruptSetMember ISTmember, void *refCon), callconv::CCall>;


// Task level only.  A nil function leaves whatever is installed in
// place, so a driver can take over the handler while keeping the
// platform's enabler and disabler.
OSStatus InstallInterruptFunctions(InterruptSetID setID, InterruptMemberNumber member, void *refCon, InterruptHandler handlerFunction, InterruptEnabler enableFunction, InterruptDisabler disableFunction);

// Installing replaces the member's handler, and PCI interrupt lines are
// shared; reading the old one first lets a driver pass on interrupts
// that are not its own.
OSStatus GetInterruptFunctions(InterruptSetID setID, InterruptMemberNumber member, GUEST<void*> *refCon, GUEST<InterruptHandler> *handlerFunction, GUEST<InterruptEnabler> *enableFunction, GUEST<InterruptDisabler> *disableFunction);

// Resident (never paged) memory from the system pool.  Task level only.
LogicalAddress PoolAllocateResident(ByteCount byteSize, Boolean clear);

OSStatus PoolDeallocate(LogicalAddress address);

// Resident and physically contiguous.  Task level only, and large
// requests made after startup are unlikely to succeed.
LogicalAddress MemAllocatePhysicallyContiguous(ByteCount byteSize, Boolean clear);

OSStatus MemDeallocatePhysicallyContiguous(LogicalAddress address);

ByteCount GetLogicalPageSize();

struct AddressRange{GUEST_STRUCT;GUEST<void*> base;GUEST<ByteCount> length;};static_assert(sizeof(AddressRange) == 8);void describeStruct(const AddressRange&, std::ostream&);

typedef AddressRange *AddressRangeTablePtr;

struct MultipleAddressRange{GUEST_STRUCT;GUEST<ItemCount> entryCount;GUEST<AddressRangeTablePtr> rangeTable;};static_assert(sizeof(MultipleAddressRange) == 8);void describeStruct(const MultipleAddressRange&, std::ostream&);

typedef GUEST<LogicalAddress> *LogicalMappingTablePtr;

typedef GUEST<PhysicalAddress> *PhysicalMappingTablePtr;

typedef KernelID IOPreparationID;

typedef OptionBits IOPreparationOptions;

enum {kIOMultipleRanges = 1,
kIOLogicalRanges = 2, // required; Mac OS does not accept physical ranges
kIOMinimalLogicalMapping = 4,
kIOShareMappingTables = 8,
kIOIsInput = 16, // data moves into main memory
kIOIsOutput = 32, // data moves out of main memory
kIOCoherentDataPath = 64,
kIOClientIsUserMode = 128,
};

typedef OptionBits IOPreparationState;

enum {kIOStateDone = 1,
};

// Fields marked <-- are filled in by PrepareMemoryForIO.
struct IOPreparationTable{GUEST_STRUCT;GUEST<IOPreparationOptions> options;GUEST<IOPreparationState> state; // <-- kIOStateDone once the whole range is prepared
GUEST<IOPreparationID> preparationID; // <-- pass to CheckpointIO
GUEST<AddressSpaceID> addressSpace;GUEST<ByteCount> granularity;GUEST<ByteCount> firstPrepared;GUEST<ByteCount> lengthPrepared; // <--
GUEST<ItemCount> mappingEntryCount;GUEST<LogicalMappingTablePtr> logicalMapping;GUEST<PhysicalMappingTablePtr> physicalMapping;union{AddressRange range;MultipleAddressRange multipleRanges;} rangeInfo;};static_assert(sizeof(IOPreparationTable) == 48);void describeStruct(const IOPreparationTable&, std::ostream&);

// Locks a buffer down, settles the caches, and returns its physical
// addresses for a bus-master transfer; CheckpointIO undoes it.  Task
// level only.
OSStatus PrepareMemoryForIO(IOPreparationTable *theIOPreparationTable);

typedef OptionBits IOCheckpointOptions;

enum {kNextIOIsInput = 1,
kNextIOIsOutput = 2,
kMoreIOTransfers = 4, // keep the preparation for another transfer
};

OSStatus CheckpointIO(IOPreparationID theIOPreparation, IOCheckpointOptions theOptions);

typedef UInt32 DriverDescVersion;

enum {kTheDescriptionSignature = "mtej"_4, // first long word of every DriverDescription
kInitialDriverDescriptor = 0,
};

// The stage byte of a NumVersion: majorRev, minorAndBugRev, stage and
// nonRelRev, one byte each from the most significant.
enum {developStage = 32,
alphaStage = 64,
betaStage = 96,
finalStage = 128,
};

struct DriverType{GUEST_STRUCT;GUEST<Str31> nameInfoStr; // must match the device's "name" in the Name Registry
NumVersion version;};static_assert(sizeof(DriverType) == 36);void describeStruct(const DriverType&, std::ostream&);

typedef DriverType *DriverTypePtr;

typedef OptionBits RuntimeOptions;

enum {kDriverIsLoadedUponDiscovery = 1,
kDriverIsOpenedUponLoad = 2,
kDriverIsUnderExpertControl = 4,
kDriverIsConcurrent = 8,
kDriverQueuesIOPB = 16,
};

struct DriverOSRuntime{GUEST_STRUCT;GUEST<RuntimeOptions> driverRuntime;GUEST<Str31> driverName;GUEST<UInt32> driverDescReserved[8];};static_assert(sizeof(DriverOSRuntime) == 68);void describeStruct(const DriverOSRuntime&, std::ostream&);

typedef DriverOSRuntime *DriverOSRuntimePtr;

struct DriverServiceInfo{GUEST_STRUCT;GUEST<OSType> serviceCategory;GUEST<OSType> serviceType;NumVersion serviceVersion;};static_assert(sizeof(DriverServiceInfo) == 12);void describeStruct(const DriverServiceInfo&, std::ostream&);

typedef DriverServiceInfo *DriverServiceInfoPtr;

// Values for DriverServiceInfo.serviceCategory.  Other families, such
// as the ATA Manager's, define their own.
enum {kServiceCategoryDisplay = "disp"_4,
kServiceCategoryOpentransport = "otan"_4,
kServiceCategoryBlockstorage = "blok"_4,
kServiceCategorySCSISim = "scsi"_4,
kServiceCategoryNdrvDriver = "ndrv"_4, // generic native driver; also used by display drivers
};

typedef UInt32 ServiceCount;

// Variable length: nServices entries.  A driver declaring more than
// one category needs its own structure with a longer array.
struct DriverOSService{GUEST_STRUCT;GUEST<ServiceCount> nServices;DriverServiceInfo service[1];};static_assert(sizeof(DriverOSService) == 16);void describeStruct(const DriverOSService&, std::ostream&);

typedef DriverOSService *DriverOSServicePtr;

// A native driver exports one of these as TheDriverDescription.  The
// system reads it to match the driver to a device and decide when to
// load and open it.
struct DriverDescription{GUEST_STRUCT;GUEST<OSType> driverDescSignature; // kTheDescriptionSignature
GUEST<DriverDescVersion> driverDescVersion; // kInitialDriverDescriptor
DriverType driverType;DriverOSRuntime driverOSRuntimeInfo;DriverOSService driverServices;};static_assert(sizeof(DriverDescription) == 128);void describeStruct(const DriverDescription&, std::ostream&);

typedef DriverDescription *DriverDescriptionPtr;

} /* namespace Executor */