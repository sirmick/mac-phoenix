/* MacPhoenix: the Image Compression Manager, _CodecDispatch $AAA3
 * (ImageCompression.yaml; docs/executor/QuickTime.md, route B).
 *
 * Inside Macintosh: QuickTime, "Image Compression Manager", and QuickTime
 * Components, "Image Compressor Components" (the calls a codec makes
 * back). Executor's own, in C++, over Apple's codec components ('imdc',
 * 'imco') registered from the QuickTime file on the System disk; Apple's
 * ICM, part of QuickTime's INIT, never runs. So far the calls the codecs
 * make back while decompressing a band; Gestalt 'icmp' waits for the rest
 * (rsys/quicktime.h). */
#include <base/common.h>
#include <ImageCompression.h>
#include <MemoryMgr.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <ResourceMgr.h>
#include <base/functions.impl.h>
#include <rsys/quicktime.h>

#include <cstring>

using namespace Executor;

/* QuickTime 2.5's: the version a codec checks */
OSErr Executor::C_CodecManagerVersion(GUEST<LONGINT> *version)
{
    if(version)
        *version = 18;
    return noErr;
}

OSErr Executor::C_GetImageDescriptionCTable(ImageDescriptionHandle desc, GUEST<CTabHandle> *ctable)
{
    if(!ctable)
        return paramErr;
    *ctable = nullptr;
    if(!desc || !*desc)
        return paramErr;
    INTEGER id = (*desc)->clutID;
    if(id > 0)
    {
        *ctable = GetCTable(id);
        return *ctable ? noErr : (OSErr)resNotFound;
    }
    if(id < 0)
        return noErr;
    /* the table after the description's fixed part, idSize covering it */
    Size have = GetHandleSize((Handle)desc);
    int32_t size = std::min<int32_t>((*desc)->idSize, have);
    const Size fixed = sizeof(ImageDescription);
    const Size header = 8; /* ctSeed, ctFlags, ctSize */
    if(size < fixed + header)
        return noErr;
    const uint8_t *p = (const uint8_t *)*desc + fixed;
    int n = (int16_t)((p[6] << 8) | p[7]) + 1;
    Size bytes = header + n * sizeof(ColorSpec);
    if(n <= 0 || fixed + bytes > size)
        return noErr;
    Handle h = NewHandle(bytes);
    if(!h)
        return MemError();
    memcpy(*h, p, bytes);
    *ctable = (CTabHandle)h;
    return noErr;
}

void Executor::C_ICMDecompressComplete(ImageSequence, OSErr err, INTEGER flag,
                                       ICMCompletionProcRecordPtr completionRtn)
{
    if(completionRtn && completionRtn->completionProc)
        completionRtn->completionProc(err, flag, completionRtn->completionRefCon);
}

/* The ICM's selectors as Universal Interfaces 3.3.2's ImageCompression.h
   declares them: the Pascal arguments' bytes and the result's (0: none).
   A call Executor's ICM doesn't answer yet pops its arguments and gets
   paramErr (0 for a long, Fixed or short result), logged: an application
   that sees $AAA3 implemented and calls it (AppleWorks 5's
   GetGraphicsImporterForFile) carries on, as it does when QuickTime says
   no. */
namespace
{
struct IcmSelector
{
    uint16_t sel;
    uint8_t arg_bytes, result_bytes;
    const char *name;
};
const IcmSelector icm_selectors[] = {
    { 0x00, 4, 2, "CodecManagerVersion" },
    { 0x01, 6, 2, "GetCodecNameList" },
    { 0x03, 12, 2, "GetCodecInfo" },
    { 0x04, 26, 2, "GetMaxCompressionSize" },
    { 0x05, 30, 2, "GetCompressionTime" },
    { 0x06, 24, 2, "CompressImage" },
    { 0x07, 48, 2, "FCompressImage" },
    { 0x08, 26, 2, "DecompressImage" },
    { 0x09, 54, 2, "FDecompressImage" },
    { 0x0A, 52, 2, "CompressSequenceBegin" },
    { 0x0B, 30, 2, "CompressSequenceFrame" },
    { 0x0C, 28, 0, "StdPix" },
    { 0x0D, 40, 2, "DecompressSequenceBegin" },
    { 0x0E, 18, 2, "DecompressSequenceFrame" },
    { 0x0F, 4, 2, "DisposeCodecNameList" },
    { 0x10, 8, 2, "SetDSequenceMatrix" },
    { 0x11, 12, 2, "SetDSequenceMatte" },
    { 0x12, 8, 2, "SetDSequenceMask" },
    { 0x13, 10, 2, "SetDSequenceTransferMode" },
    { 0x14, 12, 2, "SetDSequenceDataProc" },
    { 0x15, 8, 2, "GetDSequenceImageBuffer" },
    { 0x16, 8, 2, "GetDSequenceScreenBuffer" },
    { 0x17, 12, 2, "SetCSequenceQuality" },
    { 0x18, 12, 2, "SetCSequencePrev" },
    { 0x19, 8, 2, "GetCSequencePrevBuffer" },
    { 0x1A, 4, 2, "CDSequenceBusy" },
    { 0x1B, 4, 2, "CDSequenceEnd" },
    { 0x1C, 20, 2, "GetCompressedImageSize" },
    { 0x1D, 20, 2, "GetSimilarity" },
    { 0x1E, 8, 2, "GetImageDescriptionCTable" },
    { 0x1F, 8, 2, "SetImageDescriptionCTable" },
    { 0x20, 16, 2, "GetImageDescriptionExtension" },
    { 0x21, 12, 2, "AddImageDescriptionExtension" },
    { 0x23, 16, 2, "FindCodec" },
    { 0x24, 16, 2, "CompressPicture" },
    { 0x25, 34, 2, "FCompressPicture" },
    { 0x26, 12, 2, "CompressPictureFile" },
    { 0x27, 30, 2, "FCompressPictureFile" },
    { 0x28, 10, 2, "GetPictureFileHeader" },
    { 0x29, 10, 2, "DrawPictureFile" },
    { 0x2A, 14, 2, "MakeThumbnailFromPicture" },
    { 0x2B, 12, 2, "MakeThumbnailFromPictureFile" },
    { 0x2C, 18, 2, "MakeThumbnailFromPixMap" },
    { 0x2D, 36, 2, "TrimImage" },
    { 0x2E, 18, 2, "DrawTrimmedPicture" },
    { 0x2F, 16, 2, "DrawTrimmedPictureFile" },
    { 0x30, 38, 2, "ConvertImage" },
    { 0x33, 12, 2, "SetCSequenceFlushProc" },
    { 0x34, 8, 2, "SetDSequenceAccuracy" },
    { 0x35, 8, 2, "SetDSequenceSrcRect" },
    { 0x36, 8, 2, "SetCSequenceKeyFrameRate" },
    { 0x37, 24, 2, "GetCompressedPixMapInfo" },
    { 0x38, 24, 2, "SetCompressedPixMapInfo" },
    { 0x39, 8, 2, "TransformRgn" },
    { 0x3A, 12, 2, "RemoveImageDescriptionExtension" },
    { 0x3B, 12, 2, "CountImageDescriptionExtensionType" },
    { 0x3C, 8, 2, "GetNextImageDescriptionExtensionType" },
    { 0x41, 26, 0, "SFGetFilePreview" },
    { 0x42, 32, 0, "SFPGetFilePreview" },
    { 0x43, 14, 0, "StandardGetFilePreview" },
    { 0x44, 40, 0, "CustomGetFilePreview" },
    { 0x45, 6, 2, "MakeFilePreview" },
    { 0x46, 10, 2, "AddFilePreview" },
    { 0x47, 22, 2, "DecompressSequenceFrameS" },
    { 0x4B, 8, 2, "GetCSequenceKeyFrameRate" },
    { 0x4C, 8, 0, "AlignScreenRect" },
    { 0x4D, 14, 0, "AlignWindow" },
    { 0x4E, 20, 0, "DragAlignedWindow" },
    { 0x4F, 30, 4, "DragAlignedGrayRgn" },
    { 0x50, 8, 2, "SetCSequenceDataRateParams" },
    { 0x51, 8, 2, "SetCSequenceFrameNumber" },
    { 0x52, 12, 2, "NewImageGWorld" },
    { 0x53, 8, 2, "GetCSequenceDataRateParams" },
    { 0x54, 8, 2, "GetCSequenceFrameNumber" },
    { 0x55, 8, 2, "GetBestDeviceRect" },
    { 0x56, 8, 2, "SetSequenceProgressProc" },
    { 0x57, 12, 2, "SetDSequenceFlags" },
    { 0x5A, 10, 2, "GDHasScale" },
    { 0x5B, 12, 2, "GDGetScale" },
    { 0x5C, 10, 2, "GDSetScale" },
    { 0x5D, 48, 2, "DecompressSequenceBeginS" },
    { 0x5E, 26, 2, "DecompressSequenceFrameWhen" },
    { 0x5F, 4, 2, "CDSequenceFlush" },
    { 0x61, 12, 2, "QTGetFileNameExtension" },
    { 0x62, 4, 2, "ICMShieldSequenceCursor" },
    { 0x63, 12, 0, "ICMDecompressComplete" },
    { 0x64, 12, 2, "SetDSequenceTimeCode" },
    { 0x65, 12, 2, "CDSequenceEquivalentImageDescription" },
    { 0x66, 24, 2, "CDSequenceNewMemory" },
    { 0x67, 8, 2, "CDSequenceDisposeMemory" },
    { 0x68, 28, 2, "CDSequenceNewDataSource" },
    { 0x69, 4, 2, "CDSequenceDisposeDataSource" },
    { 0x6A, 12, 2, "CDSequenceSetSourceData" },
    { 0x6B, 4, 2, "CDSequenceChangedSourceData" },
    { 0x6C, 20, 2, "PtInDSequenceData" },
    { 0x6D, 16, 2, "ImageFieldSequenceBegin" },
    { 0x6E, 8, 2, "GetGraphicsImporterForFile" },
    { 0x6F, 24, 2, "ImageTranscodeSequenceBegin" },
    { 0x70, 4, 2, "ImageTranscodeSequenceEnd" },
    { 0x71, 20, 2, "ImageTranscodeFrame" },
    { 0x72, 8, 2, "ImageTranscodeDisposeFrameData" },
    { 0x73, 8, 2, "CDSequenceInvalidate" },
    { 0x74, 12, 2, "GetCSequenceMaxCompressionSize" },
    { 0x75, 32, 2, "ImageFieldSequenceExtractCombine" },
    { 0x76, 4, 2, "ImageFieldSequenceEnd" },
    { 0x77, 12, 2, "GetGraphicsImporterForDataRef" },
    { 0x78, 8, 2, "SetCSequencePreferredPacketSize" },
    { 0x79, 8, 2, "CDSequenceSetTimeBase" },
    { 0x7A, 8, 2, "MakeImageDescriptionForPixMap" },
    { 0x7B, 8, 2, "CDSequenceSetSourceDataQueue" },
    { 0x7C, 12, 2, "ICMSequenceLockBits" },
    { 0x7D, 8, 2, "ICMSequenceUnlockBits" },
    { 0x7E, 12, 2, "ICMSequenceGetChainMember" },
    { 0x7F, 16, 2, "CDSequenceGetDataSource" },
    { 0x80, 24, 2, "QTNewGWorld" },
    { 0x81, 24, 4, "QTUpdateGWorld" },
    { 0x82, 12, 2, "ICMDecompressCompleteS" },
    { 0x83, 8, 2, "ICMGetPixelFormatInfo" },
    { 0x84, 12, 2, "GetGraphicsImporterForFileWithFlags" },
    { 0x85, 16, 2, "GetGraphicsImporterForDataRefWithFlags" },
    { 0x86, 4, 2, "QTGetPixelSize" },
    { 0x87, 24, 2, "HitTestDSequenceData" }, /* (the header's FOURWORDINLINE says 6) */
    { 0x89, 12, 2, "ICMSequenceGetInfo" },
    { 0x8A, 8, 2, "ICMSetPixelFormatInfo" },
    { 0x8B, 32, 2, "QTNewGWorldFromPtr" },
    { 0x8D, 4, 4, "QTGetPixMapPtrRowBytes" },
    { 0x8E, 4, 4, "QTGetPixMapHandleRowBytes" },
    { 0x8F, 8, 2, "QTSetPixMapHandleRowBytes" },
    { 0x90, 8, 2, "QTSetPixMapPtrRowBytes" },
    { 0x91, 8, 2, "GetDSequenceMatrix" },
    { 0x92, 8, 2, "MakeImageDescriptionForEffect" },
    { 0x93, 4, 4, "QTGetPixMapPtrGammaLevel" },
    { 0x94, 8, 2, "QTSetPixMapPtrGammaLevel" },
    { 0x95, 12, 2, "QuadToQuadMatrix" },
    { 0x96, 4, 4, "QTGetPixMapHandleGammaLevel" },
    { 0x97, 8, 2, "QTSetPixMapHandleGammaLevel" },
    { 0x98, 4, 4, "QTGetPixMapPtrRequestedGammaLevel" },
    { 0x99, 8, 2, "QTSetPixMapPtrRequestedGammaLevel" },
    { 0x9A, 4, 4, "QTGetPixMapHandleRequestedGammaLevel" },
    { 0x9B, 8, 2, "QTSetPixMapHandleRequestedGammaLevel" },
    { 0x9D, 16, 2, "ICMSequenceSetInfo" },
    { 0x9F, 16, 2, "CDSequenceEquivalentImageDescriptionS" },
    { 0xA0, 8, 2, "ReplaceDSequenceImageDescription" }
};
}

bool Executor::ROMlib_icm_unknown_selector(uint32_t sel, uint32_t *arg_bytes, uint32_t *result_bytes,
                                           const char **name)
{
    for(const IcmSelector &s : icm_selectors)
        if(s.sel == (sel & 0xFFFF))
        {
            *arg_bytes = s.arg_bytes;
            *result_bytes = s.result_bytes;
            *name = s.name;
            return true;
        }
    return false;
}
