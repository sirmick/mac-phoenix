/*****************************************************************************
 *                                                                           *
 *                                ScriptMgr.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    INTEGER, Byte, SignedByte, ResType, LONGINT, Ptr,       *
 *                   ULONGINT, OSErr, Handle, Boolean, Fixed, ScriptCode,    *
 *                   Str15, Str255, ConstStringPtr                           *
 *     SANE.h        extended80                                              *
 *     EventMgr.h    EventRecord                                             *
 *     IntlUtil.h    LongDateTime                                            *
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
#include "IntlUtil.h"

#define MODULE_NAME ScriptMgr
#include <base/api-module.h>


namespace Executor {

typedef INTEGER String2DateStatus;

enum {smRoman = 0,
};

enum {smWordSelectTable = 0,
smWordWrapTable = 1,
smNumberPartsTable = 2,
smUnTokenTable = 3,
smWhiteSpaceList = 4,
};

enum {smCharPunct = 0,
smCharAscii = 1,
smCharEuro = 7,
};

enum {smPunctNormal = 0,
smPunctNumber = 256,
smPunctSymbol = 512,
smPunctBlank = 768,
};

enum {smCharLeft = 0,
smCharRight = 8192,
};

enum {smCharLower = 0,
smCharUpper = 16384,
};

enum {smChar1byte = 0,
smChar2byte = 32768,
};

enum {smTransAscii = 0,
smTransNative = 1,
smTransLower = 16384,
smTransUpper = 32768,
smMaskAscii = 1,
smMaskNative = 2,
smMaskAll = -1,
};

enum {smScriptVersion = 0,
smScriptMunged = 2,
smScriptEnabled = 4,
smScriptRight = 6,
smScriptJust = 8,
smScriptRedraw = 10,
smScriptSysFond = 12,
smScriptAppFond = 14,
smScriptNumber = 16,
smScriptDate = 18,
smScriptSort = 20,
smScriptRsvd1 = 22,
smScriptRsvd2 = 24,
smScriptRsvd3 = 26,
smScriptRsvd4 = 28,
smScriptRsvd5 = 30,
smScriptKeys = 32,
smScriptIcon = 34,
smScriptPrint = 36,
smScriptTrap = 38,
smScriptCreator = 40,
smScriptFile = 42,
smScriptName = 44,
smScriptMonoFondSize = 78,
smScriptPrefFondSize = 80,
smScriptSmallFondSize = 82,
smScriptSysFondSize = 84,
smScriptAppFondSize = 86,
smScriptHelpFondSize = 88,
smScriptValidStyles = 90,
smScriptAliasStyle = 92,
};

enum {smVersion = 0,
smMunged = 2,
smEnabled = 4,
smBiDirect = 6,
smFontForce = 8,
smIntlForce = 10,
smForced = 12,
smDefault = 14,
smPrint = 16,
smSysScript = 18,
smAppScript = 20,
smKeyScript = 22,
smSysRef = 24,
smKeyCache = 26,
smKeySwap = 28,
};

enum {smKCHRCache = 38,
};

enum {smVerbNotFound = -1,
};

enum {smBreakWord = 0,
smBreakChar = 1,
smBreakOverflow = 2,
};

// TruncText return codes
enum {NotTruncated = 0,
Truncated = 1,
TruncErr = -1,
};

typedef uint8_t StyledLineBreakCode;

struct DateCacheRecord{GUEST_STRUCT;GUEST<int16_t> hidden[256];};static_assert(sizeof(DateCacheRecord) == 512);void describeStruct(const DateCacheRecord&, std::ostream&);

typedef DateCacheRecord *DateCachePtr;

struct LongDateRec{GUEST_STRUCT;GUEST<int16_t> era;GUEST<int16_t> year;GUEST<int16_t> month;GUEST<int16_t> day;GUEST<int16_t> hour;GUEST<int16_t> minute;GUEST<int16_t> second;GUEST<int16_t> dayOfWeek;GUEST<int16_t> dayOfYear;GUEST<int16_t> weekOfYear;GUEST<int16_t> pm;GUEST<int16_t> res1;GUEST<int16_t> res2;GUEST<int16_t> res3;};static_assert(sizeof(LongDateRec) == 28);void describeStruct(const LongDateRec&, std::ostream&);

typedef LongDateRec *LongDatePtr;

typedef INTEGER TruncCode;

typedef int16_t JustStyleCode;

typedef int8_t CharByteTable[256];

// Not sure this is correct, since in IM
// 				  ScriptRunStatus is a record with two
// 				  Signed Bytes
typedef int16_t ScriptRunStatus;

typedef INTEGER FormatStatus;

struct NumFormatStringRec{GUEST_STRUCT;Byte fLength;Byte fVersion;SignedByte data[254];};static_assert(sizeof(NumFormatStringRec) == 256);void describeStruct(const NumFormatStringRec&, std::ostream&);

typedef uint16_t WideChar;

struct WideCharArr{GUEST_STRUCT;GUEST<INTEGER> size;GUEST<WideChar> data[10];};static_assert(sizeof(WideCharArr) == 22);void describeStruct(const WideCharArr&, std::ostream&);

struct NumberParts{GUEST_STRUCT;GUEST<INTEGER> version;GUEST<WideChar> data[31];WideCharArr pePlus;WideCharArr peMinus;WideCharArr peMinusPlus;WideCharArr altNumTable;char reserved[20];};static_assert(sizeof(NumberParts) == 172);void describeStruct(const NumberParts&, std::ostream&);

typedef extended80 Extended80;

typedef int16_t ToggleResults;

typedef uint8_t LongDateField;

typedef char DateDelta;

struct TogglePB{GUEST_STRUCT;GUEST<int32_t> togFlags;GUEST<ResType> amChars;GUEST<ResType> pmChars;GUEST<int32_t> reserved[4];};static_assert(sizeof(TogglePB) == 28);void describeStruct(const TogglePB&, std::ostream&);

enum {smSystemScript = -1,
};

// ScriptMgr ToolEqu.a (true-b);
const LowMemGlobal<INTEGER > TESysJust{ 0xBAC };
inline INTEGER  LMGetTESysJust() { return LM(TESysJust); }
inline void LMSetTESysJust(INTEGER val) { LM(TESysJust) = val; }
NOTRAP_FUNCTION2(LMGetTESysJust);
NOTRAP_FUNCTION2(LMSetTESysJust);


DISPATCHER_TRAP(ScriptUtil, 0xA8B5, StackL);


LONGINT C_VisibleLength(Ptr textp, LONGINT len);PASCAL_SUBTRAP(VisibleLength, 0xA8B5, 0x84080028, ScriptUtil);


void C_LongDateToSeconds(LongDateRec *ldatep, GUEST<ULONGINT> *secs_outp);PASCAL_SUBTRAP(LongDateToSeconds, 0xA8B5, 0x8008FFF2, ScriptUtil);


void C_LongSecondsToDate(GUEST<ULONGINT> *secs_inp, LongDateRec *ldatep);PASCAL_SUBTRAP(LongSecondsToDate, 0xA8B5, 0x8008FFF0, ScriptUtil);


LONGINT C_GetScriptManagerVariable(INTEGER verb);PASCAL_SUBTRAP(GetScriptManagerVariable, 0xA8B5, 0x84020008, ScriptUtil);


OSErr C_SetScriptManagerVariable(INTEGER verb, LONGINT param);PASCAL_SUBTRAP(SetScriptManagerVariable, 0xA8B5, 0x8206000A, ScriptUtil);


LONGINT C_GetScriptVariable(INTEGER script, INTEGER verb);PASCAL_SUBTRAP(GetScriptVariable, 0xA8B5, 0x8404000C, ScriptUtil);


OSErr C_SetScriptVariable(INTEGER script, INTEGER verb, LONGINT param);PASCAL_SUBTRAP(SetScriptVariable, 0xA8B5, 0x8208000E, ScriptUtil);


INTEGER C_FontToScript(INTEGER fontnum);PASCAL_SUBTRAP(FontToScript, 0xA8B5, 0x82020006, ScriptUtil);


INTEGER C_Transliterate(Handle srch, Handle dsth, INTEGER target, LONGINT srcmask);PASCAL_SUBTRAP(Transliterate, 0xA8B5, 0x820E0018, ScriptUtil);


INTEGER C_FontScript();PASCAL_SUBTRAP(FontScript, 0xA8B5, 0x82000000, ScriptUtil);


INTEGER C_IntlScript();PASCAL_SUBTRAP(IntlScript, 0xA8B5, 0x82000002, ScriptUtil);


Boolean C_IsCmdChar(const EventRecord *event, INTEGER test);PASCAL_SUBTRAP(IsCmdChar, 0xA8B5, 0x8206FFD0, ScriptUtil);


void C_KeyScript(INTEGER scriptcode);PASCAL_SUBTRAP(KeyScript, 0xA8B5, 0x80020004, ScriptUtil);


// MacPhoenix (name is a guess). Selector -82, 6 bytes of parameters, no
// result. Apple's Standard File (PACK 3, +$25c) calls it with (2, a
// pointer to {0, script}) right before KeyScript(script) when it sets
// up its dialog; a no-op here.
void C_SetKeyScriptState(INTEGER verb, Ptr param);PASCAL_SUBTRAP(SetKeyScriptState, 0xA8B5, 0x8006FFAE, ScriptUtil);


INTEGER C_CharType(Ptr textbufp, INTEGER offset);PASCAL_SUBTRAP(CharType, 0xA8B5, 0x82060012, ScriptUtil);


void C_MeasureJust(Ptr textbufp, INTEGER length, INTEGER slop, Ptr charlocs);PASCAL_SUBTRAP(MeasureJust, 0xA8B5, 0x800C0020, ScriptUtil);


INTEGER C_Pixel2Char(Ptr textbufp, INTEGER len, INTEGER slop, INTEGER pixwidth, Boolean *leftsidep);PASCAL_SUBTRAP(Pixel2Char, 0xA8B5, 0x820E0014, ScriptUtil);


INTEGER C_Char2Pixel(Ptr textbufp, INTEGER len, INTEGER slop, INTEGER offset, SignedByte dir);PASCAL_SUBTRAP(Char2Pixel, 0xA8B5, 0x820C0016, ScriptUtil);


void C_FindWord(Ptr textbufp, INTEGER length, INTEGER offset, Boolean leftside, Ptr breaks, GUEST<INTEGER> *offsets);PASCAL_SUBTRAP(FindWord, 0xA8B5, 0x8012001A, ScriptUtil);


void C_HiliteText(Ptr textbufp, INTEGER firstoffset, INTEGER secondoffset, GUEST<INTEGER> *offsets);PASCAL_SUBTRAP(HiliteText, 0xA8B5, 0x800E001C, ScriptUtil);


void C_DrawJust(Ptr textbufp, INTEGER length, INTEGER slop);PASCAL_SUBTRAP(DrawJust, 0xA8B5, 0x8008001E, ScriptUtil);


String2DateStatus C_StringToTime(Ptr textp, LONGINT len, Ptr cachep, GUEST<LONGINT> *lenusedp, GUEST<Ptr> *datetimep);PASCAL_SUBTRAP(StringToTime, 0xA8B5, 0x8214FFF4, ScriptUtil);


INTEGER GetSysDirection();

void SetSysDirection(INTEGER just);

OSErr C_InitDateCache(DateCachePtr theCache);PASCAL_SUBTRAP(InitDateCache, 0xA8B5, 0x8204FFF8, ScriptUtil);


INTEGER C_CharByte(Ptr textBuf, INTEGER textOffset);PASCAL_SUBTRAP(CharByte, 0xA8B5, 0x82060010, ScriptUtil);


String2DateStatus C_StringToDate(Ptr text, int32_t length, DateCachePtr cache, GUEST<int32_t> *length_used_ret, LongDatePtr date_time);PASCAL_SUBTRAP(StringToDate, 0xA8B5, 0x8214FFF6, ScriptUtil);


StyledLineBreakCode C_StyledLineBreak(Ptr textp, int32_t length, int32_t text_start, int32_t text_end, int32_t flags, GUEST<Fixed> *text_width_fp, GUEST<int32_t> *text_offset);PASCAL_SUBTRAP(StyledLineBreak, 0xA8B5, 0x821CFFFE, ScriptUtil);


void C_MeasureJustified(Ptr text, int32_t length, Fixed slop, Ptr charLocs, JustStyleCode run_pos, Point numer, Point denom);PASCAL_SUBTRAP(MeasureJustified, 0xA8B5, 0x801A0034, ScriptUtil);


Boolean C_ParseTable(CharByteTable table);PASCAL_SUBTRAP(ParseTable, 0xA8B5, 0x82040022, ScriptUtil);


Boolean C_FillParseTable(CharByteTable table, ScriptCode script);PASCAL_SUBTRAP(FillParseTable, 0xA8B5, 0xC2040022, ScriptUtil);


INTEGER C_ReplaceText(Handle base_text, Handle subst_text, Str15 key);PASCAL_SUBTRAP(ReplaceText, 0xA8B5, 0x820CFFDC, ScriptUtil);


INTEGER C_TruncText(INTEGER width, Ptr textPtr, GUEST<INTEGER> *length, TruncCode code);PASCAL_SUBTRAP(TruncText, 0xA8B5, 0x820CFFDE, ScriptUtil);


INTEGER C_TruncString(INTEGER width, Str255 string, TruncCode code);PASCAL_SUBTRAP(TruncString, 0xA8B5, 0x8208FFE0, ScriptUtil);


FormatStatus C_StringToExtended(Str255 string, NumFormatStringRec *formatp, NumberParts *partsp, GUEST<Extended80> *xp);PASCAL_SUBTRAP(StringToExtended, 0xA8B5, 0x8210FFE6, ScriptUtil);


FormatStatus C_ExtendedToString(GUEST<Extended80> *xp, NumFormatStringRec *formatp, NumberParts *partsp, Str255 string);PASCAL_SUBTRAP(ExtendedToString, 0xA8B5, 0x8210FFE8, ScriptUtil);


FormatStatus C_StringToFormatRec(ConstStringPtr in_string, NumberParts *partsp, NumFormatStringRec *out_string);PASCAL_SUBTRAP(StringToFormatRec, 0xA8B5, 0x820CFFEC, ScriptUtil);


ToggleResults C_ToggleDate(GUEST<LongDateTime> *lsecsp, LongDateField field, DateDelta delta, INTEGER ch, TogglePB *paramsp);PASCAL_SUBTRAP(ToggleDate, 0xA8B5, 0x820EFFEE, ScriptUtil);


Fixed C_PortionLine(Ptr textPtr, LONGINT textLen, JustStyleCode styleRunPosition, Point numer, Point denom);PASCAL_SUBTRAP(PortionLine, 0xA8B5, 0x84120036, ScriptUtil);


void C_DrawJustified(Ptr textPtr, LONGINT textLength, Fixed slop, JustStyleCode styleRunPosition, Point numer, Point denom);PASCAL_SUBTRAP(DrawJustified, 0xA8B5, 0x80160032, ScriptUtil);


ScriptRunStatus C_FindScriptRun(Ptr textPtr, LONGINT textLen, GUEST<LONGINT> *lenUsedp);PASCAL_SUBTRAP(FindScriptRun, 0xA8B5, 0x820C0026, ScriptUtil);


INTEGER C_PixelToChar(Ptr textBuf, LONGINT textLen, Fixed slop, Fixed pixelWidth, Boolean *leadingEdgep, GUEST<Fixed> *widthRemainingp, JustStyleCode styleRunPosition, Point numer, Point denom);PASCAL_SUBTRAP(PixelToChar, 0xA8B5, 0x8222002E, ScriptUtil);


INTEGER C_CharToPixel(Ptr textBuf, LONGINT textLen, Fixed slop, LONGINT offset, INTEGER direction, JustStyleCode styleRunPosition, Point numer, Point denom);PASCAL_SUBTRAP(CharToPixel, 0xA8B5, 0x821C0030, ScriptUtil);


void C_LowercaseText(Ptr textp, INTEGER len, ScriptCode script);NOTRAP_FUNCTION(LowercaseText);


void C_UppercaseText(Ptr textp, INTEGER len, ScriptCode script);NOTRAP_FUNCTION(UppercaseText);


void C_StripDiacritics(Ptr textp, INTEGER len, ScriptCode script);NOTRAP_FUNCTION(StripDiacritics);


void C_UppercaseStripDiacritics(Ptr textp, INTEGER len, ScriptCode script);NOTRAP_FUNCTION(UppercaseStripDiacritics);


// the above four functions are actually one entry point on 68K:
void C_TextUtilFunctions(int16_t selector, Ptr textp, INTEGER len, ScriptCode script);PASCAL_SUBTRAP(TextUtilFunctions, 0xA8B5, 0x800AFFB6, ScriptUtil);


INTEGER C_CharacterByteType(Ptr textBuf, INTEGER textOffset, ScriptCode script);PASCAL_SUBTRAP(CharacterByteType, 0xA8B5, 0xC2060010, ScriptUtil);


INTEGER C_CharacterType(Ptr textbufp, INTEGER offset, ScriptCode script);PASCAL_SUBTRAP(CharacterType, 0xA8B5, 0xC2060012, ScriptUtil);


INTEGER C_TransliterateText(Handle srch, Handle dsth, INTEGER target, LONGINT srcmask, ScriptCode script);PASCAL_SUBTRAP(TransliterateText, 0xA8B5, 0xC20E0018, ScriptUtil);


} /* namespace Executor */