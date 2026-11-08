/*****************************************************************************
 *                                                                           *
 *                             ImageCompression.h                            *
 *                             ==================                            *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     Fixed, INTEGER, LONGINT, Fract, ULONGINT, Boolean,     *
 *                    Rect, OSErr, OSType, Str31                             *
 *     FileMgr.h      wide                                                   *
 *     QuickDraw.h    CTabHandle                                             *
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
#include "QuickDraw.h"

#define MODULE_NAME ImageCompression
#include <base/api-module.h>


namespace Executor {

// The selector in D0 (TWOWORDINLINE(0x70nn, 0xABC2): moveq #nn, D0),
// Pascal arguments on the stack.
DISPATCHER_TRAP(QTMathDispatch, 0xABC2, D0W);


struct FixedPoint{GUEST_STRUCT;GUEST<Fixed> x;GUEST<Fixed> y;};static_assert(sizeof(FixedPoint) == 8);void describeStruct(const FixedPoint&, std::ostream&);

struct FixedRect{GUEST_STRUCT;GUEST<Fixed> left;GUEST<Fixed> top;GUEST<Fixed> right;GUEST<Fixed> bottom;};static_assert(sizeof(FixedRect) == 16);void describeStruct(const FixedRect&, std::ostream&);

struct MatrixRecord{GUEST_STRUCT;GUEST<Fixed> matrix[3][3];};static_assert(sizeof(MatrixRecord) == 36);void describeStruct(const MatrixRecord&, std::ostream&);

typedef MatrixRecord *MatrixRecordPtr;

enum {identityMatrixType = 0,
translateMatrixType = 1,
scaleMatrixType = 2,
scaleTranslateMatrixType = 3,
linearMatrixType = 4,
linearTranslateMatrixType = 5,
perspectiveMatrixType = 6,
};

void C_CompAdd(wide *src, wide *dst);PASCAL_SUBTRAP_UNEXPORTED(CompAdd, 0xABC2, 0x0001, QTMathDispatch);


void C_CompSub(wide *src, wide *dst);PASCAL_SUBTRAP_UNEXPORTED(CompSub, 0xABC2, 0x0002, QTMathDispatch);


void C_CompNeg(wide *dst);PASCAL_SUBTRAP_UNEXPORTED(CompNeg, 0xABC2, 0x0003, QTMathDispatch);


void C_CompShift(wide *src, INTEGER shift);PASCAL_SUBTRAP_UNEXPORTED(CompShift, 0xABC2, 0x0004, QTMathDispatch);


void C_CompMul(LONGINT src1, LONGINT src2, wide *dst);PASCAL_SUBTRAP_UNEXPORTED(CompMul, 0xABC2, 0x0005, QTMathDispatch);


LONGINT C_CompDiv(wide *numerator, LONGINT denominator, GUEST<LONGINT> *remainder);PASCAL_SUBTRAP_UNEXPORTED(CompDiv, 0xABC2, 0x0006, QTMathDispatch);


void C_CompFixMul(wide *compSrc, Fixed fixSrc, wide *compDst);PASCAL_SUBTRAP_UNEXPORTED(CompFixMul, 0xABC2, 0x0007, QTMathDispatch);


void C_CompMulDiv(wide *co, LONGINT mul, LONGINT divisor);PASCAL_SUBTRAP_UNEXPORTED(CompMulDiv, 0xABC2, 0x0008, QTMathDispatch);


LONGINT C_CompCompare(const wide *a, const wide *minusb);PASCAL_SUBTRAP_UNEXPORTED(CompCompare, 0xABC2, 0x0009, QTMathDispatch);


Fixed C_FixMulDiv(Fixed src, Fixed mul, Fixed divisor);PASCAL_SUBTRAP_UNEXPORTED(FixMulDiv, 0xABC2, 0x000A, QTMathDispatch);


Fract C_FracSinCos(Fixed degree, GUEST<Fract> *cosOut);PASCAL_SUBTRAP_UNEXPORTED(FracSinCos, 0xABC2, 0x000B, QTMathDispatch);


void C_CompMulDivTrunc(wide *co, LONGINT mul, LONGINT divisor, GUEST<LONGINT> *remainder);PASCAL_SUBTRAP_UNEXPORTED(CompMulDivTrunc, 0xABC2, 0x000C, QTMathDispatch);


Fixed C_UnsignedFixMulDiv(Fixed src, Fixed mul, Fixed divisor);PASCAL_SUBTRAP_UNEXPORTED(UnsignedFixMulDiv, 0xABC2, 0x000D, QTMathDispatch);


Fixed C_FixExp2(Fixed src);PASCAL_SUBTRAP_UNEXPORTED(FixExp2, 0xABC2, 0x000E, QTMathDispatch);


Fixed C_FixLog2(Fixed src);PASCAL_SUBTRAP_UNEXPORTED(FixLog2, 0xABC2, 0x000F, QTMathDispatch);


Fixed C_FixPow(Fixed base, Fixed exp);PASCAL_SUBTRAP_UNEXPORTED(FixPow, 0xABC2, 0x0010, QTMathDispatch);


ULONGINT C_CompSquareRoot(const wide *src);PASCAL_SUBTRAP_UNEXPORTED(CompSquareRoot, 0xABC2, 0x0011, QTMathDispatch);


INTEGER C_GetMatrixType(const MatrixRecord *m);PASCAL_SUBTRAP_UNEXPORTED(GetMatrixType, 0xABC2, 0x0014, QTMathDispatch);


void C_SetIdentityMatrix(MatrixRecord *matrix);PASCAL_SUBTRAP_UNEXPORTED(SetIdentityMatrix, 0xABC2, 0x0015, QTMathDispatch);


void C_RotateMatrix(MatrixRecord *m, Fixed degrees, Fixed aboutX, Fixed aboutY);PASCAL_SUBTRAP_UNEXPORTED(RotateMatrix, 0xABC2, 0x0016, QTMathDispatch);


void C_ScaleMatrix(MatrixRecord *m, Fixed scaleX, Fixed scaleY, Fixed aboutX, Fixed aboutY);PASCAL_SUBTRAP_UNEXPORTED(ScaleMatrix, 0xABC2, 0x0017, QTMathDispatch);


void C_SkewMatrix(MatrixRecord *m, Fixed skewX, Fixed skewY, Fixed aboutX, Fixed aboutY);PASCAL_SUBTRAP_UNEXPORTED(SkewMatrix, 0xABC2, 0x0018, QTMathDispatch);


void C_TranslateMatrix(MatrixRecord *m, Fixed deltaH, Fixed deltaV);PASCAL_SUBTRAP_UNEXPORTED(TranslateMatrix, 0xABC2, 0x0019, QTMathDispatch);


void C_ConcatMatrix(const MatrixRecord *a, MatrixRecord *b);PASCAL_SUBTRAP_UNEXPORTED(ConcatMatrix, 0xABC2, 0x001B, QTMathDispatch);


Boolean C_InverseMatrix(const MatrixRecord *m, MatrixRecord *im);PASCAL_SUBTRAP_UNEXPORTED(InverseMatrix, 0xABC2, 0x001C, QTMathDispatch);


void C_MapMatrix(MatrixRecord *matrix, const Rect *fromRect, const Rect *toRect);PASCAL_SUBTRAP_UNEXPORTED(MapMatrix, 0xABC2, 0x001D, QTMathDispatch);


void C_RectMatrix(MatrixRecord *matrix, const Rect *srcRect, const Rect *dstRect);PASCAL_SUBTRAP_UNEXPORTED(RectMatrix, 0xABC2, 0x001E, QTMathDispatch);


void C_CopyMatrix(const MatrixRecord *m1, MatrixRecord *m2);PASCAL_SUBTRAP_UNEXPORTED(CopyMatrix, 0xABC2, 0x0020, QTMathDispatch);


Boolean C_EqualMatrix(const MatrixRecord *m1, const MatrixRecord *m2);PASCAL_SUBTRAP_UNEXPORTED(EqualMatrix, 0xABC2, 0x0021, QTMathDispatch);


OSErr C_TransformFixedPoints(const MatrixRecord *m, FixedPoint *fpt, LONGINT count);PASCAL_SUBTRAP_UNEXPORTED(TransformFixedPoints, 0xABC2, 0x0022, QTMathDispatch);


OSErr C_TransformPoints(const MatrixRecord *mp, GUEST<Point> *pt1, LONGINT count);PASCAL_SUBTRAP_UNEXPORTED(TransformPoints, 0xABC2, 0x0023, QTMathDispatch);


Boolean C_TransformFixedRect(const MatrixRecord *m, FixedRect *fr, FixedPoint *fpp);PASCAL_SUBTRAP_UNEXPORTED(TransformFixedRect, 0xABC2, 0x0024, QTMathDispatch);


Boolean C_TransformRect(const MatrixRecord *m, Rect *r, FixedPoint *fpp);PASCAL_SUBTRAP_UNEXPORTED(TransformRect, 0xABC2, 0x0025, QTMathDispatch);


DISPATCHER_TRAP(CodecDispatch, 0xAAA3, D0W);


typedef OSType CodecType;

typedef LONGINT CodecQ;

typedef LONGINT ImageSequence;

struct ImageDescription{GUEST_STRUCT;GUEST<LONGINT> idSize;GUEST<CodecType> cType;GUEST<LONGINT> resvd1;GUEST<INTEGER> resvd2;GUEST<INTEGER> dataRefIndex;GUEST<INTEGER> version;GUEST<INTEGER> revisionLevel;GUEST<LONGINT> vendor;GUEST<CodecQ> temporalQuality;GUEST<CodecQ> spatialQuality;GUEST<INTEGER> width;GUEST<INTEGER> height;GUEST<Fixed> hRes;GUEST<Fixed> vRes;GUEST<LONGINT> dataSize;GUEST<INTEGER> frameCount;GUEST<Str31> name;GUEST<INTEGER> depth;GUEST<INTEGER> clutID;};static_assert(sizeof(ImageDescription) == 86);void describeStruct(const ImageDescription&, std::ostream&);

typedef ImageDescription *ImageDescriptionPtr;

typedef GUEST<ImageDescription*> *ImageDescriptionHandle;

using ICMCompletionUPP = UPP<void(OSErr result, INTEGER flags, LONGINT refcon)>;


struct ICMCompletionProcRecord{GUEST_STRUCT;GUEST<ICMCompletionUPP> completionProc;GUEST<LONGINT> completionRefCon;};static_assert(sizeof(ICMCompletionProcRecord) == 8);void describeStruct(const ICMCompletionProcRecord&, std::ostream&);

typedef ICMCompletionProcRecord *ICMCompletionProcRecordPtr;

enum {codecCompletionSource = 1,
codecCompletionDest = 2,
};

OSErr C_CodecManagerVersion(GUEST<LONGINT> *version);PASCAL_SUBTRAP_UNEXPORTED(CodecManagerVersion, 0xAAA3, 0x0000, CodecDispatch);


OSErr C_GetImageDescriptionCTable(ImageDescriptionHandle desc, GUEST<CTabHandle> *ctable);PASCAL_SUBTRAP_UNEXPORTED(GetImageDescriptionCTable, 0xAAA3, 0x001E, CodecDispatch);


void C_ICMDecompressComplete(ImageSequence seqID, OSErr err, INTEGER flag, ICMCompletionProcRecordPtr completionRtn);PASCAL_SUBTRAP_UNEXPORTED(ICMDecompressComplete, 0xAAA3, 0x0063, CodecDispatch);


} /* namespace Executor */