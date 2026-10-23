/*****************************************************************************
 *                                                                           *
 *                               ToolboxUtil.h                               *
 *                               =============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     LONGINT, Fract, Fixed, INTEGER, StringHandle,          *
 *                    ConstStringPtr, StringPtr, Handle, Ptr, Boolean,       *
 *                    Byte                                                   *
 *     QuickDraw.h    Pattern, CursHandle, PicHandle                         *
 *     SANE.h         extended80                                             *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "QuickDraw.h"
#include "SANE.h"

#define MODULE_NAME ToolboxUtil
#include <base/api-module.h>


namespace Executor {

enum {sysPatListID = 0,
};

enum {iBeamCursor = 1,
crossCursor = 2,
plusCursor = 3,
watchCursor = 4,
};

struct Int64Bit{GUEST_STRUCT;GUEST<LONGINT> hiLong;GUEST<LONGINT> loLong;};static_assert(sizeof(Int64Bit) == 8);void describeStruct(const Int64Bit&, std::ostream&);

typedef Pattern *PatPtr;

typedef GUEST<PatPtr> *PatHandle;

Fract C_FracSqrt(Fract x);PASCAL_TRAP(FracSqrt, 0xA849);


Fract C_FracSin(Fixed x);PASCAL_TRAP(FracSin, 0xA848);


Fixed C_FixATan2(LONGINT x, LONGINT y);PASCAL_TRAP(FixATan2, 0xA818);


Fract C_FracCos(Fixed x);PASCAL_TRAP(FracCos, 0xA847);


Fixed C_FixRatio(INTEGER n, INTEGER d);PASCAL_TRAP(FixRatio, 0xA869);


Fixed C_FixMul(Fixed a, Fixed b);PASCAL_TRAP(FixMul, 0xA868);


INTEGER C_FixRound(Fixed x);PASCAL_TRAP(FixRound, 0xA86C);


StringHandle C_NewString(ConstStringPtr s);PASCAL_TRAP(NewString, 0xA906);


void C_SetString(StringHandle h, ConstStringPtr s);PASCAL_TRAP(SetString, 0xA907);


StringHandle C_GetString(INTEGER i);PASCAL_TRAP(GetString, 0xA9BA);


void C_GetIndString(StringPtr s, INTEGER sid, INTEGER index);NOTRAP_FUNCTION(GetIndString);


LONGINT C_Munger(Handle h, LONGINT off, Ptr p1, LONGINT len1, Ptr p2, LONGINT len2);PASCAL_TRAP(Munger, 0xA9E0);


void C_PackBits(GUEST<Ptr> *sp, GUEST<Ptr> *dp, INTEGER len);PASCAL_TRAP(PackBits, 0xA8CF);


void C_UnpackBits(GUEST<Ptr> *sp, GUEST<Ptr> *dp, INTEGER len);PASCAL_TRAP(UnpackBits, 0xA8D0);


Boolean C_BitTst(Ptr bp, LONGINT bn);PASCAL_TRAP(BitTst, 0xA85D);


void C_BitSet(Ptr bp, LONGINT bn);PASCAL_TRAP(BitSet, 0xA85E);


void C_BitClr(Ptr bp, LONGINT bn);PASCAL_TRAP(BitClr, 0xA85F);


LONGINT C_BitAnd(LONGINT a, LONGINT b);PASCAL_TRAP(BitAnd, 0xA858);


LONGINT C_BitOr(LONGINT a, LONGINT b);PASCAL_TRAP(BitOr, 0xA85B);


LONGINT C_BitXor(LONGINT a, LONGINT b);PASCAL_TRAP(BitXor, 0xA859);


LONGINT C_BitNot(LONGINT a);PASCAL_TRAP(BitNot, 0xA85A);


LONGINT C_BitShift(LONGINT a, INTEGER n);PASCAL_TRAP(BitShift, 0xA85C);


INTEGER C_HiWord(LONGINT a);PASCAL_TRAP(HiWord, 0xA86A);


INTEGER C_LoWord(LONGINT a);PASCAL_TRAP(LoWord, 0xA86B);


void C_LongMul(LONGINT a, LONGINT b, Int64Bit *c);PASCAL_TRAP(LongMul, 0xA867);


PatHandle C_GetPattern(INTEGER id);PASCAL_TRAP(GetPattern, 0xA9B8);


void GetIndPattern(Byte *op, INTEGER plistid, INTEGER index);

CursHandle C_GetCursor(INTEGER id);PASCAL_TRAP(GetCursor, 0xA9B9);


PicHandle C_GetPicture(INTEGER id);PASCAL_TRAP(GetPicture, 0xA9BC);


LONGINT C_DeltaPoint(Point a, Point b);PASCAL_TRAP(DeltaPoint, 0xA94F);


Fixed C_SlopeFromAngle(INTEGER a);PASCAL_TRAP(SlopeFromAngle, 0xA8BC);


INTEGER C_AngleFromSlope(Fixed s);PASCAL_TRAP(AngleFromSlope, 0xA8C4);


Fract C_FracMul(Fract x, Fract y);PASCAL_TRAP(FracMul, 0xA84A);


Fixed C_FixDiv(Fixed x, Fixed y);PASCAL_TRAP(FixDiv, 0xA84D);


Fract C_FracDiv(Fract x, Fract y);PASCAL_TRAP(FracDiv, 0xA84B);


Fixed C_Long2Fix(LONGINT x);PASCAL_TRAP(Long2Fix, 0xA83F);


LONGINT C_Fix2Long(Fixed x);PASCAL_TRAP(Fix2Long, 0xA840);


Fract C_Fix2Frac(Fixed x);PASCAL_TRAP(Fix2Frac, 0xA841);


Fixed C_Frac2Fix(Fract x);PASCAL_TRAP(Frac2Fix, 0xA842);


BEGIN_EXECUTOR_ONLY
// TODO:
// Executor will need to provide both 80-bit extended versions as trap
// and 64-bit double versions for PPC.
// Multiversal Interfaces might also need to declare a 96-bit extended version for 68881.
extern Extended Fix2X(Fixed x);
extern Fixed X2Fix(Extended *xp);
extern Extended Frac2X(Fract x);
extern Fract X2Frac(Extended *xp);
END_EXECUTOR_ONLY


Fixed C_R_X2Fix(GUEST<extended80> *x);PASCAL_TRAP(R_X2Fix, 0xA844);


Fract C_R_X2Frac(GUEST<extended80> *x);PASCAL_TRAP(R_X2Frac, 0xA846);


} /* namespace Executor */