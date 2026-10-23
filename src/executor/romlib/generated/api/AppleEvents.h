/*****************************************************************************
 *                                                                           *
 *                               AppleEvents.h                               *
 *                               =============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     ResType, Handle, OSErr, Ptr, Size, Boolean,            *
 *                    LONGINT, INTEGER                                       *
 *     EventMgr.h     EventRecord                                            *
 *     NotifyMgr.h    NMRecPtr                                               *
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
#include "NotifyMgr.h"

#define MODULE_NAME AppleEvents
#include <base/api-module.h>


namespace Executor {

typedef int32_t AEEventClass;

typedef int32_t AEEventID;

typedef int32_t AEKeyword;

typedef ResType DescType;

struct AEDesc{GUEST_STRUCT;GUEST<DescType> descriptorType;GUEST<Handle> dataHandle;};static_assert(sizeof(AEDesc) == 8);void describeStruct(const AEDesc&, std::ostream&);

// ### hack, delete
typedef AEDesc descriptor_t;

BEGIN_EXECUTOR_ONLY
#define DESC_TYPE(desc) ((desc)->descriptorType)
#define DESC_DATA(desc) ((desc)->dataHandle)
END_EXECUTOR_ONLY


struct AEKeyDesc{GUEST_STRUCT;GUEST<AEKeyword> descKey;AEDesc descContent;};static_assert(sizeof(AEKeyDesc) == 12);void describeStruct(const AEKeyDesc&, std::ostream&);

typedef AEKeyDesc key_desc_t;

BEGIN_EXECUTOR_ONLY
#define KEY_DESC_KEYWORD(keydesc) ((keydesc)->descKey)
#define KEY_DESC_CONTENT(keydesc) ((keydesc)->descContent)
END_EXECUTOR_ONLY


typedef AEDesc AEAddressDesc;

typedef AEDesc AEDescList;

typedef AEDescList AERecord;

typedef AERecord AppleEvent;

typedef int32_t AESendMode;

enum {_kAEReplyMask = 3,
};

enum {kAENoReply = 1,
kAEQueueReply = 2,
kAEWaitReply = 3,
};

enum {_kAEInteractMask = 48,
};

enum {kAENeverInteract = 16,
kAECanInteract = 32,
kAEAlwaysInteract = 48,
};

enum {kAECanSwitchLayer = 64,
kAEDontReconnect = 128,
};

// #define kAEWantReceipt	???
typedef int16_t AESendPriority;

typedef uint8_t AEEventSource;

typedef uint8_t AEInteractionAllowed;

typedef uint8_t AEArrayType;

enum {kAEInteractWithSelf = 0,
kAEInteractWithLocal = 1,
kAEInteractWithAll = 2,
};

enum {kAEUnknownSource = 0,
kAEDirectCall = 1,
kAESameProcess = 2,
kAELocalProcess = 3,
kAERemoteProcess = 4,
};

enum {kAEDataArray,
kAEPackedArray,
kAEHandleArray,
kAEDescArray,
kAEKeyDescArray,
};

enum {kAutoGenerateReturnID = -1,
kAnyTransactionID = 0,
};

enum {kAENormalPriority = 0,
kAEHighPriority = 1,
};

enum {kAEDefaultTimeout = -1,
kNoTimeOut = -2,
};

union AEArrayData{GUEST_STRUCT;GUEST<int16_t> AEDataArray[1];int8_t AEPackedArray[1];GUEST<Handle> AEHandleArray[1];AEDesc AEDescArray[1];AEKeyDesc AEKeyDescArray[1];};static_assert(sizeof(AEArrayData) == 12);void describeStruct(const AEArrayData&, std::ostream&);

typedef AEArrayData *AEArrayDataPointer;

typedef ProcPtr IdleUPP;

typedef ProcPtr EventFilterUPP;

using AEEventHandlerUPP = UPP<OSErr(const GUEST<AppleEvent> *evt, GUEST<AppleEvent> *reply, int32_t refcon)>;


using AECoercePtrUPP = UPP<OSErr(DescType data_type, Ptr data, Size data_size, DescType to_type, int32_t refcon, AEDesc *desc_out)>;


using AECoerceDescUPP = UPP<OSErr(AEDesc *desc, DescType to_type, int32_t refcon, AEDesc *desc_out)>;


struct AE_hdlr{GUEST_STRUCT;GUEST<void*> fn;GUEST<int32_t> refcon;};void describeStruct(const AE_hdlr&, std::ostream&);

// #### internal
typedef AE_hdlr AE_hdlr_t;

struct AE_hdlr_selector{GUEST_STRUCT;GUEST<int32_t> sel0;GUEST<int32_t> sel1;};void describeStruct(const AE_hdlr_selector&, std::ostream&);

typedef AE_hdlr_selector AE_hdlr_selector_t;

struct AE_hdlr_table_elt{GUEST_STRUCT;GUEST<int32_t> pad_1;GUEST<AE_hdlr_selector_t> selector;GUEST<AE_hdlr_t> hdlr;GUEST<int32_t> pad_2;};void describeStruct(const AE_hdlr_table_elt&, std::ostream&);

typedef AE_hdlr_table_elt AE_hdlr_table_elt_t;

BEGIN_EXECUTOR_ONLY
#define AE_TABLE_ELTS(table) ((*table)->elts)
#define AE_TABLE_N_ELTS(table) ((*table)->n_elts)
#define AE_TABLE_N_ALLOCATED_BYTES(table) \
    ((*table)->n_allocated_bytes)
END_EXECUTOR_ONLY


struct AE_hdlr_table{GUEST_STRUCT;GUEST<int32_t> pad_1;GUEST<int32_t> n_allocated_bytes;GUEST<int32_t> n_elts;GUEST<int32_t> pad_2[10];GUEST<AE_hdlr_table_elt_t> elts[0];};void describeStruct(const AE_hdlr_table&, std::ostream&);

typedef AE_hdlr_table AE_hdlr_table_t;

typedef GUEST<AE_hdlr_table_t> *AE_hdlr_table_ptr;

typedef GUEST<AE_hdlr_table_ptr> *AE_hdlr_table_h;

struct AE_zone_tables{GUEST_STRUCT;GUEST<AE_hdlr_table_h> event_hdlr_table;GUEST<AE_hdlr_table_h> coercion_hdlr_table;GUEST<AE_hdlr_table_h> special_hdlr_table;char pad_1[28];char unknown_appl_value[4];char pad_2[8];GUEST<Handle> unknown_sys_handle;};void describeStruct(const AE_zone_tables&, std::ostream&);

// points to a 32byte handle of unknown contents (at least,
//      sometimes)
typedef AE_zone_tables AE_zone_tables_t;

typedef GUEST<AE_zone_tables_t> *AE_zone_tables_ptr;

typedef GUEST<AE_zone_tables_ptr> *AE_zone_tables_h;

struct AE_info_t{GUEST_STRUCT;char pad_0[14];GUEST<Ptr> emKeyCache; // ExpandMem+$0E: the current KCHR's data (Key Caps reads it).
char pad_1[102];GUEST<Ptr> emHelpGlobals; // ExpandMem+$78 (name is a guess): the Help Manager's globals, a
// 296-byte System-heap block Apple's PACK 14 and the System's
// patches around it share (+$11E: balloons on). helppkg.cpp.
char pad_1b[156];GUEST<Ptr> emDragGlobals; // ExpandMem+$118 (name is a guess): the Drag Manager's globals, a
// 350-byte System-heap block its five 'proc' resources and the
// System's patches around them share (+$4: the drag being tracked,
// +$14: the Gestalt attributes, +$1C: the handler list, +$84/+$88:
// the Finder's hook trampolines). drag.cpp.
char pad_1d[20];GUEST<int16_t> emHelpPackageLock; // ExpandMem+$130 (name is a guess): PACK 14's lock count for
// CallDetachedPackage ($668).
char pad_1c[34];GUEST<AE_zone_tables_h> appl_zone_tables;char pad_2[36];GUEST<AE_zone_tables_h> system_zone_tables;char pad_3[46];GUEST<int32_t> sys_heap_reserve; // ExpandMem+$1AE (name is a guess). Free space the System heap
// keeps in reserve: the Process Manager grows it by request plus
// this, Standard File and Finder check against it. 64K on a real
// 7.5.5 boot; a gpch drops it to 32K while retrying a full heap.
char pad_4[162];};static_assert(sizeof(AE_info_t) == 596);void describeStruct(const AE_info_t&, std::ostream&);

typedef AE_info_t *AE_info_ptr;

DISPATCHER_TRAP(Pack8, 0xA816, D0W);


// "internal". TODO: verify if they really exist as entry points.
OSErr C__AE_hdlr_table_alloc(int32_t , int32_t , int32_t , int8_t , GUEST<AE_hdlr_table_h> *);PASCAL_SUBTRAP(_AE_hdlr_table_alloc, 0xA816, 0x092E, Pack8);


OSErr C__AE_hdlr_delete(AE_hdlr_table_h , int32_t , GUEST<AE_hdlr_selector_t> *);PASCAL_SUBTRAP(_AE_hdlr_delete, 0xA816, 0x0632, Pack8);


OSErr C__AE_hdlr_lookup(AE_hdlr_table_h , int32_t , GUEST<AE_hdlr_selector_t> *, GUEST<AE_hdlr_t> *);PASCAL_SUBTRAP(_AE_hdlr_lookup, 0xA816, 0x0833, Pack8);


OSErr C__AE_hdlr_install(AE_hdlr_table_h , int32_t , GUEST<AE_hdlr_selector_t> *, GUEST<AE_hdlr_t> *);PASCAL_SUBTRAP(_AE_hdlr_install, 0xA816, 0x0831, Pack8);


BEGIN_EXECUTOR_ONLY
/* private */

extern bool send_application_open_aevt_p;
extern bool application_accepts_open_app_aevt_p;

/*  error codes */

#define AE_RETURN_ERROR(error)                         \
    do {                                               \
        OSErr _error_ = (error);                       \
                                                       \
        if(_error_ != noErr)                           \
            warning_unexpected("error `%d'", _error_); \
        return _error_;                                \
    } while(0)
END_EXECUTOR_ONLY


enum {invalidConnection = -609,
};

enum {errAECoercionFail = -1700,
errAEDescNotFound = -1701,
errAEWrongDataType = -1703,
errAENotAEDesc = -1704,
};

enum {errAEEventNotHandled = -1708,
errAEUnknownAddressType = -1716,
};

enum {errAEHandlerNotFound = -1717,
errAEIllegalIndex = -1719,
};

// types
enum {typeFSS = "fss "_4,
typeAEList = "list"_4,
typeAERecord = "reco"_4,
typeAppleEvent = "aevt"_4,
typeProcessSerialNumber = "psn "_4,
typeNull = "null"_4,
typeApplSignature = "sign"_4,
typeType = "type"_4,
typeWildCard = "****"_4,
typeAlias = "alis"_4,
typeBoolean = "bool"_4,
typeChar = "TEXT"_4,
typeSInt16 = "shor"_4,
typeSInt32 = "long"_4,
typeUInt32 = "magn"_4,
typeSInt64 = "comp"_4,
typeIEEE32BitFloatingPoint = "sing"_4,
typeIEEE64BitFloatingPoint = "doub"_4,
type128BitFloatingPoint = "ldbl"_4,
typeDecimalStruct = "decm"_4,
};

enum {keyAddressAttr = "addr"_4,
keyEventClassAttr = "evcl"_4,
keyEventIDAttr = "evid"_4,
keyProcessSerialNumber = "psn "_4,
keyDirectObject = "----"_4,
};

enum {kCoreEventClass = "aevt"_4,
kAEOpenApplication = "oapp"_4,
kAEOpenDocuments = "odoc"_4,
kAEPrintDocuments = "pdoc"_4,
kAEAnswer = "ansr"_4,
kAEQuitApplication = "quit"_4,
keySelectProc = "selh"_4,
};

// AppleEvents AEGizmo (true);
const LowMemGlobal<AE_info_ptr > AE_info{ 0x2B6 };
inline AE_info_ptr  LMGetAE_info() { return LM(AE_info); }
inline void LMSetAE_info(AE_info_ptr val) { LM(AE_info) = val; }
NOTRAP_FUNCTION2(LMGetAE_info);
NOTRAP_FUNCTION2(LMSetAE_info);


// prototypes go here
OSErr C_AEGetCoercionHandler(DescType from_type, DescType to_type, GUEST<AECoerceDescUPP> *hdlr_out, GUEST<int32_t> *refcon_out, Boolean *from_type_is_desc_p_out, Boolean system_handler_p);PASCAL_SUBTRAP(AEGetCoercionHandler, 0xA816, 0x0B24, Pack8);


OSErr C_AECreateDesc(DescType type, const void *data, Size data_size, AEDesc *desc_out);PASCAL_SUBTRAP(AECreateDesc, 0xA816, 0x0825, Pack8);


OSErr C_AEDisposeDesc(AEDesc *desc);PASCAL_SUBTRAP(AEDisposeDesc, 0xA816, 0x0204, Pack8);


OSErr C_AECoerceDesc(AEDesc *desc, DescType result_type, AEDesc *desc_out);PASCAL_SUBTRAP(AECoerceDesc, 0xA816, 0x0603, Pack8);


OSErr C_AEGetParamPtr(GUEST<AERecord> *record, AEKeyword keyword, DescType desired_type, GUEST<DescType> *type_out, Ptr data, Size max_size, GUEST<Size> *size_out);PASCAL_SUBTRAP(AEGetParamPtr, 0xA816, 0x0E11, Pack8);


OSErr C_AEGetParamDesc(GUEST<AERecord> *record, AEKeyword keyword, DescType desired_type, AEDesc *desc_out);PASCAL_SUBTRAP(AEGetParamDesc, 0xA816, 0x0812, Pack8);


OSErr C_AEPutParamPtr(GUEST<AERecord> *record, AEKeyword keyword, DescType type, const void *data, Size data_size);PASCAL_SUBTRAP(AEPutParamPtr, 0xA816, 0x0A0F, Pack8);


OSErr C_AEPutParamDesc(GUEST<AERecord> *record, AEKeyword keyword, AEDesc *desc);PASCAL_SUBTRAP(AEPutParamDesc, 0xA816, 0x0610, Pack8);


// extern OSErr C_AEDeleteParam(AppleEvent *evt, AEKeyword keyword);
// PASCAL_SUBTRAP(AEDeleteParam, 0xA816, 0x0413, Pack8);
// The following does not exist. Maybe it should be AEDeleteParam?
// extern OSErr C_AEDeleteAttribute(AppleEvent *evt,
//                                              AEKeyword keyword);
// PASCAL_SUBTRAP_UNKNOWN(AEDeleteAttribute, 0xA816, Pack8);
OSErr C_AESizeOfParam(GUEST<AERecord> *record, AEKeyword keyword, GUEST<DescType> *type_out, GUEST<Size> *size_out);PASCAL_SUBTRAP(AESizeOfParam, 0xA816, 0x0829, Pack8);


OSErr C_AESetInteractionAllowed(AEInteractionAllowed level);PASCAL_SUBTRAP(AESetInteractionAllowed, 0xA816, 0x011E, Pack8);


OSErr C_AEResetTimer(GUEST<AppleEvent> *evt);PASCAL_SUBTRAP(AEResetTimer, 0xA816, 0x0219, Pack8);


OSErr C_AEGetTheCurrentEvent(GUEST<AppleEvent> *return_evt);PASCAL_SUBTRAP(AEGetTheCurrentEvent, 0xA816, 0x021A, Pack8);


OSErr C_AESetTheCurrentEvent(GUEST<AppleEvent> *evt);PASCAL_SUBTRAP(AESetTheCurrentEvent, 0xA816, 0x022C, Pack8);


OSErr C_AESuspendTheCurrentEvent(GUEST<AppleEvent> *evt);PASCAL_SUBTRAP(AESuspendTheCurrentEvent, 0xA816, 0x022B, Pack8);


OSErr C_AEResumeTheCurrentEvent(GUEST<AppleEvent> *evt, GUEST<AppleEvent> *reply, AEEventHandlerUPP dispatcher, int32_t refcon);PASCAL_SUBTRAP(AEResumeTheCurrentEvent, 0xA816, 0x0818, Pack8);


OSErr C_AEGetInteractionAllowed(AEInteractionAllowed *return_level);PASCAL_SUBTRAP(AEGetInteractionAllowed, 0xA816, 0x021D, Pack8);


OSErr C_AEDuplicateDesc(AEDesc *src, AEDesc *dst);PASCAL_SUBTRAP(AEDuplicateDesc, 0xA816, 0x0405, Pack8);


OSErr C_AECountItems(GUEST<AEDescList> *list, GUEST<int32_t> *count_out);PASCAL_SUBTRAP(AECountItems, 0xA816, 0x0407, Pack8);


OSErr C_AEDeleteItem(GUEST<AEDescList> *list, int32_t index);PASCAL_SUBTRAP(AEDeleteItem, 0xA816, 0x040E, Pack8);


OSErr C_AEDeleteParam(GUEST<AERecord> *record, AEKeyword keyword);PASCAL_SUBTRAP(AEDeleteParam, 0xA816, 0x0413, Pack8);


OSErr C_AEInstallSpecialHandler(AEKeyword function_class, AEEventHandlerUPP hdlr, Boolean system_handler_p);PASCAL_SUBTRAP(AEInstallSpecialHandler, 0xA816, 0x0500, Pack8);


OSErr C_AERemoveSpecialHandler(AEKeyword function_class, AEEventHandlerUPP hdlr, Boolean system_handler_p);PASCAL_SUBTRAP(AERemoveSpecialHandler, 0xA816, 0x0501, Pack8);


OSErr C_AEGetSpecialHandler(AEKeyword function_class, GUEST<AEEventHandlerUPP> *hdlr_out, Boolean system_handler_p);PASCAL_SUBTRAP(AEGetSpecialHandler, 0xA816, 0x052D, Pack8);


OSErr C_AESend(GUEST<AppleEvent> *evt, GUEST<AppleEvent> *reply, AESendMode send_mode, AESendPriority send_priority, int32_t timeout, IdleUPP idle_proc, EventFilterUPP filter_proc);PASCAL_SUBTRAP(AESend, 0xA816, 0x0D17, Pack8);


OSErr C_AECoercePtr(DescType data_type, Ptr data, Size data_size, DescType result_type, AEDesc *desc_out);PASCAL_SUBTRAP(AECoercePtr, 0xA816, 0x0A02, Pack8);


OSErr C_AEGetEventHandler(AEEventClass event_class, AEEventID event_id, GUEST<AEEventHandlerUPP> *hdlr, GUEST<int32_t> *refcon, Boolean system_handler_p);PASCAL_SUBTRAP(AEGetEventHandler, 0xA816, 0x0921, Pack8);


OSErr C_AERemoveEventHandler(AEEventClass event_class, AEEventID event_id, AEEventHandlerUPP hdlr, Boolean system_handler_p);PASCAL_SUBTRAP(AERemoveEventHandler, 0xA816, 0x0720, Pack8);


OSErr C_AEProcessAppleEvent(EventRecord *evt);PASCAL_SUBTRAP(AEProcessAppleEvent, 0xA816, 0x021B, Pack8);


OSErr C_AEPutDesc(GUEST<AEDescList> *list, int32_t index, AEDesc *desc);PASCAL_SUBTRAP(AEPutDesc, 0xA816, 0x0609, Pack8);


OSErr C_AEPutAttributePtr(GUEST<AppleEvent> *evt, AEKeyword keyword, DescType type, const void *data, Size size);PASCAL_SUBTRAP(AEPutAttributePtr, 0xA816, 0x0A16, Pack8);


OSErr C_AEPutAttributeDesc(GUEST<AppleEvent> *evt, AEKeyword keyword, AEDesc *desc);PASCAL_SUBTRAP(AEPutAttributeDesc, 0xA816, 0x0627, Pack8);


OSErr C_AEGetNthPtr(GUEST<AEDescList> *list, int32_t index, DescType desired_type, GUEST<AEKeyword> *keyword_out, GUEST<DescType> *type_out, void *data, int32_t max_size, GUEST<int32_t> *size_out);PASCAL_SUBTRAP(AEGetNthPtr, 0xA816, 0x100A, Pack8);


OSErr C_AEGetAttributePtr(GUEST<AppleEvent> *evt, AEKeyword keyword, DescType desired_type, GUEST<DescType> *type_out, void *data, Size max_size, GUEST<Size> *size_out);PASCAL_SUBTRAP(AEGetAttributePtr, 0xA816, 0x0E15, Pack8);


OSErr C_AEGetArray(GUEST<AEDescList> *list, AEArrayType array_type, AEArrayDataPointer array_ptr, Size max_size, GUEST<DescType> *return_item_type, GUEST<Size> *return_item_size, GUEST<int32_t> *return_item_count);PASCAL_SUBTRAP(AEGetArray, 0xA816, 0x0D0C, Pack8);


OSErr C_AECreateAppleEvent(AEEventClass event_class, AEEventID event_id, GUEST<AEAddressDesc> *target, int16_t return_id, int32_t transaction_id, GUEST<AppleEvent> *evt);PASCAL_SUBTRAP(AECreateAppleEvent, 0xA816, 0x0B14, Pack8);


OSErr C_AEInstallCoercionHandler(DescType from_type, DescType to_type, AECoerceDescUPP hdlr, int32_t refcon, Boolean from_type_is_desc_p, Boolean system_handler_p);PASCAL_SUBTRAP(AEInstallCoercionHandler, 0xA816, 0x0A22, Pack8);


OSErr C_AEInstallEventHandler(AEEventClass event_class, AEEventID event_id, AEEventHandlerUPP hdlr, int32_t refcon, Boolean system_handler_p);PASCAL_SUBTRAP(AEInstallEventHandler, 0xA816, 0x091F, Pack8);


OSErr C_AERemoveCoercionHandler(DescType from_type, DescType to_type, AECoerceDescUPP hdlr, Boolean system_handler_p);PASCAL_SUBTRAP(AERemoveCoercionHandler, 0xA816, 0x0723, Pack8);


OSErr C_AEPutArray(GUEST<AEDescList> *list, AEArrayType type, AEArrayDataPointer array_data, DescType item_type, Size item_size, int32_t item_count);PASCAL_SUBTRAP(AEPutArray, 0xA816, 0x0B0D, Pack8);


OSErr C_AECreateList(Ptr list_elt_prefix, Size list_elt_prefix_size, Boolean is_record_p, GUEST<AEDescList> *list_out);PASCAL_SUBTRAP(AECreateList, 0xA816, 0x0706, Pack8);


OSErr C_AEGetAttributeDesc(GUEST<AppleEvent> *evt, AEKeyword keyword, DescType desired_type, AEDesc *desc_out);PASCAL_SUBTRAP(AEGetAttributeDesc, 0xA816, 0x0826, Pack8);


OSErr C_AESizeOfAttribute(GUEST<AppleEvent> *evt, AEKeyword keyword, GUEST<DescType> *type_out, GUEST<Size> *size_out);PASCAL_SUBTRAP(AESizeOfAttribute, 0xA816, 0x0828, Pack8);


OSErr C_AEGetNthDesc(GUEST<AEDescList> *list, int32_t index, DescType desired_type, GUEST<AEKeyword> *keyword_out, AEDesc *desc_out);PASCAL_SUBTRAP(AEGetNthDesc, 0xA816, 0x0A0B, Pack8);


OSErr C_AESizeOfNthItem(GUEST<AEDescList> *list, int32_t index, GUEST<DescType> *type_out, GUEST<Size> *size_out);PASCAL_SUBTRAP(AESizeOfNthItem, 0xA816, 0x082A, Pack8);


OSErr C_AEPutPtr(GUEST<AEDescList> *list, int32_t index, DescType type, const void *data, Size data_size);PASCAL_SUBTRAP(AEPutPtr, 0xA816, 0x0A08, Pack8);


OSErr C_AEInteractWithUser(int32_t timeout, NMRecPtr nm_req, IdleUPP idle_proc);PASCAL_SUBTRAP(AEInteractWithUser, 0xA816, 0x061C, Pack8);


// MacPhoenix: (keyWord, result *), 8 bytes, as the selector says (was
// declared with the result alone). 7.5.5's PACK 8 has no routine $41
// (its table ends at $3E), so the call answers paramErr there; Disk Copy
// 6.3.3 takes that and refuses to run on noErr with a version of 0.
OSErr C_AEManagerInfo(AEKeyword keyWord, GUEST<LONGINT> *resultp);PASCAL_SUBTRAP(AEManagerInfo, 0xA816, 0x0441, Pack8);


OSErr C_AEDisposeToken(AEDesc *theToken);PASCAL_SUBTRAP(AEDisposeToken, 0xA816, 0x023A, Pack8);


OSErr C_AEResolve(AEDesc *objectSpecifier, INTEGER callbackFlags, AEDesc *theToken);PASCAL_SUBTRAP(AEResolve, 0xA816, 0x0536, Pack8);


OSErr C_AERemoveObjectAccessor(DescType desiredClass, DescType containerType, ProcPtr theAccessor, Boolean isSysHandler);PASCAL_SUBTRAP(AERemoveObjectAccessor, 0xA816, 0x0738, Pack8);


OSErr C_AEInstallObjectAccessor(DescType desiredClass, DescType containerType, ProcPtr theAccessor, LONGINT refcon, Boolean isSysHandler);PASCAL_SUBTRAP(AEInstallObjectAccessor, 0xA816, 0x0937, Pack8);


OSErr C_AEGetObjectAccessor(DescType desiredClass, DescType containerType, GUEST<ProcPtr> *theAccessor, GUEST<LONGINT> *accessorRefcon, Boolean isSysHandler);PASCAL_SUBTRAP(AEGetObjectAccessor, 0xA816, 0x0939, Pack8);


OSErr C_AECallObjectAccessor(DescType desiredClass, AEDesc *containerToken, DescType containerClass, DescType keyForm, AEDesc *keyData, AEDesc *theToken);PASCAL_SUBTRAP(AECallObjectAccessor, 0xA816, 0x0C3B, Pack8);


OSErr C_AESetObjectCallbacks(ProcPtr myCompareProc, ProcPtr myCountProc, ProcPtr myDisposeTokenProc, ProcPtr myGetMarkTokenProc, ProcPtr myMarkProc, ProcPtr myAdjustMarksProc, ProcPtr myGetErrDescProc);PASCAL_SUBTRAP(AESetObjectCallbacks, 0xA816, 0x0E35, Pack8);


} /* namespace Executor */