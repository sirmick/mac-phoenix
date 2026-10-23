/*****************************************************************************
 *                                                                           *
 *                                 IntlUtil.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    Byte, LONGINT, INTEGER, SignedByte, StringPtr,          *
 *                   Handle, Boolean, Ptr, ConstStringPtr, Str255,           *
 *                   ScriptCode, LangCode, Str63                             *
 *     SANE.h        comp                                                    *
 *     EventMgr.h    EventRecord                                             *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "SANE.h"
#include "EventMgr.h"

#define MODULE_NAME IntlUtil
#include <base/api-module.h>


namespace Executor {

enum {currSymLead = 16,
currNegSym = 32,
currTrailingZ = 64,
currLeadingZ = 128,
};

enum {mdy = 0,
dmy = 1,
ymd = 2,
};

enum {dayLdingZ = 32,
mntLdingZ = 64,
century = 128,
};

enum {secLeadingZ = 32,
minLeadingZ = 64,
hrLeadingZ = 128,
};

enum {verUS = 0,
verFrance = 1,
verBritain = 2,
verGermany = 3,
verItaly = 4,
verNetherlands = 5,
verBelgiumLux = 6,
verSweden = 7,
verSpain = 8,
verDenmark = 9,
verPortugal = 10,
verFrCanada = 11,
verNorway = 12,
verIsreal = 13,
verJapan = 14,
verAustralia = 15,
verArabia = 16,
verFinland = 17,
verFrSwiss = 18,
verGrSwiss = 19,
verGreece = 20,
verIceland = 21,
verMalta = 22,
verCyprus = 23,
verTurkey = 24,
verYugoslavia = 25,
};

struct Intl0Rec{GUEST_STRUCT;Byte decimalPt;Byte thousSep;Byte listSep;Byte currSym1;Byte currSym2;Byte currSym3;Byte currFmt;Byte dateOrder;Byte shrtDateFmt;Byte dateSep;Byte timeCycle;Byte timeFmt;GUEST<LONGINT> mornStr;GUEST<LONGINT> eveStr;Byte timeSep;Byte time1Suff;Byte time2Suff;Byte time3Suff;Byte time4Suff;Byte time5Suff;Byte time6Suff;Byte time7Suff;Byte time8Suff;Byte metricSys;GUEST<INTEGER> intl0Vers;};static_assert(sizeof(Intl0Rec) == 32);void describeStruct(const Intl0Rec&, std::ostream&);

typedef Intl0Rec *Intl0Ptr;

typedef GUEST<Intl0Ptr> *Intl0Hndl;

typedef Byte STRING15[16];

struct Intl1Rec{GUEST_STRUCT;GUEST<STRING15> days[7];GUEST<STRING15> months[12];Byte suppressDay;Byte lngDateFmt;Byte dayLeading0;Byte abbrLen;GUEST<LONGINT> st0;GUEST<LONGINT> st1;GUEST<LONGINT> st2;GUEST<LONGINT> st3;GUEST<LONGINT> st4;GUEST<INTEGER> intl1Vers;GUEST<INTEGER> localRtn;};static_assert(sizeof(Intl1Rec) == 332);void describeStruct(const Intl1Rec&, std::ostream&);

typedef Intl1Rec *Intl1Ptr;

typedef GUEST<Intl1Ptr> *Intl1Hndl;

typedef comp LongDateTime;

typedef SignedByte DateForm;

enum {shortDate = 0,
longDate = 1,
abbrevDate = 2,
};

DISPATCHER_TRAP(Pack6, 0xA9ED, StackW);


void C_IUDatePString(LONGINT date, DateForm form, StringPtr p, Handle h);PASCAL_SUBTRAP(IUDatePString, 0xA9ED, 0x000E, Pack6);


Handle C_GetIntlResource(INTEGER id);PASCAL_SUBTRAP(GetIntlResource, 0xA9ED, 0x0006, Pack6);


void C_IUDateString(LONGINT date, DateForm form, StringPtr p);PASCAL_SUBTRAP(IUDateString, 0xA9ED, 0x0000, Pack6);


void C_IUTimePString(LONGINT date, Boolean secs, StringPtr p, Handle h);PASCAL_SUBTRAP(IUTimePString, 0xA9ED, 0x0010, Pack6);


void C_IUTimeString(LONGINT date, Boolean secs, StringPtr p);PASCAL_SUBTRAP(IUTimeString, 0xA9ED, 0x0002, Pack6);


Boolean C_IsMetric();PASCAL_SUBTRAP(IsMetric, 0xA9ED, 0x0004, Pack6);


void C_SetIntlResource(INTEGER rn, INTEGER id, Handle newh);PASCAL_SUBTRAP(SetIntlResource, 0xA9ED, 0x0008, Pack6);


INTEGER C_IUMagString(Ptr ptr1, Ptr ptr2, INTEGER len1, INTEGER len2);PASCAL_SUBTRAP(IUMagString, 0xA9ED, 0x000A, Pack6);


INTEGER IUCompString(ConstStringPtr str1, ConstStringPtr str2);

INTEGER C_IUMagIDString(Ptr ptr1, Ptr ptr2, INTEGER len1, INTEGER len2);PASCAL_SUBTRAP(IUMagIDString, 0xA9ED, 0x000C, Pack6);


INTEGER IUEqualString(ConstStringPtr str1, ConstStringPtr str2);

void C_IUMystery(Ptr arg1, Ptr arg2, INTEGER arg3, INTEGER arg4);PASCAL_SUBTRAP(IUMystery, 0xA9ED, 0x0012, Pack6);


void C_IULDateString(GUEST<LongDateTime> *datetimep, DateForm longflag, Str255 result, Handle intlhand);PASCAL_SUBTRAP(IULDateString, 0xA9ED, 0x0014, Pack6);


void C_IULTimeString(GUEST<LongDateTime> *datetimep, Boolean wantseconds, Str255 result, Handle intlhand);PASCAL_SUBTRAP(IULTimeString, 0xA9ED, 0x0016, Pack6);


void C_ClearIntlResourceCache();PASCAL_SUBTRAP(ClearIntlResourceCache, 0xA9ED, 0x0018, Pack6);


INTEGER C_IUMagPString(Ptr ptra, Ptr ptrb, INTEGER lena, INTEGER lenb, Handle itl2hand);PASCAL_SUBTRAP(IUMagPString, 0xA9ED, 0x001A, Pack6);


INTEGER C_IUMagIDPString(Ptr ptra, Ptr ptrb, INTEGER lena, INTEGER lenb, Handle itl2hand);PASCAL_SUBTRAP(IUMagIDPString, 0xA9ED, 0x001C, Pack6);


INTEGER C_IUScriptOrder(ScriptCode script1, ScriptCode script2);PASCAL_SUBTRAP(IUScriptOrder, 0xA9ED, 0x001E, Pack6);


INTEGER C_IULangOrder(LangCode l1, LangCode l2);PASCAL_SUBTRAP(IULangOrder, 0xA9ED, 0x0020, Pack6);


INTEGER C_IUTextOrder(Ptr ptra, Ptr ptrb, INTEGER lena, INTEGER lenb, ScriptCode scripta, ScriptCode bscript, LangCode langa, LangCode langb);PASCAL_SUBTRAP(IUTextOrder, 0xA9ED, 0x0022, Pack6);


void C_GetIntlResourceTable(ScriptCode script, INTEGER tablecode, GUEST<Handle> *itlhandlep, GUEST<LONGINT> *offsetp, GUEST<LONGINT> *lengthp);PASCAL_SUBTRAP(GetIntlResourceTable, 0xA9ED, 0x0024, Pack6);


// MacPhoenix (name is a guess). Apple's Standard File (PACK 3, +$2f16)
// asks it with (2, the item's script, -1) and hands the handle to
// IUMagPString as its 'itl2' sorting table, caching it per script. One
// script here, so GetIntlResource(theID).
Handle C_GetIntlResourceForScript(INTEGER theID, ScriptCode script, INTEGER flag);PASCAL_SUBTRAP(GetIntlResourceForScript, 0xA9ED, 0x0032, Pack6);


struct TypeSelectRecord{GUEST_STRUCT;GUEST<uint32_t> tsrLastKeyTime;GUEST<ScriptCode> tsrScript;GUEST<Str63> tsrKeyStrokes;};void describeStruct(const TypeSelectRecord&, std::ostream&);

using IndexToStringUPP = UPP<Boolean(INTEGER item, GUEST<ScriptCode> *itemsScript, GUEST<StringPtr> *itemsStringPtr, void *yourDataPtr)>;


void C_TypeSelectClear(TypeSelectRecord *tsr);PASCAL_SUBTRAP(TypeSelectClear, 0xA9ED, 0x0028, Pack6);


Boolean C_TypeSelectNewKey(EventRecord *theEvent, TypeSelectRecord *tsr);PASCAL_SUBTRAP(TypeSelectNewKey, 0xA9ED, 0x002A, Pack6);


INTEGER C_TypeSelectFindItem(TypeSelectRecord *tsr, INTEGER listSize, INTEGER selectMode, IndexToStringUPP getStringProc, void *getStringRefCon);PASCAL_SUBTRAP(TypeSelectFindItem, 0xA9ED, 0x002C, Pack6);


INTEGER C_TypeSelectCompare(TypeSelectRecord *tsr, ScriptCode testStringScript, StringPtr testStringPtr);PASCAL_SUBTRAP(TypeSelectCompare, 0xA9ED, 0x002E, Pack6);


} /* namespace Executor */