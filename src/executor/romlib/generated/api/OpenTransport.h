/*****************************************************************************
 *                                                                           *
 *                              OpenTransport.h                              *
 *                              ===============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          UInt32, SInt32, UInt8, UInt16, UnsignedWide,      *
 *                         Boolean, SInt16, SInt8, OSType, StringPtr,        *
 *                         ConstStringPtr                                    *
 *     DriverServices.h    OSStatus                                          *
 *     FileMgr.h           FSSpec                                            *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "DriverServices.h"
#include "FileMgr.h"

#define MODULE_NAME OpenTransport
#include <base/api-module.h>


namespace Executor {

enum {gestaltOpenTptVersions = "otvr"_4,
gestaltOpenTptPresent = 1,
gestaltOpenTptLoaded = 2,
gestaltOpenTptAppleTalkPresent = 4,
gestaltOpenTptAppleTalkLoaded = 8,
gestaltOpenTptTCPPresent = 16,
gestaltOpenTptTCPLoaded = 32,
gestaltOpenTptIPXSPXPresent = 64,
gestaltOpenTptIPXSPXLoaded = 128,
};

typedef UInt32 OTByteCount;

typedef UInt32 OTItemCount;

typedef SInt32 OTInt32;

typedef UInt32 OTUInt32;

// milliseconds
typedef UInt32 OTTimeout;

typedef SInt32 OTSequence;

typedef SInt32 OTNameID;

typedef SInt32 OTReason;

typedef UInt32 OTQLen;

typedef void *OTClient;

typedef UInt8 *OTClientName;

typedef UInt32 OTOpenFlags;

typedef UInt16 OTUnixErr;

typedef UInt16 OTXTIErr;

typedef SInt32 OTResult;

typedef UInt16 OTAddressType;

typedef UInt32 OTStructType;

typedef UInt32 OTFlags;

typedef UInt32 OTBand;

typedef UInt32 OTEventCode;

typedef UInt32 OTXTILevel;

typedef UInt32 OTXTIName;

typedef UInt32 OTPortRef;

typedef UInt8 OTLock;

typedef void *ProviderRef;

typedef void *EndpointRef;

typedef void *MapperRef;

typedef void *StreamRef;

// A client's record, OT's state for it (36 bytes in the client's own
// globals): the last argument of most ...Priv routines, which the public
// calls are glue for (custom/OpenTransportGlue.c, which describes what
// the client sets in it first). The task routines take its first word.
struct OTClientContext;

typedef OTClientContext *OTClientContextPtr;

// A provider's configuration (OTCreateConfiguration); opaque.
struct OTConfiguration;

enum {kOTInvalidRef = 0,
};

enum {TSUCCESS = 0,
TBADADDR = 1,
TBADOPT = 2,
TACCES = 3,
TBADF = 4,
TNOADDR = 5,
TOUTSTATE = 6,
TBADSEQ = 7,
TSYSERR = 8,
TLOOK = 9,
TBADDATA = 10,
TBUFOVFLW = 11,
TFLOW = 12,
TNODATA = 13,
TNODIS = 14,
TNOUDERR = 15,
TBADFLAG = 16,
TNOREL = 17,
TNOTSUPPORT = 18,
TSTATECHNG = 19,
TNOSTRUCTYPE = 20,
TBADNAME = 21,
TBADQLEN = 22,
TADDRBUSY = 23,
TINDOUT = 24,
TPROVMISMATCH = 25,
TRESQLEN = 26,
TRESADDR = 27,
TQFULL = 28,
TPROTO = 29,
TBADSYNC = 30,
TCANCELED = 31,
TLASTXTIERROR = 31,
};

enum {kOTNoError = 0,
kOTOutOfMemoryErr = -3211,
kOTNotFoundErr = -3201,
kOTDuplicateFoundErr = -3216,
kOTBadAddressErr = -3150,
kOTBadOptionErr = -3151,
kOTAccessErr = -3152,
kOTBadReferenceErr = -3153,
kOTNoAddressErr = -3154,
kOTOutStateErr = -3155,
kOTBadSequenceErr = -3156,
kOTSysErrorErr = -3157,
kOTLookErr = -3158,
kOTBadDataErr = -3159,
kOTBufferOverflowErr = -3160,
kOTFlowErr = -3161,
kOTNoDataErr = -3162,
kOTNoDisconnectErr = -3163,
kOTNoUDErrErr = -3164,
kOTBadFlagErr = -3165,
kOTNoReleaseErr = -3166,
kOTNotSupportedErr = -3167,
kOTStateChangeErr = -3168,
kOTNoStructureTypeErr = -3169,
kOTBadNameErr = -3170,
kOTBadQLenErr = -3171,
kOTAddressBusyErr = -3172,
kOTIndOutErr = -3173,
kOTProviderMismatchErr = -3174,
kOTResQLenErr = -3175,
kOTResAddressErr = -3176,
kOTQFullErr = -3177,
kOTProtocolErr = -3178,
kOTBadSyncErr = -3179,
kOTCanceledErr = -3180,
kEPERMErr = -3200,
kENOENTErr = -3201,
kENORSRCErr = -3202,
kEINTRErr = -3203,
kEIOErr = -3204,
kENXIOErr = -3205,
kEBADFErr = -3208,
kEAGAINErr = -3210,
kENOMEMErr = -3211,
kEACCESErr = -3212,
kEFAULTErr = -3213,
kEBUSYErr = -3215,
kEEXISTErr = -3216,
kENODEVErr = -3218,
kEINVALErr = -3221,
kENOTTYErr = -3224,
kEPIPEErr = -3231,
kERANGEErr = -3233,
kEWOULDBLOCKErr = -3234,
kEDEADLKErr = -3234,
kEALREADYErr = -3236,
kENOTSOCKErr = -3237,
kEDESTADDRREQErr = -3238,
kEMSGSIZEErr = -3239,
kEPROTOTYPEErr = -3240,
kENOPROTOOPTErr = -3241,
kEPROTONOSUPPORTErr = -3242,
kESOCKTNOSUPPORTErr = -3243,
kEOPNOTSUPPErr = -3244,
kEADDRINUSEErr = -3247,
kEADDRNOTAVAILErr = -3248,
kENETDOWNErr = -3249,
kENETUNREACHErr = -3250,
kENETRESETErr = -3251,
kECONNABORTEDErr = -3252,
kECONNRESETErr = -3253,
kENOBUFSErr = -3254,
kEISCONNErr = -3255,
kENOTCONNErr = -3256,
kESHUTDOWNErr = -3257,
kETOOMANYREFSErr = -3258,
kETIMEDOUTErr = -3259,
kECONNREFUSEDErr = -3260,
kEHOSTDOWNErr = -3263,
kEHOSTUNREACHErr = -3264,
kEPROTOErr = -3269,
kETIMEErr = -3270,
kENOSRErr = -3271,
kEBADMSGErr = -3272,
kECANCELErr = -3273,
kENOSTRErr = -3274,
kENODATAErr = -3275,
kEINPROGRESSErr = -3276,
kESRCHErr = -3277,
kENOMSGErr = -3278,
kOTClientNotInittedErr = -3279,
kOTPortHasDiedErr = -3280,
kOTPortWasEjectedErr = -3281,
kOTBadConfigurationErr = -3282,
kOTConfigurationChangedErr = -3283,
kOTUserRequestedErr = -3284,
kOTPortLostConnection = -3285,
};

enum {kOTGenericName = 0,
};

struct OTAddress{GUEST_STRUCT;GUEST<OTAddressType> fAddressType;UInt8 fAddress[1];};static_assert(sizeof(OTAddress) == 4);void describeStruct(const OTAddress&, std::ostream&);

enum {T_BIND = 1,
T_OPTMGMT = 2,
T_CALL = 3,
T_DIS = 4,
T_UNITDATA = 5,
T_UDERROR = 6,
T_INFO = 7,
T_REPLYDATA = 8,
T_REQUESTDATA = 9,
T_UNITREQUEST = 10,
T_UNITREPLY = 11,
};

enum {T_ADDR = 1,
T_OPT = 2,
T_UDATA = 4,
T_ALL = 65535,
};

enum {T_MORE = 1,
T_EXPEDITED = 2,
T_ACKNOWLEDGED = 4,
T_PARTIALDATA = 8,
T_NORECEIPT = 16,
T_TIMEDOUT = 32,
};

using OTNotifyProcPtr = UPP<void(void *contextPtr, OTEventCode code, OTResult result, void *cookie)>;


enum {T_LISTEN = 1,
T_CONNECT = 2,
T_DATA = 4,
T_EXDATA = 8,
T_DISCONNECT = 16,
T_ERROR = 32,
T_UDERR = 64,
T_ORDREL = 128,
T_GODATA = 256,
T_GOEXDATA = 512,
T_REQUEST = 1024,
T_REPLY = 2048,
T_PASSCON = 4096,
T_RESET = 8192,
kPRIVATEEVENT = 268435456,
kCOMPLETEEVENT = 536870912,
T_BINDCOMPLETE = 536870913,
T_UNBINDCOMPLETE = 536870914,
T_ACCEPTCOMPLETE = 536870915,
T_REPLYCOMPLETE = 536870916,
T_DISCONNECTCOMPLETE = 536870917,
T_OPTMGMTCOMPLETE = 536870918,
T_OPENCOMPLETE = 536870919,
T_GETPROTADDRCOMPLETE = 536870920,
T_RESOLVEADDRCOMPLETE = 536870921,
T_GETINFOCOMPLETE = 536870922,
T_SYNCCOMPLETE = 536870923,
T_MEMORYRELEASED = 536870924,
T_REGNAMECOMPLETE = 536870925,
T_DELNAMECOMPLETE = 536870926,
T_LKUPNAMECOMPLETE = 536870927,
T_LKUPNAMERESULT = 536870928,
kSTREAMEVENT = 553648128,
kSIGNALEVENT = 570425344,
kPROTOCOLEVENT = 587202560,
kOTProviderIsDisconnected = 587202561,
kOTProviderIsReconnected = 587202562,
kOTProviderWillClose = 603979777,
kOTProviderIsClosed = 603979778,
kOTPortDisabled = 620756993,
kOTPortEnabled = 620756994,
kOTRequestHardware = 620756995,
kOTHardwareInUse = 620756995,
kOTReleaseHardware = 620756996,
kOTHardwareIsFree = 620756996,
kOTClosePortRequest = 620756997,
kOTNewPortRegistered = 620756998,
kOTConfigurationChanged = 637534209,
kOTSystemSleep = 637534210,
kOTSystemShutdown = 637534211,
kOTSystemAwaken = 637534212,
kOTSystemIdle = 637534213,
kOTSystemSleepPrep = 637534214,
kOTSystemShutdownPrep = 637534215,
kOTSystemAwakenPrep = 637534216,
};

enum {XTI_GENERIC = 65535,
XTI_DEBUG = 1,
XTI_LINGER = 128,
XTI_RCVBUF = 4098,
XTI_RCVLOWAT = 4100,
XTI_SNDBUF = 4097,
XTI_SNDLOWAT = 4099,
XTI_PROTOTYPE = 4101,
OPT_CHECKSUM = 1536,
OPT_RETRYCNT = 1537,
OPT_INTERVAL = 1538,
OPT_ENABLEEOM = 1539,
OPT_SELFSEND = 1540,
OPT_SERVERSTATUS = 1541,
OPT_KEEPALIVE = 8,
};

// Ioctl commands: MIOC_CMD(t, v) is t << 8 | v, the groups by letter.
enum {MIOC_STREAMIO = 65, // 'A'
MIOC_STRLOG = 98, // 'b'
MIOC_SAD = 103, // 'g'
MIOC_TCP = 107, // 'k'
MIOC_DLPI = 108, // 'l'
MIOC_OT = 79, // 'O'
MIOC_ATALK = 84, // 'T'
MIOC_SRL = 85, // 'U'
MIOC_CFIG = 122, // 'z'
I_OTGetMiscellaneousEvents = 20225,
I_OTSetFramingType = 20226,
kOTGetFramingValue = 4294967295,
I_OTSetRawMode = 20227,
};

enum {kMaxModuleNameLength = 31,
kMaxModuleNameSize = 32,
kMaxProviderNameLength = 35,
kMaxProviderNameSize = 36,
kMaxSlotIDLength = 7,
kMaxSlotIDSize = 8,
kMaxResourceInfoLength = 31,
kMaxResourceInfoSize = 32,
};

// Endpoint states (OTGetEndpointState, OTSync).
enum {T_UNINIT = 0,
T_UNBND = 1,
T_IDLE = 2,
T_OUTCON = 3,
T_INCON = 4,
T_DATAXFER = 5,
T_OUTREL = 6,
T_INREL = 7,
};

// Option management's requests and results; general values.
enum {T_NEGOTIATE = 4,
T_CHECK = 8,
T_DEFAULT = 16,
T_CURRENT = 128,
T_SUCCESS = 32,
T_FAILURE = 64,
T_PARTSUCCESS = 256,
T_READONLY = 512,
T_NOTSUPPORT = 1024,
T_YES = 1,
T_NO = 0,
T_UNUSED = -1,
T_NULL = 0,
T_ABSREQ = 32768,
T_UNSPEC = -3,
T_ALLOPT = 0,
};

struct t_kpalive{GUEST_STRUCT;GUEST<SInt32> kp_onoff;GUEST<SInt32> kp_timeout;};static_assert(sizeof(t_kpalive) == 8);void describeStruct(const t_kpalive&, std::ostream&);

struct t_linger{GUEST_STRUCT;GUEST<SInt32> l_onoff;GUEST<SInt32> l_linger;};static_assert(sizeof(t_linger) == 8);void describeStruct(const t_linger&, std::ostream&);

struct TOptionHeader{GUEST_STRUCT;GUEST<OTByteCount> len;GUEST<OTXTILevel> level;GUEST<OTXTIName> name;GUEST<UInt32> status;};static_assert(sizeof(TOptionHeader) == 16);void describeStruct(const TOptionHeader&, std::ostream&);

struct TOption{GUEST_STRUCT;GUEST<OTByteCount> len;GUEST<OTXTILevel> level;GUEST<OTXTIName> name;GUEST<UInt32> status;GUEST<UInt32> value[1];};static_assert(sizeof(TOption) == 20);void describeStruct(const TOption&, std::ostream&);

enum {kOTOptionHeaderSize = 16,
kOTBooleanOptionDataSize = 4,
kOTBooleanOptionSize = 20,
kOTOneByteOptionSize = 17,
kOTTwoByteOptionSize = 18,
kOTFourByteOptionSize = 20,
};

struct TEndpointInfo{GUEST_STRUCT;GUEST<SInt32> addr;GUEST<SInt32> options;GUEST<SInt32> tsdu;GUEST<SInt32> etsdu;GUEST<SInt32> connect;GUEST<SInt32> discon;GUEST<UInt32> servtype;GUEST<UInt32> flags;};static_assert(sizeof(TEndpointInfo) == 32);void describeStruct(const TEndpointInfo&, std::ostream&);

enum {T_COTS = 1,
T_COTS_ORD = 2,
T_CLTS = 3,
T_TRANS = 5,
T_TRANS_ORD = 6,
T_TRANS_CLTS = 7,
T_SENDZERO = 1,
T_XPG4_1 = 2,
T_CAN_SUPPORT_MDATA = 268435456,
T_CAN_RESOLVE_ADDR = 1073741824,
T_CAN_SUPPLY_MIB = 536870912,
T_INFINITE = -1,
T_INVALID = -2,
};

enum {kOTNoDeviceType = 0,
kOTPseudoDevice = 1023,
kOTLastDeviceIndex = 1022,
kOTLastSlotNumber = 255,
kOTLastOtherNumber = 255,
kMaxPortNameLength = 35,
kMaxPortNameSize = 36,
kOTInvalidPortRef = 0,
kOTUnknownBusPort = 0,
kOTMotherboardBus = 1,
kOTNuBus = 2,
kOTPCIBus = 3,
kOTGeoPort = 4,
kOTPCMCIABus = 5,
kOTLastBusIndex = 15,
};

// Device types of an OTPortRef (OpenTptLinks.h).
enum {kOTADEVDevice = 1,
kOTMDEVDevice = 2,
kOTLocalTalkDevice = 3,
kOTIRTalkDevice = 4,
kOTTokenRingDevice = 5,
kOTISDNDevice = 6,
kOTATMDevice = 7,
kOTSMDSDevice = 8,
kOTSerialDevice = 9,
kOTEthernetDevice = 10,
kOTSLIPDevice = 11,
kOTPPPDevice = 12,
kOTModemDevice = 13,
kOTFastEthernetDevice = 14,
kOTFDDIDevice = 15,
kOTATMLANEDevice = 16,
kOTATMSNAPDevice = 17,
};

struct OTPortRecord{GUEST_STRUCT;GUEST<OTPortRef> fRef;GUEST<UInt32> fPortFlags;GUEST<UInt32> fInfoFlags;GUEST<UInt32> fCapabilities;GUEST<OTItemCount> fNumChildPorts;GUEST<GUEST<OTPortRef>*> fChildPorts;char fPortName[36];char fModuleName[32];char fSlotID[8];char fResourceInfo[32];char fReserved[164];};static_assert(sizeof(OTPortRecord) == 296);void describeStruct(const OTPortRecord&, std::ostream&);

enum {kOTPortIsDLPI = 1,
kOTPortIsTPI = 2,
kOTPortCanYield = 4,
kOTPortIsSystemRegistered = 16384,
kOTPortIsPrivate = 32768,
kOTPortIsActive = 1,
kOTPortIsDisabled = 2,
kOTPortIsUnavailable = 4,
};

struct OTData{GUEST_STRUCT;GUEST<void*> fNext;GUEST<void*> fData;GUEST<OTByteCount> fLen;};static_assert(sizeof(OTData) == 12);void describeStruct(const OTData&, std::ostream&);

enum {kNetbufDataIsOTData = 4294967294,
kOTNetbufDataIsOTBufferStar = 4294967293,
};

struct TNetbuf{GUEST_STRUCT;GUEST<OTByteCount> maxlen;GUEST<OTByteCount> len;GUEST<UInt8*> buf;};static_assert(sizeof(TNetbuf) == 12);void describeStruct(const TNetbuf&, std::ostream&);

struct TBind{GUEST_STRUCT;TNetbuf addr;GUEST<OTQLen> qlen;};static_assert(sizeof(TBind) == 16);void describeStruct(const TBind&, std::ostream&);

struct TDiscon{GUEST_STRUCT;TNetbuf udata;GUEST<OTReason> reason;GUEST<OTSequence> sequence;};static_assert(sizeof(TDiscon) == 20);void describeStruct(const TDiscon&, std::ostream&);

struct TCall{GUEST_STRUCT;TNetbuf addr;TNetbuf opt;TNetbuf udata;GUEST<OTSequence> sequence;};static_assert(sizeof(TCall) == 40);void describeStruct(const TCall&, std::ostream&);

struct TUnitData{GUEST_STRUCT;TNetbuf addr;TNetbuf opt;TNetbuf udata;};static_assert(sizeof(TUnitData) == 36);void describeStruct(const TUnitData&, std::ostream&);

struct TUDErr{GUEST_STRUCT;TNetbuf addr;TNetbuf opt;GUEST<SInt32> error;};static_assert(sizeof(TUDErr) == 28);void describeStruct(const TUDErr&, std::ostream&);

struct TOptMgmt{GUEST_STRUCT;TNetbuf opt;GUEST<OTFlags> flags;};static_assert(sizeof(TOptMgmt) == 16);void describeStruct(const TOptMgmt&, std::ostream&);

struct TRequest{GUEST_STRUCT;TNetbuf data;TNetbuf opt;GUEST<OTSequence> sequence;};static_assert(sizeof(TRequest) == 28);void describeStruct(const TRequest&, std::ostream&);

struct TReply{GUEST_STRUCT;TNetbuf data;TNetbuf opt;GUEST<OTSequence> sequence;};static_assert(sizeof(TReply) == 28);void describeStruct(const TReply&, std::ostream&);

struct TUnitRequest{GUEST_STRUCT;TNetbuf addr;TNetbuf opt;TNetbuf udata;GUEST<OTSequence> sequence;};static_assert(sizeof(TUnitRequest) == 40);void describeStruct(const TUnitRequest&, std::ostream&);

struct TUnitReply{GUEST_STRUCT;TNetbuf opt;TNetbuf udata;GUEST<OTSequence> sequence;};static_assert(sizeof(TUnitReply) == 28);void describeStruct(const TUnitReply&, std::ostream&);

struct TRegisterRequest{GUEST_STRUCT;TNetbuf name;TNetbuf addr;GUEST<OTFlags> flags;};static_assert(sizeof(TRegisterRequest) == 28);void describeStruct(const TRegisterRequest&, std::ostream&);

struct TRegisterReply{GUEST_STRUCT;TNetbuf addr;GUEST<OTNameID> nameid;};static_assert(sizeof(TRegisterReply) == 16);void describeStruct(const TRegisterReply&, std::ostream&);

struct TLookupRequest{GUEST_STRUCT;TNetbuf name;TNetbuf addr;GUEST<UInt32> maxcnt;GUEST<OTTimeout> timeout;GUEST<OTFlags> flags;};static_assert(sizeof(TLookupRequest) == 36);void describeStruct(const TLookupRequest&, std::ostream&);

struct TLookupBuffer{GUEST_STRUCT;GUEST<UInt16> fAddressLength;GUEST<UInt16> fNameLength;UInt8 fAddressBuffer[1];};static_assert(sizeof(TLookupBuffer) == 6);void describeStruct(const TLookupBuffer&, std::ostream&);

struct TLookupReply{GUEST_STRUCT;TNetbuf names;GUEST<UInt32> rspcount;};static_assert(sizeof(TLookupReply) == 16);void describeStruct(const TLookupReply&, std::ostream&);

using OTProcessProcPtr = UPP<void(void *arg)>;


typedef UnsignedWide OTTimeStamp;

struct OTLink{GUEST_STRUCT;GUEST<OTLink*> fNext;};static_assert(sizeof(OTLink) == 4);void describeStruct(const OTLink&, std::ostream&);

struct OTLIFO{GUEST_STRUCT;GUEST<OTLink*> fHead;};static_assert(sizeof(OTLIFO) == 4);void describeStruct(const OTLIFO&, std::ostream&);

struct OTList{GUEST_STRUCT;GUEST<OTLink*> fHead;};static_assert(sizeof(OTList) == 4);void describeStruct(const OTList&, std::ostream&);

using OTListSearchProcPtr = UPP<Boolean(const void *ref, OTLink *linkToCheck), callconv::CCall>;


namespace ot { uint32_t init_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, InitOpenTransportPriv, "OTClnt$LclUtils", "InitOpenTransportPriv", false, "LL", 'L', init_priv);


namespace ot { uint32_t close_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, CloseOpenTransportPriv, "OTClnt$LclUtils", "CloseOpenTransportPriv", false, "L", 'V', close_priv);


namespace ot { uint32_t register_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, OTRegisterAsClientPriv, "OTClnt$LclUtils", "OTRegisterAsClientPriv", false, "LLL", 'L', register_priv);


namespace ot { uint32_t unregister_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, OTUnregisterAsClientPriv, "OTClnt$LclUtils", "OTUnregisterAsClientPriv", false, "L", 'L', unregister_priv);


namespace ot { uint32_t alloc_mem_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, OTAllocMemPriv, "OTClnt$LclUtils", "OTAllocMemPriv", false, "LL", 'L', alloc_mem_priv);


namespace ot { uint32_t free_mem_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, OTFreeMemPriv, "OTClnt$LclUtils", "OTFreeMemPriv", false, "LL", 'V', free_mem_priv);


namespace ot { uint32_t open_endpoint_priv(const Call &); }OT_FUNCTION(OTClientLib, OTOpenEndpointPriv, "OTClnt$LclClnt", "OTOpenEndpointPriv", false, "LLLLL", 'L', open_endpoint_priv);


namespace ot { uint32_t async_open_endpoint_priv(const Call &); }OT_FUNCTION(OTClientLib, OTAsyncOpenEndpointPriv, "OTClnt$LclClnt", "OTAsyncOpenEndpointPriv", false, "LLLLLL", 'L', async_open_endpoint_priv);


namespace ot { uint32_t close_provider_priv(const Call &); }OT_FUNCTION(OTClientLib, OTCloseProviderPriv, "OTClnt$LclClnt", "OTCloseProviderPriv", false, "LL", 'L', close_provider_priv);


namespace ot { uint32_t set_async(const Call &); }OT_FUNCTION(OTClientLib, OTSetAsynchronous, "OTClnt$LclClnt", "OTSETASYNCHRONOUS", true, "L", 'L', set_async);


namespace ot { uint32_t set_sync(const Call &); }OT_FUNCTION(OTClientLib, OTSetSynchronous, "OTClnt$LclClnt", "OTSETSYNCHRONOUS", true, "L", 'L', set_sync);


namespace ot { uint32_t is_sync(const Call &); }OT_FUNCTION(OTClientLib, OTIsSynchronous, "OTClnt$LclClnt", "OTISSYNCHRONOUS", true, "L", 'B', is_sync);


namespace ot { uint32_t set_blocking(const Call &); }OT_FUNCTION(OTClientLib, OTSetBlocking, "OTClnt$LclClnt", "OTSETBLOCKING", true, "L", 'L', set_blocking);


namespace ot { uint32_t set_nonblocking(const Call &); }OT_FUNCTION(OTClientLib, OTSetNonBlocking, "OTClnt$LclClnt", "OTSETNONBLOCKING", true, "L", 'L', set_nonblocking);


namespace ot { uint32_t is_blocking(const Call &); }OT_FUNCTION(OTClientLib, OTIsBlocking, "OTClnt$LclClnt", "OTISBLOCKING", true, "L", 'B', is_blocking);


namespace ot { uint32_t ack_sends(const Call &); }OT_FUNCTION(OTClientLib, OTAckSends, "OTClnt$LclClnt", "OTACKSENDS", true, "L", 'L', ack_sends);


namespace ot { uint32_t dont_ack_sends(const Call &); }OT_FUNCTION(OTClientLib, OTDontAckSends, "OTClnt$LclClnt", "OTDONTACKSENDS", true, "L", 'L', dont_ack_sends);


namespace ot { uint32_t is_acking(const Call &); }OT_FUNCTION(OTClientLib, OTIsAckingSends, "OTClnt$LclClnt", "OTISACKINGSENDS", true, "L", 'B', is_acking);


namespace ot { uint32_t install_notifier(const Call &); }OT_FUNCTION(OTClientLib, OTInstallNotifier, "OTClnt$LclClnt", "OTINSTALLNOTIFIER", true, "LLL", 'L', install_notifier);


namespace ot { uint32_t remove_notifier(const Call &); }OT_FUNCTION(OTClientLib, OTRemoveNotifier, "OTClnt$LclClnt", "OTREMOVENOTIFIER", true, "L", 'V', remove_notifier);


namespace ot { uint32_t get_endpoint_state(const Call &); }OT_FUNCTION(OTClientLib, OTGetEndpointState, "OTClnt$LclClnt", "OTGETENDPOINTSTATE", true, "L", 'L', get_endpoint_state);


namespace ot { uint32_t get_endpoint_info(const Call &); }OT_FUNCTION(OTClientLib, OTGetEndpointInfo, "OTClnt$LclClnt", "OTGETENDPOINTINFO", true, "LL", 'L', get_endpoint_info);


namespace ot { uint32_t look(const Call &); }OT_FUNCTION(OTClientLib, OTLook, "OTClnt$LclClnt", "OTLOOK", true, "L", 'L', look);


namespace ot { uint32_t count_data_bytes(const Call &); }OT_FUNCTION(OTClientLib, OTCountDataBytes, "OTClnt$LclClnt", "OTCOUNTDATABYTES", true, "LL", 'L', count_data_bytes);


namespace ot { uint32_t get_prot_address(const Call &); }OT_FUNCTION(OTClientLib, OTGetProtAddress, "OTClnt$LclClnt", "OTGETPROTADDRESS", true, "LLL", 'L', get_prot_address);


namespace ot { uint32_t ot_bind(const Call &); }OT_FUNCTION(OTClientLib, OTBind, "OTClnt$LclClnt", "OTBIND", true, "LLL", 'L', ot_bind);


namespace ot { uint32_t unbind(const Call &); }OT_FUNCTION(OTClientLib, OTUnbind, "OTClnt$LclClnt", "OTUNBIND", true, "L", 'L', unbind);


namespace ot { uint32_t ot_connect(const Call &); }OT_FUNCTION(OTClientLib, OTConnect, "OTClnt$LclClnt", "OTCONNECT", true, "LLL", 'L', ot_connect);


namespace ot { uint32_t rcv_connect(const Call &); }OT_FUNCTION(OTClientLib, OTRcvConnect, "OTClnt$LclClnt", "OTRCVCONNECT", true, "LL", 'L', rcv_connect);


namespace ot { uint32_t ot_listen(const Call &); }OT_FUNCTION(OTClientLib, OTListen, "OTClnt$LclClnt", "OTLISTEN", true, "LL", 'L', ot_listen);


namespace ot { uint32_t ot_accept(const Call &); }OT_FUNCTION(OTClientLib, OTAccept, "OTClnt$LclClnt", "OTACCEPT", true, "LLL", 'L', ot_accept);


namespace ot { uint32_t snd_disconnect(const Call &); }OT_FUNCTION(OTClientLib, OTSndDisconnect, "OTClnt$LclClnt", "OTSNDDISCONNECT", true, "LL", 'L', snd_disconnect);


namespace ot { uint32_t rcv_disconnect(const Call &); }OT_FUNCTION(OTClientLib, OTRcvDisconnect, "OTClnt$LclClnt", "OTRCVDISCONNECT", true, "LL", 'L', rcv_disconnect);


namespace ot { uint32_t snd_orderly(const Call &); }OT_FUNCTION(OTClientLib, OTSndOrderlyDisconnect, "OTClnt$LclClnt", "OTSNDORDERLYDISCONNECT", true, "L", 'L', snd_orderly);


namespace ot { uint32_t rcv_orderly(const Call &); }OT_FUNCTION(OTClientLib, OTRcvOrderlyDisconnect, "OTClnt$LclClnt", "OTRCVORDERLYDISCONNECT", true, "L", 'L', rcv_orderly);


namespace ot { uint32_t snd(const Call &); }OT_FUNCTION(OTClientLib, OTSnd, "OTClnt$LclClnt", "OTSND", true, "LLLL", 'L', snd);


namespace ot { uint32_t rcv(const Call &); }OT_FUNCTION(OTClientLib, OTRcv, "OTClnt$LclClnt", "OTRCV", true, "LLLL", 'L', rcv);


namespace ot { uint32_t snd_udata(const Call &); }OT_FUNCTION(OTClientLib, OTSndUData, "OTClnt$LclClnt", "OTSNDUDATA", true, "LL", 'L', snd_udata);


namespace ot { uint32_t rcv_udata(const Call &); }OT_FUNCTION(OTClientLib, OTRcvUData, "OTClnt$LclClnt", "OTRCVUDATA", true, "LLL", 'L', rcv_udata);


namespace ot { uint32_t ot_option_management(const Call &); }OT_FUNCTION(OTClientLib, OTOptionManagement, "OTClnt$LclClnt", "OTOPTIONMANAGEMENT", true, "LLL", 'L', ot_option_management);


namespace ot { uint32_t next_option(const Call &); }OT_FUNCTION(OTClientLib, OTNextOption, "OTClnt$LclClnt", "OTNEXTOPTION", true, "LLL", 'L', next_option);


namespace ot { uint32_t find_option(const Call &); }OT_FUNCTION(OTClientLib, OTFindOption, "OTClnt$LclClnt", "OTFINDOPTION", true, "LLLL", 'L', find_option);


namespace ot { uint32_t rcv_uderr(const Call &); }OT_FUNCTION(OTClientLib, OTRcvUDErr, "OTClnt$LclClnt", "OTRCVUDERR", true, "LL", 'L', rcv_uderr);


namespace ot { uint32_t ot_sync(const Call &); }OT_FUNCTION(OTClientLib, OTSync, "OTClnt$LclClnt", "OTSYNC", true, "L", 'L', ot_sync);


namespace ot { uint32_t cancel_sync_calls(const Call &); }OT_FUNCTION(OTClientLib, OTCancelSynchronousCalls, "OTClnt$LclClnt", "OTCANCELSYNCHRONOUSCALLS", true, "LL", 'L', cancel_sync_calls);


namespace ot { uint32_t resolve_address(const Call &); }OT_FUNCTION(OTClientLib, OTResolveAddress, "OTClnt$LclClnt", "OTRESOLVEADDRESS", true, "LLLL", 'L', resolve_address);


namespace ot { uint32_t get_provider_port_ref(const Call &); }OT_FUNCTION(OTClientLib, OTGetProviderPortRef, "OTClnt$LclClnt", "OTGETPROVIDERPORTREF", true, "L", 'L', get_provider_port_ref);


namespace ot { uint32_t ot_ioctl(const Call &); }OT_FUNCTION(OTClientLib, OTIoctl, "OTClnt$LclClnt", "OTIOCTL", true, "LLL", 'L', ot_ioctl);


namespace ot { uint32_t alloc_priv(const Call &); }OT_FUNCTION(OTClientLib, OTAllocPriv, "OTClnt$LclClnt", "OTAllocPriv", false, "LLLLL", 'L', alloc_priv);


namespace ot { uint32_t ot_free(const Call &); }OT_FUNCTION(OTClientLib, OTFree, "OTClnt$LclClnt", "OTFREE", true, "LL", 'L', ot_free);


namespace ot { uint32_t idle(const Call &); }OT_FUNCTION(OTClientLib, OTIdle, "OTClnt$LclClnt", "OTIDLE", true, "", 'V', idle);


namespace ot { uint32_t delay(const Call &); }OT_FUNCTION(OTClientLib, OTDelay, "OTClnt$LclClnt", "OTDELAY", true, "L", 'V', delay);


namespace ot { uint32_t create_deferred(const Call &); }OT_FUNCTION(OTUtilityLib, OTCreateDeferredTaskPriv, "OTClnt$CmnShared", "OTCreateDeferredTaskPriv", false, "LLL", 'L', create_deferred);


namespace ot { uint32_t destroy(const Call &); }OT_FUNCTION(OTUtilityLib, OTDestroyDeferredTask, "OTClnt$CmnShared", "OTDESTROYDEFERREDTASK", true, "L", 'L', destroy);


namespace ot { uint32_t schedule(const Call &); }OT_FUNCTION(OTUtilityLib, OTScheduleDeferredTask, "OTClnt$CmnShared", "OTSCHEDULEDEFERREDTASK", true, "L", 'B', schedule);


namespace ot { uint32_t schedule(const Call &); }OT_FUNCTION(OTUtilityLib, OTScheduleInterruptTask, "OTClnt$CmnShared", "OTSCHEDULEINTERRUPTTASK", true, "L", 'B', schedule);


namespace ot { uint32_t create_system(const Call &); }OT_FUNCTION(OTUtilityLib, OTCreateSystemTaskPriv, "OTClnt$CmnShared", "OTCreateSystemTaskPriv", false, "LLL", 'L', create_system);


namespace ot { uint32_t destroy(const Call &); }OT_FUNCTION(OTUtilityLib, OTDestroySystemTask, "OTClnt$CmnShared", "OTDESTROYSYSTEMTASK", true, "L", 'L', destroy);


namespace ot { uint32_t schedule(const Call &); }OT_FUNCTION(OTUtilityLib, OTScheduleSystemTask, "OTClnt$CmnShared", "OTSCHEDULESYSTEMTASK", true, "L", 'B', schedule);


namespace ot { uint32_t cancel(const Call &); }OT_FUNCTION(OTUtilityLib, OTCancelSystemTask, "OTClnt$CmnShared", "OTCANCELSYSTEMTASK", true, "L", 'B', cancel);


namespace ot { uint32_t can_make_sync_call(const Call &); }OT_FUNCTION(OTUtilityLib, OTCanMakeSyncCall, "OTClnt$CmnShared", "OTCANMAKESYNCCALL", true, "", 'B', can_make_sync_call);


namespace ot { uint32_t is_at_interrupt_level(const Call &); }OT_FUNCTION(OTUtilityLib, OTIsAtInterruptLevel, "OTClnt$CmnShared", "OTIsAtInterruptLevel", false, "", 'B', is_at_interrupt_level);


namespace ot { uint32_t can_load_libraries(const Call &); }OT_FUNCTION(OTUtilityLib, OTCanLoadLibraries, "OTClnt$CmnShared", "OTCanLoadLibraries", false, "", 'B', can_load_libraries);


namespace ot { uint32_t enter_interrupt(const Call &); }OT_FUNCTION(OTUtilityLib, OTEnterInterrupt, "OTClnt$CmnShared", "OTENTERINTERRUPT", true, "", 'V', enter_interrupt);


namespace ot { uint32_t leave_interrupt(const Call &); }OT_FUNCTION(OTUtilityLib, OTLeaveInterrupt, "OTClnt$CmnShared", "OTLEAVEINTERRUPT", true, "", 'V', leave_interrupt);


namespace ot { uint32_t ot_memcpy(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemcpy, "OTClnt$CmnShared", "OTMemcpy", false, "LLL", 'V', ot_memcpy);


namespace ot { uint32_t ot_memcpy(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemmove, "OTClnt$CmnShared", "OTMemmove", false, "LLL", 'V', ot_memcpy);


namespace ot { uint32_t memzero(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemzero, "OTClnt$CmnShared", "OTMemzero", false, "LL", 'V', memzero);


namespace ot { uint32_t ot_memset(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemset, "OTClnt$CmnShared", "OTMemset", false, "LWL", 'V', ot_memset);


namespace ot { uint32_t ot_memcmp(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemcmp, "OTClnt$CmnShared", "OTMemcmp", false, "LLL", 'B', ot_memcmp);


namespace ot { uint32_t strlength(const Call &); }OT_FUNCTION(OTUtilityLib, OTStrLength, "OTClnt$CmnShared", "OTStrLength", false, "L", 'L', strlength);


namespace ot { uint32_t strcopy(const Call &); }OT_FUNCTION(OTUtilityLib, OTStrCopy, "OTClnt$CmnShared", "OTStrCopy", false, "LL", 'V', strcopy);


namespace ot { uint32_t ot_strcat(const Call &); }OT_FUNCTION(OTUtilityLib, OTStrCat, "OTClnt$CmnShared", "OTStrCat", false, "LL", 'V', ot_strcat);


namespace ot { uint32_t strequal(const Call &); }OT_FUNCTION(OTUtilityLib, OTStrEqual, "OTClnt$CmnShared", "OTStrEqual", false, "LL", 'B', strequal);


namespace ot { uint32_t get_time_stamp(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetTimeStamp, "OTClnt$CmnShared", "OTGetTimeStamp", false, "L", 'V', get_time_stamp);


namespace ot { uint32_t subtract_time_stamps(const Call &); }OT_FUNCTION(OTUtilityLib, OTSubtractTimeStamps, "OTClnt$CmnShared", "OTSubtractTimeStamps", false, "LLL", 'L', subtract_time_stamps);


namespace ot { uint32_t stamp_in_ms(const Call &); }OT_FUNCTION(OTUtilityLib, OTTimeStampInMilliseconds, "OTClnt$CmnShared", "OTTimeStampInMilliseconds", false, "L", 'L', stamp_in_ms);


namespace ot { uint32_t stamp_in_us(const Call &); }OT_FUNCTION(OTUtilityLib, OTTimeStampInMicroseconds, "OTClnt$CmnShared", "OTTimeStampInMicroseconds", false, "L", 'L', stamp_in_us);


namespace ot { uint32_t elapsed_ms(const Call &); }OT_FUNCTION(OTUtilityLib, OTElapsedMilliseconds, "OTClnt$CmnShared", "OTElapsedMilliseconds", false, "L", 'L', elapsed_ms);


namespace ot { uint32_t elapsed_us(const Call &); }OT_FUNCTION(OTUtilityLib, OTElapsedMicroseconds, "OTClnt$CmnShared", "OTElapsedMicroseconds", false, "L", 'L', elapsed_us);


namespace ot { uint32_t clock_secs(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetClockTimeInSecs, "OTClnt$CmnShared", "OTGetClockTimeInSecs", false, "", 'L', clock_secs);


namespace ot { uint32_t lifo_enqueue(const Call &); }OT_FUNCTION(OTUtilityLib, OTLIFOEnqueue, "OTClnt$CmnShared", "OTLIFOEnqueue", false, "LL", 'V', lifo_enqueue);


namespace ot { uint32_t lifo_dequeue(const Call &); }OT_FUNCTION(OTUtilityLib, OTLIFODequeue, "OTClnt$CmnShared", "OTLIFODequeue", false, "L", 'L', lifo_dequeue);


namespace ot { uint32_t lifo_steal(const Call &); }OT_FUNCTION(OTUtilityLib, OTLIFOStealList, "OTClnt$CmnShared", "OTLIFOStealList", false, "L", 'L', lifo_steal);


namespace ot { uint32_t reverse_list(const Call &); }OT_FUNCTION(OTUtilityLib, OTReverseList, "OTClnt$CmnShared", "OTReverseList", false, "L", 'L', reverse_list);


namespace ot { uint32_t add_first(const Call &); }OT_FUNCTION(OTUtilityLib, OTAddFirst, "OTClnt$CmnShared", "OTAddFirst", false, "LL", 'V', add_first);


namespace ot { uint32_t add_last(const Call &); }OT_FUNCTION(OTUtilityLib, OTAddLast, "OTClnt$CmnShared", "OTAddLast", false, "LL", 'V', add_last);


namespace ot { uint32_t remove_first(const Call &); }OT_FUNCTION(OTUtilityLib, OTRemoveFirst, "OTClnt$CmnShared", "OTRemoveFirst", false, "L", 'L', remove_first);


namespace ot { uint32_t remove_last(const Call &); }OT_FUNCTION(OTUtilityLib, OTRemoveLast, "OTClnt$CmnShared", "OTRemoveLast", false, "L", 'L', remove_last);


namespace ot { uint32_t get_first(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetFirst, "OTClnt$CmnShared", "OTGetFirst", false, "L", 'L', get_first);


namespace ot { uint32_t get_last(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetLast, "OTClnt$CmnShared", "OTGetLast", false, "L", 'L', get_last);


namespace ot { uint32_t is_in_list(const Call &); }OT_FUNCTION(OTUtilityLib, OTIsInList, "OTClnt$CmnShared", "OTIsInList", false, "LL", 'B', is_in_list);


namespace ot { uint32_t remove_link(const Call &); }OT_FUNCTION(OTUtilityLib, OTRemoveLink, "OTClnt$CmnShared", "OTRemoveLink", false, "LL", 'B', remove_link);


namespace ot { uint32_t get_indexed_link(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetIndexedLink, "OTClnt$CmnShared", "OTGetIndexedLink", false, "LL", 'L', get_indexed_link);


namespace ot { uint32_t enqueue(const Call &); }OT_FUNCTION(OTUtilityLib, OTEnqueue, "OTClnt$CmnShared", "OTEnqueue", false, "LLL", 'V', enqueue);


namespace ot { uint32_t dequeue(const Call &); }OT_FUNCTION(OTUtilityLib, OTDequeue, "OTClnt$CmnShared", "OTDequeue", false, "LL", 'L', dequeue);


namespace ot { uint32_t find_link(const Call &); }OT_FUNCTION(OTUtilityLib, OTFindLink, "OTClnt$CmnShared", "OTFindLink", false, "LLL", 'L', find_link);


namespace ot { uint32_t find_and_remove_link(const Call &); }OT_FUNCTION(OTUtilityLib, OTFindAndRemoveLink, "OTClnt$CmnShared", "OTFindAndRemoveLink", false, "LLL", 'L', find_and_remove_link);


namespace ot { uint32_t atomic_set_bit(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicSetBit, "OTClnt$CmnShared", "OTAtomicSetBit", false, "LL", 'B', atomic_set_bit);


namespace ot { uint32_t atomic_clear_bit(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicClearBit, "OTClnt$CmnShared", "OTAtomicClearBit", false, "LL", 'B', atomic_clear_bit);


namespace ot { uint32_t atomic_test_bit(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicTestBit, "OTClnt$CmnShared", "OTAtomicTestBit", false, "LL", 'B', atomic_test_bit);


namespace ot { uint32_t cas32(const Call &); }OT_FUNCTION(OTUtilityLib, OTCompareAndSwapPtr, "OTClnt$CmnShared", "OTCompareAndSwapPtr", false, "LLL", 'B', cas32);


namespace ot { uint32_t cas32(const Call &); }OT_FUNCTION(OTUtilityLib, OTCompareAndSwap32, "OTClnt$CmnShared", "OTCompareAndSwap32", false, "LLL", 'B', cas32);


namespace ot { uint32_t cas16(const Call &); }OT_FUNCTION(OTUtilityLib, OTCompareAndSwap16, "OTClnt$CmnShared", "OTCompareAndSwap16", false, "LLL", 'B', cas16);


namespace ot { uint32_t cas8(const Call &); }OT_FUNCTION(OTUtilityLib, OTCompareAndSwap8, "OTClnt$CmnShared", "OTCompareAndSwap8", false, "LLL", 'B', cas8);


namespace ot { uint32_t atomic_add32(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicAdd32, "OTClnt$CmnShared", "OTAtomicAdd32", false, "LL", 'L', atomic_add32);


namespace ot { uint32_t atomic_add16(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicAdd16, "OTClnt$CmnShared", "OTAtomicAdd16", false, "LL", 'W', atomic_add16);


namespace ot { uint32_t atomic_add8(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicAdd8, "OTClnt$CmnShared", "OTAtomicAdd8", false, "LL", 'W', atomic_add8);


namespace ot { uint32_t create_port_ref(const Call &); }OT_FUNCTION(OTUtilityLib, OTCreatePortRef, "OTClnt$CmnShared", "OTCREATEPORTREF", true, "WWWW", 'L', create_port_ref);


namespace ot { uint32_t device_type_of(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetDeviceTypeFromPortRef, "OTClnt$CmnShared", "OTGETDEVICETYPEFROMPORTREF", true, "L", 'W', device_type_of);


namespace ot { uint32_t bus_type_of(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetBusTypeFromPortRef, "OTClnt$CmnShared", "OTGETBUSTYPEFROMPORTREF", true, "L", 'W', bus_type_of);


namespace ot { uint32_t slot_of(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetSlotFromPortRef, "OTClnt$CmnShared", "OTGETSLOTFROMPORTREF", true, "LL", 'W', slot_of);


namespace ot { uint32_t get_indexed_port(const Call &); }OT_FUNCTION(OTClientUtilLib, OTGetIndexedPort, "OTClnt$LclUtils", "OTGETINDEXEDPORT", true, "LL", 'B', get_indexed_port);


namespace ot { uint32_t find_port(const Call &); }OT_FUNCTION(OTClientUtilLib, OTFindPort, "OTClnt$LclUtils", "OTFINDPORT", true, "LL", 'B', find_port);


namespace ot { uint32_t find_port_by_ref(const Call &); }OT_FUNCTION(OTClientUtilLib, OTFindPortByRef, "OTClnt$LclUtils", "OTFINDPORTBYREF", true, "LL", 'B', find_port_by_ref);


namespace ot { uint32_t create_configuration(const Call &); }OT_FUNCTION(OTClientUtilLib, OTCreateConfiguration, "OTClnt$LclUtils", "OTCREATECONFIGURATION", true, "L", 'L', create_configuration);


namespace ot { uint32_t clone_configuration(const Call &); }OT_FUNCTION(OTClientUtilLib, OTCloneConfiguration, "OTClnt$LclUtils", "OTCLONECONFIGURATION", true, "L", 'L', clone_configuration);


namespace ot { uint32_t destroy_configuration(const Call &); }OT_FUNCTION(OTClientUtilLib, OTDestroyConfiguration, "OTClnt$LclUtils", "OTDESTROYCONFIGURATION", true, "L", 'V', destroy_configuration);


namespace ot { uint32_t cfg_open_db(const Call &); }OT_FUNCTION(OTConfigLib, OTOpenConfigurationsDB, "OTLib$CfigLib", "OTOpenConfigurationsDB", false, "LLL", 'L', cfg_open_db);


namespace ot { uint32_t cfg_close_db(const Call &); }OT_FUNCTION(OTConfigLib, OTCloseConfigurationsDB, "OTLib$CfigLib", "OTCloseConfigurationsDB", false, "L", 'L', cfg_close_db);


namespace ot { uint32_t cfg_get_attribute_value(const Call &); }OT_FUNCTION(OTConfigLib, OTGetAttributeValue, "OTLib$CfigLib", "OTGetAttributeValue", false, "LLLLL", 'L', cfg_get_attribute_value);


namespace ot { uint32_t cfg_is_port_compatible(const Call &); }OT_FUNCTION(OTConfigLib, OTIsPortCompatibleWith, "OTLib$CfigLib", "OTIsPortCompatibleWith", false, "LL", 'B', cfg_is_port_compatible);


namespace ot { uint32_t cfg_user_port_name(const Call &); }OT_FUNCTION(OTConfigLib, OTGetUserPortNameFromPortRef, "OTLib$CfigLib", "OTGetUserPortNameFromPortRef", false, "LL", 'B', cfg_user_port_name);


namespace ot { uint32_t cfg_active_config(const Call &); }OT_FUNCTION(OTConfigLib, OTGetActiveConfigID, "OTLib$CfigLib", "OTGetActiveConfigID", false, "L", 'L', cfg_active_config);


namespace ot { uint32_t cfg_attribute_size(const Call &); }OT_FUNCTION(OTConfigLib, OTGetAttributeSize, "OTLib$CfigLib", "OTGetAttributeSize", false, "LLL", 'L', cfg_attribute_size);


namespace ot { uint32_t cfg_default_port(const Call &); }OT_FUNCTION(OTConfigLib, OTGetDefaultPort, "OTLib$CfigLib", "OTGetDefaultPort", false, "L", 'L', cfg_default_port);


namespace ot { uint32_t cfg_configs_count(const Call &); }OT_FUNCTION(OTConfigLib, OTGetConfigsCount, "OTLib$CfigLib", "OTGetConfigsCount", false, "L", 'L', cfg_configs_count);


namespace ot { uint32_t cfg_indexed_config(const Call &); }OT_FUNCTION(OTConfigLib, OTGetIndexedConfigID, "OTLib$CfigLib", "OTGetIndexedConfigID", false, "LL", 'L', cfg_indexed_config);


namespace ot { uint32_t cfg_config_name(const Call &); }OT_FUNCTION(OTConfigLib, OTGetConfigName, "OTLib$CfigLib", "OTGetConfigName", false, "LLL", 'L', cfg_config_name);


namespace ot { uint32_t cfg_create(const Call &); }OT_FUNCTION(OTConfigLib, OTCreateAConfiguration, "OTLib$CfigLib", "OTCreateAConfiguration", false, "LL", 'L', cfg_create);


namespace ot { uint32_t cfg_copy(const Call &); }OT_FUNCTION(OTConfigLib, OTCopyAConfiguration, "OTLib$CfigLib", "OTCopyAConfiguration", false, "LLLLL", 'L', cfg_copy);


namespace ot { uint32_t cfg_delete(const Call &); }OT_FUNCTION(OTConfigLib, OTDeleteAConfiguration, "OTLib$CfigLib", "OTDeleteAConfiguration", false, "LL", 'L', cfg_delete);


namespace ot { uint32_t cfg_tcp_change(const Call &); }OT_FUNCTION(OTConfigLib, TCPChangeConfiguration, "OTLib$CfigLib", "TCPChangeConfiguration", false, "LL", 'L', cfg_tcp_change);


namespace ot { uint32_t cfg_tcp_check_consequences(const Call &); }OT_FUNCTION(OTConfigLib, TCPCheckChangeConfigurationConsequences, "OTLib$CfigLib", "TCPCheckChangeConfigurationConsequences", false, "LL", 'B', cfg_tcp_check_consequences);


namespace ot { uint32_t at_open_services(const Call &); }OT_FUNCTION(OTATalkClientLib, OTAsyncOpenAppleTalkServicesPriv, "ot:atlk$LclATClnt", "OTAsyncOpenAppleTalkServicesPriv", false, "LLLLL", 'L', at_open_services);


namespace ot { uint32_t at_my_zone(const Call &); }OT_FUNCTION(OTATalkClientLib, OTATalkGetMyZone, "ot:atlk$LclATClnt", "OTATalkGetMyZone", false, "LL", 'L', at_my_zone);


namespace ot { uint32_t at_zone_list(const Call &); }OT_FUNCTION(OTATalkClientLib, OTATalkGetZoneList, "ot:atlk$LclATClnt", "OTATalkGetZoneList", false, "LL", 'L', at_zone_list);


namespace ot { uint32_t cfg_set_active(const Call &); }OT_FUNCTION(OTConfigLib, OTSetActiveConfigID, "OTLib$CfigLib", "OTSetActiveConfigID", false, "LL", 'L', cfg_set_active);


namespace ot { uint32_t cfg_set_name(const Call &); }OT_FUNCTION(OTConfigLib, OTSetConfigName, "OTLib$CfigLib", "OTSetConfigName", false, "LLL", 'L', cfg_set_name);


namespace ot { uint32_t cfg_config_id(const Call &); }OT_FUNCTION(OTConfigLib, OTGetConfigID, "OTLib$CfigLib", "OTGetConfigID", false, "LL", 'L', cfg_config_id);


namespace ot { uint32_t cfg_set_attribute_value(const Call &); }OT_FUNCTION(OTConfigLib, OTSetAttributeValue, "OTLib$CfigLib", "OTSetAttributeValue", false, "LLLLL", 'L', cfg_set_attribute_value);


} /* namespace Executor */