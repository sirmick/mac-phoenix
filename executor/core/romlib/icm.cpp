/* MacPhoenix: the Image Compression Manager, _CodecDispatch $AAA3
 * (ImageCompression.yaml; docs/executor/QuickTime.md, route B).
 *
 * Inside Macintosh: QuickTime, "Image Compression Manager", and QuickTime
 * Components, "Image Compressor Components" (the calls a codec answers and
 * the calls it makes back). Executor's own, in C++, over Apple's codec
 * components ('imco' compressors, 'imdc' decompressors) registered from the
 * QuickTime file on the System disk; Apple's ICM, part of QuickTime's INIT,
 * never runs.
 *
 * A codec is asked to compress or decompress a whole image as one band.
 * Before the first band it says what it can do with the destination it is
 * given (PreDecompress: the depth it wants, whether it scales, masks, does
 * the transfer mode); what it can't do the ICM does: the codec draws into
 * an offscreen of the depth it wants and CopyBits takes that to the
 * destination, through the matrix, mode and mask. A screen destination
 * always goes that way, so that Executor's QuickDraw sees what changed. A
 * compressor gets its source at the depth it asks for (PreCompress), copied
 * through an offscreen when the source is at another. */
#include <base/common.h>
#include <ImageCompression.h>
#include <Components.h>
#include <MemoryMgr.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <ResourceMgr.h>
#include <OSUtil.h>
#include <FontMgr.h>
#include <ToolboxUtil.h>
#include <StdFilePkg.h>
#include <base/functions.impl.h>
#include <quickdraw/cquick.h>
#include <rsys/component.h>
#include <rsys/quicktime.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Executor;

namespace
{
enum : uint32_t
{
    kCompressorType = "imco"_4,
    kDecompressorType = "imdc"_4,
};

/* ImageCodec.h's selectors */
enum : int16_t
{
    kCodecGetCodecInfo = 0,
    kCodecGetCompressionTime = 1,
    kCodecGetMaxCompressionSize = 2,
    kCodecPreCompress = 3,
    kCodecBandCompress = 4,
    kCodecPreDecompress = 5,
    kCodecBandDecompress = 6,
    kCodecBusy = 7,
    kCodecGetCompressedImageSize = 8,
    kCodecFlush = 14,
};

enum : uint32_t
{
    codecCanScale = 1 << 0,
    codecCanMask = 1 << 1,
    codecCanMatte = 1 << 2,
    codecCanTransform = 1 << 3,
    codecCanTransferMode = 1 << 4,
    codecCanSrcExtract = 1 << 11,
};

enum : uint32_t
{
    codecConditionFirstBand = 1 << 0,
    codecConditionLastBand = 1 << 1,
    codecConditionFirstFrame = 1 << 2,
    codecConditionNewDepth = 1 << 3,
    codecConditionNewTransform = 1 << 4,
    codecConditionNewSrcRect = 1 << 5,
    codecConditionNewMask = 1 << 6,
    codecConditionNewMatte = 1 << 7,
    codecConditionNewTransferMode = 1 << 8,
    codecConditionNewClut = 1 << 9,
    codecConditionNewAccuracy = 1 << 10,
    codecConditionNewDestination = 1 << 11,
    /* everything is new: the first frame of a sequence */
    kConditionAllNew = 0x0FFF,
};

enum : int16_t
{
    kDitherCopy = 64, /* QuickDraw's ditherCopy */
    OP_PnSize_ = 0x0007, /* the picture opcode */
    kGeneva = 3,
};

enum : uint16_t
{
    codecFlagUpdatePrevious = 1 << 2,
    codecFlagWasCompressed = 1 << 4,
    codecFlagForceKeyFrame = 1 << 7,
};

/* codecInfo format flags: the depths the compressed data can have */
enum : uint32_t
{
    codecInfoDepth1 = 1 << 0,
    codecInfoDepth2 = 1 << 1,
    codecInfoDepth4 = 1 << 2,
    codecInfoDepth8 = 1 << 3,
    codecInfoDepth16 = 1 << 4,
    codecInfoDepth32 = 1 << 5,
    codecInfoDepth24 = 1 << 6,
};

/* ImageCodec.h's records, as QuickTime 5 has them: a QuickTime 2.5 codec
   reads the fields 2.5 had, the rest stay zero. */
struct CodecCapabilities
{
    GUEST_STRUCT;
    GUEST<int32_t> flags;
    GUEST<int16_t> wantedPixelSize, extendWidth, extendHeight, bandMin, bandInc, pad;
    GUEST<uint32_t> time;
    GUEST<int32_t> flags2;
};
static_assert(sizeof(CodecCapabilities) == 24);

struct CodecDecompressParams
{
    GUEST_STRUCT;
    GUEST<ImageSequence> sequenceID;
    GUEST<ImageDescriptionHandle> imageDescription;
    GUEST<Ptr> data;
    GUEST<int32_t> bufferSize, frameNumber, startLine, stopLine, conditionFlags;
    GUEST<CodecFlags> callerFlags;
    GUEST<CodecCapabilities *> capabilities;
    ICMProgressProcRecord progressProcRecord;
    ICMCompletionProcRecord completionProcRecord;
    ICMDataProcRecord dataProcRecord;
    GUEST<CGrafPtr> port;
    PixMap dstPixMap;
    GUEST<BitMap *> maskBits;
    GUEST<PixMap *> mattePixMap;
    Rect srcRect;
    GUEST<MatrixRecord *> matrix;
    GUEST<CodecQ> accuracy;
    GUEST<int16_t> transferMode;
    GUEST<uint32_t> frameTime;
    GUEST<int32_t> reserved;
    int8_t matrixFlags, matrixType;
    Rect dstRect;
    GUEST<uint16_t> majorSourceChangeSeed, minorSourceChangeSeed;
    GUEST<uint32_t> sourceData;
    GUEST<RgnHandle> maskRegion;
    GUEST<uint32_t> wantedDestinationPixelTypes;
    GUEST<int32_t> screenFloodMethod, screenFloodValue;
    GUEST<int16_t> preferredOffscreenPixelSize;
    GUEST<uint32_t> syncFrameTime;
    uint8_t needUpdateOnTimeChange, enableBlackLining, needUpdateOnSourceChange, pad;
    GUEST<int32_t> unused;
    GUEST<CGrafPtr> finalDestinationPort;
    GUEST<int32_t> requestedBufferWidth, requestedBufferHeight;
    Rect displayableAreaOfRequestedBuffer;
    uint8_t requestedSingleField, needUpdateOnNextIdle, pad2[2];
    GUEST<Fixed> bufferGammaLevel;
    GUEST<uint32_t> taskWeight, taskName;
};
static_assert(sizeof(CodecDecompressParams) == 234);

struct CodecCompressParams
{
    GUEST_STRUCT;
    GUEST<ImageSequence> sequenceID;
    GUEST<ImageDescriptionHandle> imageDescription;
    GUEST<Ptr> data;
    GUEST<int32_t> bufferSize, frameNumber, startLine, stopLine, conditionFlags;
    GUEST<CodecFlags> callerFlags;
    GUEST<CodecCapabilities *> capabilities;
    ICMProgressProcRecord progressProcRecord;
    ICMCompletionProcRecord completionProcRecord;
    ICMFlushProcRecord flushProcRecord;
    PixMap srcPixMap;
    PixMap prevPixMap;
    GUEST<CodecQ> spatialQuality, temporalQuality;
    GUEST<Fixed> similarity;
    GUEST<uint32_t> dataRateParams;
    GUEST<int32_t> reserved;
    GUEST<uint16_t> majorSourceChangeSeed, minorSourceChangeSeed;
    GUEST<uint32_t> sourceData;
    GUEST<int32_t> preferredPacketSizeInBytes, requestedBufferWidth, requestedBufferHeight;
    GUEST<uint32_t> wantedSourcePixelType;
    GUEST<int32_t> compressedDataSize;
    GUEST<uint32_t> taskWeight, taskName;
};
static_assert(sizeof(CodecCompressParams) == 218);

template<typename T>
void zero(T &x)
{
    memset((void *)&x, 0, sizeof x);
}

/* An open codec: the one asked for, or the first of the type. */
struct Codec
{
    uint32_t ci = 0;
    Codec() = default;
    Codec(const Codec &) = delete;
    Codec &operator=(const Codec &) = delete;
    ~Codec() { close(); }

    OSErr open(uint32_t type, CodecType cType, Component spec)
    {
        close();
        int32_t s = ptr_to_longint(spec);
        Component c;
        if(s > 0)
            c = spec; /* a component, or an instance standing for one */
        else
        {
            /* anyCodec, bestSpeed/Fidelity/CompressionCodec: the first */
            ComponentDescription cd;
            cd.componentType = type;
            cd.componentSubType = cType;
            cd.componentManufacturer = 0;
            cd.componentFlags = 0;
            cd.componentFlagsMask = 0;
            c = FindNextComponent(nullptr, &cd);
        }
        if(!c)
            return noCodecErr;
        ci = ptr_to_longint(OpenComponent(c));
        return ci ? noErr : (OSErr)codecOpenErr;
    }
    void close()
    {
        if(ci)
            CloseComponent(ptr_from_longint<ComponentInstance>(ci));
        ci = 0;
    }
    int32_t call(int16_t what, std::initializer_list<ComponentArg> args)
    {
        return ROMlib_call_component(ci, what, args);
    }
};

int16_t depth_bit(int16_t depth)
{
    switch(depth)
    {
        case 1: return codecInfoDepth1;
        case 2: return codecInfoDepth2;
        case 4: return codecInfoDepth4;
        case 8: return codecInfoDepth8;
        case 16: return codecInfoDepth16;
        case 24: return codecInfoDepth24;
        case 32: return codecInfoDepth32;
    }
    return 0;
}

/* The depth an image is compressed at when the caller says 0: the
   source's, or the nearest the codec's data can have (formatFlags). */
int16_t pick_depth(uint32_t format, int16_t depth, PixMapHandle src)
{
    if(depth)
        return depth;
    int16_t have = src && *src ? (int16_t)(*src)->pixelSize : 32;
    if(!(format & 0x7F))
        return have;
    if(have == 32 && !(format & codecInfoDepth32) && (format & codecInfoDepth24))
        return 24;
    if(format & depth_bit(have))
        return have;
    static const int16_t down[] = { 32, 24, 16, 8, 4, 2, 1 };
    for(int16_t d : down)
        if(d <= have && (format & depth_bit(d)))
            return d;
    for(int i = 6; i >= 0; i--)
        if(format & depth_bit(down[i]))
            return down[i];
    return have;
}

/* An offscreen pixel depth for image depth d (24: 32; 33-40: the grays) */
int16_t pixel_depth(int16_t d)
{
    if(d == 24)
        return 32;
    if(d > 32)
        return d - 32;
    return d ? d : 32;
}

Rect image_rect(ImageDescriptionHandle desc)
{
    Rect r;
    r.top = 0;
    r.left = 0;
    r.bottom = (*desc)->height;
    r.right = (*desc)->width;
    return r;
}

bool same_rect(const Rect &a, const Rect &b)
{
    return a.top == b.top && a.left == b.left && a.bottom == b.bottom && a.right == b.right;
}

/* The color table an offscreen of an image needs (nil: QuickDraw's
   standard one) */
CTabHandle image_ctable(ImageDescriptionHandle desc, int16_t pixel_size)
{
    if(pixel_size > 8)
        return nullptr;
    int16_t depth = (*desc)->depth;
    if(depth > 32)
        return GetCTable(depth);
    GUEST<CTabHandle> ct;
    if(C_GetImageDescriptionCTable(desc, &ct) == noErr && ct)
        return ct;
    return nullptr;
}

GWorldPtr new_offscreen(int16_t depth, const Rect &bounds, CTabHandle ctab)
{
    GUEST<GWorldPtr> gw;
    if(NewGWorld(&gw, depth, &bounds, ctab, nullptr, 0) != noErr
       && NewGWorld(&gw, depth, &bounds, ctab, nullptr, useTempMem) != noErr)
        return nullptr;
    LockPixels(GetGWorldPixMap(gw));
    return gw;
}

/* A pixel map's pixels where they are, for as long as a codec works on
   them: a GWorld's baseAddr is the pixels only while they are locked
   (Apple's codecs write through it directly). */
struct PixelsLock
{
    PixMapHandle pm;
    GWorldFlags state = 0;
    explicit PixelsLock(PixMapHandle p) : pm(p)
    {
        if(pm)
        {
            state = GetPixelsState(pm);
            LockPixels(pm);
        }
    }
    ~PixelsLock()
    {
        if(pm)
            SetPixelsState(pm, state);
    }
};

/* rgn covers r entirely */
bool covers(RgnHandle rgn, const Rect &r)
{
    if(!rgn)
        return true;
    RgnHandle t = NewRgn();
    RectRgn(t, &r);
    DiffRgn(t, rgn, t);
    bool e = EmptyRgn(t);
    DisposeRgn(t);
    return e;
}

bool is_screen(const PixMap &pm)
{
    for(GDHandle gd = LM(DeviceList); gd; gd = (*gd)->gdNextGD)
        if((*gd)->gdPMap && (*(*gd)->gdPMap)->baseAddr == pm.baseAddr)
            return true;
    return false;
}

/* A destination: a port, or a pixel map (then the port, if the current
   port draws into it). */
struct Dest
{
    CGrafPtr port = nullptr;
    PixMapHandle pm = nullptr;
    PixMap pix; /* a copy (an old port's bitmap made one) */

    void set(CGrafPtr p, PixMapHandle dst)
    {
        port = p;
        pm = dst;
        zero(pix);
        if(dst)
        {
            pix = **dst;
            GrafPtr cur = qdGlobals().thePort;
            if(port == nullptr && cur && CGrafPort_p(cur) && ((CGrafPtr)cur)->portPixMap
               && (*((CGrafPtr)cur)->portPixMap)->baseAddr == pix.baseAddr)
                port = (CGrafPtr)cur;
        }
        else if(port && CGrafPort_p(port))
            pix = **port->portPixMap;
        else if(port)
        {
            BitMap &b = ((GrafPtr)port)->portBits;
            pix.baseAddr = b.baseAddr;
            pix.rowBytes = b.rowBytes;
            pix.bounds = b.bounds;
            pix.hRes = pix.vRes = 72 << 16;
            pix.pixelSize = pix.cmpCount = pix.cmpSize = 1;
        }
    }
    bool to_port() const { return pm == nullptr; }
};

/* What a decompression is asked to do (DecompressImage, or a sequence's
   current state) */
struct DState
{
    uint32_t id = 0;
    Codec codec;
    ImageDescriptionHandle desc = nullptr;
    Dest dst;
    Rect src_rect;
    MatrixRecord matrix;
    int16_t mode = srcCopy;
    RgnHandle mask = nullptr;
    PixMapHandle matte = nullptr;
    Rect matte_rect;
    CodecFlags flags = 0;
    CodecQ accuracy = codecNormalQuality;
    ICMDataProcRecord data_proc;
    ICMProgressProcRecord progress;
    int32_t buffer_size = 0;
    uint32_t changed = kConditionAllNew;
    int32_t frame = 0;
    bool direct = false;
    bool natural = false; /* the offscreen at the image's own depth */
    GWorldPtr off = nullptr;

    DState()
    {
        zero(matrix);
        zero(data_proc);
        zero(progress);
        zero(src_rect);
        zero(matte_rect);
    }
    ~DState()
    {
        if(off)
            DisposeGWorld(off);
    }
};

void fill_dparams(CodecDecompressParams &p, DState &s, CodecCapabilities *caps, Ptr data, int32_t size)
{
    zero(p);
    p.sequenceID = s.id;
    p.imageDescription = s.desc;
    p.data = data;
    p.bufferSize = size;
    p.frameNumber = s.frame;
    p.startLine = 0;
    p.stopLine = (*s.desc)->height;
    p.callerFlags = s.flags;
    p.capabilities = caps;
    p.progressProcRecord = s.progress;
    p.dataProcRecord = s.data_proc;
    p.accuracy = s.accuracy;
}

/* QuickTime 3's codecs read a pixel map's format from planeBytes, its
   pixelFormat since QuickTime 3 (Universal Interfaces 3.x's PixMap): for
   QuickDraw's own depths the format is the pixel size (k8IndexedPixelFormat
   8, k16BE555PixelFormat 16, k32ARGBPixelFormat 32). 8.6's 'smc '
   decompressor checks it on a first frame and answers codecErr for 0;
   QuickTime 2.5's codecs don't read it. */
void set_pixel_format(PixMap &pm)
{
    if(pm.planeBytes == 0)
        pm.planeBytes = pm.pixelSize;
}

/* The codec into the destination itself, or into an offscreen. Its copy of
   the pixel map has rowBytes without the flags, as a compressor's source
   (source_pixmap), and a pixel format. */
void aim(CodecDecompressParams &p, DState &s, MatrixRecord *m, bool direct)
{
    if(direct)
    {
        p.port = s.dst.port;
        p.dstPixMap = s.dst.pix;
        p.dstPixMap.rowBytes = p.dstPixMap.rowBytes & 0x3FFF;
        p.srcRect = s.src_rect;
        p.matrix = m;
        *m = s.matrix;
        /* srcCopy to fewer bits than the image has: dithered */
        p.transferMode = s.mode == srcCopy && s.dst.pix.pixelSize < pixel_depth((*s.desc)->depth)
            ? kDitherCopy : s.mode;
        p.maskRegion = s.mask;
    }
    else
    {
        p.port = s.off;
        p.dstPixMap = **GetGWorldPixMap(s.off);
        p.dstPixMap.rowBytes = p.dstPixMap.rowBytes & 0x3FFF;
        p.srcRect = image_rect(s.desc);
        p.matrix = m;
        SetIdentityMatrix(m);
        p.transferMode = srcCopy;
    }
    set_pixel_format(p.dstPixMap);
    INTEGER type = GetMatrixType(m);
    p.matrixType = type;
    p.matrixFlags = 0;
    Rect d = p.srcRect;
    TransformRect(m, &d, nullptr);
    p.dstRect = d;
}

/* Whether the codec, given the destination, does all that is asked: the
   depth (it wants the destination's), no scaling (Apple's codecs scale
   through ICM routines Executor doesn't have; CopyBits does that from an
   offscreen), the mode, mask and source rectangle, and the port's regions
   let it draw the whole rectangle. */
bool codec_does_it(DState &s, const CodecCapabilities &caps, const Rect &dst_rect)
{
    uint32_t f = caps.flags;
    int16_t wanted = caps.wantedPixelSize;
    if(wanted && wanted != s.dst.pix.pixelSize)
        return false;
    if(!s.dst.port || is_screen(s.dst.pix))
        return false;
    INTEGER type = GetMatrixType(&s.matrix);
    if(type != identityMatrixType && type != translateMatrixType)
        return false;
    if(s.mode != srcCopy && s.mode != kDitherCopy && !(f & codecCanTransferMode))
        return false;
    if(s.mask && !(f & codecCanMask))
        return false;
    if(s.matte)
        return false;
    if(!same_rect(s.src_rect, image_rect(s.desc)) && !(f & codecCanSrcExtract))
        return false;
    CGrafPtr port = s.dst.port;
    if(!covers(port->clipRgn, dst_rect) || !covers(port->visRgn, dst_rect))
        return false;
    return true;
}

/* Get ready for a frame: PreDecompress, and an offscreen if the codec
   can't go straight to the destination. The offscreen is at the depth the
   codec wants for the destination; after a band failed there (a depth it
   converts to through the ICM's private routines), at the image's own. */
OSErr prepare(DState &s, Ptr data, int32_t size)
{
    CodecCapabilities caps;
    CodecDecompressParams p;
    MatrixRecord m;
    zero(caps);
    fill_dparams(p, s, &caps, data, size);
    p.conditionFlags = s.changed | codecConditionFirstBand | codecConditionLastBand;
    if(s.frame <= 1)
        p.conditionFlags = p.conditionFlags | codecConditionFirstFrame;

    s.direct = false;
    aim(p, s, &m, true);
    Rect dst_rect = p.dstRect;
    bool usable = s.dst.port && !is_screen(s.dst.pix);
    if(!usable)
        p.port = nullptr;
    int32_t r = s.codec.call(kCodecPreDecompress, { &p });
    if(r == noErr && usable && !s.natural && codec_does_it(s, caps, dst_rect))
    {
        s.direct = true;
        s.changed = 0;
        return noErr;
    }

    int16_t depth = pixel_depth((*s.desc)->depth);
    if(!s.natural && r == noErr && caps.wantedPixelSize)
        depth = caps.wantedPixelSize;
    for(int attempt = 0;; attempt++)
    {
        Rect bounds = image_rect(s.desc);
        bounds.right = bounds.right + (int16_t)caps.extendWidth;
        bounds.bottom = bounds.bottom + (int16_t)caps.extendHeight;
        if(s.off)
        {
            PixMapHandle opm = GetGWorldPixMap(s.off);
            if((*opm)->pixelSize != depth || !same_rect((*opm)->bounds, bounds))
            {
                DisposeGWorld(s.off);
                s.off = nullptr;
            }
        }
        if(!s.off)
        {
            s.off = new_offscreen(depth, bounds, image_ctable(s.desc, depth));
            if(!s.off)
                return memFullErr;
        }
        zero(caps);
        fill_dparams(p, s, &caps, data, size);
        aim(p, s, &m, false);
        p.conditionFlags = kConditionAllNew | codecConditionFirstBand | codecConditionLastBand;
        if(s.frame <= 1)
            p.conditionFlags = p.conditionFlags | codecConditionFirstFrame;
        r = s.codec.call(kCodecPreDecompress, { &p });
        if(r != noErr)
            return r;
        int16_t wanted = caps.wantedPixelSize;
        if(attempt == 0 && wanted && wanted != depth)
        {
            depth = wanted;
            continue;
        }
        break;
    }
    s.changed = 0;
    return noErr;
}

/* The offscreen to the destination */
void show(DState &s)
{
    PixMapHandle opm = GetGWorldPixMap(s.off);
    Rect src = s.src_rect, dst = s.src_rect;
    TransformRect(&s.matrix, &dst, nullptr);
    GUEST<CGrafPtr> save_port;
    GUEST<GDHandle> save_dev;
    GetGWorld(&save_port, &save_dev);
    BitMap *dst_bits;
    SignedByte state = 0;
    if(s.dst.to_port())
    {
        /* a GWorld with its own device current, as for the codec */
        if(GRAPHICS_WORLD_P(s.dst.port))
            SetGWorld(s.dst.port, nullptr);
        else
            SetPort((GrafPtr)s.dst.port);
        dst_bits = PORT_BITS_FOR_COPY(s.dst.port);
    }
    else
    {
        state = HGetState((Handle)s.dst.pm);
        HLock((Handle)s.dst.pm);
        dst_bits = (BitMap *)*s.dst.pm;
    }
    if(s.matte)
    {
        SignedByte mstate = HGetState((Handle)s.matte);
        HLock((Handle)s.matte);
        Rect mr = s.matte_rect;
        if(EmptyRect(&mr))
            mr = (*s.matte)->bounds;
        CopyDeepMask((BitMap *)*opm, (BitMap *)*s.matte, dst_bits, &src, &mr, &dst, s.mode, s.mask);
        HSetState((Handle)s.matte, mstate);
    }
    else
        CopyBits((BitMap *)*opm, dst_bits, &src, &dst, s.mode, s.mask);
    if(!s.dst.to_port())
        HSetState((Handle)s.dst.pm, state);
    SetGWorld(save_port, save_dev);
}

/* The bands being decompressed, innermost last: the sequence and the
   pixel map the codec was given (ICMSequenceLockBits answers it). */
struct Band
{
    ImageSequence id;
    const PixMap *dst;
};
std::vector<Band> bands;

int32_t band_decompress(DState &s, CodecDecompressParams &p)
{
    bands.push_back({ static_cast<ImageSequence>(s.id), &p.dstPixMap });
    int32_t r = s.codec.call(kCodecBandDecompress, { &p });
    bands.pop_back();
    return r;
}

OSErr decompress_frame(DState &s, Ptr data, int32_t size, CodecFlags *out_flags)
{
    PixMapHandle dpm = s.dst.pm;
    if(!dpm && s.dst.port && CGrafPort_p(s.dst.port))
        dpm = s.dst.port->portPixMap;
    PixelsLock lock(dpm);
    if(dpm)
        s.dst.pix.baseAddr = GetPixBaseAddr(dpm);
    s.frame++;
    if(size <= 0)
        size = s.buffer_size ? s.buffer_size : (int32_t)(*s.desc)->dataSize;
    if(s.changed || (!s.direct && !s.off))
    {
        OSErr err = prepare(s, data, size);
        if(err != noErr)
            return err;
    }
    CodecCapabilities caps;
    CodecDecompressParams p;
    MatrixRecord m;
    int32_t r;
    for(;;)
    {
        zero(caps);
        fill_dparams(p, s, &caps, data, size);
        aim(p, s, &m, s.direct);
        p.conditionFlags = codecConditionFirstBand | codecConditionLastBand
            | (s.frame <= 1 ? codecConditionFirstFrame : 0);
        if(s.direct)
        {
            /* the codec draws in the current port: the destination, with
               a GWorld's own device (a codec setting the port's colors
               under the screen's device gave a 32-bit GWorld an 8-bit
               background, and CopyBits into it black) */
            GUEST<CGrafPtr> save_port;
            GUEST<GDHandle> save_dev;
            GetGWorld(&save_port, &save_dev);
            if(s.dst.port && GRAPHICS_WORLD_P(s.dst.port))
                SetGWorld(s.dst.port, nullptr);
            else if(s.dst.port)
                SetPort((GrafPtr)s.dst.port);
            r = band_decompress(s, p);
            SetGWorld(save_port, save_dev);
        }
        else
        {
            GUEST<CGrafPtr> save_port;
            GUEST<GDHandle> save_dev;
            GetGWorld(&save_port, &save_dev);
            SetGWorld(s.off, nullptr);
            r = band_decompress(s, p);
            SetGWorld(save_port, save_dev);
            if(r == noErr)
                show(s);
        }
        if(r == noErr || s.natural)
            break;
        /* again, through an offscreen at the image's depth */
        s.natural = true;
        s.changed = kConditionAllNew;
        OSErr err = prepare(s, data, size);
        if(err != noErr)
            return err;
    }
    if(out_flags)
        *out_flags = p.callerFlags;
    return r;
}

/* Sequences, by ID */
struct CState
{
    uint32_t id = 0;
    Codec codec;
    ImageDescriptionHandle desc = nullptr;
    int16_t depth = 0;
    CodecQ spatial = codecNormalQuality, temporal = 0;
    int32_t key_rate = 0, frame = 0, buffer_size = 0;
    CodecFlags flags = 0;
    PixMapHandle prev = nullptr; /* the caller's */
    Rect prev_rect;
    GWorldPtr prev_buf = nullptr; /* the ICM's */
    GWorldPtr src_buf = nullptr;
    ICMFlushProcRecord flush;
    ICMProgressProcRecord progress;
    CTabHandle ctable = nullptr;
    bool prepared = false;

    CState()
    {
        zero(prev_rect);
        zero(flush);
        zero(progress);
    }
    ~CState()
    {
        if(prev_buf)
            DisposeGWorld(prev_buf);
        if(src_buf)
            DisposeGWorld(src_buf);
    }
};

std::map<uint32_t, std::unique_ptr<DState>> dseqs;
std::map<uint32_t, std::unique_ptr<CState>> cseqs;
uint32_t next_seq = 0x00510001;

DState *dseq(ImageSequence id)
{
    auto it = dseqs.find(id);
    return it == dseqs.end() ? nullptr : it->second.get();
}

CState *cseq(ImageSequence id)
{
    auto it = cseqs.find(id);
    return it == cseqs.end() ? nullptr : it->second.get();
}

OSErr codec_info(Codec &c, CodecInfo *info)
{
    zero(*info);
    return c.call(kCodecGetCodecInfo, { info });
}

/* The source as the compressor gets it: srcRect of src, or a copy at the
   depth it wants. A compressor takes baseAddr as the pixel map's origin and
   bounds as the rectangle to compress, from it; rowBytes without the
   pixel map flags (it multiplies by it as a signed word). */
bool source_pixmap(PixMap *out, PixMapHandle src, const Rect &r, int16_t wanted, CTabHandle ctab,
                   GWorldPtr *buf)
{
    PixelsLock lock(src);
    PixMap pm = **src;
    pm.baseAddr = GetPixBaseAddr(src);
    int bits = pm.pixelSize;
    if(!wanted || wanted == bits)
    {
        Rect b;
        b.top = r.top - pm.bounds.top;
        b.left = r.left - pm.bounds.left;
        b.bottom = r.bottom - pm.bounds.top;
        b.right = r.right - pm.bounds.left;
        pm.bounds = b;
        pm.rowBytes = pm.rowBytes & 0x3FFF;
        set_pixel_format(pm);
        *out = pm;
        return true;
    }
    Rect b;
    b.top = b.left = 0;
    b.bottom = r.bottom - r.top;
    b.right = r.right - r.left;
    if(*buf)
    {
        PixMapHandle bpm = GetGWorldPixMap(*buf);
        if((*bpm)->pixelSize != wanted || !same_rect((*bpm)->bounds, b))
        {
            DisposeGWorld(*buf);
            *buf = nullptr;
        }
    }
    if(!*buf && !(*buf = new_offscreen(wanted, b, ctab)))
        return false;
    PixMapHandle bpm = GetGWorldPixMap(*buf);
    GUEST<CGrafPtr> save_port;
    GUEST<GDHandle> save_dev;
    GetGWorld(&save_port, &save_dev);
    SetGWorld(*buf, nullptr);
    SignedByte state = HGetState((Handle)src);
    HLock((Handle)src);
    /* to fewer bits than a direct source has: dithered, as the Quadra's
       ICM gives 'smc ' its 8-bit frame (QTTest icm.smc_) */
    short mode = bits > 8 && wanted <= 8 ? ditherCopy : srcCopy;
    CopyBits((BitMap *)*src, (BitMap *)*bpm, &r, &b, mode, nullptr);
    HSetState((Handle)src, state);
    SetGWorld(save_port, save_dev);
    *out = **bpm;
    out->rowBytes = out->rowBytes & 0x3FFF;
    set_pixel_format(*out);
    return true;
}

/* The fields of an image description the ICM fills before compressing */
OSErr describe(ImageDescriptionHandle desc, CodecType cType, const CodecInfo &info, const Rect &r,
               PixMapHandle src, int16_t depth, CodecQ spatial, CodecQ temporal, CTabHandle ctable)
{
    if(GetHandleSize((Handle)desc) < (Size)sizeof(ImageDescription))
    {
        SetHandleSize((Handle)desc, sizeof(ImageDescription));
        if(MemError() != noErr)
            return MemError();
    }
    ImageDescription *d = *desc;
    memset(d, 0, sizeof(ImageDescription));
    d->idSize = sizeof(ImageDescription);
    d->cType = cType;
    d->version = info.version;
    d->revisionLevel = info.revisionLevel;
    d->vendor = info.vendor;
    d->temporalQuality = temporal;
    d->spatialQuality = spatial;
    d->width = r.right - r.left;
    d->height = r.bottom - r.top;
    d->hRes = (*src)->hRes;
    d->vRes = (*src)->vRes;
    if(!d->hRes)
        d->hRes = d->vRes = 72 << 16;
    d->dataSize = 0;
    d->frameCount = 1;
    memcpy(d->name, info.typeName, 32);
    d->depth = depth;
    if(depth <= 8 || depth > 32)
        d->clutID = ctable ? 0 : depth;
    else
        d->clutID = -1;
    if(ctable && (depth <= 8 || depth > 32))
        return C_SetImageDescriptionCTable(desc, ctable);
    return noErr;
}

OSErr compress_band(CState &s, PixMapHandle src, const Rect &r, Ptr data, CodecFlags flags, int32_t *size,
                    uint8_t *similarity)
{
    s.frame++;
    bool key = s.frame == 1 || (flags & codecFlagForceKeyFrame) || !s.temporal
        || (s.key_rate > 0 && (s.frame - 1) % s.key_rate == 0);
    CodecCapabilities caps;
    CodecCompressParams p;
    zero(caps);
    zero(p);
    p.sequenceID = s.id;
    p.imageDescription = s.desc;
    p.data = data;
    p.bufferSize = s.buffer_size;
    p.frameNumber = s.frame;
    p.startLine = 0;
    p.stopLine = r.bottom - r.top;
    p.callerFlags = (CodecFlags)(flags | (key && s.temporal ? codecFlagForceKeyFrame : 0));
    p.capabilities = &caps;
    p.progressProcRecord = s.progress;
    p.flushProcRecord = s.flush;
    p.spatialQuality = s.spatial;
    p.temporalQuality = key ? 0 : s.temporal;
    int16_t bits = (*src)->pixelSize;
    if(!source_pixmap(&p.srcPixMap, src, r, 0, nullptr, &s.src_buf))
        return memFullErr;
    PixMap prev;
    zero(prev);
    if(s.temporal && !key)
    {
        if(s.prev)
        {
            Rect pr = s.prev_rect;
            if(EmptyRect(&pr))
                pr = (*s.prev)->bounds;
            GWorldPtr none = nullptr;
            source_pixmap(&prev, s.prev, pr, 0, nullptr, &none);
        }
        else if(s.prev_buf)
        {
            GWorldPtr none = nullptr;
            PixMapHandle ppm = GetGWorldPixMap(s.prev_buf);
            source_pixmap(&prev, ppm, (*ppm)->bounds, 0, nullptr, &none);
        }
    }
    p.prevPixMap = prev;
    uint32_t cond = codecConditionFirstBand | codecConditionLastBand | (key ? codecConditionFirstFrame : 0);
    if(!s.prepared)
    {
        p.conditionFlags = cond | kConditionAllNew;
        int32_t e = s.codec.call(kCodecPreCompress, { &p });
        if(e != noErr)
            return e;
        int16_t wanted = caps.wantedPixelSize;
        if(wanted && wanted != bits)
        {
            /* the source at the depth the compressor wants */
            if(!source_pixmap(&p.srcPixMap, src, r, wanted, s.ctable, &s.src_buf))
                return memFullErr;
            zero(caps);
            e = s.codec.call(kCodecPreCompress, { &p });
            if(e != noErr)
                return e;
        }
        s.prepared = true;
    }
    else if(s.src_buf)
    {
        int16_t wanted = (*GetGWorldPixMap(s.src_buf))->pixelSize;
        if(!source_pixmap(&p.srcPixMap, src, r, wanted, s.ctable, &s.src_buf))
            return memFullErr;
    }
    p.conditionFlags = cond;
    (*s.desc)->dataSize = 0;
    int32_t e = s.codec.call(kCodecBandCompress, { &p });
    if(e != noErr)
        return e;
    int32_t n = (*s.desc)->dataSize;
    if(n <= 0)
    {
        n = p.bufferSize;
        (*s.desc)->dataSize = n;
    }
    if(size)
        *size = n;
    if(similarity)
        *similarity = key ? 0 : (uint8_t)std::min<int32_t>(255, ((int32_t)p.similarity) >> 8);
    /* the frame the next is compared with */
    if(s.temporal && !s.prev && (flags & codecFlagUpdatePrevious))
    {
        Rect b;
        b.top = b.left = 0;
        b.bottom = r.bottom - r.top;
        b.right = r.right - r.left;
        if(!s.prev_buf)
            s.prev_buf = new_offscreen(bits, b, nullptr);
        if(s.prev_buf)
        {
            GUEST<CGrafPtr> save_port;
            GUEST<GDHandle> save_dev;
            GetGWorld(&save_port, &save_dev);
            SetGWorld(s.prev_buf, nullptr);
            SignedByte state = HGetState((Handle)src);
            HLock((Handle)src);
            CopyBits((BitMap *)*src, (BitMap *)*GetGWorldPixMap(s.prev_buf), &r, &b, srcCopy, nullptr);
            HSetState((Handle)src, state);
            SetGWorld(save_port, save_dev);
        }
    }
    return noErr;
}

/* Image description extensions: after the fixed part and the color
   table, {size.l, type.l, data}, the size counting its header */
int32_t extensions_start(ImageDescriptionHandle desc)
{
    int32_t at = sizeof(ImageDescription);
    int32_t id_size = (*desc)->idSize;
    if((*desc)->clutID == 0 && id_size >= at + 8)
    {
        const uint8_t *p = (const uint8_t *)*desc + at;
        int n = (int16_t)((p[6] << 8) | p[7]) + 1;
        if(n > 0 && at + 8 + n * 8 <= id_size)
            at += 8 + n * 8;
    }
    return at;
}

/* the index-th (from 1) extension of type (0: any): its offset, or -1 */
int32_t find_extension(ImageDescriptionHandle desc, uint32_t type, int32_t index, int32_t *count = nullptr)
{
    int32_t end = std::min<int32_t>((*desc)->idSize, GetHandleSize((Handle)desc));
    int32_t at = extensions_start(desc);
    int32_t seen = 0, found = -1;
    while(at + 8 <= end)
    {
        const uint8_t *p = (const uint8_t *)*desc + at;
        int32_t size = (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
        uint32_t t = (uint32_t)((p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7]);
        if(size < 8 || at + size > end)
            break;
        if(!type || t == type)
        {
            seen++;
            if(seen == index && found < 0)
                found = at;
        }
        at += size;
    }
    if(count)
        *count = seen;
    return found;
}
}

/* QuickTime 2.5's: the version a codec checks */
OSErr Executor::C_CodecManagerVersion(GUEST<LONGINT> *version)
{
    if(version)
        *version = 18;
    return noErr;
}

OSErr Executor::C_GetCodecNameList(GUEST<CodecNameSpecListPtr> *list, INTEGER showAll)
{
    if(!list)
        return paramErr;
    *list = nullptr;
    struct Entry
    {
        Component c;
        uint32_t type;
    };
    std::vector<Entry> found;
    for(uint32_t kind : { kCompressorType, kDecompressorType })
    {
        ComponentDescription cd;
        cd.componentType = kind;
        cd.componentSubType = 0;
        cd.componentManufacturer = 0;
        cd.componentFlags = 0;
        cd.componentFlagsMask = 0;
        for(Component c = FindNextComponent(nullptr, &cd); c; c = FindNextComponent(c, &cd))
        {
            ComponentDescription got;
            GetComponentInfo(c, &got, nullptr, nullptr, nullptr);
            uint32_t t = got.componentSubType;
            bool dup = false;
            for(const Entry &e : found)
                if(e.type == t && (!showAll || kind == kDecompressorType))
                    dup = true;
            if(!dup)
                found.push_back({ c, t });
        }
    }
    Size bytes = 2 + found.size() * sizeof(CodecNameSpec);
    CodecNameSpecListPtr l = (CodecNameSpecListPtr)NewPtrClear(bytes);
    if(!l)
        return MemError();
    l->count = found.size();
    CodecNameSpec *spec = l->list;
    for(const Entry &e : found)
    {
        Handle name = NewHandle(0);
        GetComponentInfo(e.c, nullptr, name, nullptr, nullptr);
        spec->codec = showAll ? e.c : nullptr;
        spec->cType = e.type;
        if(name && GetHandleSize(name) > 0)
        {
            int n = std::min<int>(31, *(uint8_t *)*name);
            spec->typeName[0] = n;
            memcpy(&spec->typeName[1], (uint8_t *)*name + 1, n);
        }
        spec->name = name;
        spec++;
    }
    *list = l;
    return noErr;
}

OSErr Executor::C_DisposeCodecNameList(CodecNameSpecListPtr list)
{
    if(!list)
        return noErr;
    int n = list->count;
    for(int i = 0; i < n; i++)
        if(Handle h = list->list[i].name)
            DisposeHandle(h);
    DisposePtr((Ptr)list);
    return noErr;
}

OSErr Executor::C_GetCodecInfo(CodecInfo *info, CodecType cType, CodecComponent codec)
{
    if(!info)
        return paramErr;
    Codec c;
    OSErr err = c.open(kDecompressorType, cType, codec);
    if(err != noErr && !ptr_to_longint(codec))
        err = c.open(kCompressorType, cType, codec);
    if(err != noErr)
        return err;
    return codec_info(c, info);
}

OSErr Executor::C_FindCodec(CodecType cType, CodecComponent specCodec, GUEST<CompressorComponent> *compressor,
                            GUEST<DecompressorComponent> *decompressor)
{
    bool any = false;
    for(uint32_t kind : { kCompressorType, kDecompressorType })
    {
        GUEST<Component> *out = kind == kCompressorType ? compressor : decompressor;
        Component c = nullptr;
        if(ptr_to_longint(specCodec) > 0)
            c = specCodec;
        else
        {
            ComponentDescription cd;
            cd.componentType = kind;
            cd.componentSubType = cType;
            cd.componentManufacturer = 0;
            cd.componentFlags = 0;
            cd.componentFlagsMask = 0;
            c = FindNextComponent(nullptr, &cd);
        }
        if(c)
            any = true;
        if(out)
            *out = c;
    }
    return any ? noErr : (OSErr)noCodecErr;
}

OSErr Executor::C_GetMaxCompressionSize(PixMapHandle src, const Rect *srcRect, INTEGER colorDepth, CodecQ quality,
                                        CodecType cType, CompressorComponent codec, GUEST<LONGINT> *size)
{
    if(!src || !size)
        return paramErr;
    Codec c;
    OSErr err = c.open(kCompressorType, cType, codec);
    if(err != noErr)
        return err;
    CodecInfo info;
    codec_info(c, &info);
    int16_t depth = pick_depth(info.formatFlags, colorDepth, src);
    Rect r = srcRect ? *srcRect : (*src)->bounds;
    GUEST<LONGINT> n = 0;
    err = c.call(kCodecGetMaxCompressionSize, { src, &r, ComponentWord(depth), (uint32_t)quality, &n });
    *size = n;
    return err;
}

OSErr Executor::C_GetCompressionTime(PixMapHandle src, const Rect *srcRect, INTEGER colorDepth, CodecType cType,
                                     CompressorComponent codec, GUEST<CodecQ> *spatialQuality,
                                     GUEST<CodecQ> *temporalQuality, GUEST<ULONGINT> *compressTime)
{
    Codec c;
    OSErr err = c.open(kCompressorType, cType, codec);
    if(err != noErr)
        return err;
    CodecInfo info;
    codec_info(c, &info);
    int16_t depth = pick_depth(info.formatFlags, colorDepth, src);
    Rect r = srcRect ? *srcRect : (*src)->bounds;
    GUEST<CodecQ> sq = spatialQuality ? *spatialQuality : GUEST<CodecQ>(0);
    GUEST<CodecQ> tq = temporalQuality ? *temporalQuality : GUEST<CodecQ>(0);
    GUEST<ULONGINT> t = 0;
    err = c.call(kCodecGetCompressionTime, { src, &r, ComponentWord(depth), &sq, &tq, &t });
    if(spatialQuality)
        *spatialQuality = sq;
    if(temporalQuality)
        *temporalQuality = tq;
    if(compressTime)
        *compressTime = t;
    return err;
}

OSErr Executor::C_CompressImage(PixMapHandle src, const Rect *srcRect, CodecQ quality, CodecType cType,
                                ImageDescriptionHandle desc, Ptr data)
{
    return C_FCompressImage(src, srcRect, 0, quality, cType, nullptr, nullptr, 0, 0, nullptr, nullptr, desc, data);
}

OSErr Executor::C_FCompressImage(PixMapHandle src, const Rect *srcRect, INTEGER colorDepth, CodecQ quality,
                                 CodecType cType, CompressorComponent codec, CTabHandle ctable, CodecFlags flags,
                                 LONGINT bufferSize, ICMFlushProcRecordPtr flushProc,
                                 ICMProgressProcRecordPtr progressProc, ImageDescriptionHandle desc, Ptr data)
{
    if(!src || !desc || (!data && !flushProc))
        return paramErr;
    CState s;
    OSErr err = s.codec.open(kCompressorType, cType, codec);
    if(err != noErr)
        return err;
    CodecInfo info;
    codec_info(s.codec, &info);
    Rect r = srcRect ? *srcRect : (*src)->bounds;
    s.depth = pick_depth(info.formatFlags, colorDepth, src);
    s.spatial = quality;
    s.temporal = 0;
    s.desc = desc;
    s.ctable = ctable;
    if(flushProc)
        s.flush = *flushProc;
    if(progressProc)
        s.progress = *progressProc;
    s.buffer_size = bufferSize;
    if(!s.buffer_size)
    {
        GUEST<LONGINT> n = 0;
        s.codec.call(kCodecGetMaxCompressionSize, { src, &r, ComponentWord(s.depth), (uint32_t)quality, &n });
        s.buffer_size = n;
    }
    err = describe(desc, cType, info, r, src, s.depth, quality, 0, ctable);
    if(err != noErr)
        return err;
    SignedByte state = HGetState((Handle)desc);
    HLock((Handle)desc);
    err = compress_band(s, src, r, data, flags, nullptr, nullptr);
    HSetState((Handle)desc, state);
    return err;
}

OSErr Executor::C_DecompressImage(Ptr data, ImageDescriptionHandle desc, PixMapHandle dst, const Rect *srcRect,
                                  const Rect *dstRect, INTEGER mode, RgnHandle mask)
{
    if(!desc || !dst)
        return paramErr;
    Rect sr = srcRect ? *srcRect : image_rect(desc);
    Rect dr = dstRect ? *dstRect : sr;
    MatrixRecord m;
    RectMatrix(&m, &sr, &dr);
    return C_FDecompressImage(data, desc, dst, &sr, &m, mode, mask, nullptr, nullptr, codecNormalQuality, nullptr, 0,
                              nullptr, nullptr);
}

OSErr Executor::C_FDecompressImage(Ptr data, ImageDescriptionHandle desc, PixMapHandle dst, const Rect *srcRect,
                                   MatrixRecordPtr matrix, INTEGER mode, RgnHandle mask, PixMapHandle matte,
                                   const Rect *matteRect, CodecQ accuracy, DecompressorComponent codec,
                                   LONGINT bufferSize, ICMDataProcRecordPtr dataProc,
                                   ICMProgressProcRecordPtr progressProc)
{
    if(!desc || !*desc || !dst)
        return paramErr;
    DState s;
    OSErr err = s.codec.open(kDecompressorType, (*desc)->cType, codec);
    if(err != noErr)
        return err;
    s.desc = desc;
    s.dst.set(nullptr, dst);
    s.src_rect = srcRect ? *srcRect : image_rect(desc);
    if(matrix)
        s.matrix = *matrix;
    else
        SetIdentityMatrix(&s.matrix);
    s.mode = mode;
    s.mask = mask;
    s.matte = matte;
    if(matteRect)
        s.matte_rect = *matteRect;
    s.accuracy = accuracy;
    if(dataProc)
        s.data_proc = *dataProc;
    if(progressProc)
        s.progress = *progressProc;
    s.buffer_size = bufferSize;
    SignedByte state = HGetState((Handle)desc);
    HLock((Handle)desc);
    err = decompress_frame(s, data, bufferSize, nullptr);
    HSetState((Handle)desc, state);
    return err;
}

OSErr Executor::C_GetCompressedImageSize(ImageDescriptionHandle desc, Ptr data, LONGINT bufferSize,
                                         ICMDataProcRecordPtr dataProc, GUEST<LONGINT> *dataSize)
{
    if(!desc || !*desc || !dataSize)
        return paramErr;
    Codec c;
    OSErr err = c.open(kDecompressorType, (*desc)->cType, nullptr);
    if(err != noErr)
        return err;
    GUEST<LONGINT> n = 0;
    int32_t r = c.call(kCodecGetCompressedImageSize, { desc, data, (uint32_t)bufferSize, dataProc, &n });
    if(r == badComponentSelector)
    {
        *dataSize = (*desc)->dataSize;
        return noErr;
    }
    *dataSize = n;
    return r;
}

/* Decompression sequences */

namespace
{
OSErr begin_dseq(GUEST<ImageSequence> *seqID, ImageDescriptionHandle desc, CGrafPtr port, const Rect *srcRect,
                 MatrixRecordPtr matrix, INTEGER mode, RgnHandle mask, CodecFlags flags, CodecQ accuracy,
                 DecompressorComponent codec)
{
    if(!seqID || !desc || !*desc)
        return paramErr;
    *seqID = 0;
    auto s = std::make_unique<DState>();
    OSErr err = s->codec.open(kDecompressorType, (*desc)->cType, codec);
    if(err != noErr)
        return err;
    GUEST<Handle> copy = (Handle)desc;
    if(HandToHand(&copy) != noErr)
        return MemError();
    s->desc = (ImageDescriptionHandle)(Handle)copy;
    s->dst.set(port ? port : (CGrafPtr)qdGlobals().thePort, nullptr);
    s->src_rect = srcRect ? *srcRect : image_rect(desc);
    if(matrix)
        s->matrix = *matrix;
    else
        SetIdentityMatrix(&s->matrix);
    s->mode = mode;
    if(mask)
    {
        s->mask = NewRgn();
        CopyRgn(mask, s->mask);
    }
    s->flags = flags;
    s->accuracy = accuracy;
    s->id = next_seq++;
    *seqID = s->id;
    dseqs[s->id] = std::move(s);
    return noErr;
}

void end_dseq(DState *s)
{
    if(s->mask)
        DisposeRgn(s->mask);
    if(s->desc)
        DisposeHandle((Handle)s->desc);
}
}

OSErr Executor::C_DecompressSequenceBegin(GUEST<ImageSequence> *seqID, ImageDescriptionHandle desc, CGrafPtr port,
                                          GDHandle gdh, const Rect *srcRect, MatrixRecordPtr matrix, INTEGER mode,
                                          RgnHandle mask, CodecFlags flags, CodecQ accuracy,
                                          DecompressorComponent codec)
{
    return begin_dseq(seqID, desc, port, srcRect, matrix, mode, mask, flags, accuracy, codec);
}

OSErr Executor::C_DecompressSequenceBeginS(GUEST<ImageSequence> *seqID, ImageDescriptionHandle desc, Ptr data,
                                           LONGINT dataSize, CGrafPtr port, GDHandle gdh, const Rect *srcRect,
                                           MatrixRecordPtr matrix, INTEGER mode, RgnHandle mask, CodecFlags flags,
                                           CodecQ accuracy, DecompressorComponent codec)
{
    return begin_dseq(seqID, desc, port, srcRect, matrix, mode, mask, flags, accuracy, codec);
}

OSErr Executor::C_DecompressSequenceFrame(ImageSequence seqID, Ptr data, CodecFlags inFlags,
                                          GUEST<CodecFlags> *outFlags, ICMCompletionProcRecordPtr asyncCompletionProc)
{
    return C_DecompressSequenceFrameS(seqID, data, 0, inFlags, outFlags, asyncCompletionProc);
}

OSErr Executor::C_DecompressSequenceFrameS(ImageSequence seqID, Ptr data, LONGINT dataSize, CodecFlags inFlags,
                                           GUEST<CodecFlags> *outFlags,
                                           ICMCompletionProcRecordPtr asyncCompletionProc)
{
    DState *s = dseq(seqID);
    if(!s)
        return paramErr;
    CodecFlags saved = s->flags;
    s->flags = s->flags | inFlags;
    CodecFlags out = 0;
    SignedByte state = HGetState((Handle)s->desc);
    HLock((Handle)s->desc);
    OSErr err = decompress_frame(*s, data, dataSize, &out);
    HSetState((Handle)s->desc, state);
    s->flags = saved;
    if(outFlags)
        *outFlags = out;
    /* done with source and destination at once: synchronous */
    if(asyncCompletionProc && asyncCompletionProc->completionProc)
        asyncCompletionProc->completionProc(err, codecCompletionSource | codecCompletionDest,
                                            asyncCompletionProc->completionRefCon);
    return err;
}

OSErr Executor::C_SetDSequenceMatrix(ImageSequence seqID, MatrixRecordPtr matrix)
{
    DState *s = dseq(seqID);
    if(!s)
        return paramErr;
    if(matrix)
        s->matrix = *matrix;
    else
        SetIdentityMatrix(&s->matrix);
    s->changed |= codecConditionNewTransform;
    return noErr;
}

OSErr Executor::C_SetDSequenceMatte(ImageSequence seqID, PixMapHandle matte, const Rect *matteRect)
{
    DState *s = dseq(seqID);
    if(!s)
        return paramErr;
    s->matte = matte;
    if(matteRect)
        s->matte_rect = *matteRect;
    else
        zero(s->matte_rect);
    s->changed |= codecConditionNewMatte;
    return noErr;
}

OSErr Executor::C_SetDSequenceMask(ImageSequence seqID, RgnHandle mask)
{
    DState *s = dseq(seqID);
    if(!s)
        return paramErr;
    if(mask)
    {
        if(!s->mask)
            s->mask = NewRgn();
        CopyRgn(mask, s->mask);
    }
    else if(s->mask)
    {
        DisposeRgn(s->mask);
        s->mask = nullptr;
    }
    s->changed |= codecConditionNewMask;
    return noErr;
}

OSErr Executor::C_SetDSequenceTransferMode(ImageSequence seqID, INTEGER mode, const RGBColor *opColor)
{
    DState *s = dseq(seqID);
    if(!s)
        return paramErr;
    s->mode = mode;
    if(opColor)
        OpColor(const_cast<RGBColor *>(opColor));
    s->changed |= codecConditionNewTransferMode;
    return noErr;
}

OSErr Executor::C_SetDSequenceDataProc(ImageSequence seqID, ICMDataProcRecordPtr dataProc, LONGINT bufferSize)
{
    DState *s = dseq(seqID);
    if(!s)
        return paramErr;
    if(dataProc)
        s->data_proc = *dataProc;
    else
        zero(s->data_proc);
    s->buffer_size = bufferSize;
    return noErr;
}

OSErr Executor::C_SetDSequenceAccuracy(ImageSequence seqID, CodecQ accuracy)
{
    DState *s = dseq(seqID);
    if(!s)
        return paramErr;
    s->accuracy = accuracy;
    s->changed |= codecConditionNewAccuracy;
    return noErr;
}

OSErr Executor::C_SetDSequenceSrcRect(ImageSequence seqID, const Rect *srcRect)
{
    DState *s = dseq(seqID);
    if(!s)
        return paramErr;
    s->src_rect = srcRect ? *srcRect : image_rect(s->desc);
    s->changed |= codecConditionNewSrcRect;
    return noErr;
}

OSErr Executor::C_SetDSequenceFlags(ImageSequence seqID, LONGINT flags, LONGINT flagsMask)
{
    if(!dseq(seqID))
        return paramErr;
    return noErr;
}

OSErr Executor::C_GetDSequenceImageBuffer(ImageSequence seqID, GUEST<GWorldPtr> *gworld)
{
    DState *s = dseq(seqID);
    if(!s || !gworld)
        return paramErr;
    *gworld = nullptr;
    return s->off ? noErr : (OSErr)codecImageBufErr;
}

OSErr Executor::C_GetDSequenceScreenBuffer(ImageSequence seqID, GUEST<GWorldPtr> *gworld)
{
    if(!dseq(seqID) || !gworld)
        return paramErr;
    *gworld = nullptr;
    return codecScreenBufErr;
}

OSErr Executor::C_SetSequenceProgressProc(ImageSequence seqID, ICMProgressProcRecord *progressProc)
{
    if(DState *s = dseq(seqID))
    {
        if(progressProc)
            s->progress = *progressProc;
        else
            zero(s->progress);
        return noErr;
    }
    if(CState *s = cseq(seqID))
    {
        if(progressProc)
            s->progress = *progressProc;
        else
            zero(s->progress);
        return noErr;
    }
    return paramErr;
}

OSErr Executor::C_CDSequenceBusy(ImageSequence seqID)
{
    uint32_t ci = 0;
    if(DState *s = dseq(seqID))
        ci = s->codec.ci;
    else if(CState *s = cseq(seqID))
        ci = s->codec.ci;
    else
        return paramErr;
    int32_t r = ROMlib_call_component(ci, kCodecBusy, { (uint32_t)seqID });
    return r == badComponentSelector || r < 0 ? 0 : r;
}

OSErr Executor::C_CDSequenceFlush(ImageSequence seqID)
{
    uint32_t ci = 0;
    if(DState *s = dseq(seqID))
        ci = s->codec.ci;
    else if(CState *s = cseq(seqID))
        ci = s->codec.ci;
    else
        return paramErr;
    ROMlib_call_component(ci, kCodecFlush, {});
    return noErr;
}

OSErr Executor::C_CDSequenceEnd(ImageSequence seqID)
{
    if(DState *s = dseq(seqID))
    {
        end_dseq(s);
        dseqs.erase(seqID);
        return noErr;
    }
    if(cseqs.erase(seqID))
        return noErr;
    return paramErr;
}

/* Compression sequences */

OSErr Executor::C_CompressSequenceBegin(GUEST<ImageSequence> *seqID, PixMapHandle src, PixMapHandle prev,
                                        const Rect *srcRect, const Rect *prevRect, INTEGER colorDepth,
                                        CodecType cType, CompressorComponent codec, CodecQ spatialQuality,
                                        CodecQ temporalQuality, LONGINT keyFrameRate, CTabHandle ctable,
                                        CodecFlags flags, ImageDescriptionHandle desc)
{
    if(!seqID || !src || !desc)
        return paramErr;
    *seqID = 0;
    auto s = std::make_unique<CState>();
    OSErr err = s->codec.open(kCompressorType, cType, codec);
    if(err != noErr)
        return err;
    CodecInfo info;
    codec_info(s->codec, &info);
    Rect r = srcRect ? *srcRect : (*src)->bounds;
    s->depth = pick_depth(info.formatFlags, colorDepth, src);
    s->spatial = spatialQuality;
    s->temporal = temporalQuality;
    s->key_rate = keyFrameRate;
    s->flags = flags;
    s->ctable = ctable;
    s->prev = prev;
    if(prevRect)
        s->prev_rect = *prevRect;
    GUEST<LONGINT> n = 0;
    s->codec.call(kCodecGetMaxCompressionSize, { src, &r, ComponentWord(s->depth), (uint32_t)spatialQuality, &n });
    s->buffer_size = n;
    err = describe(desc, cType, info, r, src, s->depth, spatialQuality, temporalQuality, ctable);
    if(err != noErr)
        return err;
    s->desc = desc;
    s->id = next_seq++;
    *seqID = s->id;
    cseqs[s->id] = std::move(s);
    return noErr;
}

OSErr Executor::C_CompressSequenceFrame(ImageSequence seqID, PixMapHandle src, const Rect *srcRect,
                                        CodecFlags flags, Ptr data, GUEST<LONGINT> *dataSize, UInt8 *similarity,
                                        ICMCompletionProcRecordPtr asyncCompletionProc)
{
    CState *s = cseq(seqID);
    if(!s || !src)
        return paramErr;
    Rect r = srcRect ? *srcRect : (*src)->bounds;
    int32_t n = 0;
    uint8_t sim = 0;
    SignedByte state = HGetState((Handle)s->desc);
    HLock((Handle)s->desc);
    OSErr err = compress_band(*s, src, r, data, flags | s->flags, &n, &sim);
    HSetState((Handle)s->desc, state);
    if(dataSize)
        *dataSize = n;
    if(similarity)
        *similarity = sim;
    if(asyncCompletionProc && asyncCompletionProc->completionProc)
        asyncCompletionProc->completionProc(err, codecCompletionSource | codecCompletionDest,
                                            asyncCompletionProc->completionRefCon);
    return err;
}

OSErr Executor::C_SetCSequenceQuality(ImageSequence seqID, CodecQ spatialQuality, CodecQ temporalQuality)
{
    CState *s = cseq(seqID);
    if(!s)
        return paramErr;
    s->spatial = spatialQuality;
    s->temporal = temporalQuality;
    return noErr;
}

OSErr Executor::C_SetCSequencePrev(ImageSequence seqID, PixMapHandle prev, const Rect *prevRect)
{
    CState *s = cseq(seqID);
    if(!s)
        return paramErr;
    s->prev = prev;
    if(prevRect)
        s->prev_rect = *prevRect;
    else
        zero(s->prev_rect);
    return noErr;
}

OSErr Executor::C_GetCSequencePrevBuffer(ImageSequence seqID, GUEST<GWorldPtr> *gworld)
{
    CState *s = cseq(seqID);
    if(!s || !gworld)
        return paramErr;
    *gworld = s->prev_buf;
    return s->prev_buf ? noErr : (OSErr)codecImageBufErr;
}

OSErr Executor::C_SetCSequenceKeyFrameRate(ImageSequence seqID, LONGINT keyFrameRate)
{
    CState *s = cseq(seqID);
    if(!s)
        return paramErr;
    s->key_rate = keyFrameRate;
    return noErr;
}

OSErr Executor::C_GetCSequenceKeyFrameRate(ImageSequence seqID, GUEST<LONGINT> *keyFrameRate)
{
    CState *s = cseq(seqID);
    if(!s || !keyFrameRate)
        return paramErr;
    *keyFrameRate = s->key_rate;
    return noErr;
}

OSErr Executor::C_SetCSequenceFrameNumber(ImageSequence seqID, LONGINT frameNumber)
{
    CState *s = cseq(seqID);
    if(!s)
        return paramErr;
    s->frame = frameNumber - 1;
    return noErr;
}

OSErr Executor::C_GetCSequenceFrameNumber(ImageSequence seqID, GUEST<LONGINT> *frameNumber)
{
    CState *s = cseq(seqID);
    if(!s || !frameNumber)
        return paramErr;
    *frameNumber = s->frame;
    return noErr;
}

/* Image descriptions */

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

OSErr Executor::C_SetImageDescriptionCTable(ImageDescriptionHandle desc, CTabHandle ctable)
{
    if(!desc || !*desc)
        return paramErr;
    const int32_t fixed = sizeof(ImageDescription);
    int32_t start = extensions_start(desc);
    int32_t old_table = start - fixed;
    int32_t new_table = 0;
    if(ctable)
    {
        int n = (int16_t)(*ctable)->ctSize + 1;
        new_table = 8 + std::max(n, 0) * 8;
    }
    int32_t id_size = std::max<int32_t>((*desc)->idSize, fixed);
    int32_t ext = id_size - start;
    int32_t new_size = fixed + new_table + ext;
    if(new_size > GetHandleSize((Handle)desc))
    {
        SetHandleSize((Handle)desc, new_size);
        if(MemError() != noErr)
            return MemError();
    }
    uint8_t *p = (uint8_t *)*desc;
    memmove(p + fixed + new_table, p + start, ext);
    if(ctable)
        memcpy(p + fixed, *ctable, new_table);
    if(new_size < GetHandleSize((Handle)desc))
        SetHandleSize((Handle)desc, new_size);
    (*desc)->idSize = new_size;
    if(ctable)
        (*desc)->clutID = 0;
    else if(old_table && (*desc)->clutID == 0)
        (*desc)->clutID = -1;
    return noErr;
}

OSErr Executor::C_GetImageDescriptionExtension(ImageDescriptionHandle desc, GUEST<Handle> *extension,
                                               LONGINT idType, LONGINT index)
{
    if(!desc || !*desc || !extension)
        return paramErr;
    *extension = nullptr;
    int32_t at = find_extension(desc, idType, index ? index : 1);
    if(at < 0)
        return codecExtensionNotFoundErr;
    const uint8_t *p = (const uint8_t *)*desc + at;
    int32_t size = (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]) - 8;
    Handle h = NewHandle(size);
    if(!h)
        return MemError();
    memcpy(*h, (const uint8_t *)*desc + at + 8, size);
    *extension = h;
    return noErr;
}

OSErr Executor::C_AddImageDescriptionExtension(ImageDescriptionHandle desc, Handle extension, LONGINT idType)
{
    if(!desc || !*desc || !extension)
        return paramErr;
    int32_t n = GetHandleSize(extension);
    int32_t id_size = std::max<int32_t>((*desc)->idSize, sizeof(ImageDescription));
    SetHandleSize((Handle)desc, id_size + 8 + n);
    if(MemError() != noErr)
        return MemError();
    uint8_t *p = (uint8_t *)*desc + id_size;
    uint32_t size = n + 8, type = idType;
    for(int i = 0; i < 4; i++)
    {
        p[i] = size >> (24 - 8 * i);
        p[4 + i] = type >> (24 - 8 * i);
    }
    memcpy(p + 8, *extension, n);
    (*desc)->idSize = id_size + 8 + n;
    return noErr;
}

OSErr Executor::C_RemoveImageDescriptionExtension(ImageDescriptionHandle desc, LONGINT idType, LONGINT index)
{
    if(!desc || !*desc)
        return paramErr;
    int32_t at = find_extension(desc, idType, index ? index : 1);
    if(at < 0)
        return codecExtensionNotFoundErr;
    uint8_t *p = (uint8_t *)*desc;
    int32_t size = (int32_t)((p[at] << 24) | (p[at + 1] << 16) | (p[at + 2] << 8) | p[at + 3]);
    int32_t id_size = (*desc)->idSize;
    memmove(p + at, p + at + size, id_size - at - size);
    (*desc)->idSize = id_size - size;
    SetHandleSize((Handle)desc, id_size - size);
    return noErr;
}

OSErr Executor::C_CountImageDescriptionExtensionType(ImageDescriptionHandle desc, LONGINT idType,
                                                     GUEST<LONGINT> *count)
{
    if(!desc || !*desc || !count)
        return paramErr;
    int32_t n = 0;
    find_extension(desc, idType, 0, &n);
    *count = n;
    return noErr;
}

OSErr Executor::C_GetNextImageDescriptionExtensionType(ImageDescriptionHandle desc, GUEST<LONGINT> *idType)
{
    if(!desc || !*desc || !idType)
        return paramErr;
    /* the types in the order they first appear; the one after *idType
       (0: the first), 0 after the last */
    uint32_t after = (uint32_t)(LONGINT)*idType;
    int32_t end = std::min<int32_t>((*desc)->idSize, GetHandleSize((Handle)desc));
    int32_t at = extensions_start(desc);
    std::vector<uint32_t> types;
    while(at + 8 <= end)
    {
        const uint8_t *p = (const uint8_t *)*desc + at;
        int32_t size = (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
        uint32_t t = (uint32_t)((p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7]);
        if(size < 8)
            break;
        if(std::find(types.begin(), types.end(), t) == types.end())
            types.push_back(t);
        at += size;
    }
    size_t next = 0;
    if(after)
        next = std::find(types.begin(), types.end(), after) - types.begin() + 1;
    *idType = next < types.size() ? types[next] : 0;
    return noErr;
}

OSErr Executor::C_MakeImageDescriptionForPixMap(PixMapHandle pixmap, GUEST<ImageDescriptionHandle> *idh)
{
    if(!pixmap || !idh)
        return paramErr;
    *idh = nullptr;
    ImageDescriptionHandle desc = (ImageDescriptionHandle)NewHandleClear(sizeof(ImageDescription));
    if(!desc)
        return MemError();
    PixMap &pm = **pixmap;
    ImageDescription *d = *desc;
    d->idSize = sizeof(ImageDescription);
    d->cType = "raw "_4;
    d->vendor = "appl"_4;
    d->width = pm.bounds.right - pm.bounds.left;
    d->height = pm.bounds.bottom - pm.bounds.top;
    d->hRes = pm.hRes;
    d->vRes = pm.vRes;
    d->frameCount = 1;
    d->depth = pm.pixelSize;
    d->clutID = -1;
    if(pm.pixelSize <= 8 && pm.pmTable)
    {
        OSErr err = C_SetImageDescriptionCTable(desc, pm.pmTable);
        if(err != noErr)
        {
            DisposeHandle((Handle)desc);
            return err;
        }
    }
    *idh = desc;
    return noErr;
}

void Executor::C_ICMDecompressComplete(ImageSequence, OSErr err, INTEGER flag,
                                       ICMCompletionProcRecordPtr completionRtn)
{
    if(completionRtn && completionRtn->completionProc)
        completionRtn->completionProc(err, flag, completionRtn->completionRefCon);
}

/* Pictures (Inside Macintosh: QuickTime, "Compressing Pictures"). A
   compressed picture, as QuickTime 2.5 makes one (QTICM's pict.* lines on
   the Quadra): the version 2 header, a ClipRgn of the frame, then opcode
   $8200 CompressedQuickTime, its data
     version.w 0, matrix (image to picture), matteSize.l, matteRect,
     mode.w, srcRect (the image's), accuracy.l, maskSize.l,
     [matte description and data], [mask region], the image description,
     the compressed data (padded to a word);
   then PnSize($AE, n): the n bytes of opcodes after it tell a Mac without
   QuickTime what it would need ("QuickTime and a Photo - JPEG
   decompressor are needed to see this picture.", Geneva 12 scaled to the
   frame's width); a reader with QuickTime skips them. */

void Executor::ROMlib_draw_quicktime_opcode(uint16_t opcode, Ptr data, int32_t n, const Rect *src_frame,
                                            const Rect *dst_frame)
{
    if(opcode != 0x8200)
    {
        /* UncompressedQuickTime: a matrix and matte for the opcodes that
           follow, which draw the image themselves */
        warning_unimplemented("UncompressedQuickTime ($8201) picture opcode: its matte and matrix left out");
        return;
    }
    const uint8_t *b = (const uint8_t *)data;
    auto rd32 = [&](int32_t at) {
        return at + 4 <= n ? (int32_t)((b[at] << 24) | (b[at + 1] << 16) | (b[at + 2] << 8) | b[at + 3]) : 0;
    };
    auto rd16 = [&](int32_t at) { return at + 2 <= n ? (int16_t)((b[at] << 8) | b[at + 1]) : (int16_t)0; };
    auto rect_at = [&](int32_t at) {
        Rect r;
        r.top = rd16(at);
        r.left = rd16(at + 2);
        r.bottom = rd16(at + 4);
        r.right = rd16(at + 6);
        return r;
    };
    if(n < 68)
        return;
    MatrixRecord m;
    for(int i = 0; i < 9; i++)
        m.matrix[i / 3][i % 3] = rd32(2 + 4 * i);
    int32_t matte_size = rd32(38);
    int16_t mode = rd16(50);
    Rect src_rect = rect_at(52);
    CodecQ accuracy = rd32(60);
    int32_t mask_size = rd32(64);
    int32_t at = 68;
    if(matte_size < 0 || mask_size < 0 || at + matte_size + mask_size > n)
        return;
    if(matte_size)
        warning_unimplemented("CompressedQuickTime picture opcode: its matte left out");
    at += matte_size;
    RgnHandle mask = nullptr;
    if(mask_size >= 10)
    {
        mask = NewRgn();
        SetHandleSize((Handle)mask, mask_size);
        memcpy(*mask, b + at, mask_size);
        MapRgn(mask, src_frame, dst_frame);
    }
    at += mask_size;
    int32_t id_size = rd32(at);
    if(id_size < (int32_t)sizeof(ImageDescription) || at + id_size > n)
    {
        if(mask)
            DisposeRgn(mask);
        return;
    }
    ImageDescriptionHandle desc = (ImageDescriptionHandle)NewHandle(id_size);
    if(!desc)
    {
        if(mask)
            DisposeRgn(mask);
        return;
    }
    memcpy(*desc, b + at, id_size);
    Ptr image = data + at + id_size;
    int32_t image_size = std::min<int32_t>((*desc)->dataSize, n - at - id_size);

    /* the picture's coordinates to the port's */
    TranslateMatrix(&m, -((Fixed)src_frame->left << 16), -((Fixed)src_frame->top << 16));
    ScaleMatrix(&m, FixRatio(dst_frame->right - dst_frame->left, src_frame->right - src_frame->left),
                FixRatio(dst_frame->bottom - dst_frame->top, src_frame->bottom - src_frame->top), 0, 0);
    TranslateMatrix(&m, (Fixed)dst_frame->left << 16, (Fixed)dst_frame->top << 16);

    DState s;
    if(s.codec.open(kDecompressorType, (*desc)->cType, nullptr) == noErr)
    {
        s.desc = desc;
        s.dst.set((CGrafPtr)qdGlobals().thePort, nullptr);
        s.src_rect = EmptyRect(&src_rect) ? image_rect(desc) : src_rect;
        s.matrix = m;
        s.mode = mode;
        s.mask = mask;
        s.accuracy = accuracy;
        HLock((Handle)desc);
        decompress_frame(s, image, image_size, nullptr);
    }
    else
    {
        uint32_t t = (*desc)->cType;
        char fourcc[5] = { (char)(t >> 24), (char)(t >> 16), (char)(t >> 8), (char)t, 0 };
        warning_unimplemented("CompressedQuickTime picture opcode: no '%s' decompressor", fourcc);
    }
    s.desc = nullptr;
    s.mask = nullptr;
    DisposeHandle((Handle)desc);
    if(mask)
        DisposeRgn(mask);
}

namespace
{
struct PictWriter
{
    std::vector<uint8_t> b;
    void w8(uint8_t v) { b.push_back(v); }
    void w16(int32_t v)
    {
        w8(v >> 8);
        w8(v);
    }
    void w32(int32_t v)
    {
        w16(v >> 16);
        w16(v);
    }
    void rect(const Rect &r)
    {
        w16(r.top);
        w16(r.left);
        w16(r.bottom);
        w16(r.right);
    }
    void bytes(const void *p, size_t n) { b.insert(b.end(), (const uint8_t *)p, (const uint8_t *)p + n); }
    void pad()
    {
        if(b.size() & 1)
            w8(0);
    }
};

/* What a Mac without QuickTime draws of a compressed picture: three
   centred lines in Geneva 12, the text scaled to the frame's width */
void fallback_text(PictWriter &w, const Rect &frame, const unsigned char *type_name)
{
    std::string lines[3];
    lines[0] = "QuickTime\xAA and a";
    lines[1] = std::string((const char *)type_name + 1, type_name[0]) + " decompressor";
    lines[2] = "are needed to see this picture.";
    INTEGER widths[3], widest = 1;
    {
        GUEST<INTEGER> font = qdGlobals().thePort->txFont, size = qdGlobals().thePort->txSize;
        Style face = qdGlobals().thePort->txFace;
        TextFont(kGeneva);
        TextSize(12);
        TextFace(0);
        for(int i = 0; i < 3; i++)
        {
            Str255 s;
            s[0] = lines[i].size();
            memcpy(&s[1], lines[i].data(), lines[i].size());
            widths[i] = StringWidth(s);
            widest = std::max(widest, widths[i]);
        }
        TextFont(font);
        TextSize(size);
        TextFace(face);
    }
    int width = frame.right - frame.left;
    int spacing = 12 * width / widest;
    int mid = frame.top + (frame.bottom - frame.top) / 2;
    w.w16(OP_PnSize_);
    w.w16(1);
    w.w16(1);
    w.w16(0x0003); /* TxFont */
    w.w16(kGeneva);
    w.w16(0x0004); /* TxFace */
    w.w8(0);
    w.pad();
    w.w16(0x000D); /* TxSize */
    w.w16(12);
    w.w16(0x0010); /* TxRatio */
    w.w16(width);
    w.w16(width);
    w.w16(widest);
    w.w16(widest);
    for(int i = 0; i < 3; i++)
    {
        w.w16(0x0028); /* LongText */
        w.w16(mid + (i - 1) * spacing);
        w.w16(frame.left + (width - widths[i] * width / widest) / 2);
        w.w8(lines[i].size());
        w.bytes(lines[i].data(), lines[i].size());
        w.pad();
    }
    w.w16(0x0000); /* NOP */
}
}

OSErr Executor::C_CompressPicture(PicHandle srcPicture, PicHandle dstPicture, CodecQ quality, CodecType cType)
{
    return C_FCompressPicture(srcPicture, dstPicture, 0, nullptr, quality, 0, 0, nullptr, cType, nullptr);
}

OSErr Executor::C_FCompressPicture(PicHandle srcPicture, PicHandle dstPicture, INTEGER colorDepth, CTabHandle ctable,
                                   CodecQ quality, INTEGER doDither, INTEGER compressAgain,
                                   ICMProgressProcRecordPtr progressProc, CodecType cType,
                                   CompressorComponent codec)
{
    if(!srcPicture || !*srcPicture || !dstPicture)
        return paramErr;
    Rect frame = (*srcPicture)->picFrame;
    if(EmptyRect(&frame))
        return paramErr;
    /* the picture drawn */
    GWorldPtr off = new_offscreen(32, frame, nullptr);
    if(!off)
        return memFullErr;
    PixMapHandle pm = GetGWorldPixMap(off);
    GUEST<CGrafPtr> save_port;
    GUEST<GDHandle> save_dev;
    GetGWorld(&save_port, &save_dev);
    SetGWorld(off, nullptr);
    ClipRect(&frame);
    EraseRect(&frame);
    DrawPicture(srcPicture, &frame);
    OSErr err = noErr;
    if(cType == "raw "_4)
    {
        /* uncompressed: a picture of the pixels, without QuickTime */
        PicHandle pic = OpenPicture(&frame);
        CopyBits((BitMap *)*pm, (BitMap *)*pm, &frame, &frame, srcCopy, nullptr);
        ClosePicture();
        SetGWorld(save_port, save_dev);
        Size n = GetHandleSize((Handle)pic);
        SetHandleSize((Handle)dstPicture, n);
        err = MemError();
        if(err == noErr)
            memcpy(*dstPicture, *pic, n);
        KillPicture(pic);
        DisposeGWorld(off);
        return err;
    }
    SetGWorld(save_port, save_dev);

    GUEST<LONGINT> max = 0;
    err = C_GetMaxCompressionSize(pm, &frame, colorDepth, quality, cType, codec, &max);
    ImageDescriptionHandle desc = (ImageDescriptionHandle)NewHandle(sizeof(ImageDescription));
    Ptr data = err == noErr && desc ? NewPtr(max) : nullptr;
    if(err == noErr && !data)
        err = memFullErr;
    if(err == noErr)
        err = C_FCompressImage(pm, &frame, colorDepth, quality, cType, codec, ctable, 0, 0, nullptr, progressProc,
                               desc, data);
    CodecInfo info;
    zero(info);
    if(err == noErr)
        C_GetCodecInfo(&info, cType, nullptr);
    DisposeGWorld(off);
    if(err != noErr)
    {
        if(data)
            DisposePtr(data);
        if(desc)
            DisposeHandle((Handle)desc);
        return err;
    }

    PictWriter w;
    w.w16(0); /* picSize, below */
    w.rect(frame);
    w.w16(0x0011); /* VersionOp */
    w.w16(0x02FF);
    w.w16(0x0C00); /* HeaderOp */
    w.w16(-2);
    w.w16(0);
    w.w32((*desc)->hRes);
    w.w32((*desc)->vRes);
    w.rect(frame);
    w.w32(0);
    w.w16(0x0001); /* ClipRgn */
    w.w16(10);
    w.rect(frame);
    w.w16(0x8200); /* CompressedQuickTime */
    size_t length_at = w.b.size();
    w.w32(0);
    w.w16(0); /* version */
    MatrixRecord m;
    SetIdentityMatrix(&m);
    TranslateMatrix(&m, (Fixed)frame.left << 16, (Fixed)frame.top << 16);
    for(int i = 0; i < 9; i++)
        w.w32(m.matrix[i / 3][i % 3]);
    w.w32(0); /* matteSize */
    w.w32(0); /* matteRect */
    w.w32(0);
    w.w16(srcCopy);
    w.rect(image_rect(desc));
    w.w32(codecHighQuality); /* accuracy */
    w.w32(0); /* maskSize */
    int32_t id_size = (*desc)->idSize;
    w.bytes(*desc, id_size);
    w.bytes(data, (*desc)->dataSize);
    w.pad();
    int32_t length = w.b.size() - length_at - 4;
    for(int i = 0; i < 4; i++)
        w.b[length_at + i] = length >> (24 - 8 * i);
    /* for a Mac without QuickTime */
    PictWriter text;
    fallback_text(text, frame, info.typeName);
    w.w16(OP_PnSize_);
    w.w16(0x00AE);
    w.w16(text.b.size());
    w.bytes(text.b.data(), text.b.size());
    w.w16(0x00FF); /* OpEndPic */
    w.b[0] = w.b.size() >> 8;
    w.b[1] = w.b.size();
    DisposePtr(data);
    DisposeHandle((Handle)desc);

    SetHandleSize((Handle)dstPicture, w.b.size());
    err = MemError();
    if(err == noErr)
        memcpy(*dstPicture, w.b.data(), w.b.size());
    return err;
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
    /* private, no header names them (sizes from the codecs' call sites;
       ROMlib_icm_private_selector) */
    { 0x49, 18, 2, "(private $49: a color translation)" },
    { 0x4A, 4, 2, "(private $4A: dispose a color translation)" },
    { 0x58, 8, 2, "(private $58: before a band)" },
    { 0x59, 8, 2, "(private $59: after a band)" },
    { 0x60, 6, 2, "(private $60: lock a color translation)" },
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
    { 0x7E, 12, 2, "ICMSequenceGetChainMember" },
    { 0x7F, 16, 2, "CDSequenceGetDataSource" },
    { 0x80, 24, 2, "QTNewGWorld" },
    { 0x81, 24, 4, "QTUpdateGWorld" },
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

/* QuickTime 2.5's codecs call a few ICM routines no header declares; what
   they pass and how they use the answer, from their call sites:

   $49 (CTabHandle colors, long 0, Handle *translation, short 0,
        short *flags): a translation of an image's colors into the current
        device's. The codecs keep the handle and give it back to $60 (lock:
        translation, Boolean) around drawing and $4A (dispose). It is 12
        bytes, then for each color the device's pixel repeated to fill a
        long (smc fills four 8-bit pixels with one; Cinepak's palette
        codebooks take the long for 32 bits, its high word for 16). With
        flags (smc; Cinepak passes nil), a set high bit says the colors are
        the device's. Executor makes one for a direct device, or an indexed
        one with the image's colors (the ICM's offscreen for an indexed
        image); otherwise an error: raw and rle then draw without one, smc
        and rle's indexed paths fail and the ICM draws through an offscreen
        with the image's table, and Cinepak gives up the frame.
   $58, $59 (params, long 1): around a band; nothing to do. */
namespace
{
bool same_colors(CTabHandle a, CTabHandle b)
{
    if(!a || !b || !*a || !*b)
        return false;
    int n = (int16_t)(*a)->ctSize;
    if(n != (int16_t)(*b)->ctSize)
        return false;
    for(int i = 0; i <= n; i++)
    {
        const RGBColor &x = (*a)->ctTable[i].rgb, &y = (*b)->ctTable[i].rgb;
        if(x.red != y.red || x.green != y.green || x.blue != y.blue)
            return false;
    }
    return true;
}
}

bool Executor::ROMlib_icm_private_selector(uint32_t sel, uint32_t args, int32_t *result)
{
    switch(sel & 0xFFFF)
    {
        case 0x49:
        {
            uint32_t flags = READUL(args), out = READUL(args + 6);
            CTabHandle colors = ptr_from_longint<CTabHandle>(READUL(args + 14));
            GDHandle gd = LM(TheGDevice);
            CTabHandle dev = gd && (*gd)->gdPMap ? (CTabHandle)(*(*gd)->gdPMap)->pmTable : nullptr;
            if(!out || !colors || !*colors || !gd || !(*gd)->gdPMap)
            {
                *result = paramErr;
                return true;
            }
            int16_t pixel_size = (*(*gd)->gdPMap)->pixelSize;
            bool direct = pixel_size == 16 || pixel_size == 32;
            if(!direct && !same_colors(colors, dev))
            {
                *result = paramErr;
                return true;
            }
            /* 12 bytes, then for each of the 256 colors the device's pixel
               repeated to fill a long: smc fills four 8-bit pixels with one,
               Cinepak's palette codebooks take a 32-bit pixel or the high
               word for 16 bits */
            Handle h = NewHandleClear(12 + 256 * 4);
            if(!h)
            {
                *result = MemError();
                return true;
            }
            uint32_t table = US_TO_SYN68K_CHECK0(*h) + 12;
            int n = (int16_t)(*colors)->ctSize;
            for(int i = 0; i < 256; i++)
            {
                uint32_t v = i * 0x01010101u;
                if(direct && i <= n)
                {
                    const RGBColor &c = (*colors)->ctTable[i].rgb;
                    if(pixel_size == 32)
                        v = ((uint32_t)(c.red >> 8) << 16) | ((uint32_t)(c.green >> 8) << 8) | (c.blue >> 8);
                    else
                    {
                        uint32_t p16 = ((c.red >> 11) << 10) | ((c.green >> 11) << 5) | (c.blue >> 11);
                        v = (p16 << 16) | p16;
                    }
                }
                WRITEUL(table + 4 * i, v);
            }
            WRITEUL(out, ptr_to_longint(h));
            if(flags)
                WRITEUW(flags, direct ? 0 : 0x8000);
            *result = noErr;
            return true;
        }
        case 0x4A:
            if(Handle h = ptr_from_longint<Handle>(READUL(args)))
                DisposeHandle(h);
            *result = noErr;
            return true;
        case 0x58:
        case 0x59:
        case 0x60:
            *result = noErr;
            return true;
    }
    return false;
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

/* Standard File with previews: the dialogs without the preview pane */

void Executor::C_SFGetFilePreview(Point p, ConstStringPtr prompt, FileFilterUPP filef, INTEGER numt, SFTypeList tl,
                                  DlgHookUPP dh, SFReply *rep)
{
    SFGetFile(p, prompt, filef, numt, tl, dh, rep);
}

void Executor::C_SFPGetFilePreview(Point p, ConstStringPtr prompt, FileFilterUPP filef, INTEGER numt, SFTypeList tl,
                                   DlgHookUPP dh, SFReply *rep, INTEGER dig, ModalFilterUPP fp)
{
    SFPGetFile(p, prompt, filef, numt, tl, dh, rep, dig, fp);
}

void Executor::C_StandardGetFilePreview(FileFilterUPP filef, INTEGER numt, SFTypeList tl, StandardFileReply *replyp)
{
    StandardGetFile(filef, numt, tl, replyp);
}

void Executor::C_CustomGetFilePreview(FileFilterYDUPP filefilter, INTEGER numtypes, SFTypeList typelist,
                                      StandardFileReply *replyp, INTEGER dlgid, Point where, DlgHookYDUPP dlghook,
                                      ModalFilterYDUPP filterproc, Ptr activeList, ActivateYDUPP activateproc,
                                      void *yourdatap)
{
    CustomGetFile(filefilter, numtypes, typelist, replyp, dlgid, where, dlghook, filterproc, activeList,
                  activateproc, yourdatap);
}

/* The pixel formats QuickTime 3 names (ImageCompression.h): the old
   depths 1-32 as themselves, the indexed greys (33-40) as 1-8, the
   four-character ones by their bits; 0 for a format it doesn't know. */
INTEGER Executor::C_QTGetPixelSize(OSType format)
{
    if(format <= 32)
        return format;
    if(format >= 33 && format <= 40)
        return format - 32;
    switch(format)
    {
        case "yuv2"_4: case "yuvs"_4: case "yuvu"_4: case "2vuy"_4:
        case "L555"_4: case "L565"_4: case "B565"_4: case "b16g"_4:
            return 16;
        case "24BG"_4:
            return 24;
        case "ABGR"_4: case "BGRA"_4: case "RGBA"_4: case "b32a"_4:
            return 32;
        case "b48r"_4:
            return 48;
        case "b64a"_4:
            return 64;
    }
    return 0;
}

/* QuickTime 3: a sequence's destination pixels locked for a codec to
   write, and unlocked after. dst gets the pixel map the band is being
   decompressed into (rowBytes without the flags): the 'base' codec works
   out where its codec's band goes from its baseAddr, bounds, rowBytes and
   pixelSize (rpza, given none, drew from address 0 over the trap table).
   Executor's offscreen pixels never move, and the band's destination is
   locked while it is decompressed. No band of that sequence in progress:
   an error, and the codec draws some other way. */
OSErr Executor::C_ICMSequenceLockBits(ImageSequence seqID, PixMapPtr dst, LONGINT flags)
{
    for(auto it = bands.rbegin(); it != bands.rend(); ++it)
        if(it->id == seqID)
        {
            if(dst)
                *dst = *it->dst;
            return noErr;
        }
    return paramErr;
}

OSErr Executor::C_ICMSequenceUnlockBits(ImageSequence seqID, LONGINT flags)
{
    return noErr;
}

/* QuickTime 3's form, an OSErr back: the 'base' codec calls it as each
   frame is done (flags $33: source, destination, unlock bits, flush). */
OSErr Executor::C_ICMDecompressCompleteS(ImageSequence seqID, OSErr err, INTEGER flag,
                                         ICMCompletionProcRecordPtr completionRtn)
{
    C_ICMDecompressComplete(seqID, err, flag, completionRtn);
    return noErr;
}
