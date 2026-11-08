/*****************************************************************************
 *                                                                           *
 *                             ImageCompression.h                            *
 *                             ==================                            *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h      Fixed, INTEGER, LONGINT, Fract, ULONGINT, Boolean,    *
 *                     Rect, OSErr, OSType, Str31, Handle, Ptr, UInt8        *
 *     FileMgr.h       wide                                                  *
 *     Components.h    Component                                             *
 *     QuickDraw.h     CTabHandle, PixMapHandle, RgnHandle, CGrafPtr,        *
 *                     RGBColor, PicHandle                                   *
 *     CQuickDraw.h    GDHandle, GWorldPtr                                   *
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
#include "Components.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"

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

typedef uint16_t CodecFlags;

typedef Component CodecComponent;

typedef Component CompressorComponent;

typedef Component DecompressorComponent;

enum {codecErr = -8960,
noCodecErr = -8961,
codecUnimpErr = -8962,
codecSizeErr = -8963,
codecScreenBufErr = -8964,
codecImageBufErr = -8965,
codecSpoolErr = -8966,
codecAbortErr = -8967,
codecWouldOffscreenErr = -8968,
codecBadDataErr = -8969,
codecDataVersErr = -8970,
codecExtensionNotFoundErr = -8971,
codecConditionErr = -8972,
codecOpenErr = -8973,
};

enum {codecMinQuality = 0,
codecLowQuality = 256,
codecNormalQuality = 512,
codecHighQuality = 768,
codecMaxQuality = 1023,
codecLosslessQuality = 1024,
};

enum {codecProgressOpen = 0,
codecProgressUpdatePercent = 1,
codecProgressClose = 2,
};

struct CodecInfo{GUEST_STRUCT;GUEST<Str31> typeName;GUEST<INTEGER> version;GUEST<INTEGER> revisionLevel;GUEST<LONGINT> vendor;GUEST<LONGINT> decompressFlags;GUEST<LONGINT> compressFlags;GUEST<LONGINT> formatFlags;uint8_t compressionAccuracy;uint8_t decompressionAccuracy;GUEST<uint16_t> compressionSpeed;GUEST<uint16_t> decompressionSpeed;uint8_t compressionLevel;uint8_t resvd;GUEST<INTEGER> minimumHeight;GUEST<INTEGER> minimumWidth;GUEST<INTEGER> decompressPipelineLatency;GUEST<INTEGER> compressPipelineLatency;GUEST<LONGINT> privateData;};static_assert(sizeof(CodecInfo) == 72);void describeStruct(const CodecInfo&, std::ostream&);

struct CodecNameSpec{GUEST_STRUCT;GUEST<CodecComponent> codec;GUEST<CodecType> cType;GUEST<Str31> typeName;GUEST<Handle> name;};static_assert(sizeof(CodecNameSpec) == 44);void describeStruct(const CodecNameSpec&, std::ostream&);

struct CodecNameSpecList{GUEST_STRUCT;GUEST<INTEGER> count;CodecNameSpec list[1];};static_assert(sizeof(CodecNameSpecList) == 46);void describeStruct(const CodecNameSpecList&, std::ostream&);

typedef CodecNameSpecList *CodecNameSpecListPtr;

using ICMDataUPP = UPP<OSErr(GUEST<Ptr> *dataP, LONGINT bytesNeeded, LONGINT refcon)>;


using ICMFlushUPP = UPP<OSErr(Ptr data, LONGINT bytesAdded, LONGINT refcon)>;


using ICMProgressUPP = UPP<OSErr(INTEGER message, Fixed completeness, LONGINT refcon)>;


struct ICMDataProcRecord{GUEST_STRUCT;GUEST<ICMDataUPP> dataProc;GUEST<LONGINT> dataRefCon;};static_assert(sizeof(ICMDataProcRecord) == 8);void describeStruct(const ICMDataProcRecord&, std::ostream&);

typedef ICMDataProcRecord *ICMDataProcRecordPtr;

struct ICMFlushProcRecord{GUEST_STRUCT;GUEST<ICMFlushUPP> flushProc;GUEST<LONGINT> flushRefCon;};static_assert(sizeof(ICMFlushProcRecord) == 8);void describeStruct(const ICMFlushProcRecord&, std::ostream&);

typedef ICMFlushProcRecord *ICMFlushProcRecordPtr;

struct ICMProgressProcRecord{GUEST_STRUCT;GUEST<ICMProgressUPP> progressProc;GUEST<LONGINT> progressRefCon;};static_assert(sizeof(ICMProgressProcRecord) == 8);void describeStruct(const ICMProgressProcRecord&, std::ostream&);

typedef ICMProgressProcRecord *ICMProgressProcRecordPtr;

OSErr C_CodecManagerVersion(GUEST<LONGINT> *version);PASCAL_SUBTRAP_UNEXPORTED(CodecManagerVersion, 0xAAA3, 0x0000, CodecDispatch);


OSErr C_GetImageDescriptionCTable(ImageDescriptionHandle desc, GUEST<CTabHandle> *ctable);PASCAL_SUBTRAP_UNEXPORTED(GetImageDescriptionCTable, 0xAAA3, 0x001E, CodecDispatch);


void C_ICMDecompressComplete(ImageSequence seqID, OSErr err, INTEGER flag, ICMCompletionProcRecordPtr completionRtn);PASCAL_SUBTRAP_UNEXPORTED(ICMDecompressComplete, 0xAAA3, 0x0063, CodecDispatch);


OSErr C_GetCodecNameList(GUEST<CodecNameSpecListPtr> *list, INTEGER showAll);PASCAL_SUBTRAP_UNEXPORTED(GetCodecNameList, 0xAAA3, 0x0001, CodecDispatch);


OSErr C_GetCodecInfo(CodecInfo *info, CodecType cType, CodecComponent codec);PASCAL_SUBTRAP_UNEXPORTED(GetCodecInfo, 0xAAA3, 0x0003, CodecDispatch);


OSErr C_GetMaxCompressionSize(PixMapHandle src, const Rect *srcRect, INTEGER colorDepth, CodecQ quality, CodecType cType, CompressorComponent codec, GUEST<LONGINT> *size);PASCAL_SUBTRAP_UNEXPORTED(GetMaxCompressionSize, 0xAAA3, 0x0004, CodecDispatch);


OSErr C_GetCompressionTime(PixMapHandle src, const Rect *srcRect, INTEGER colorDepth, CodecType cType, CompressorComponent codec, GUEST<CodecQ> *spatialQuality, GUEST<CodecQ> *temporalQuality, GUEST<ULONGINT> *compressTime);PASCAL_SUBTRAP_UNEXPORTED(GetCompressionTime, 0xAAA3, 0x0005, CodecDispatch);


OSErr C_CompressImage(PixMapHandle src, const Rect *srcRect, CodecQ quality, CodecType cType, ImageDescriptionHandle desc, Ptr data);PASCAL_SUBTRAP_UNEXPORTED(CompressImage, 0xAAA3, 0x0006, CodecDispatch);


OSErr C_FCompressImage(PixMapHandle src, const Rect *srcRect, INTEGER colorDepth, CodecQ quality, CodecType cType, CompressorComponent codec, CTabHandle ctable, CodecFlags flags, LONGINT bufferSize, ICMFlushProcRecordPtr flushProc, ICMProgressProcRecordPtr progressProc, ImageDescriptionHandle desc, Ptr data);PASCAL_SUBTRAP_UNEXPORTED(FCompressImage, 0xAAA3, 0x0007, CodecDispatch);


OSErr C_DecompressImage(Ptr data, ImageDescriptionHandle desc, PixMapHandle dst, const Rect *srcRect, const Rect *dstRect, INTEGER mode, RgnHandle mask);PASCAL_SUBTRAP_UNEXPORTED(DecompressImage, 0xAAA3, 0x0008, CodecDispatch);


OSErr C_FDecompressImage(Ptr data, ImageDescriptionHandle desc, PixMapHandle dst, const Rect *srcRect, MatrixRecordPtr matrix, INTEGER mode, RgnHandle mask, PixMapHandle matte, const Rect *matteRect, CodecQ accuracy, DecompressorComponent codec, LONGINT bufferSize, ICMDataProcRecordPtr dataProc, ICMProgressProcRecordPtr progressProc);PASCAL_SUBTRAP_UNEXPORTED(FDecompressImage, 0xAAA3, 0x0009, CodecDispatch);


OSErr C_CompressSequenceBegin(GUEST<ImageSequence> *seqID, PixMapHandle src, PixMapHandle prev, const Rect *srcRect, const Rect *prevRect, INTEGER colorDepth, CodecType cType, CompressorComponent codec, CodecQ spatialQuality, CodecQ temporalQuality, LONGINT keyFrameRate, CTabHandle ctable, CodecFlags flags, ImageDescriptionHandle desc);PASCAL_SUBTRAP_UNEXPORTED(CompressSequenceBegin, 0xAAA3, 0x000A, CodecDispatch);


OSErr C_CompressSequenceFrame(ImageSequence seqID, PixMapHandle src, const Rect *srcRect, CodecFlags flags, Ptr data, GUEST<LONGINT> *dataSize, UInt8 *similarity, ICMCompletionProcRecordPtr asyncCompletionProc);PASCAL_SUBTRAP_UNEXPORTED(CompressSequenceFrame, 0xAAA3, 0x000B, CodecDispatch);


OSErr C_DecompressSequenceBegin(GUEST<ImageSequence> *seqID, ImageDescriptionHandle desc, CGrafPtr port, GDHandle gdh, const Rect *srcRect, MatrixRecordPtr matrix, INTEGER mode, RgnHandle mask, CodecFlags flags, CodecQ accuracy, DecompressorComponent codec);PASCAL_SUBTRAP_UNEXPORTED(DecompressSequenceBegin, 0xAAA3, 0x000D, CodecDispatch);


OSErr C_DecompressSequenceFrame(ImageSequence seqID, Ptr data, CodecFlags inFlags, GUEST<CodecFlags> *outFlags, ICMCompletionProcRecordPtr asyncCompletionProc);PASCAL_SUBTRAP_UNEXPORTED(DecompressSequenceFrame, 0xAAA3, 0x000E, CodecDispatch);


OSErr C_DisposeCodecNameList(CodecNameSpecListPtr list);PASCAL_SUBTRAP_UNEXPORTED(DisposeCodecNameList, 0xAAA3, 0x000F, CodecDispatch);


OSErr C_SetDSequenceMatrix(ImageSequence seqID, MatrixRecordPtr matrix);PASCAL_SUBTRAP_UNEXPORTED(SetDSequenceMatrix, 0xAAA3, 0x0010, CodecDispatch);


OSErr C_SetDSequenceMatte(ImageSequence seqID, PixMapHandle matte, const Rect *matteRect);PASCAL_SUBTRAP_UNEXPORTED(SetDSequenceMatte, 0xAAA3, 0x0011, CodecDispatch);


OSErr C_SetDSequenceMask(ImageSequence seqID, RgnHandle mask);PASCAL_SUBTRAP_UNEXPORTED(SetDSequenceMask, 0xAAA3, 0x0012, CodecDispatch);


OSErr C_SetDSequenceTransferMode(ImageSequence seqID, INTEGER mode, const RGBColor *opColor);PASCAL_SUBTRAP_UNEXPORTED(SetDSequenceTransferMode, 0xAAA3, 0x0013, CodecDispatch);


OSErr C_SetDSequenceDataProc(ImageSequence seqID, ICMDataProcRecordPtr dataProc, LONGINT bufferSize);PASCAL_SUBTRAP_UNEXPORTED(SetDSequenceDataProc, 0xAAA3, 0x0014, CodecDispatch);


OSErr C_GetDSequenceImageBuffer(ImageSequence seqID, GUEST<GWorldPtr> *gworld);PASCAL_SUBTRAP_UNEXPORTED(GetDSequenceImageBuffer, 0xAAA3, 0x0015, CodecDispatch);


OSErr C_GetDSequenceScreenBuffer(ImageSequence seqID, GUEST<GWorldPtr> *gworld);PASCAL_SUBTRAP_UNEXPORTED(GetDSequenceScreenBuffer, 0xAAA3, 0x0016, CodecDispatch);


OSErr C_SetCSequenceQuality(ImageSequence seqID, CodecQ spatialQuality, CodecQ temporalQuality);PASCAL_SUBTRAP_UNEXPORTED(SetCSequenceQuality, 0xAAA3, 0x0017, CodecDispatch);


OSErr C_SetCSequencePrev(ImageSequence seqID, PixMapHandle prev, const Rect *prevRect);PASCAL_SUBTRAP_UNEXPORTED(SetCSequencePrev, 0xAAA3, 0x0018, CodecDispatch);


OSErr C_GetCSequencePrevBuffer(ImageSequence seqID, GUEST<GWorldPtr> *gworld);PASCAL_SUBTRAP_UNEXPORTED(GetCSequencePrevBuffer, 0xAAA3, 0x0019, CodecDispatch);


OSErr C_CDSequenceBusy(ImageSequence seqID);PASCAL_SUBTRAP_UNEXPORTED(CDSequenceBusy, 0xAAA3, 0x001A, CodecDispatch);


OSErr C_CDSequenceEnd(ImageSequence seqID);PASCAL_SUBTRAP_UNEXPORTED(CDSequenceEnd, 0xAAA3, 0x001B, CodecDispatch);


OSErr C_GetCompressedImageSize(ImageDescriptionHandle desc, Ptr data, LONGINT bufferSize, ICMDataProcRecordPtr dataProc, GUEST<LONGINT> *dataSize);PASCAL_SUBTRAP_UNEXPORTED(GetCompressedImageSize, 0xAAA3, 0x001C, CodecDispatch);


OSErr C_SetImageDescriptionCTable(ImageDescriptionHandle desc, CTabHandle ctable);PASCAL_SUBTRAP_UNEXPORTED(SetImageDescriptionCTable, 0xAAA3, 0x001F, CodecDispatch);


OSErr C_GetImageDescriptionExtension(ImageDescriptionHandle desc, GUEST<Handle> *extension, LONGINT idType, LONGINT index);PASCAL_SUBTRAP_UNEXPORTED(GetImageDescriptionExtension, 0xAAA3, 0x0020, CodecDispatch);


OSErr C_AddImageDescriptionExtension(ImageDescriptionHandle desc, Handle extension, LONGINT idType);PASCAL_SUBTRAP_UNEXPORTED(AddImageDescriptionExtension, 0xAAA3, 0x0021, CodecDispatch);


OSErr C_FindCodec(CodecType cType, CodecComponent specCodec, GUEST<CompressorComponent> *compressor, GUEST<DecompressorComponent> *decompressor);PASCAL_SUBTRAP_UNEXPORTED(FindCodec, 0xAAA3, 0x0023, CodecDispatch);


OSErr C_CompressPicture(PicHandle srcPicture, PicHandle dstPicture, CodecQ quality, CodecType cType);PASCAL_SUBTRAP_UNEXPORTED(CompressPicture, 0xAAA3, 0x0024, CodecDispatch);


OSErr C_FCompressPicture(PicHandle srcPicture, PicHandle dstPicture, INTEGER colorDepth, CTabHandle ctable, CodecQ quality, INTEGER doDither, INTEGER compressAgain, ICMProgressProcRecordPtr progressProc, CodecType cType, CompressorComponent codec);PASCAL_SUBTRAP_UNEXPORTED(FCompressPicture, 0xAAA3, 0x0025, CodecDispatch);


OSErr C_SetDSequenceAccuracy(ImageSequence seqID, CodecQ accuracy);PASCAL_SUBTRAP_UNEXPORTED(SetDSequenceAccuracy, 0xAAA3, 0x0034, CodecDispatch);


OSErr C_SetDSequenceSrcRect(ImageSequence seqID, const Rect *srcRect);PASCAL_SUBTRAP_UNEXPORTED(SetDSequenceSrcRect, 0xAAA3, 0x0035, CodecDispatch);


OSErr C_SetCSequenceKeyFrameRate(ImageSequence seqID, LONGINT keyFrameRate);PASCAL_SUBTRAP_UNEXPORTED(SetCSequenceKeyFrameRate, 0xAAA3, 0x0036, CodecDispatch);


OSErr C_RemoveImageDescriptionExtension(ImageDescriptionHandle desc, LONGINT idType, LONGINT index);PASCAL_SUBTRAP_UNEXPORTED(RemoveImageDescriptionExtension, 0xAAA3, 0x003A, CodecDispatch);


OSErr C_CountImageDescriptionExtensionType(ImageDescriptionHandle desc, LONGINT idType, GUEST<LONGINT> *count);PASCAL_SUBTRAP_UNEXPORTED(CountImageDescriptionExtensionType, 0xAAA3, 0x003B, CodecDispatch);


OSErr C_GetNextImageDescriptionExtensionType(ImageDescriptionHandle desc, GUEST<LONGINT> *idType);PASCAL_SUBTRAP_UNEXPORTED(GetNextImageDescriptionExtensionType, 0xAAA3, 0x003C, CodecDispatch);


OSErr C_DecompressSequenceFrameS(ImageSequence seqID, Ptr data, LONGINT dataSize, CodecFlags inFlags, GUEST<CodecFlags> *outFlags, ICMCompletionProcRecordPtr asyncCompletionProc);PASCAL_SUBTRAP_UNEXPORTED(DecompressSequenceFrameS, 0xAAA3, 0x0047, CodecDispatch);


OSErr C_GetCSequenceKeyFrameRate(ImageSequence seqID, GUEST<LONGINT> *keyFrameRate);PASCAL_SUBTRAP_UNEXPORTED(GetCSequenceKeyFrameRate, 0xAAA3, 0x004B, CodecDispatch);


OSErr C_SetCSequenceFrameNumber(ImageSequence seqID, LONGINT frameNumber);PASCAL_SUBTRAP_UNEXPORTED(SetCSequenceFrameNumber, 0xAAA3, 0x0051, CodecDispatch);


OSErr C_GetCSequenceFrameNumber(ImageSequence seqID, GUEST<LONGINT> *frameNumber);PASCAL_SUBTRAP_UNEXPORTED(GetCSequenceFrameNumber, 0xAAA3, 0x0054, CodecDispatch);


OSErr C_SetSequenceProgressProc(ImageSequence seqID, ICMProgressProcRecordPtr progressProc);PASCAL_SUBTRAP_UNEXPORTED(SetSequenceProgressProc, 0xAAA3, 0x0056, CodecDispatch);


OSErr C_SetDSequenceFlags(ImageSequence seqID, LONGINT flags, LONGINT flagsMask);PASCAL_SUBTRAP_UNEXPORTED(SetDSequenceFlags, 0xAAA3, 0x0057, CodecDispatch);


OSErr C_DecompressSequenceBeginS(GUEST<ImageSequence> *seqID, ImageDescriptionHandle desc, Ptr data, LONGINT dataSize, CGrafPtr port, GDHandle gdh, const Rect *srcRect, MatrixRecordPtr matrix, INTEGER mode, RgnHandle mask, CodecFlags flags, CodecQ accuracy, DecompressorComponent codec);PASCAL_SUBTRAP_UNEXPORTED(DecompressSequenceBeginS, 0xAAA3, 0x005D, CodecDispatch);


OSErr C_CDSequenceFlush(ImageSequence seqID);PASCAL_SUBTRAP_UNEXPORTED(CDSequenceFlush, 0xAAA3, 0x005F, CodecDispatch);


OSErr C_MakeImageDescriptionForPixMap(PixMapHandle pixmap, GUEST<ImageDescriptionHandle> *idh);PASCAL_SUBTRAP_UNEXPORTED(MakeImageDescriptionForPixMap, 0xAAA3, 0x007A, CodecDispatch);


} /* namespace Executor */