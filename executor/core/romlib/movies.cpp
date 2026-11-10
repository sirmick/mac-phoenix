/* MacPhoenix: the Movie Toolbox, _QuickTime $AAAA (QuickTime.yaml;
 * docs/executor/QuickTime.md, route B).
 *
 * Inside Macintosh: QuickTime, "Movie Toolbox", and Apple's QuickTime File
 * Format specification (atoms, the movie, track and media headers, edit
 * lists, sample tables, sample descriptions, data references). Executor's
 * own, in C++; Apple's Movie Toolbox, part of QuickTime's INIT, never runs.
 *
 * A movie is read whole from its 'moov' atom (a 'moov' resource, or the
 * atom in the data fork of a flattened movie) into host structures: tracks,
 * their edit lists and media, each media's sample tables as chunks. The
 * guest sees a Movie, Track, Media, TimeBase or UserData as a small block
 * of its own (the ID of the host structure in it). The media's data are
 * read from their data references (the movie file itself, another file
 * through an alias, or memory for media made with no file) when needed.
 *
 * Time: a time base is a rate and a time at an instant of Executor's
 * millisecond clock; its time is worked out when asked. MoviesTask draws
 * each enabled visual track's sample at the movie's time: the ICM decodes
 * it into the track's own offscreen at the destination's depth (temporal
 * codecs need the previous frame there, a seek decodes from the sync
 * sample before), CopyBits takes that to the movie's port through the
 * track's and the movie's matrices. A sound track plays through a Sound
 * Manager channel of its own, its samples decoded to the mixer's 8-bit mono
 * and queued a little ahead of the movie's time (sound_task).
 *
 * Editing: NewMovie/CreateMovieFile, NewMovieTrack, NewTrackMedia,
 * AddMediaSample (appended to the media's data file, or kept in memory),
 * InsertMediaIntoTrack; AddMovieResource, FlattenMovie and FlattenMovieData
 * write the 'moov' atom back. */
#include <base/common.h>
#include <QuickTime.h>
#include <ImageCompression.h>
#include <Components.h>
#include <MemoryMgr.h>
#include <mman/mman.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <AliasMgr.h>
#include <OSUtil.h>
#include <ToolboxUtil.h>
#include <base/functions.impl.h>
#include <quickdraw/cquick.h>
#include <rsys/inflate.h>
#include <rsys/quicktime.h>
#include <time/time.h>
#include <file/file.h>
#include <syn68k_public.h>

#include <sound/soundopts.h>
#include <sound/sounddecode.h>
#include <SoundMgr.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Executor;

namespace
{
enum : int16_t
{
    movieInDataForkResID = -1,
    kDitherCopy = 64,
};

/* Movie Toolbox errors (Movies.h) */
enum : OSErr
{
    couldNotResolveDataRef = -2000,
    badImageDescription = -2001,
    badPublicMovieAtom = -2002,
    cantFindHandler = -2003,
    cantOpenHandler = -2004,
    badComponentType = -2005,
    noMediaHandler = -2006,
    noDataHandler = -2007,
    invalidMedia = -2008,
    invalidTrack = -2009,
    invalidMovie = -2010,
    invalidSampleTable = -2011,
    invalidDataRef = -2012,
    invalidHandler = -2013,
    invalidDuration = -2014,
    invalidTime = -2015,
    cantPutPublicMovieAtom = -2016,
    badEditList = -2017,
    mediaTypesDontMatch = -2018,
    progressProcAborted = -2019,
    movieToolboxUninitialized = -2020,
    noRecordOfApp = movieToolboxUninitialized,
    wfFileNotFound = -2021,
    cantCreateSingleForkFile = -2022,
    invalidEditState = -2023,
    nonMatchingEditState = -2024,
    staleEditState = -2025,
    userDataItemNotFound = -2026,
    maxSizeToGrowTooSmall = -2027,
    badTrackIndex = -2028,
    trackIDNotFound = -2029,
    trackNotInMovie = -2030,
    timeNotInTrack = -2031,
    timeNotInMedia = -2032,
    badEditIndex = -2033,
    internalQuickTimeError = -2034,
    cantEnableTrack = -2035,
    invalidRect = -2036,
    invalidSampleNum = -2037,
    invalidChunkNum = -2038,
    invalidSampleDescIndex = -2039,
    invalidChunkCache = -2040,
    invalidSampleDescription = -2041,
    dataNotOpenForRead = -2042,
    dataNotOpenForWrite = -2043,
    dataAlreadyOpenForWrite = -2044,
    dataAlreadyClosed = -2045,
    endOfDataReached = -2046,
    dataNoDataRef = -2047,
    noMovieFound = -2048,
    invalidDataRefContainer = -2049,
    badDataRefIndex = -2050,
    noDefaultDataRef = -2051,
    couldNotUseAnExistingSample = -2052,
    featureUnsupported = -2053,
};

/* newMovieFlags */
enum : int16_t
{
    newMovieToolbox_Active = 1 << 0,
    newMovieDontResolveDataRefs = 1 << 1,
    newMovieDontAskUnresolvedDataRefs = 1 << 2,
    newMovieDontAutoAlternates = 1 << 3,
};

/* the time base's flags */
enum : int32_t
{
    loopTimeBase = 1,
    palindromeLoopTimeBase = 2,
    maintainTimeBaseZero = 4,
};

/* GetMovieNextInterestingTime's flags */
enum : int32_t
{
    nextTimeMediaSample = 1 << 0,
    nextTimeMediaEdit = 1 << 1,
    nextTimeTrackEdit = 1 << 2,
    nextTimeSyncSample = 1 << 3,
    nextTimeStep = 1 << 4,
    nextTimeEdgeOK = 1 << 14,
    nextTimeIgnoreActiveSegment = 1 << 15,
};

/* GetMovieIndTrackType's flags */
enum : int32_t
{
    movieTrackMediaType = 1 << 0,
    movieTrackCharacteristic = 1 << 1,
    movieTrackEnabledOnly = 1 << 2,
};

/* AddMediaSample's flags */
enum : int16_t
{
    mediaSampleNotSync = 1 << 0,
    mediaSampleShadowSync = 1 << 1,
};

/* FlattenMovie's flags */
enum : int32_t
{
    flattenAddMovieToDataFork = 1 << 0,
    flattenActiveTracksOnly = 1 << 2,
    flattenDontInterleaveFlatten = 1 << 3,
    flattenFSSpecPtrIsDataRefRecordPtr = 1 << 4,
    flattenCompressMovieResource = 1 << 5,
    flattenForceMovieResourceBeforeMovieData = 1 << 6,
};

/* CreateMovieFile's flags */
enum : uint32_t
{
    createMovieFileDeleteCurFile = 1u << 31,
    createMovieFileDontCreateMovie = 1u << 30,
    createMovieFileDontOpenFile = 1u << 29,
    createMovieFileDontCreateResFile = 1u << 28,
};

/* the track header's flags */
enum : uint32_t
{
    trackEnabled = 1,
    trackInMovie = 2,
    trackInPreview = 4,
    trackInPoster = 8,
};

enum : uint32_t
{
    kVideoMediaType = "vide"_4,
    kSoundMediaType = "soun"_4,
    kTextMediaType = "text"_4,
    kAliasDataType = "alis"_4,
    kMediaHandlerType = "mhlr"_4,
    kDataHandlerType = "dhlr"_4,
    kAppleManufacturer = "appl"_4,
};

/* the File Manager's calls take FSSpec *, the Movie Toolbox's const */
FSSpec *mut(const FSSpec *s)
{
    return const_cast<FSSpec *>(s);
}

OSErr movies_error = noErr;
OSErr sticky_error = noErr;
bool entered = false;

OSErr set_error(OSErr err)
{
    movies_error = err;
    if(err != noErr && sticky_error == noErr)
        sticky_error = err;
    return err;
}

/* --- big-endian bytes ------------------------------------------------- */

using Bytes = std::vector<uint8_t>;

uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

void put32(Bytes &b, uint32_t v)
{
    b.push_back(v >> 24);
    b.push_back(v >> 16);
    b.push_back(v >> 8);
    b.push_back(v);
}

void put16(Bytes &b, uint16_t v)
{
    b.push_back(v >> 8);
    b.push_back(v);
}

void put_bytes(Bytes &b, const void *p, size_t n)
{
    b.insert(b.end(), (const uint8_t *)p, (const uint8_t *)p + n);
}

/* An atom being written: its size patched at the end */
struct AtomOut
{
    Bytes &b;
    size_t at;
    AtomOut(Bytes &bytes, uint32_t type) : b(bytes), at(bytes.size())
    {
        put32(b, 0);
        put32(b, type);
    }
    ~AtomOut()
    {
        uint32_t n = b.size() - at;
        b[at] = n >> 24;
        b[at + 1] = n >> 16;
        b[at + 2] = n >> 8;
        b[at + 3] = n;
    }
};

/* The atoms in [p, end): type, payload */
struct Atom
{
    uint32_t type;
    const uint8_t *data;
    uint32_t size;
};

std::vector<Atom> atoms(const uint8_t *p, const uint8_t *end)
{
    std::vector<Atom> out;
    while(end - p >= 8)
    {
        uint32_t size = be32(p);
        uint32_t type = be32(p + 4);
        if(size == 0)
            size = end - p;
        if(size < 8 || size > (uint32_t)(end - p))
            break;
        out.push_back({ type, p + 8, size - 8 });
        p += size;
    }
    return out;
}

const Atom *find(const std::vector<Atom> &list, uint32_t type)
{
    for(const Atom &a : list)
        if(a.type == type)
            return &a;
    return nullptr;
}

std::vector<Atom> children(const Atom &a)
{
    return atoms(a.data, a.data + a.size);
}

/* --- matrices ---------------------------------------------------------- */

/* (not SetIdentityMatrix: the host structures' matrices are in the
   host's heap) */
void identity(MatrixRecord &m)
{
    for(int i = 0; i < 3; i++)
        for(int j = 0; j < 3; j++)
            m.matrix[i][j] = i == j ? (i == 2 ? 0x40000000 : 0x10000) : 0;
}

void read_matrix(MatrixRecord &m, const uint8_t *p)
{
    for(int i = 0; i < 3; i++)
        for(int j = 0; j < 3; j++)
            m.matrix[i][j] = (Fixed)be32(p + 4 * (3 * i + j));
}

void write_matrix(Bytes &b, const MatrixRecord &m)
{
    for(int i = 0; i < 3; i++)
        for(int j = 0; j < 3; j++)
            put32(b, (uint32_t)(Fixed)m.matrix[i][j]);
}

/* the rectangle r through m (scaling and translation; QuickDraw has no
   rotation) */
Rect transform(const MatrixRecord &m, Rect r)
{
    MatrixRecord copy = m;
    TransformRect(&copy, &r, nullptr);
    return r;
}

/* --- data references ------------------------------------------------------ */

/* Where a media's samples are: a file's data fork, or memory */
struct DataFile
{
    bool in_memory = false;
    FSSpec spec;
    Bytes memory;
    INTEGER rn = 0;
    bool writable = false;

    DataFile() { memset(&spec, 0, sizeof spec); }
    ~DataFile() { close(); }

    void close()
    {
        if(rn)
            FSClose(rn);
        rn = 0;
        writable = false;
    }
    OSErr open(bool write)
    {
        if(in_memory)
            return noErr;
        if(rn && (writable || !write))
            return noErr;
        close();
        /* the File Manager's calls may go through 68k patches: their
           arguments in guest memory (Executor's stack), not the host's heap */
        FSSpec s = spec;
        GUEST<INTEGER> r;
        OSErr err = FSpOpenDF(&s, write ? fsRdWrPerm : fsRdPerm, &r);
        if(err != noErr)
            return err;
        rn = r;
        writable = write;
        return noErr;
    }
    int64_t size()
    {
        if(in_memory)
            return memory.size();
        if(open(false) != noErr)
            return 0;
        GUEST<LONGINT> eof;
        if(GetEOF(rn, &eof) != noErr)
            return 0;
        return eof;
    }
    /* n bytes at offset into guest memory dst */
    OSErr read(int64_t offset, uint32_t n, Ptr dst)
    {
        if(in_memory)
        {
            if(offset < 0 || offset + n > memory.size())
                return eofErr;
            memcpy(dst, memory.data() + offset, n);
            return noErr;
        }
        OSErr err = open(false);
        if(err != noErr)
            return err;
        err = SetFPos(rn, fsFromStart, offset);
        if(err != noErr)
            return err;
        GUEST<LONGINT> count = n;
        err = FSReadAll(rn, &count, dst);
        return err == noErr && (uint32_t)count != n ? eofErr : err;
    }
    /* n bytes from guest memory src at the end; where they went */
    OSErr append(Ptr src, uint32_t n, int64_t *where)
    {
        if(in_memory)
        {
            *where = memory.size();
            put_bytes(memory, src, n);
            return noErr;
        }
        OSErr err = open(true);
        if(err != noErr)
            return err;
        GUEST<LONGINT> eof;
        err = GetEOF(rn, &eof);
        if(err != noErr)
            return err;
        *where = eof;
        err = SetFPos(rn, fsFromStart, eof);
        if(err != noErr)
            return err;
        GUEST<LONGINT> count = n;
        return FSWriteAll(rn, &count, src);
    }
};

/* A guest buffer for reading samples into */
struct Buffer
{
    Handle h = nullptr;
    ~Buffer()
    {
        if(h)
            DisposeHandle(h);
    }
    Ptr get(uint32_t n)
    {
        if(!h)
        {
            h = NewHandle(std::max<uint32_t>(n, 1));
            if(!h)
                h = TempNewHandle(std::max<uint32_t>(n, 1), nullptr);
            if(!h)
                return nullptr;
        }
        else if((uint32_t)GetHandleSize(h) < n)
        {
            HUnlock(h);
            SetHandleSize(h, n);
            if(MemError() != noErr)
                return nullptr;
        }
        HLock(h);
        return *h;
    }
};

/* --- the host structures ------------------------------------------------ */

struct MovieS;
struct TrackS;
struct MediaS;

struct Chunk
{
    int64_t offset;
    uint32_t first;   /* its first sample (0-based) */
    uint32_t count;
    uint16_t desc;    /* 1-based */
    uint16_t dref;    /* 1-based */
};

struct TimeRun
{
    uint32_t count;
    uint32_t duration;
};

struct UserItem
{
    uint32_t type;
    Bytes data;
};

struct UserDataS
{
    Ptr guest = nullptr;
    std::vector<UserItem> items;
};

/* a movie's time base changed: its sound starts over from its time (B5) */
void sound_retime(MovieS *m);

struct TimeBaseS
{
    Ptr guest = nullptr;
    MovieS *movie = nullptr;     /* the movie whose time base it is */
    uint32_t generation = 0;     /* counts rate, time and flag changes */
    Fixed rate = 0;
    int64_t value = 0;           /* at anchor_ms, in scale */
    int32_t scale = 600;
    unsigned long anchor_ms = 0;
    int32_t flags = 0;
    int64_t start = 0, stop = 0; /* in scale */

    int64_t now()
    {
        int64_t t = value;
        if(rate)
        {
            int64_t ms = (int64_t)(msecs_elapsed() - anchor_ms);
            t += ms * rate * scale / (65536LL * 1000);
            int64_t len = stop - start;
            if(len > 0 && (flags & (loopTimeBase | palindromeLoopTimeBase)))
            {
                if(t >= stop || t < start)
                {
                    int64_t k = (t - start) % len;
                    if(k < 0)
                        k += len;
                    t = start + k;
                }
            }
            else
                t = std::max(start, std::min(stop, t));
        }
        return t;
    }
    void anchor()
    {
        value = now();
        anchor_ms = msecs_elapsed();
    }
    void set_rate(Fixed r)
    {
        anchor();
        bool other = r != rate;
        rate = r;
        if(other)
            changed();
    }
    void set_time(int64_t t)
    {
        value = t;
        anchor_ms = msecs_elapsed();
        changed();
    }
    /* the time set back to t, as the sound clock says: no change of
       rate or time to start the sound over for */
    void slip(int64_t t)
    {
        value = t;
        anchor_ms = msecs_elapsed();
    }
    void changed()
    {
        generation++;
        if(movie)
            sound_retime(movie);
    }
    /* milliseconds since it reached its stop time playing forward; < 0
       before, or not playing */
    int64_t ms_past_stop()
    {
        if(rate <= 0 || (flags & (loopTimeBase | palindromeLoopTimeBase)))
            return -1;
        int64_t ms = (int64_t)(msecs_elapsed() - anchor_ms);
        if(value >= stop)
            return ms;
        int64_t reach = (stop - value) * 65536LL * 1000 / ((int64_t)rate * std::max(scale, 1));
        return ms - reach;
    }
};

struct MediaS
{
    Ptr guest = nullptr;
    TrackS *track = nullptr;
    uint32_t type = 0;
    uint32_t manufacturer = kAppleManufacturer;
    std::string handler_name;
    int32_t scale = 600;
    int64_t duration = 0;
    int16_t language = 0, quality = 0;
    std::vector<Bytes> descs;                 /* stsd entries, size first */
    std::vector<std::shared_ptr<DataFile>> drefs;
    std::vector<TimeRun> times;
    std::vector<uint32_t> sizes;              /* empty: const_size each */
    uint32_t const_size = 0;
    uint32_t count = 0;
    std::vector<Chunk> chunks;
    std::vector<uint32_t> sync;               /* 1-based; none: all sync */
    bool has_sync_table = false;
    int16_t graphics_mode = kDitherCopy;
    RGBColor opcolor;
    int16_t balance = 0;
    bool editing = false;

    MediaS() { opcolor.red = opcolor.green = opcolor.blue = 0x8000; }

    /* bytes of sample s (0-based) */
    uint32_t sample_bytes(uint32_t s, uint16_t desc) const
    {
        if(type == kSoundMediaType)
            return sound_bytes(1, desc);
        return sizes.empty() ? const_size : (s < sizes.size() ? sizes[s] : 0);
    }
    /* the bytes of n sound samples */
    uint32_t sound_bytes(uint32_t n, uint16_t desc) const
    {
        if(desc < 1 || desc > descs.size() || descs[desc - 1].size() < 36)
            return n * std::max<uint32_t>(const_size, 1);
        const uint8_t *d = descs[desc - 1].data();
        uint32_t format = be32(d + 4);
        uint32_t channels = std::max<uint32_t>(be16(d + 24), 1);
        uint32_t bits = be16(d + 26);
        switch(format)
        {
            case "ima4"_4:
                return (n + 63) / 64 * 34 * channels;
            case "MAC3"_4:
                return (n + 5) / 6 * 2 * channels;
            case "MAC6"_4:
                return (n + 5) / 6 * channels;
            case "ulaw"_4:
            case "alaw"_4:
                return n * channels;
            default:
                return n * channels * std::max<uint32_t>(bits / 8, 1);
        }
    }
    uint32_t chunk_bytes(const Chunk &c) const
    {
        if(type == kSoundMediaType)
            return sound_bytes(c.count, c.desc);
        if(sizes.empty())
            return c.count * const_size;
        uint32_t n = 0;
        for(uint32_t s = c.first; s < c.first + c.count && s < sizes.size(); s++)
            n += sizes[s];
        return n;
    }
    /* the chunk that has sample s */
    const Chunk *chunk_of(uint32_t s) const
    {
        auto it = std::upper_bound(chunks.begin(), chunks.end(), s,
                                   [](uint32_t v, const Chunk &c) { return v < c.first; });
        if(it == chunks.begin())
            return nullptr;
        --it;
        return s < it->first + it->count ? &*it : nullptr;
    }
    int64_t sample_offset(uint32_t s, const Chunk &c) const
    {
        int64_t off = c.offset;
        if(type == kSoundMediaType)
            return off + sound_bytes(s - c.first, c.desc);
        for(uint32_t k = c.first; k < s; k++)
            off += sample_bytes(k, c.desc);
        return off;
    }
    /* the sample at media time t; its start and duration */
    bool sample_at(int64_t t, uint32_t *s, int64_t *start, int64_t *dur) const
    {
        if(t < 0)
            return false;
        int64_t at = 0;
        uint32_t n = 0;
        for(const TimeRun &r : times)
        {
            int64_t len = (int64_t)r.count * r.duration;
            if(t < at + len && r.duration)
            {
                uint32_t k = (t - at) / r.duration;
                *s = n + k;
                *start = at + (int64_t)k * r.duration;
                *dur = r.duration;
                return true;
            }
            at += len;
            n += r.count;
        }
        return false;
    }
    /* sample s's media time and duration */
    bool time_of(uint32_t s, int64_t *start, int64_t *dur) const
    {
        int64_t at = 0;
        uint32_t n = 0;
        for(const TimeRun &r : times)
        {
            if(s < n + r.count)
            {
                *start = at + (int64_t)(s - n) * r.duration;
                *dur = r.duration;
                return true;
            }
            at += (int64_t)r.count * r.duration;
            n += r.count;
        }
        return false;
    }
    bool is_sync(uint32_t s) const
    {
        if(!has_sync_table)
            return true;
        return std::binary_search(sync.begin(), sync.end(), s + 1);
    }
    /* the sync sample at or before s */
    uint32_t sync_before(uint32_t s) const
    {
        if(!has_sync_table)
            return s;
        auto it = std::upper_bound(sync.begin(), sync.end(), s + 1);
        if(it == sync.begin())
            return 0;
        return *(it - 1) - 1;
    }
    void recount_duration()
    {
        int64_t d = 0;
        for(const TimeRun &r : times)
            d += (int64_t)r.count * r.duration;
        duration = d;
    }
};

struct Edit
{
    int64_t duration;    /* movie time scale */
    int64_t media_time;  /* -1: empty */
    Fixed rate;
};

struct TrackS
{
    Ptr guest = nullptr;
    MovieS *movie = nullptr;
    int32_t id = 0;
    uint32_t flags = trackEnabled | trackInMovie;
    bool enabled = true;
    int64_t duration = 0; /* movie time scale */
    Fixed width = 0, height = 0;
    MatrixRecord matrix;
    int16_t layer = 0, alternate = 0, volume = 0;
    uint32_t usage = 0;
    std::vector<Edit> edits;
    bool has_edits = false;
    std::unique_ptr<MediaS> media;
    UserDataS udta;
    TrackS *alternate_next = nullptr;

    /* drawing */
    ImageSequence seq = 0;
    uint16_t seq_desc = 0;
    Handle seq_desc_handle = nullptr;
    GWorldPtr buffer = nullptr;
    int16_t buffer_depth = 0;
    int64_t shown = -1;  /* the sample in buffer */
    bool drawn = false;

    /* sound (sound_task): a Sound Manager channel, the buffers queued on
       it (oldest first: each with the track time it starts at, movie
       scale, and the movie time a frame of it lasts), and where the next
       one starts: an edit and a media time into it */
    struct Queued
    {
        Ptr p;
        int64_t at;
        double per_frame;
    };
    SndChannelPtr chan = nullptr;
    std::deque<Queued> queued;
    uint32_t sound_generation = 0;
    bool sound_feeding = false;
    size_t sound_edit = 0;
    int64_t sound_offset = 0;     /* media time into the edit */
    int16_t chan_volume = -1;     /* the volume the channel has */
    bool sound_unknown = false;   /* a format not decoded: said once */

    TrackS() { identity(matrix); }

    void drop_drawing()
    {
        if(seq)
            CDSequenceEnd(seq);
        seq = 0;
        seq_desc = 0;
        if(seq_desc_handle)
            DisposeHandle(seq_desc_handle);
        seq_desc_handle = nullptr;
        if(buffer)
            DisposeGWorld(buffer);
        buffer = nullptr;
        shown = -1;
    }
    ~TrackS() { drop_drawing(); }

    /* media time at track time t (movie scale); false where the track
       has nothing */
    bool media_time(int64_t t, int32_t movie_scale, int64_t *mt) const
    {
        if(!media)
            return false;
        if(!has_edits)
        {
            if(t < 0 || t >= duration)
                return false;
            *mt = t * media->scale / std::max(movie_scale, 1);
            return true;
        }
        int64_t at = 0;
        for(const Edit &e : edits)
        {
            if(t >= at && t < at + e.duration)
            {
                if(e.media_time < 0)
                    return false;
                int64_t into = (t - at) * media->scale / std::max(movie_scale, 1);
                if(e.rate && e.rate != 0x10000)
                    into = into * e.rate / 0x10000;
                *mt = e.media_time + into;
                return true;
            }
            at += e.duration;
        }
        return false;
    }
    /* track time (movie scale) of media time mt in the first edit that
       has it; -1 if none */
    int64_t track_time(int64_t mt, int32_t movie_scale) const
    {
        if(!media)
            return -1;
        int32_t ms = std::max(media->scale, 1);
        if(!has_edits)
            return mt * movie_scale / ms;
        int64_t at = 0;
        for(const Edit &e : edits)
        {
            if(e.media_time >= 0)
            {
                int64_t len = e.duration * ms / std::max(movie_scale, 1);
                if(mt >= e.media_time && mt < e.media_time + len)
                    return at + (mt - e.media_time) * movie_scale / ms;
            }
            at += e.duration;
        }
        return -1;
    }
    void recount_duration()
    {
        if(!has_edits)
            return;
        int64_t d = 0;
        for(const Edit &e : edits)
            d += e.duration;
        duration = d;
    }
    Rect natural() const
    {
        Rect r;
        r.top = r.left = 0;
        r.right = width >> 16;
        r.bottom = height >> 16;
        return r;
    }
    bool visual() const
    {
        return media && media->type == kVideoMediaType && width > 0 && height > 0;
    }
};

struct MovieS
{
    Ptr guest = nullptr;
    int32_t scale = 600;
    int64_t duration = 0;
    Fixed preferred_rate = 0x10000;
    int16_t preferred_volume = 0x100, volume = 0x100;
    MatrixRecord matrix;
    int64_t preview_time = 0, preview_duration = 0, poster_time = 0;
    int64_t selection_time = 0, selection_duration = 0;
    uint32_t created = 0, modified = 0;
    int32_t next_track_id = 1;
    std::vector<std::unique_ptr<TrackS>> tracks;
    UserDataS udta;
    TimeBaseS tb;
    bool active = false;
    CGrafPtr port = nullptr;
    GDHandle gdh = nullptr;
    bool need_update = true;
    bool changed = false; /* edited since made, read or ClearMovieChanged */
    int32_t hints = 0;
    RgnHandle clip = nullptr, display_clip = nullptr;
    RgnHandle bounds_rgn = nullptr;
    CTabHandle ctab = nullptr;
    int64_t segment_start = 0, segment_duration = -1;
    std::shared_ptr<DataFile> self;   /* the file it came from, or was made for */
    bool resource_file_known = false;

    MovieS() { identity(matrix); }
    ~MovieS()
    {
        for(RgnHandle r : { clip, display_clip, bounds_rgn })
            if(r)
                DisposeRgn(r);
        if(ctab)
            DisposeHandle((Handle)ctab);
    }
    void recount_duration()
    {
        int64_t d = 0;
        for(auto &t : tracks)
            d = std::max(d, t->duration);
        duration = d;
        set_bounds();
    }
    void set_bounds()
    {
        tb.scale = scale;
        tb.start = segment_duration >= 0 ? segment_start : 0;
        tb.stop = segment_duration >= 0 ? segment_start + segment_duration : duration;
    }
    /* the movie's box: its visual tracks through their matrices and its */
    Rect box() const
    {
        Rect u;
        SetRect(&u, 0, 0, 0, 0);
        bool any = false;
        for(auto &t : tracks)
        {
            if(!t->visual() || !(t->flags & trackInMovie))
                continue;
            Rect r = transform(t->matrix, t->natural());
            if(!any)
                u = r;
            else
                UnionRect(&u, &r, &u);
            any = true;
        }
        if(!any)
            return u;
        return transform(matrix, u);
    }
    Rect natural_box() const
    {
        MatrixRecord saved = matrix;
        MovieS *self_ = const_cast<MovieS *>(this);
        identity(self_->matrix);
        Rect r = box();
        self_->matrix = saved;
        return r;
    }
};

/* the guest's blocks, by the ID in them */
struct Registry
{
    std::map<uint32_t, MovieS *> movies;
    std::map<uint32_t, TrackS *> tracks;
    std::map<uint32_t, MediaS *> medias;
    std::map<uint32_t, TimeBaseS *> timebases;
    std::map<uint32_t, UserDataS *> userdatas;
    std::map<uint32_t, std::unique_ptr<MovieS>> owned;
    uint32_t next = 0x6D760001;
} reg;

Ptr guest_block(uint32_t *id)
{
    Ptr p = NewPtrClear(8);
    if(!p)
        p = NewPtrSysClear(8);
    if(!p)
        return nullptr;
    *id = reg.next++;
    *(GUEST<uint32_t> *)p = *id;
    *(GUEST<uint32_t> *)(p + 4) = "QTmv"_4;
    return p;
}

uint32_t id_of(const void *p)
{
    if(!p)
        return 0;
    if(*(const GUEST<uint32_t> *)((const uint8_t *)p + 4) != "QTmv"_4)
        return 0;
    return *(const GUEST<uint32_t> *)p;
}

template<typename T>
T *lookup(std::map<uint32_t, T *> &map, const void *p)
{
    uint32_t id = id_of(p);
    if(!id)
        return nullptr;
    auto it = map.find(id);
    return it == map.end() ? nullptr : it->second;
}

MovieS *movie_of(Movie m)
{
    MovieS *s = lookup(reg.movies, m);
    set_error(s ? noErr : invalidMovie);
    return s;
}

TrackS *track_of(Track t)
{
    TrackS *s = lookup(reg.tracks, t);
    set_error(s ? noErr : invalidTrack);
    return s;
}

MediaS *media_of(Media m)
{
    MediaS *s = lookup(reg.medias, m);
    set_error(s ? noErr : invalidMedia);
    return s;
}

template<typename T>
bool give_id(T *s, std::map<uint32_t, T *> &map)
{
    uint32_t id;
    s->guest = guest_block(&id);
    if(!s->guest)
        return false;
    map[id] = s;
    return true;
}

void forget(void *guest)
{
    uint32_t id = id_of(guest);
    if(!id)
        return;
    reg.movies.erase(id);
    reg.tracks.erase(id);
    reg.medias.erase(id);
    reg.timebases.erase(id);
    reg.userdatas.erase(id);
    *(GUEST<uint32_t> *)((uint8_t *)guest + 4) = 0;
    DisposePtr((Ptr)guest);
}

bool register_media(MediaS *md)
{
    return give_id(md, reg.medias);
}

bool register_track(TrackS *t)
{
    if(!give_id(t, reg.tracks) || !give_id(&t->udta, reg.userdatas))
        return false;
    if(t->media && !register_media(t->media.get()))
        return false;
    return true;
}

MovieS *new_movie()
{
    auto m = std::make_unique<MovieS>();
    if(!give_id(m.get(), reg.movies))
        return nullptr;
    if(!give_id(&m->tb, reg.timebases) || !give_id(&m->udta, reg.userdatas))
    {
        forget(m->guest);
        return nullptr;
    }
    m->port = (CGrafPtr)qdGlobals().thePort;
    m->gdh = (GDHandle)LM(TheGDevice);
    m->created = m->modified = LM(Time);
    m->tb.movie = m.get();
    MovieS *raw = m.get();
    reg.owned[id_of(raw->guest)] = std::move(m);
    return raw;
}

void sound_dispose(TrackS &t);

void dispose_track(TrackS *t)
{
    sound_dispose(*t);
    if(t->media)
    {
        forget(t->media->guest);
        t->media.reset();
    }
    forget(t->udta.guest);
    forget(t->guest);
}

void dispose_movie(MovieS *m)
{
    for(auto &t : m->tracks)
        dispose_track(t.get());
    m->tracks.clear();
    forget(m->tb.guest);
    forget(m->udta.guest);
    uint32_t id = id_of(m->guest);
    forget(m->guest);
    reg.owned.erase(id);
}

/* --- reading a movie ----------------------------------------------------- */

void read_udta(UserDataS &u, const Atom &a)
{
    for(const Atom &c : children(a))
    {
        if(c.type == 0)
            break;
        UserItem item;
        item.type = c.type;
        item.data.assign(c.data, c.data + c.size);
        u.items.push_back(std::move(item));
    }
}

/* A data reference ('alis'): the movie's own file, or the file an alias
   names */
std::shared_ptr<DataFile> resolve_alias(const Atom &entry, const std::shared_ptr<DataFile> &self, int16_t flags)
{
    if(entry.size < 4)
        return nullptr;
    uint32_t vf = be32(entry.data);
    if(vf & 1)
        return self;
    if(flags & newMovieDontResolveDataRefs)
        return nullptr;
    uint32_t n = entry.size - 4;
    Handle h = NewHandle(n);
    if(!h)
        return nullptr;
    memcpy(*h, entry.data + 4, n);
    FSSpec target;
    Boolean changed;
    FSSpec self_spec;
    FSSpec *from = nullptr;
    if(self && !self->in_memory)
    {
        self_spec = self->spec;
        from = &self_spec;
    }
    OSErr err = ResolveAlias(from, (AliasHandle)h, &target, &changed);
    DisposeHandle(h);
    if(err != noErr)
        return nullptr;
    auto f = std::make_shared<DataFile>();
    f->spec = target;
    return f;
}

bool read_stbl(MediaS &md, const Atom &stbl)
{
    auto list = children(stbl);
    std::vector<std::pair<uint32_t, std::pair<uint32_t, uint16_t>>> stsc;
    std::vector<int64_t> offsets;
    for(const Atom &a : list)
    {
        const uint8_t *p = a.data;
        if(a.size < 8)
            continue;
        uint32_t n = be32(p + 4);
        switch(a.type)
        {
            case "stsd"_4:
            {
                const uint8_t *q = p + 8, *end = a.data + a.size;
                for(uint32_t i = 0; i < n && end - q >= 16; i++)
                {
                    uint32_t size = be32(q);
                    if(size < 16 || size > (uint32_t)(end - q))
                        break;
                    md.descs.emplace_back(q, q + size);
                    q += size;
                }
                break;
            }
            case "stts"_4:
                for(uint32_t i = 0; i < n && 8 + 8 * (i + 1) <= a.size; i++)
                    md.times.push_back({ be32(p + 8 + 8 * i), be32(p + 12 + 8 * i) });
                break;
            case "stss"_4:
                md.has_sync_table = true;
                for(uint32_t i = 0; i < n && 8 + 4 * (i + 1) <= a.size; i++)
                    md.sync.push_back(be32(p + 8 + 4 * i));
                std::sort(md.sync.begin(), md.sync.end());
                break;
            case "stsc"_4:
                for(uint32_t i = 0; i < n && 8 + 12 * (i + 1) <= a.size; i++)
                    stsc.push_back({ be32(p + 8 + 12 * i),
                                     { be32(p + 12 + 12 * i), (uint16_t)be32(p + 16 + 12 * i) } });
                break;
            case "stsz"_4:
            {
                md.const_size = be32(p + 4);
                md.count = be32(p + 8);
                if(md.const_size == 0)
                    for(uint32_t i = 0; i < md.count && 12 + 4 * (i + 1) <= a.size; i++)
                        md.sizes.push_back(be32(p + 12 + 4 * i));
                break;
            }
            case "stco"_4:
                for(uint32_t i = 0; i < n && 8 + 4 * (i + 1) <= a.size; i++)
                    offsets.push_back(be32(p + 8 + 4 * i));
                break;
            case "co64"_4:
                for(uint32_t i = 0; i < n && 8 + 8 * (i + 1) <= a.size; i++)
                    offsets.push_back((int64_t)be32(p + 8 + 8 * i) << 32 | be32(p + 12 + 8 * i));
                break;
        }
    }
    /* the chunks: stsc's runs over the chunk offsets */
    uint32_t sample = 0;
    for(size_t k = 0; k < stsc.size(); k++)
    {
        uint32_t first = stsc[k].first;
        uint32_t last = k + 1 < stsc.size() ? stsc[k + 1].first : offsets.size() + 1;
        for(uint32_t c = first; c < last && c >= 1 && c <= offsets.size(); c++)
        {
            Chunk ch;
            ch.offset = offsets[c - 1];
            ch.first = sample;
            ch.count = stsc[k].second.first;
            ch.desc = stsc[k].second.second;
            ch.dref = 1;
            if(ch.desc >= 1 && ch.desc <= md.descs.size() && md.descs[ch.desc - 1].size() >= 16)
                ch.dref = std::max<uint16_t>(be16(md.descs[ch.desc - 1].data() + 14), 1);
            md.chunks.push_back(ch);
            sample += ch.count;
        }
    }
    if(md.count == 0)
        md.count = sample;
    return true;
}

bool read_trak(MovieS &m, const Atom &trak, int16_t flags)
{
    auto t = std::make_unique<TrackS>();
    t->movie = &m;
    auto list = children(trak);
    if(const Atom *tkhd = find(list, "tkhd"_4))
    {
        const uint8_t *p = tkhd->data;
        if(tkhd->size >= 84)
        {
            t->flags = be32(p) & 0xFFFFFF;
            t->enabled = t->flags & trackEnabled;
            t->id = be32(p + 12);
            t->duration = be32(p + 20);
            t->layer = (int16_t)be16(p + 32);
            t->alternate = (int16_t)be16(p + 34);
            t->volume = (int16_t)be16(p + 36);
            read_matrix(t->matrix, p + 40);
            t->width = (Fixed)be32(p + 76);
            t->height = (Fixed)be32(p + 80);
        }
    }
    std::vector<Atom> edits_list;
    if(const Atom *edts = find(list, "edts"_4))
        edits_list = children(*edts);
    if(const Atom *elst = find(edits_list, "elst"_4))
    {
        const uint8_t *p = elst->data;
        uint32_t n = elst->size >= 8 ? be32(p + 4) : 0;
        for(uint32_t i = 0; i < n && 8 + 12 * (i + 1) <= elst->size; i++)
            t->edits.push_back({ be32(p + 8 + 12 * i), (int32_t)be32(p + 12 + 12 * i),
                                 (Fixed)be32(p + 16 + 12 * i) });
        t->has_edits = true;
    }
    if(const Atom *udta = find(list, "udta"_4))
        read_udta(t->udta, *udta);
    const Atom *mdia = find(list, "mdia"_4);
    if(mdia)
    {
        auto md = std::make_unique<MediaS>();
        md->track = t.get();
        auto ml = children(*mdia);
        if(const Atom *mdhd = find(ml, "mdhd"_4); mdhd && mdhd->size >= 24)
        {
            const uint8_t *p = mdhd->data;
            md->scale = be32(p + 12);
            md->duration = be32(p + 16);
            md->language = (int16_t)be16(p + 20);
            md->quality = (int16_t)be16(p + 22);
        }
        if(const Atom *hdlr = find(ml, "hdlr"_4); hdlr && hdlr->size >= 24)
        {
            const uint8_t *p = hdlr->data;
            md->type = be32(p + 8);
            md->manufacturer = be32(p + 12);
            if(hdlr->size > 24)
                md->handler_name.assign((const char *)p + 25, std::min<uint32_t>(p[24], hdlr->size - 25));
        }
        if(const Atom *minf = find(ml, "minf"_4))
        {
            auto il = children(*minf);
            if(const Atom *vmhd = find(il, "vmhd"_4); vmhd && vmhd->size >= 12)
            {
                md->graphics_mode = (int16_t)be16(vmhd->data + 4);
                md->opcolor.red = be16(vmhd->data + 6);
                md->opcolor.green = be16(vmhd->data + 8);
                md->opcolor.blue = be16(vmhd->data + 10);
            }
            if(const Atom *smhd = find(il, "smhd"_4); smhd && smhd->size >= 6)
                md->balance = (int16_t)be16(smhd->data + 4);
            std::vector<Atom> dinf_list;
            if(const Atom *dinf = find(il, "dinf"_4))
                dinf_list = children(*dinf);
            if(const Atom *dref = find(dinf_list, "dref"_4); dref && dref->size >= 8)
                for(const Atom &e : atoms(dref->data + 8, dref->data + dref->size))
                {
                    std::shared_ptr<DataFile> f;
                    if(e.type == kAliasDataType)
                        f = resolve_alias(e, m.self, flags);
                    md->drefs.push_back(f);
                }
            if(const Atom *stbl = find(il, "stbl"_4))
                read_stbl(*md, *stbl);
        }
        if(md->drefs.empty())
            md->drefs.push_back(m.self);
        t->media = std::move(md);
    }
    m.next_track_id = std::max(m.next_track_id, t->id + 1);
    m.tracks.push_back(std::move(t));
    return true;
}

OSErr read_moov(MovieS &m, const uint8_t *p, const uint8_t *end, int16_t flags)
{
    auto top = atoms(p, end);
    std::vector<Atom> list;
    if(const Atom *moov = find(top, "moov"_4))
        list = children(*moov);
    else
        list = top;
    if(const Atom *cmov = find(list, "cmov"_4))
    {
        /* QuickTime 3's compressed header: 'dcom' names the method, 'cmvd'
           holds the inflated size and the data, a whole 'moov' atom */
        auto parts = children(*cmov);
        const Atom *dcom = find(parts, "dcom"_4), *cmvd = find(parts, "cmvd"_4);
        if(!dcom || dcom->size < 4 || be32(dcom->data) != "zlib"_4 || !cmvd || cmvd->size < 4)
        {
            warning_unimplemented("compressed movie header ('cmov') not zlib");
            return badPublicMovieAtom;
        }
        uint32_t want = be32(cmvd->data);
        std::vector<uint8_t> moov;
        if(want > (64u << 20)
           || !ROMlib_zlib_inflate(cmvd->data + 4, cmvd->size - 4, moov, want) || moov.size() != want)
            return badPublicMovieAtom;
        return read_moov(m, moov.data(), moov.data() + moov.size(), flags);
    }
    const Atom *mvhd = find(list, "mvhd"_4);
    if(!mvhd || mvhd->size < 100)
        return badPublicMovieAtom;
    const uint8_t *h = mvhd->data;
    m.created = be32(h + 4);
    m.modified = be32(h + 8);
    m.scale = be32(h + 12);
    m.duration = be32(h + 16);
    m.preferred_rate = (Fixed)be32(h + 20);
    m.preferred_volume = (int16_t)be16(h + 24);
    m.volume = m.preferred_volume;
    read_matrix(m.matrix, h + 36);
    m.preview_time = be32(h + 72);
    m.preview_duration = be32(h + 76);
    m.poster_time = be32(h + 80);
    m.selection_time = be32(h + 84);
    m.selection_duration = be32(h + 88);
    m.next_track_id = be32(h + 96);
    for(const Atom &a : list)
        if(a.type == "trak"_4)
            read_trak(m, a, flags);
    if(const Atom *udta = find(list, "udta"_4))
        read_udta(m.udta, *udta);
    /* alternates: of each group the first enabled track is the one */
    if(!(flags & newMovieDontAutoAlternates))
    {
        std::map<int16_t, bool> taken;
        for(auto &t : m.tracks)
        {
            if(!t->alternate || !t->enabled)
                continue;
            if(taken[t->alternate])
                t->enabled = false;
            taken[t->alternate] = true;
        }
    }
    m.set_bounds();
    m.tb.set_time(0);
    return noErr;
}

/* the movie in bytes (a 'moov' resource or atom) */
OSErr movie_from_bytes(MovieS **out, const uint8_t *p, size_t n, std::shared_ptr<DataFile> self, int16_t flags)
{
    MovieS *m = new_movie();
    if(!m)
        return memFullErr;
    m->self = self;
    OSErr err = read_moov(*m, p, p + n, flags);
    if(err == noErr)
        for(auto &t : m->tracks)
            if(!register_track(t.get()))
                err = memFullErr;
    if(err != noErr)
    {
        dispose_movie(m);
        return err;
    }
    m->active = flags & newMovieToolbox_Active;
    *out = m;
    return noErr;
}

/* --- writing a movie ------------------------------------------------------- */

void write_udta(Bytes &b, const UserDataS &u)
{
    if(u.items.empty())
        return;
    AtomOut a(b, "udta"_4);
    for(const UserItem &i : u.items)
    {
        AtomOut c(b, i.type);
        put_bytes(b, i.data.data(), i.data.size());
    }
    put32(b, 0);
}

void write_alias_dref(Bytes &b)
{
    AtomOut dinf(b, "dinf"_4);
    AtomOut dref(b, "dref"_4);
    put32(b, 0);
    put32(b, 1);
    AtomOut alis(b, kAliasDataType);
    put32(b, 1); /* self-contained: the movie's own file */
}

/* The movie, its chunks at the offsets given (per track, per chunk: the
   offsets in the file it is written for) */
Bytes write_moov(const MovieS &m, const std::vector<std::vector<int64_t>> &offsets)
{
    Bytes b;
    {
        AtomOut moov(b, "moov"_4);
        {
            AtomOut mvhd(b, "mvhd"_4);
            put32(b, 0);
            put32(b, m.created);
            put32(b, m.modified);
            put32(b, m.scale);
            put32(b, m.duration);
            put32(b, m.preferred_rate);
            put16(b, m.preferred_volume);
            for(int i = 0; i < 10; i++)
                b.push_back(0);
            write_matrix(b, m.matrix);
            put32(b, m.preview_time);
            put32(b, m.preview_duration);
            put32(b, m.poster_time);
            put32(b, m.selection_time);
            put32(b, m.selection_duration);
            put32(b, 0); /* current time */
            put32(b, m.next_track_id);
        }
        for(size_t ti = 0; ti < m.tracks.size(); ti++)
        {
            const TrackS &t = *m.tracks[ti];
            AtomOut trak(b, "trak"_4);
            {
                AtomOut tkhd(b, "tkhd"_4);
                put32(b, (t.flags & ~trackEnabled) | (t.enabled ? trackEnabled : 0));
                put32(b, m.created);
                put32(b, m.modified);
                put32(b, t.id);
                put32(b, 0);
                put32(b, t.duration);
                put32(b, 0);
                put32(b, 0);
                put16(b, t.layer);
                put16(b, t.alternate);
                put16(b, t.volume);
                put16(b, 0);
                write_matrix(b, t.matrix);
                put32(b, t.width);
                put32(b, t.height);
            }
            if(t.has_edits)
            {
                AtomOut edts(b, "edts"_4);
                AtomOut elst(b, "elst"_4);
                put32(b, 0);
                put32(b, t.edits.size());
                for(const Edit &e : t.edits)
                {
                    put32(b, e.duration);
                    put32(b, (uint32_t)e.media_time);
                    put32(b, e.rate);
                }
            }
            if(const MediaS *md = t.media.get())
            {
                AtomOut mdia(b, "mdia"_4);
                {
                    AtomOut mdhd(b, "mdhd"_4);
                    put32(b, 0);
                    put32(b, m.created);
                    put32(b, m.modified);
                    put32(b, md->scale);
                    put32(b, md->duration);
                    put16(b, md->language);
                    put16(b, md->quality);
                }
                auto hdlr = [&](uint32_t type, uint32_t sub, const std::string &name) {
                    AtomOut h(b, "hdlr"_4);
                    put32(b, 0);
                    put32(b, type);
                    put32(b, sub);
                    put32(b, kAppleManufacturer);
                    put32(b, 0);
                    put32(b, 0);
                    b.push_back(name.size());
                    put_bytes(b, name.data(), name.size());
                };
                hdlr(kMediaHandlerType, md->type, md->handler_name);
                AtomOut minf(b, "minf"_4);
                if(md->type == kVideoMediaType)
                {
                    AtomOut vmhd(b, "vmhd"_4);
                    put32(b, 1);
                    put16(b, md->graphics_mode);
                    put16(b, md->opcolor.red);
                    put16(b, md->opcolor.green);
                    put16(b, md->opcolor.blue);
                }
                else if(md->type == kSoundMediaType)
                {
                    AtomOut smhd(b, "smhd"_4);
                    put32(b, 0);
                    put16(b, md->balance);
                    put16(b, 0);
                }
                else
                {
                    AtomOut gmhd(b, "gmhd"_4);
                }
                hdlr(kDataHandlerType, kAliasDataType, "Apple Alias Data Handler");
                write_alias_dref(b);
                AtomOut stbl(b, "stbl"_4);
                {
                    AtomOut stsd(b, "stsd"_4);
                    put32(b, 0);
                    put32(b, md->descs.size());
                    for(const Bytes &d : md->descs)
                    {
                        Bytes copy = d;
                        if(copy.size() >= 16)
                        {
                            copy[14] = 0; /* data reference 1: the file itself */
                            copy[15] = 1;
                        }
                        put_bytes(b, copy.data(), copy.size());
                    }
                }
                {
                    AtomOut stts(b, "stts"_4);
                    put32(b, 0);
                    put32(b, md->times.size());
                    for(const TimeRun &r : md->times)
                    {
                        put32(b, r.count);
                        put32(b, r.duration);
                    }
                }
                if(md->has_sync_table)
                {
                    AtomOut stss(b, "stss"_4);
                    put32(b, 0);
                    put32(b, md->sync.size());
                    for(uint32_t s : md->sync)
                        put32(b, s);
                }
                {
                    AtomOut stsc(b, "stsc"_4);
                    put32(b, 0);
                    size_t at = b.size();
                    put32(b, 0);
                    uint32_t n = 0;
                    for(size_t c = 0; c < md->chunks.size(); c++)
                    {
                        const Chunk &ch = md->chunks[c];
                        if(c && ch.count == md->chunks[c - 1].count && ch.desc == md->chunks[c - 1].desc)
                            continue;
                        put32(b, c + 1);
                        put32(b, ch.count);
                        put32(b, ch.desc);
                        n++;
                    }
                    b[at] = n >> 24;
                    b[at + 1] = n >> 16;
                    b[at + 2] = n >> 8;
                    b[at + 3] = n;
                }
                {
                    AtomOut stsz(b, "stsz"_4);
                    put32(b, 0);
                    put32(b, md->sizes.empty() ? md->const_size : 0);
                    put32(b, md->count);
                    for(uint32_t s : md->sizes)
                        put32(b, s);
                }
                {
                    AtomOut stco(b, "stco"_4);
                    put32(b, 0);
                    put32(b, md->chunks.size());
                    for(size_t c = 0; c < md->chunks.size(); c++)
                        put32(b, (uint32_t)(ti < offsets.size() && c < offsets[ti].size() ? offsets[ti][c]
                                                                                           : md->chunks[c].offset));
                }
            }
            write_udta(b, t.udta);
        }
        write_udta(b, m.udta);
    }
    return b;
}

/* the movie as it is, its chunks where they are */
Bytes write_moov(const MovieS &m)
{
    return write_moov(m, {});
}

/* --- drawing ---------------------------------------------------------------- */

/* the destination's pixel depth */
int16_t port_depth(CGrafPtr port)
{
    if(port && CGrafPort_p(port) && port->portPixMap)
        return (*port->portPixMap)->pixelSize;
    return 1;
}

/* sample s of the track's media into the guest buffer; its size */
Ptr read_sample(MediaS &md, uint32_t s, uint32_t *size, uint16_t *desc, Buffer &buf)
{
    const Chunk *c = md.chunk_of(s);
    if(!c || c->dref < 1 || c->dref > md.drefs.size() || !md.drefs[c->dref - 1])
        return nullptr;
    uint32_t n = md.sample_bytes(s, c->desc);
    Ptr p = buf.get(n);
    if(!p)
        return nullptr;
    if(md.drefs[c->dref - 1]->read(md.sample_offset(s, *c), n, p) != noErr)
        return nullptr;
    *size = n;
    *desc = c->desc;
    return p;
}

/* never freed: a heap's block may outlive the host's statics */
Buffer &sample_buffer = *new Buffer;

/* the track's sample s into its buffer, decoding from the sync sample
   before it when the buffer doesn't have the one before */
OSErr decode_to(TrackS &t, uint32_t s)
{
    MediaS &md = *t.media;
    int16_t depth = port_depth(t.movie->port);
    if(depth < 16)
        depth = 32;
    uint32_t first = s;
    if(!md.is_sync(s))
    {
        uint32_t sync = md.sync_before(s);
        first = t.shown >= sync && t.shown < s ? t.shown + 1 : sync;
    }
    for(uint32_t k = first; k <= s; k++)
    {
        uint32_t size;
        uint16_t desc;
        Ptr data = read_sample(md, k, &size, &desc, sample_buffer);
        if(!data)
            return set_error(dataNotOpenForRead);
        if(desc < 1 || desc > md.descs.size())
            return set_error(invalidSampleDescIndex);
        const Bytes &d = md.descs[desc - 1];
        if(d.size() < sizeof(ImageDescription))
            return set_error(badImageDescription);
        int16_t w = be16(d.data() + 32), h = be16(d.data() + 34);
        Rect bounds;
        SetRect(&bounds, 0, 0, w, h);
        if(t.buffer)
        {
            PixMapHandle pm = GetGWorldPixMap(t.buffer);
            Rect b = (*pm)->bounds;
            if((*pm)->pixelSize != depth || b.right - b.left != w || b.bottom - b.top != h)
                t.drop_drawing();
        }
        if(!t.buffer)
        {
            GUEST<GWorldPtr> gw;
            if(NewGWorld(&gw, depth, &bounds, nullptr, nullptr, 0) != noErr
               && NewGWorld(&gw, depth, &bounds, nullptr, nullptr, useTempMem) != noErr)
                return set_error(memFullErr);
            t.buffer = gw;
            t.buffer_depth = depth;
            LockPixels(GetGWorldPixMap(t.buffer));
        }
        if(t.seq && t.seq_desc != desc)
        {
            CDSequenceEnd(t.seq);
            t.seq = 0;
        }
        if(!t.seq)
        {
            if(t.seq_desc_handle)
                DisposeHandle(t.seq_desc_handle);
            t.seq_desc_handle = NewHandle(d.size());
            if(!t.seq_desc_handle)
                return set_error(memFullErr);
            memcpy(*t.seq_desc_handle, d.data(), d.size());
            GUEST<ImageSequence> seq;
            HLock(t.seq_desc_handle);
            CodecQ accuracy = (t.movie->hints & (1 << 8)) ? codecHighQuality : codecNormalQuality;
            OSErr err = DecompressSequenceBeginS(&seq, (ImageDescriptionHandle)t.seq_desc_handle, data, size,
                                                 t.buffer, nullptr, nullptr, nullptr, srcCopy, nullptr, 0,
                                                 accuracy, nullptr);
            HUnlock(t.seq_desc_handle);
            if(err != noErr)
                return set_error(err);
            t.seq = seq;
            t.seq_desc = desc;
        }
        GUEST<CodecFlags> out;
        OSErr err = DecompressSequenceFrameS(t.seq, data, size, 0, &out, nullptr);
        HUnlock(sample_buffer.h);
        if(err != noErr)
        {
            t.shown = -1;
            return set_error(err);
        }
        t.shown = k;
    }
    return noErr;
}

/* the track's buffer to the movie's port */
void show(TrackS &t)
{
    MovieS &m = *t.movie;
    if(!t.buffer || !m.port)
        return;
    PixMapHandle pm = GetGWorldPixMap(t.buffer);
    Rect src = (*pm)->bounds;
    Rect dst = transform(m.matrix, transform(t.matrix, t.natural()));
    GUEST<CGrafPtr> save_port;
    GUEST<GDHandle> save_dev;
    GetGWorld(&save_port, &save_dev);
    SetGWorld(m.port, m.gdh);
    RGBColor fg, bg;
    GetForeColor(&fg);
    GetBackColor(&bg);
    RGBColor black, white;
    black.red = black.green = black.blue = 0;
    white.red = white.green = white.blue = 0xFFFF;
    RGBForeColor(&black);
    RGBBackColor(&white);
    int16_t mode = t.media->graphics_mode == kDitherCopy || t.media->graphics_mode == srcCopy
        ? srcCopy : t.media->graphics_mode;
    RgnHandle mask = m.display_clip;
    CopyBits((BitMap *)*pm, PORT_BITS_FOR_COPY(m.port), &src, &dst, mode, mask);
    RGBForeColor(&fg);
    RGBBackColor(&bg);
    SetGWorld(save_port, save_dev);
    t.drawn = true;
}

/* what the movie shows at its time; force: draw even if unchanged. The
   first frame that failed to decode, else noErr. */
void sound_task(MovieS &m);

OSErr task(MovieS &m, bool force)
{
    OSErr result = noErr;
    sound_task(m);
    if(!m.active || !m.port)
        return result;
    int64_t now = m.tb.now();
    std::vector<TrackS *> order;
    for(auto &t : m.tracks)
        if(t->enabled && t->visual() && (t->flags & trackInMovie))
            order.push_back(t.get());
    /* back to front: higher layers first */
    std::stable_sort(order.begin(), order.end(), [](TrackS *a, TrackS *b) { return a->layer > b->layer; });
    for(TrackS *t : order)
    {
        int64_t mt, start, dur;
        uint32_t s;
        if(!t->media_time(now, m.scale, &mt) || !t->media->sample_at(mt, &s, &start, &dur))
            continue;
        if(!force && t->shown == s && t->drawn)
            continue;
        if(t->shown != s)
        {
            OSErr err = decode_to(*t, s);
            if(err != noErr)
            {
                if(result == noErr)
                    result = err;
                continue;
            }
        }
        show(*t);
    }
    m.need_update = false;
    return result;
}

/* --- sound ------------------------------------------------------------------ */

/* A sound track plays through a Sound Manager channel of its own: its
   samples, from the movie's time on, decoded (sound/sounddecode.cpp) to
   the mixer's 8-bit mono in buffers of an eighth of a second, a bufferCmd
   each, kept half a second ahead by MoviesTask. The movie's clock is the
   time base's (Executor's millisecond clock, which paces the mixer too);
   a change of rate, time or flags flushes the channel and starts again
   from the new time, at the sample rate times the movie's rate. The
   channel and its buffers are in the System heap, so they don't go with
   an application's heap before ROMlib_movies_process_exit disposes them. */

const int kSoundBuffersPerSecond = 8;
const int kSoundLeadBuffers = 4;
const int kSoundSlipMs = 100;       /* the sound this far behind: the clock waits */
const LONGINT kInitMono = 0x80;     /* IM: Sound, SndNewChannel's init */

struct SoundFormat
{
    uint32_t format = 0;
    int channels = 1, bits = 8;
    uint32_t rate = 0;   /* Fixed */
};

SoundFormat sound_format(const MediaS &md, uint16_t desc)
{
    SoundFormat f;
    if(desc < 1 || desc > md.descs.size() || md.descs[desc - 1].size() < 36)
        return f;
    const uint8_t *d = md.descs[desc - 1].data();
    f.format = be32(d + 4);
    f.channels = std::max<int>(be16(d + 24), 1);
    f.bits = be16(d + 26);
    f.rate = be32(d + 32);
    if(!f.rate)
        f.rate = (uint32_t)md.scale << 16;
    return f;
}

bool is_sound(const TrackS &t)
{
    return t.media && t.media->type == kSoundMediaType;
}

/* the track's edits; one over the whole media without an edit list */
std::vector<Edit> edit_list(const TrackS &t)
{
    if(t.has_edits)
        return t.edits;
    return { { t.duration, 0, 0x10000 } };
}

/* bufferCmds still on the channel */
int sound_pending(SndChannelPtr c)
{
    int q = c->qLength > 0 ? ((int)c->qTail - (int)c->qHead + c->qLength) % c->qLength : 0;
    return q + (SND_CHAN_CMDINPROG_P(c) ? 1 : 0);
}

/* buffers the channel has played: freed */
void sound_reap(TrackS &t)
{
    if(!t.chan)
        return;
    int pending = sound_pending(t.chan);
    while((int)t.queued.size() > pending)
    {
        DisposePtr(t.queued.front().p);
        t.queued.pop_front();
    }
}

void sound_flush(TrackS &t)
{
    if(t.chan)
    {
        SndCommand cmd;
        cmd.cmd = flushCmd;
        cmd.param1 = 0;
        cmd.param2 = 0;
        C_SndDoImmediate(t.chan, &cmd);
        cmd.cmd = quietCmd;
        C_SndDoImmediate(t.chan, &cmd);
    }
    for(auto &q : t.queued)
        DisposePtr(q.p);
    t.queued.clear();
    t.sound_feeding = false;
}

void sound_dispose(TrackS &t)
{
    sound_flush(t);
    if(t.chan)
    {
        C_SndDisposeChannel(t.chan, true);
        DisposePtr((Ptr)t.chan);
    }
    t.chan = nullptr;
    t.chan_volume = -1;
}

/* the edit at track time tt (movie scale) and how far into it, in media
   time; false past the last */
bool sound_position(TrackS &t, int64_t tt)
{
    std::vector<Edit> el = edit_list(t);
    int64_t at = 0;
    int32_t ms = std::max(t.movie->scale, 1), mds = t.media->scale;
    for(size_t i = 0; i < el.size(); i++)
    {
        if(tt < at + el[i].duration)
        {
            t.sound_edit = i;
            t.sound_offset = std::max<int64_t>(0, tt - at) * mds / ms;
            return true;
        }
        at += el[i].duration;
    }
    t.sound_edit = el.size();
    t.sound_offset = 0;
    return false;
}

/* frames of the media from sample s on, decoded into out */
void sound_read(TrackS &t, uint32_t s, uint32_t frames, uint8_t *out)
{
    MediaS &md = *t.media;
    memset(out, 0x80, frames);
    uint32_t done = 0;
    while(done < frames)
    {
        const Chunk *c = md.chunk_of(s + done);
        if(!c || c->dref < 1 || c->dref > md.drefs.size() || !md.drefs[c->dref - 1])
            return;
        SoundFormat f = sound_format(md, c->desc);
        uint32_t here = std::min(frames - done, c->first + c->count - (s + done));
        uint32_t fpp = ROMlib_sound_frames_per_packet(f.format);
        uint32_t bpp = ROMlib_sound_bytes_per_packet(f.format, f.channels, f.bits);
        uint32_t rel = s + done - c->first;
        uint32_t skip = rel % fpp;
        uint32_t bytes = (skip + here + fpp - 1) / fpp * bpp;
        Ptr p = sample_buffer.get(bytes);
        if(!p)
            return;
        bool ok = md.drefs[c->dref - 1]->read(c->offset + (int64_t)(rel / fpp) * bpp, bytes, p) == noErr
            && ROMlib_decode_sound(f.format, f.channels, f.bits, (const uint8_t *)p, bytes, skip, here, out + done);
        HUnlock(sample_buffer.h);
        if(!ok && !t.sound_unknown)
        {
            t.sound_unknown = true;
            warning_unimplemented("sound media '%c%c%c%c' %d-bit: not played",
                                  (char)(f.format >> 24), (char)(f.format >> 16), (char)(f.format >> 8),
                                  (char)f.format, f.bits);
        }
        done += here;
    }
}

/* the next buffer onto the channel; false when there is no more */
bool sound_feed_one(TrackS &t)
{
    MovieS &m = *t.movie;
    MediaS &md = *t.media;
    std::vector<Edit> el = edit_list(t);
    int32_t ms = std::max(m.scale, 1), mds = std::max(md.scale, 1);
    for(int wraps = 0;;)
    {
        int64_t at = 0;
        for(size_t i = 0; i < t.sound_edit && i < el.size(); i++)
            at += el[i].duration;
        if(t.sound_edit < el.size() && at < m.tb.stop)
        {
            const Edit &e = el[t.sound_edit];
            int64_t len = (std::min(at + e.duration, m.tb.stop) - at) * mds / ms;
            if(t.sound_offset < len)
            {
                /* this buffer's frames: the media's samples from s on */
                uint32_t s = 0;
                int64_t start = 0, dur = 1;
                bool empty = e.media_time < 0
                    || !md.sample_at(e.media_time + t.sound_offset, &s, &start, &dur);
                const Chunk *c = md.chunk_of(empty ? 0 : s);
                SoundFormat f = sound_format(md, c ? c->desc : 1);
                dur = std::max<int64_t>(dur, 1);
                uint32_t want = std::max<uint32_t>(f.rate / 65536 / kSoundBuffersPerSecond, 64);
                uint32_t frames = (uint32_t)std::min<int64_t>(want, (len - t.sound_offset + dur - 1) / dur);
                if(!empty)
                    frames = std::min<uint32_t>(frames, md.count > s ? md.count - s : 0);
                if(!frames)
                    return false;
                Ptr hp;
                {
                    TheZoneGuard guard(LM(SysZone));
                    hp = NewPtr(offsetof(SoundHeader, sampleArea) + frames);
                }
                if(!hp)
                    return false;
                SoundHeaderPtr h = (SoundHeaderPtr)hp;
                uint8_t *data = (uint8_t *)hp + offsetof(SoundHeader, sampleArea);
                if(empty)
                    memset(data, 0x80, frames);
                else
                    sound_read(t, s, frames, data);
                h->samplePtr = nullptr;
                h->length = frames;
                int64_t rate = (int64_t)f.rate * m.tb.rate / 0x10000;
                if(e.rate > 0 && e.rate != 0x10000)
                    rate = rate * e.rate / 0x10000;
                h->sampleRate = (Fixed)(uint32_t)std::min<int64_t>(rate, 0xFFFFFFFFLL);
                h->loopStart = 0;
                h->loopEnd = 0;
                h->encode = stdSH;
                h->baseFrequency = 60;
                SndCommand cmd;
                cmd.cmd = bufferCmd;
                cmd.param1 = 0;
                cmd.param2 = (LONGINT)US_TO_SYN68K(hp);
                if(C_SndDoCommand(t.chan, &cmd, true) != noErr)
                {
                    DisposePtr(hp);
                    return false;
                }
                t.queued.push_back({ hp, at + t.sound_offset * ms / mds, (double)dur * ms / mds });
                t.sound_offset += (int64_t)frames * dur;
                return true;
            }
            /* this edit is over: the next */
            if(at + e.duration < m.tb.stop && t.sound_edit + 1 < el.size())
            {
                t.sound_edit++;
                t.sound_offset = 0;
                continue;
            }
        }
        /* the end: a looping movie goes on from its start */
        if(!(m.tb.flags & loopTimeBase) || (m.tb.flags & palindromeLoopTimeBase) || wraps++
           || !sound_position(t, m.tb.start))
            return false;
    }
}

/* the track time (movie scale) of the next sample the mixer takes from
   the track's channel; false when it has none queued */
bool sound_time(TrackS &t, double *now)
{
    if(!t.chan || t.queued.empty())
        return false;
    const TrackS::Queued &q = t.queued.front();
    double frames = 0;
    if(SND_CHAN_CMDINPROG_P(t.chan) && ROMlib_sound_rate > 0)
    {
        snd_time start = SND_CHAN_CURRENT_START(t.chan), at = SND_PROMOTE(SND_CHAN_TIME(t.chan));
        if(at > start)
            frames = (double)(at - start) / 4294967296.0
                * ((uint32_t)((SoundHeaderPtr)q.p)->sampleRate / 65536.0) / ROMlib_sound_rate;
    }
    *now = q.at + frames * q.per_frame;
    return true;
}

/* the track's channel's volume: the movie's times the track's; either
   below zero (muted) is silence */
int16_t sound_volume(const TrackS &t)
{
    const MovieS &m = *t.movie;
    if(m.volume <= 0 || t.volume <= 0)
        return 0;
    return (int16_t)std::min<int32_t>((int32_t)m.volume * t.volume / 0x100, 0x7FFF);
}

/* each sound track at the movie's time and rate: started, stopped, kept
   fed, at its volume */
void sound_task(MovieS &m)
{
    bool clocked = false;
    for(auto &tp : m.tracks)
    {
        TrackS &t = *tp;
        if(!is_sound(t))
            continue;
        sound_reap(t);
        if(!m.active || m.tb.rate <= 0 || !t.enabled || !(t.flags & trackInMovie))
        {
            if(t.sound_feeding || !t.queued.empty())
                sound_flush(t);
            continue;
        }
        /* at the end what is queued plays out */
        if(m.tb.now() >= m.tb.stop && !(m.tb.flags & loopTimeBase))
            continue;
        if(!t.chan)
        {
            /* no sound here (or only pretended): the movie keeps time */
            TheZoneGuard guard(LM(SysZone));
            GUEST<SndChannelPtr> chan = (SndChannelPtr)NewPtrClear(sizeof(SndChannel));
            if(!chan)
                continue;
            if(C_SndNewChannel(&chan, sampledSynth, kInitMono, nullptr) != noErr
               || !((SndChannelPtr)chan)->firstMod)
            {
                DisposePtr((Ptr)(SndChannelPtr)chan);
                continue;
            }
            t.chan = chan;
            t.chan_volume = -1;
            t.sound_feeding = false;
        }
        if(!t.sound_feeding || t.sound_generation != m.tb.generation)
        {
            sound_flush(t);
            t.sound_generation = m.tb.generation;
            if(!sound_position(t, m.tb.now()))
                continue;
            t.sound_feeding = true;
        }
        int16_t vol = sound_volume(t);
        if(vol != t.chan_volume)
        {
            SndCommand cmd;
            cmd.cmd = volumeCmd;
            cmd.param1 = 0;
            cmd.param2 = (LONGINT)((uint32_t)(uint16_t)vol << 16 | (uint16_t)vol);
            C_SndDoImmediate(t.chan, &cmd);
            t.chan_volume = vol;
        }
        while(t.queued.size() < (size_t)kSoundLeadBuffers && sound_feed_one(t))
            ;
        /* the movie's clock follows the first sound's: when the mixer fell
           behind (it is fed at interrupt time, which host code holds off)
           the movie waits for it rather than its sound being cut short */
        double st;
        if(!clocked && sound_time(t, &st))
        {
            clocked = true;
            int64_t now = m.tb.now();
            if(now - st > m.scale * kSoundSlipMs / 1000.0)
                m.tb.slip((int64_t)st);
        }
    }
}

/* the movie's sound is still playing */
bool sound_playing(MovieS &m)
{
    for(auto &t : m.tracks)
        if(is_sound(*t) && t->chan)
        {
            sound_reap(*t);
            if(!t->queued.empty())
                return true;
        }
    return false;
}

void sound_retime(MovieS *m)
{
    sound_task(*m);
}

}  // namespace

/* --- the Movie Toolbox's calls --------------------------------------------- */

OSErr Executor::C_EnterMovies()
{
    entered = true;
    sticky_error = noErr;
    return set_error(noErr);
}

void Executor::C_ExitMovies()
{
    set_error(noErr);
}

OSErr Executor::C_GetMoviesError()
{
    return movies_error;
}

void Executor::C_ClearMoviesStickyError()
{
    sticky_error = noErr;
}

OSErr Executor::C_GetMoviesStickyError()
{
    return sticky_error;
}

void Executor::C_MoviesTask(Movie movie, LONGINT maxmillisecs)
{
    if(!movie)
    {
        std::vector<MovieS *> all;
        for(auto &e : reg.movies)
            all.push_back(e.second);
        OSErr err = noErr;
        for(MovieS *m : all)
        {
            OSErr e = task(*m, m->need_update);
            if(err == noErr)
                err = e;
        }
        set_error(err);
        return;
    }
    /* MoviesTask's own result: what its frames gave, not what a call a
       component made meanwhile left (QuickTime 3's base codec asks
       GetTimeBaseTime of a nil time base each frame: paramErr) */
    if(MovieS *m = movie_of(movie))
        set_error(task(*m, m->need_update));
}

OSErr Executor::C_UpdateMovie(Movie movie)
{
    MovieS *m = movie_of(movie);
    if(!m)
        return invalidMovie;
    m->need_update = true;
    for(auto &t : m->tracks)
        t->drawn = false;
    return noErr;
}

OSErr Executor::C_PrerollMovie(Movie movie, TimeValue time, Fixed rate)
{
    return movie_of(movie) ? noErr : invalidMovie;
}

OSErr Executor::C_LoadMovieIntoRam(Movie theMovie, TimeValue time, TimeValue duration, LONGINT flags)
{
    return movie_of(theMovie) ? noErr : invalidMovie;
}

OSErr Executor::C_LoadTrackIntoRam(Track theTrack, TimeValue time, TimeValue duration, LONGINT flags)
{
    return track_of(theTrack) ? noErr : invalidTrack;
}

void Executor::C_SetMovieActive(Movie movie, Boolean active)
{
    if(MovieS *m = movie_of(movie))
    {
        m->active = active;
        if(!active)
            m->tb.set_rate(0);
        m->need_update = true;
    }
}

Boolean Executor::C_GetMovieActive(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m && m->active;
}

void Executor::C_StartMovie(Movie movie)
{
    if(MovieS *m = movie_of(movie))
    {
        m->active = true;
        int64_t t = m->tb.now();
        if(m->preferred_rate > 0 && t >= m->tb.stop)
            m->tb.set_time(m->tb.start);
        m->tb.set_rate(m->preferred_rate);
    }
}

void Executor::C_StopMovie(Movie movie)
{
    if(MovieS *m = movie_of(movie))
        m->tb.set_rate(0);
}

void Executor::C_GoToBeginningOfMovie(Movie movie)
{
    if(MovieS *m = movie_of(movie))
    {
        m->tb.set_time(m->tb.start);
        m->need_update = true;
    }
}

void Executor::C_GoToEndOfMovie(Movie theMovie)
{
    if(MovieS *m = movie_of(theMovie))
    {
        m->tb.set_time(m->tb.stop);
        m->need_update = true;
    }
}

Boolean Executor::C_IsMovieDone(Movie movie)
{
    MovieS *m = movie_of(movie);
    if(!m)
        return true;
    int64_t t = m->tb.now();
    if(m->tb.flags & (loopTimeBase | palindromeLoopTimeBase))
        return false;
    if(m->tb.rate < 0)
        return t <= m->tb.start;
    if(t < m->tb.stop)
        return false;
    /* a movie ends when its sound has played (a second at most more) */
    return !(m->tb.ms_past_stop() < 1000 && sound_playing(*m));
}

void Executor::C_SetMovieGWorld(Movie movie, CGrafPtr cgrafp, GDHandle gdh)
{
    if(MovieS *m = movie_of(movie))
    {
        m->port = cgrafp ? cgrafp : (CGrafPtr)qdGlobals().thePort;
        m->gdh = gdh ? gdh : (GDHandle)LM(TheGDevice);
        m->need_update = true;
    }
}

void Executor::C_GetMovieGWorld(Movie theMovie, GUEST<CGrafPtr> *port, GUEST<GDHandle> *gdh)
{
    MovieS *m = movie_of(theMovie);
    if(port)
        *port = m ? m->port : nullptr;
    if(gdh)
        *gdh = m ? m->gdh : nullptr;
}

void Executor::C_DisposeMovie(Movie movie)
{
    if(MovieS *m = movie_of(movie))
        dispose_movie(m);
}

/* --- the movie's geometry ----------------------------------------------- */

void Executor::C_GetMovieBox(Movie movie, Rect *boxp)
{
    MovieS *m = movie_of(movie);
    if(!boxp)
        return;
    if(!m)
    {
        memset(boxp, 0, sizeof *boxp);
        return;
    }
    *boxp = m->box();
}

void Executor::C_SetMovieBox(Movie movie, const Rect *boxp)
{
    MovieS *m = movie_of(movie);
    if(!m || !boxp)
        return;
    Rect n = m->natural_box();
    MatrixRecord mx;
    identity(mx);
    int w = n.right - n.left, h = n.bottom - n.top;
    if(w > 0 && h > 0)
    {
        Rect from = n, to = *boxp;
        RectMatrix(&mx, &from, &to);
    }
    else
        TranslateMatrix(&mx, (Fixed)(boxp->left - n.left) << 16, (Fixed)(boxp->top - n.top) << 16);
    m->matrix = mx;
    m->need_update = true;
}

void Executor::C_GetMovieMatrix(Movie theMovie, MatrixRecord *matrix)
{
    MovieS *m = movie_of(theMovie);
    if(matrix)
    {
        if(m)
            *matrix = m->matrix;
        else
            identity(*matrix);
    }
}

void Executor::C_SetMovieMatrix(Movie theMovie, const MatrixRecord *matrix)
{
    if(MovieS *m = movie_of(theMovie))
    {
        if(matrix)
            m->matrix = *matrix;
        else
            identity(m->matrix);
        m->need_update = true;
    }
}

namespace
{
RgnHandle box_region(MovieS *m)
{
    RgnHandle r = NewRgn();
    if(m)
    {
        Rect b = m->box();
        RectRgn(r, &b);
    }
    return r;
}
}

RgnHandle Executor::C_GetMovieDisplayBoundsRgn(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    RgnHandle r = box_region(m);
    if(m && m->display_clip)
        SectRgn(r, m->display_clip, r);
    return r;
}

RgnHandle Executor::C_GetMovieBoundsRgn(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    RgnHandle r = NewRgn();
    if(m)
    {
        Rect b = m->natural_box();
        RectRgn(r, &b);
    }
    return r;
}

RgnHandle Executor::C_GetMovieClipRgn(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    if(!m || !m->clip)
        return nullptr;
    RgnHandle r = NewRgn();
    CopyRgn(m->clip, r);
    return r;
}

void Executor::C_SetMovieClipRgn(Movie theMovie, RgnHandle theClip)
{
    if(MovieS *m = movie_of(theMovie))
    {
        if(m->clip)
            DisposeRgn(m->clip);
        m->clip = nullptr;
        if(theClip)
        {
            m->clip = NewRgn();
            CopyRgn(theClip, m->clip);
        }
        m->need_update = true;
    }
}

RgnHandle Executor::C_GetMovieDisplayClipRgn(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    if(!m || !m->display_clip)
        return nullptr;
    RgnHandle r = NewRgn();
    CopyRgn(m->display_clip, r);
    return r;
}

void Executor::C_SetMovieDisplayClipRgn(Movie theMovie, RgnHandle theClip)
{
    if(MovieS *m = movie_of(theMovie))
    {
        if(m->display_clip)
            DisposeRgn(m->display_clip);
        m->display_clip = nullptr;
        if(theClip)
        {
            m->display_clip = NewRgn();
            CopyRgn(theClip, m->display_clip);
        }
        m->need_update = true;
    }
}

Boolean Executor::C_PtInMovie(Movie theMovie, Point pt)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return false;
    Rect b = m->box();
    return PtInRect(pt, &b);
}

OSErr Executor::C_GetMovieColorTable(Movie theMovie, GUEST<CTabHandle> *ctab)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return invalidMovie;
    if(!ctab)
        return set_error(paramErr);
    *ctab = nullptr;
    if(m->ctab)
    {
        GUEST<Handle> h = (Handle)m->ctab;
        if(HandToHand(&h) != noErr)
            return set_error(MemError());
        *ctab = (CTabHandle)(Handle)h;
    }
    return noErr;
}

OSErr Executor::C_SetMovieColorTable(Movie theMovie, CTabHandle ctab)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return invalidMovie;
    if(m->ctab)
        DisposeHandle((Handle)m->ctab);
    m->ctab = nullptr;
    if(ctab)
    {
        GUEST<Handle> h = (Handle)ctab;
        if(HandToHand(&h) != noErr)
            return set_error(MemError());
        m->ctab = (CTabHandle)(Handle)h;
    }
    return noErr;
}

/* --- time ----------------------------------------------------------------- */

namespace
{
TimeBaseS *timebase_of(TimeBase tb)
{
    TimeBaseS *s = lookup(reg.timebases, tb);
    set_error(s ? noErr : paramErr);
    return s;
}

int64_t rescale(int64_t v, int32_t from, int32_t to)
{
    if(from == to || from <= 0)
        return v;
    return v * to / from;
}

void fill_record(TimeRecord *tr, int64_t v, int32_t scale, TimeBaseS *tb)
{
    if(!tr)
        return;
    tr->value.hi = (int32_t)(v >> 32);
    tr->value.lo = (uint32_t)v;
    tr->scale = scale;
    tr->base = tb ? (TimeBase)tb->guest : nullptr;
}

int64_t record_value(const TimeRecord *tr, int32_t scale)
{
    int64_t v = (int64_t)(int32_t)tr->value.hi << 32 | (uint32_t)tr->value.lo;
    return rescale(v, tr->scale, scale);
}
}

TimeBase Executor::C_GetMovieTimeBase(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? (TimeBase)m->tb.guest : nullptr;
}

TimeScale Executor::C_GetMovieTimeScale(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? m->scale : 0;
}

void Executor::C_SetMovieTimeScale(Movie theMovie, TimeScale timeScale)
{
    MovieS *m = movie_of(theMovie);
    if(!m || timeScale <= 0)
        return;
    int32_t old = m->scale;
    int64_t now = m->tb.now();
    for(auto &t : m->tracks)
    {
        t->duration = rescale(t->duration, old, timeScale);
        for(Edit &e : t->edits)
            e.duration = rescale(e.duration, old, timeScale);
    }
    m->scale = timeScale;
    m->poster_time = rescale(m->poster_time, old, timeScale);
    m->selection_time = rescale(m->selection_time, old, timeScale);
    m->selection_duration = rescale(m->selection_duration, old, timeScale);
    m->recount_duration();
    m->tb.set_time(rescale(now, old, timeScale));
}

TimeValue Executor::C_GetMovieDuration(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? m->duration : 0;
}

Fixed Executor::C_GetMovieRate(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? m->tb.rate : 0;
}

void Executor::C_SetMovieRate(Movie theMovie, Fixed rate)
{
    if(MovieS *m = movie_of(theMovie))
        m->tb.set_rate(rate);
}

Fixed Executor::C_GetMoviePreferredRate(Movie movie)
{
    MovieS *m = movie_of(movie);
    return m ? m->preferred_rate : 0;
}

void Executor::C_SetMoviePreferredRate(Movie theMovie, Fixed rate)
{
    if(MovieS *m = movie_of(theMovie))
        m->preferred_rate = rate;
}

TimeValue Executor::C_GetMovieTime(Movie theMovie, TimeRecord *currentTime)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return 0;
    int64_t t = m->tb.now();
    fill_record(currentTime, t, m->scale, &m->tb);
    return t;
}

void Executor::C_SetMovieTime(Movie theMovie, const TimeRecord *newtime)
{
    MovieS *m = movie_of(theMovie);
    if(!m || !newtime)
        return;
    m->tb.set_time(record_value(newtime, m->scale));
    m->need_update = true;
}

void Executor::C_SetMovieTimeValue(Movie theMovie, TimeValue newtime)
{
    if(MovieS *m = movie_of(theMovie))
    {
        m->tb.set_time(newtime);
        m->need_update = true;
    }
}

INTEGER Executor::C_GetMovieVolume(Movie movie)
{
    MovieS *m = movie_of(movie);
    return m ? m->volume : 0;
}

void Executor::C_SetMovieVolume(Movie theMovie, INTEGER volume)
{
    if(MovieS *m = movie_of(theMovie))
    {
        m->volume = volume;
        sound_task(*m);
    }
}

INTEGER Executor::C_GetMoviePreferredVolume(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? m->preferred_volume : 0;
}

void Executor::C_SetMoviePreferredVolume(Movie theMovie, INTEGER volume)
{
    if(MovieS *m = movie_of(theMovie))
        m->preferred_volume = volume;
}

void Executor::C_SetMoviePlayHints(Movie theMovie, LONGINT flags, LONGINT flagsMask)
{
    if(MovieS *m = movie_of(theMovie))
        m->hints = (m->hints & ~flagsMask) | (flags & flagsMask);
}

TimeValue Executor::C_GetMoviePosterTime(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? m->poster_time : 0;
}

void Executor::C_SetMoviePosterTime(Movie theMovie, TimeValue posterTime)
{
    if(MovieS *m = movie_of(theMovie))
        m->poster_time = posterTime;
}

void Executor::C_GetMovieSelection(Movie theMovie, GUEST<TimeValue> *selectionTime,
                                   GUEST<TimeValue> *selectionDuration)
{
    MovieS *m = movie_of(theMovie);
    if(selectionTime)
        *selectionTime = m ? m->selection_time : 0;
    if(selectionDuration)
        *selectionDuration = m ? m->selection_duration : 0;
}

void Executor::C_SetMovieSelection(Movie theMovie, TimeValue selectionTime, TimeValue selectionDuration)
{
    if(MovieS *m = movie_of(theMovie))
    {
        m->selection_time = selectionTime;
        m->selection_duration = selectionDuration;
    }
}

void Executor::C_GetMovieActiveSegment(Movie theMovie, GUEST<TimeValue> *startTime, GUEST<TimeValue> *duration)
{
    MovieS *m = movie_of(theMovie);
    if(startTime)
        *startTime = m && m->segment_duration >= 0 ? m->segment_start : -1;
    if(duration)
        *duration = m && m->segment_duration >= 0 ? m->segment_duration : 0;
}

void Executor::C_SetMovieActiveSegment(Movie theMovie, TimeValue startTime, TimeValue duration)
{
    if(MovieS *m = movie_of(theMovie))
    {
        m->segment_start = startTime;
        m->segment_duration = startTime < 0 ? -1 : duration;
        m->set_bounds();
    }
}

void Executor::C_GetMovieNextInterestingTime(Movie theMovie, INTEGER interestingTimeFlags, INTEGER numMediaTypes,
                                             const GUEST<OSType> *whichMediaTypes, TimeValue time, Fixed rate,
                                             GUEST<TimeValue> *interestingTime,
                                             GUEST<TimeValue> *interestingDuration)
{
    MovieS *m = movie_of(theMovie);
    int64_t best = -1, best_dur = 0;
    if(m)
    {
        bool edge = interestingTimeFlags & nextTimeEdgeOK;
        for(auto &t : m->tracks)
        {
            if(!t->enabled || !t->media)
                continue;
            if(numMediaTypes > 0 && whichMediaTypes)
            {
                bool want = false;
                for(int i = 0; i < numMediaTypes; i++)
                    want = want || whichMediaTypes[i] == t->media->type;
                if(!want)
                    continue;
            }
            MediaS &md = *t->media;
            int64_t mt, start, dur;
            uint32_t s;
            int64_t cand = -1, cand_dur = 0;
            if(interestingTimeFlags & (nextTimeMediaSample | nextTimeSyncSample | nextTimeStep))
            {
                bool sync_only = interestingTimeFlags & nextTimeSyncSample;
                if(t->media_time(time, m->scale, &mt) && md.sample_at(mt, &s, &start, &dur))
                {
                    int64_t k = s;
                    if(edge && start == mt && (!sync_only || md.is_sync(s)))
                        cand = time;
                    else
                    {
                        for(k = rate < 0 ? (int64_t)s - (start == mt ? 1 : 0) : s + 1;
                            k >= 0 && k < md.count; k += rate < 0 ? -1 : 1)
                            if(!sync_only || md.is_sync(k))
                                break;
                        if(k >= 0 && k < md.count && md.time_of(k, &start, &dur))
                            cand = t->track_time(start, m->scale);
                    }
                    if(cand >= 0 && md.time_of(cand == time ? s : k, &start, &dur))
                        cand_dur = rescale(dur, md.scale, m->scale);
                }
                else if(rate >= 0 && time < 0 && md.count && md.time_of(0, &start, &dur))
                {
                    cand = t->track_time(start, m->scale);
                    cand_dur = rescale(dur, md.scale, m->scale);
                }
            }
            if(interestingTimeFlags & (nextTimeMediaEdit | nextTimeTrackEdit))
            {
                int64_t at = 0;
                std::vector<Edit> edits = t->has_edits ? t->edits : std::vector<Edit>{ { t->duration, 0, 0x10000 } };
                for(const Edit &e : edits)
                {
                    bool ok = rate < 0 ? (at < time || (edge && at == time)) : (at > time || (edge && at == time));
                    if(ok && (cand < 0 || (rate < 0 ? at > cand : at < cand)))
                    {
                        cand = at;
                        cand_dur = e.duration;
                    }
                    at += e.duration;
                }
            }
            if(cand >= 0 && (best < 0 || (rate < 0 ? cand > best : cand < best)))
            {
                best = cand;
                best_dur = cand_dur;
            }
        }
    }
    if(interestingTime)
        *interestingTime = best;
    if(interestingDuration)
        *interestingDuration = best_dur;
}

/* time bases */

TimeValue Executor::C_GetTimeBaseTime(TimeBase tb, TimeScale s, TimeRecord *tr)
{
    TimeBaseS *b = timebase_of(tb);
    if(!b)
        return 0;
    int32_t scale = s ? s : b->scale;
    int64_t v = rescale(b->now(), b->scale, scale);
    fill_record(tr, v, scale, b);
    return v;
}

void Executor::C_SetTimeBaseTime(TimeBase tb, const TimeRecord *tr)
{
    if(TimeBaseS *b = timebase_of(tb); b && tr)
        b->set_time(record_value(tr, b->scale));
}

void Executor::C_SetTimeBaseValue(TimeBase tb, TimeValue t, TimeScale s)
{
    if(TimeBaseS *b = timebase_of(tb))
        b->set_time(rescale(t, s, b->scale));
}

Fixed Executor::C_GetTimeBaseRate(TimeBase tb)
{
    TimeBaseS *b = timebase_of(tb);
    return b ? b->rate : 0;
}

void Executor::C_SetTimeBaseRate(TimeBase tb, Fixed r)
{
    if(TimeBaseS *b = timebase_of(tb))
        b->set_rate(r);
}

LONGINT Executor::C_GetTimeBaseFlags(TimeBase tb)
{
    TimeBaseS *b = timebase_of(tb);
    return b ? b->flags : 0;
}

void Executor::C_SetTimeBaseFlags(TimeBase tb, LONGINT timeBaseFlags)
{
    if(TimeBaseS *b = timebase_of(tb))
    {
        b->anchor();
        b->flags = timeBaseFlags;
        b->changed();
    }
}

TimeValue Executor::C_GetTimeBaseStartTime(TimeBase tb, TimeScale s, TimeRecord *tr)
{
    TimeBaseS *b = timebase_of(tb);
    if(!b)
        return 0;
    int32_t scale = s ? s : b->scale;
    int64_t v = rescale(b->start, b->scale, scale);
    fill_record(tr, v, scale, b);
    return v;
}

TimeValue Executor::C_GetTimeBaseStopTime(TimeBase tb, TimeScale s, TimeRecord *tr)
{
    TimeBaseS *b = timebase_of(tb);
    if(!b)
        return 0;
    int32_t scale = s ? s : b->scale;
    int64_t v = rescale(b->stop, b->scale, scale);
    fill_record(tr, v, scale, b);
    return v;
}

void Executor::C_SetTimeBaseStartTime(TimeBase tb, const TimeRecord *tr)
{
    if(TimeBaseS *b = timebase_of(tb); b && tr)
    {
        b->anchor();
        b->start = record_value(tr, b->scale);
    }
}

void Executor::C_SetTimeBaseStopTime(TimeBase tb, const TimeRecord *tr)
{
    if(TimeBaseS *b = timebase_of(tb); b && tr)
    {
        b->anchor();
        b->stop = record_value(tr, b->scale);
    }
}

/* --- tracks ------------------------------------------------------------- */

LONGINT Executor::C_GetMovieTrackCount(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? m->tracks.size() : 0;
}

Track Executor::C_GetMovieTrack(Movie theMovie, LONGINT trackID)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return nullptr;
    for(auto &t : m->tracks)
        if(t->id == trackID)
            return (Track)t->guest;
    set_error(trackIDNotFound);
    return nullptr;
}

Track Executor::C_GetMovieIndTrack(Movie theMovie, LONGINT index)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return nullptr;
    if(index < 1 || index > (LONGINT)m->tracks.size())
    {
        set_error(badTrackIndex);
        return nullptr;
    }
    return (Track)m->tracks[index - 1]->guest;
}

Track Executor::C_GetMovieIndTrackType(Movie theMovie, LONGINT index, OSType trackType, LONGINT flags)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return nullptr;
    LONGINT k = 0;
    for(auto &t : m->tracks)
    {
        if((flags & movieTrackEnabledOnly) && !t->enabled)
            continue;
        bool match;
        if(flags & movieTrackCharacteristic)
            match = (trackType == "eyes"_4 && t->visual()) || (trackType == "ears"_4 && t->media
                                                               && t->media->type == kSoundMediaType);
        else
            match = t->media && t->media->type == trackType;
        if(match && ++k == index)
            return (Track)t->guest;
    }
    set_error(badTrackIndex);
    return nullptr;
}

LONGINT Executor::C_GetTrackID(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t ? t->id : 0;
}

Movie Executor::C_GetTrackMovie(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t ? (Movie)t->movie->guest : nullptr;
}

Media Executor::C_GetTrackMedia(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t && t->media ? (Media)t->media->guest : nullptr;
}

Boolean Executor::C_GetTrackEnabled(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t && t->enabled;
}

void Executor::C_SetTrackEnabled(Track theTrack, Boolean isEnabled)
{
    if(TrackS *t = track_of(theTrack))
    {
        t->enabled = isEnabled;
        t->movie->need_update = true;
        sound_task(*t->movie);
    }
}

TimeValue Executor::C_GetTrackDuration(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t ? t->duration : 0;
}

TimeValue Executor::C_GetTrackOffset(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    if(!t || !t->has_edits)
        return 0;
    int64_t at = 0;
    for(const Edit &e : t->edits)
    {
        if(e.media_time >= 0)
            return at;
        at += e.duration;
    }
    return at;
}

void Executor::C_GetTrackDimensions(Track theTrack, GUEST<Fixed> *width, GUEST<Fixed> *height)
{
    TrackS *t = track_of(theTrack);
    if(width)
        *width = t ? t->width : 0;
    if(height)
        *height = t ? t->height : 0;
}

void Executor::C_SetTrackDimensions(Track theTrack, Fixed width, Fixed height)
{
    if(TrackS *t = track_of(theTrack))
    {
        t->width = width;
        t->height = height;
        t->movie->need_update = true;
    }
}

void Executor::C_GetTrackMatrix(Track theTrack, MatrixRecord *matrix)
{
    TrackS *t = track_of(theTrack);
    if(matrix)
    {
        if(t)
            *matrix = t->matrix;
        else
            identity(*matrix);
    }
}

void Executor::C_SetTrackMatrix(Track theTrack, const MatrixRecord *matrix)
{
    if(TrackS *t = track_of(theTrack))
    {
        if(matrix)
            t->matrix = *matrix;
        else
            identity(t->matrix);
        t->movie->need_update = true;
    }
}

INTEGER Executor::C_GetTrackLayer(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t ? t->layer : 0;
}

void Executor::C_SetTrackLayer(Track theTrack, INTEGER layer)
{
    if(TrackS *t = track_of(theTrack))
        t->layer = layer;
}

Track Executor::C_GetTrackAlternate(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    if(!t)
        return nullptr;
    if(!t->alternate)
        return theTrack;
    /* the next track of its group, round to itself */
    auto &list = t->movie->tracks;
    size_t me = 0;
    while(me < list.size() && list[me].get() != t)
        me++;
    for(size_t k = 1; k <= list.size(); k++)
    {
        TrackS *o = list[(me + k) % list.size()].get();
        if(o->alternate == t->alternate)
            return (Track)o->guest;
    }
    return theTrack;
}

void Executor::C_SetTrackAlternate(Track theTrack, Track alternateT)
{
    TrackS *t = track_of(theTrack);
    if(!t)
        return;
    if(!alternateT)
    {
        t->alternate = 0;
        return;
    }
    TrackS *o = track_of(alternateT);
    if(!o)
        return;
    if(!o->alternate)
    {
        int16_t g = 1;
        for(auto &x : t->movie->tracks)
            g = std::max<int16_t>(g, x->alternate + 1);
        o->alternate = g;
    }
    t->alternate = o->alternate;
}

INTEGER Executor::C_GetTrackVolume(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t ? t->volume : 0;
}

void Executor::C_SetTrackVolume(Track theTrack, INTEGER volume)
{
    if(TrackS *t = track_of(theTrack))
    {
        t->volume = volume;
        if(t->movie)
            sound_task(*t->movie);
    }
}

UserData Executor::C_GetTrackUserData(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t ? (UserData)t->udta.guest : nullptr;
}

LONGINT Executor::C_GetTrackUsage(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    return t ? (t->flags & (trackInMovie | trackInPreview | trackInPoster)) : 0;
}

void Executor::C_SetTrackUsage(Track theTrack, LONGINT usage)
{
    if(TrackS *t = track_of(theTrack))
        t->flags = (t->flags & ~(trackInMovie | trackInPreview | trackInPoster))
            | (usage & (trackInMovie | trackInPreview | trackInPoster));
}

ComponentResult Executor::C_GetMovieStatus(Movie theMovie, GUEST<Track> *firstProblemTrack)
{
    MovieS *m = movie_of(theMovie);
    if(firstProblemTrack)
        *firstProblemTrack = nullptr;
    if(!m)
        return invalidMovie;
    for(auto &t : m->tracks)
        if(t->media)
            for(auto &d : t->media->drefs)
                if(!d)
                {
                    if(firstProblemTrack)
                        *firstProblemTrack = (Track)t->guest;
                    return couldNotResolveDataRef;
                }
    return noErr;
}

ComponentResult Executor::C_GetTrackStatus(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    if(!t)
        return invalidTrack;
    if(t->media)
        for(auto &d : t->media->drefs)
            if(!d)
                return couldNotResolveDataRef;
    return noErr;
}

Track Executor::C_NewMovieTrack(Movie theMovie, Fixed width, Fixed height, INTEGER trackVolume)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return nullptr;
    auto t = std::make_unique<TrackS>();
    t->movie = m;
    t->id = m->next_track_id++;
    t->width = width;
    t->height = height;
    t->volume = trackVolume;
    t->has_edits = true;
    if(!register_track(t.get()))
    {
        set_error(memFullErr);
        return nullptr;
    }
    Track g = (Track)t->guest;
    m->tracks.push_back(std::move(t));
    m->changed = true;
    return g;
}

void Executor::C_DisposeMovieTrack(Track theTrack)
{
    TrackS *t = track_of(theTrack);
    if(!t)
        return;
    MovieS *m = t->movie;
    m->changed = true;
    dispose_track(t);
    m->tracks.erase(std::remove_if(m->tracks.begin(), m->tracks.end(),
                                   [t](const std::unique_ptr<TrackS> &p) { return p.get() == t; }),
                    m->tracks.end());
    m->recount_duration();
}

/* --- media -------------------------------------------------------------- */

Track Executor::C_GetMediaTrack(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    return md ? (Track)md->track->guest : nullptr;
}

TimeScale Executor::C_GetMediaTimeScale(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    return md ? md->scale : 0;
}

void Executor::C_SetMediaTimeScale(Media theMedia, TimeScale timeScale)
{
    MediaS *md = media_of(theMedia);
    if(md && timeScale > 0)
        md->scale = timeScale;
}

TimeValue Executor::C_GetMediaDuration(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    return md ? md->duration : 0;
}

void Executor::C_GetMediaHandlerDescription(Media theMedia, GUEST<OSType> *mediaType, StringPtr creatorName,
                                            GUEST<OSType> *creatorManufacturer)
{
    MediaS *md = media_of(theMedia);
    if(mediaType)
        *mediaType = md ? md->type : 0;
    if(creatorManufacturer)
        *creatorManufacturer = md ? md->manufacturer : 0;
    if(creatorName)
    {
        std::string n = md ? md->handler_name : "";
        creatorName[0] = std::min<size_t>(n.size(), 255);
        memcpy(creatorName + 1, n.data(), creatorName[0]);
    }
}

LONGINT Executor::C_GetMediaSampleCount(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    return md ? md->count : 0;
}

LONGINT Executor::C_GetMediaSampleDescriptionCount(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    return md ? md->descs.size() : 0;
}

void Executor::C_GetMediaSampleDescription(Media theMedia, LONGINT index, SampleDescriptionHandle descH)
{
    MediaS *md = media_of(theMedia);
    if(!md || !descH)
        return;
    if(index < 1 || index > (LONGINT)md->descs.size())
    {
        set_error(invalidSampleDescIndex);
        return;
    }
    const Bytes &d = md->descs[index - 1];
    SetHandleSize((Handle)descH, d.size());
    if(MemError() != noErr)
    {
        set_error(MemError());
        return;
    }
    memcpy(*(Handle)descH, d.data(), d.size());
}

INTEGER Executor::C_GetMediaLanguage(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    return md ? md->language : 0;
}

INTEGER Executor::C_GetMediaQuality(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    return md ? md->quality : 0;
}

Media Executor::C_NewTrackMedia(Track theTrack, OSType mediaType, TimeScale timeScale, Handle dataRef,
                                OSType dataRefType)
{
    TrackS *t = track_of(theTrack);
    if(!t)
        return nullptr;
    if(t->media)
    {
        set_error(invalidMedia);
        return nullptr;
    }
    auto md = std::make_unique<MediaS>();
    md->track = t;
    md->type = mediaType;
    md->scale = timeScale > 0 ? timeScale : 600;
    switch(mediaType)
    {
        case kVideoMediaType:
            md->handler_name = "Apple Video Media Handler";
            break;
        case kSoundMediaType:
            md->handler_name = "Apple Sound Media Handler";
            break;
        default:
            md->handler_name = "Apple Generic Media Handler";
            break;
    }
    std::shared_ptr<DataFile> f;
    if(dataRef && dataRefType == kAliasDataType)
    {
        uint32_t n = GetHandleSize(dataRef);
        Bytes entry(4 + n);
        memcpy(entry.data() + 4, *dataRef, n);
        f = resolve_alias({ kAliasDataType, entry.data(), (uint32_t)entry.size() }, t->movie->self, 0);
        if(!f)
        {
            set_error(couldNotResolveDataRef);
            return nullptr;
        }
    }
    else if(dataRef)
    {
        set_error(cantFindHandler);
        return nullptr;
    }
    else
        f = t->movie->self;
    if(!f)
    {
        /* a movie with no file: its media's data in memory */
        f = std::make_shared<DataFile>();
        f->in_memory = true;
        t->movie->self = f;
    }
    md->drefs.push_back(f);
    if(!register_media(md.get()))
    {
        set_error(memFullErr);
        return nullptr;
    }
    t->media = std::move(md);
    return (Media)t->media->guest;
}

void Executor::C_DisposeTrackMedia(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    if(!md)
        return;
    TrackS *t = md->track;
    t->drop_drawing();
    sound_dispose(*t);
    forget(md->guest);
    t->media.reset();
}

OSErr Executor::C_BeginMediaEdits(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    if(!md)
        return invalidMedia;
    md->editing = true;
    return noErr;
}

OSErr Executor::C_EndMediaEdits(Media theMedia)
{
    MediaS *md = media_of(theMedia);
    if(!md)
        return invalidMedia;
    md->editing = false;
    if(!md->drefs.empty() && md->drefs[0])
        md->drefs[0]->close();
    return noErr;
}

OSErr Executor::C_AddMediaSample(Media theMedia, Handle dataIn, LONGINT inOffset, ULONGINT size,
                                 TimeValue durationPerSample, SampleDescriptionHandle sampleDescriptionH,
                                 LONGINT numberOfSamples, INTEGER sampleFlags, GUEST<TimeValue> *sampleTime)
{
    MediaS *md = media_of(theMedia);
    if(!md)
        return invalidMedia;
    if(!dataIn || numberOfSamples <= 0 || durationPerSample <= 0
       || inOffset < 0 || (uint32_t)GetHandleSize(dataIn) < inOffset + size)
        return set_error(paramErr);
    if(md->drefs.empty() || !md->drefs[0])
        return set_error(dataNotOpenForWrite);
    /* the description: one already there, or a new one */
    uint16_t desc = 0;
    if(sampleDescriptionH && *sampleDescriptionH)
    {
        uint32_t n = GetHandleSize((Handle)sampleDescriptionH);
        const uint8_t *p = (const uint8_t *)*(Handle)sampleDescriptionH;
        Bytes d(p, p + std::min<uint32_t>(n, std::max<uint32_t>(be32(p), 16)));
        for(size_t i = 0; i < md->descs.size(); i++)
            if(md->descs[i] == d)
                desc = i + 1;
        if(!desc)
        {
            md->descs.push_back(d);
            desc = md->descs.size();
        }
    }
    else if(!md->descs.empty())
        desc = md->descs.size();
    else
        return set_error(invalidSampleDescription);

    SignedByte state = HGetState(dataIn);
    HLock(dataIn);
    int64_t where;
    OSErr err = md->drefs[0]->append(*dataIn + inOffset, size, &where);
    HSetState(dataIn, state);
    if(err != noErr)
        return set_error(err);

    uint32_t first = md->count;
    Chunk c = { where, first, (uint32_t)numberOfSamples, desc, 1 };
    md->chunks.push_back(c);
    if(md->type == kSoundMediaType)
    {
        if(!md->const_size)
            md->const_size = 1;
    }
    else
    {
        uint32_t each = size / numberOfSamples;
        if(md->count == 0 && md->sizes.empty())
            md->const_size = each;
        if(!md->sizes.empty() || each != md->const_size || numberOfSamples * each != size)
        {
            if(md->sizes.empty())
                md->sizes.assign(md->count, md->const_size);
            for(LONGINT k = 0; k < numberOfSamples; k++)
                md->sizes.push_back(k + 1 < numberOfSamples ? each : size - each * (numberOfSamples - 1));
            md->const_size = 0;
        }
    }
    if(sampleFlags & mediaSampleNotSync)
    {
        if(!md->has_sync_table)
        {
            md->has_sync_table = true;
            for(uint32_t k = 1; k <= md->count; k++)
                md->sync.push_back(k);
        }
    }
    else if(md->has_sync_table)
        for(LONGINT k = 0; k < numberOfSamples; k++)
            md->sync.push_back(first + k + 1);
    md->count += numberOfSamples;
    if(!md->times.empty() && md->times.back().duration == (uint32_t)durationPerSample)
        md->times.back().count += numberOfSamples;
    else
        md->times.push_back({ (uint32_t)numberOfSamples, (uint32_t)durationPerSample });
    int64_t start = md->duration;
    md->recount_duration();
    if(sampleTime)
        *sampleTime = start;
    return noErr;
}

OSErr Executor::C_InsertMediaIntoTrack(Track theTrack, TimeValue trackStart, TimeValue mediaTime,
                                       TimeValue mediaDuration, Fixed mediaRate)
{
    TrackS *t = track_of(theTrack);
    if(!t)
        return invalidTrack;
    if(!t->media)
        return set_error(invalidMedia);
    if(mediaTime < 0 || mediaDuration < 0 || mediaTime + mediaDuration > t->media->duration)
        return set_error(invalidDuration);
    MovieS &m = *t->movie;
    m.changed = true;
    Fixed rate = mediaRate ? mediaRate : 0x10000;
    int64_t dur = rescale(mediaDuration, t->media->scale, m.scale) * 0x10000 / rate;
    if(!t->has_edits)
    {
        t->has_edits = true;
        if(t->duration)
            t->edits.push_back({ t->duration, 0, 0x10000 });
    }
    if(trackStart < 0)
        trackStart = t->duration;
    /* split the edit trackStart falls into */
    std::vector<Edit> out;
    int64_t at = 0;
    bool placed = false;
    for(const Edit &e : t->edits)
    {
        if(!placed && trackStart >= at && trackStart < at + e.duration)
        {
            int64_t head = trackStart - at;
            if(head > 0)
                out.push_back({ head, e.media_time, e.rate });
            out.push_back({ dur, mediaTime, rate });
            Edit tail = e;
            tail.duration = e.duration - head;
            if(tail.media_time >= 0)
                tail.media_time += rescale(head, m.scale, t->media->scale) * e.rate / 0x10000;
            out.push_back(tail);
            placed = true;
        }
        else
            out.push_back(e);
        at += e.duration;
    }
    if(!placed)
    {
        if(trackStart > at)
            out.push_back({ trackStart - at, -1, 0x10000 });
        out.push_back({ dur, mediaTime, rate });
    }
    t->edits = out;
    t->recount_duration();
    m.recount_duration();
    t->movie->need_update = true;
    return noErr;
}

void Executor::C_MediaTimeToSampleNum(Media theMedia, TimeValue time, GUEST<LONGINT> *sampleNum,
                                      GUEST<TimeValue> *sampleTime, GUEST<TimeValue> *sampleDuration)
{
    MediaS *md = media_of(theMedia);
    uint32_t s = 0;
    int64_t start = 0, dur = 0;
    bool ok = md && md->sample_at(time, &s, &start, &dur);
    if(md && !ok)
        set_error(timeNotInMedia);
    if(sampleNum)
        *sampleNum = ok ? s + 1 : 0;
    if(sampleTime)
        *sampleTime = ok ? start : -1;
    if(sampleDuration)
        *sampleDuration = ok ? dur : 0;
}

void Executor::C_SampleNumToMediaTime(Media theMedia, LONGINT logicalSampleNum, GUEST<TimeValue> *sampleTime,
                                      GUEST<TimeValue> *sampleDuration)
{
    MediaS *md = media_of(theMedia);
    int64_t start = 0, dur = 0;
    bool ok = md && logicalSampleNum >= 1 && md->time_of(logicalSampleNum - 1, &start, &dur);
    if(md && !ok)
        set_error(invalidSampleNum);
    if(sampleTime)
        *sampleTime = ok ? start : -1;
    if(sampleDuration)
        *sampleDuration = ok ? dur : 0;
}

OSErr Executor::C_GetMediaSample(Media theMedia, Handle dataOut, LONGINT maxSizeToGrow, GUEST<LONGINT> *size,
                                 TimeValue time, GUEST<TimeValue> *sampleTime, GUEST<TimeValue> *durationPerSample,
                                 SampleDescriptionHandle sampleDescriptionH, GUEST<LONGINT> *sampleDescriptionIndex,
                                 LONGINT maxNumberOfSamples, GUEST<LONGINT> *numberOfSamples,
                                 GUEST<INTEGER> *sampleFlags)
{
    MediaS *md = media_of(theMedia);
    if(!md)
        return invalidMedia;
    uint32_t s;
    int64_t start, dur;
    if(!md->sample_at(time, &s, &start, &dur))
        return set_error(timeNotInMedia);
    const Chunk *c = md->chunk_of(s);
    if(!c)
        return set_error(invalidSampleTable);
    uint32_t n = md->sample_bytes(s, c->desc);
    if(dataOut)
    {
        if(maxSizeToGrow && n > (uint32_t)maxSizeToGrow)
            return set_error(maxSizeToGrowTooSmall);
        SetHandleSize(dataOut, n);
        if(MemError() != noErr)
            return set_error(MemError());
        SignedByte state = HGetState(dataOut);
        HLock(dataOut);
        OSErr err = c->dref >= 1 && c->dref <= md->drefs.size() && md->drefs[c->dref - 1]
            ? md->drefs[c->dref - 1]->read(md->sample_offset(s, *c), n, *dataOut)
            : (OSErr)couldNotResolveDataRef;
        HSetState(dataOut, state);
        if(err != noErr)
            return set_error(err);
    }
    if(size)
        *size = n;
    if(sampleTime)
        *sampleTime = start;
    if(durationPerSample)
        *durationPerSample = dur;
    if(sampleDescriptionIndex)
        *sampleDescriptionIndex = c->desc;
    if(sampleDescriptionH)
        C_GetMediaSampleDescription(theMedia, c->desc, sampleDescriptionH);
    if(numberOfSamples)
        *numberOfSamples = 1;
    if(sampleFlags)
        *sampleFlags = md->is_sync(s) ? 0 : mediaSampleNotSync;
    return set_error(noErr);
}

/* --- user data ------------------------------------------------------------ */

UserData Executor::C_GetMovieUserData(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? (UserData)m->udta.guest : nullptr;
}

namespace
{
const UserItem *user_item(UserDataS *u, uint32_t type, LONGINT index)
{
    LONGINT k = 0;
    for(const UserItem &i : u->items)
        if(i.type == type && ++k == index)
            return &i;
    return nullptr;
}
}

OSErr Executor::C_GetUserData(UserData theUserData, Handle data, OSType udType, LONGINT index)
{
    UserDataS *u = lookup(reg.userdatas, theUserData);
    if(!u)
        return set_error(paramErr);
    const UserItem *i = user_item(u, udType, index);
    if(!i)
        return set_error(userDataItemNotFound);
    if(data)
    {
        SetHandleSize(data, i->data.size());
        if(MemError() != noErr)
            return set_error(MemError());
        memcpy(*data, i->data.data(), i->data.size());
    }
    return set_error(noErr);
}

/* an item's data into a buffer of size bytes, which must hold it
   (HyperCard's Movie XCMD reads its settings so) */
OSErr Executor::C_GetUserDataItem(UserData theUserData, void *data, LONGINT size, OSType udType, LONGINT index)
{
    UserDataS *u = lookup(reg.userdatas, theUserData);
    if(!u)
        return set_error(paramErr);
    const UserItem *i = user_item(u, udType, index ? index : 1);
    if(!i)
        return set_error(userDataItemNotFound);
    if((LONGINT)i->data.size() > size)
        return set_error(paramErr);
    if(data)
        memcpy(data, i->data.data(), i->data.size());
    return set_error(noErr);
}

INTEGER Executor::C_CountUserDataType(UserData theUserData, OSType udType)
{
    UserDataS *u = lookup(reg.userdatas, theUserData);
    if(!u)
        return 0;
    INTEGER n = 0;
    for(const UserItem &i : u->items)
        n += i.type == udType;
    return n;
}

LONGINT Executor::C_GetNextUserDataType(UserData theUserData, OSType udType)
{
    UserDataS *u = lookup(reg.userdatas, theUserData);
    if(!u)
        return 0;
    std::vector<uint32_t> types;
    for(const UserItem &i : u->items)
        if(std::find(types.begin(), types.end(), i.type) == types.end())
            types.push_back(i.type);
    if(!udType)
        return types.empty() ? 0 : types[0];
    auto it = std::find(types.begin(), types.end(), (uint32_t)udType);
    return it == types.end() || it + 1 == types.end() ? 0 : *(it + 1);
}

OSErr Executor::C_GetUserDataText(UserData theUserData, Handle data, OSType udType, LONGINT index,
                                  INTEGER itlRegionTag)
{
    UserDataS *u = lookup(reg.userdatas, theUserData);
    if(!u)
        return set_error(paramErr);
    /* '©xxx' items: (size.w, region.w, text) entries; the index-th of the
       region asked for */
    LONGINT k = 0;
    for(const UserItem &i : u->items)
    {
        if(i.type != udType)
            continue;
        const uint8_t *p = i.data.data(), *end = p + i.data.size();
        while(end - p >= 4)
        {
            uint16_t n = be16(p), region = be16(p + 2);
            if(n > end - p - 4)
                break;
            if(region == (uint16_t)itlRegionTag && ++k == index)
            {
                if(data)
                {
                    SetHandleSize(data, n);
                    if(MemError() != noErr)
                        return set_error(MemError());
                    memcpy(*data, p + 4, n);
                }
                return set_error(noErr);
            }
            p += 4 + n;
        }
    }
    return set_error(userDataItemNotFound);
}

/* the movie's tracks edited (tracks made, disposed, media inserted):
   MoviePlayer asks before it closes a movie whether to save it */
Boolean Executor::C_HasMovieChanged(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m && m->changed;
}

void Executor::C_ClearMovieChanged(Movie theMovie)
{
    if(MovieS *m = movie_of(theMovie))
        m->changed = false;
}

ULONGINT Executor::C_GetMovieCreationTime(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? m->created : 0;
}

ULONGINT Executor::C_GetMovieModificationTime(Movie theMovie)
{
    MovieS *m = movie_of(theMovie);
    return m ? m->modified : 0;
}

/* --- movie files ----------------------------------------------------------- */

namespace
{
/* data-fork refnums OpenMovieFile gave for files with no resource fork */
std::map<INTEGER, FSSpec> data_fork_refs;

bool spec_of_refnum(INTEGER rn, FSSpec *spec)
{
    auto it = data_fork_refs.find(rn);
    if(it != data_fork_refs.end())
    {
        *spec = it->second;
        return true;
    }
    FCBPBRec pb;
    memset(&pb, 0, sizeof pb);
    Str255 name;
    pb.ioRefNum = rn;
    pb.ioFCBIndx = 0;
    pb.ioNamePtr = name;
    if(PBGetFCBInfo(&pb, false) != noErr)
        return false;
    return FSMakeFSSpec(pb.ioFCBVRefNum, pb.ioFCBParID, name, spec) == noErr;
}

/* the 'moov' atom in a data fork, past mdat and the rest */
OSErr read_data_fork_movie(const FSSpec &spec, int64_t start, Bytes *out)
{
    DataFile f;
    f.spec = spec;
    int64_t size = f.size();
    Buffer buf;
    int64_t at = start;
    while(at + 8 <= size)
    {
        Ptr p = buf.get(16);
        if(!p || f.read(at, 8, p) != noErr)
            return noMovieFound;
        uint32_t n = be32((uint8_t *)p);
        uint32_t type = be32((uint8_t *)p + 4);
        int64_t len = n;
        if(n == 1)
        {
            if(f.read(at + 8, 8, p + 8) != noErr)
                return noMovieFound;
            len = (int64_t)be32((uint8_t *)p + 8) << 32 | be32((uint8_t *)p + 12);
        }
        else if(n == 0)
            len = size - at;
        if(len < 8)
            return noMovieFound;
        if(type == "moov"_4)
        {
            if(len > 64 * 1024 * 1024)
                return noMovieFound;
            Ptr q = buf.get(len);
            if(!q)
                return memFullErr;
            if(f.read(at, len, q) != noErr)
                return noMovieFound;
            out->assign((uint8_t *)q, (uint8_t *)q + len);
            return noErr;
        }
        at += len;
    }
    return noMovieFound;
}

std::shared_ptr<DataFile> file_for(const FSSpec &spec)
{
    auto f = std::make_shared<DataFile>();
    f->spec = spec;
    return f;
}
}

OSErr Executor::C_OpenMovieFile(const FSSpec *filespecp, GUEST<INTEGER> *refnump, uint8_t perm)
{
    if(!filespecp || !refnump)
        return set_error(paramErr);
    INTEGER rn = FSpOpenResFile(mut(filespecp), perm);
    if(rn != -1)
    {
        *refnump = rn;
        return set_error(noErr);
    }
    OSErr err = ResError();
    /* no resource fork: the data fork, for a movie flattened into it */
    GUEST<INTEGER> d;
    OSErr derr = FSpOpenDF(mut(filespecp), fsRdPerm, &d);
    if(derr != noErr)
        return set_error(err != noErr ? err : derr);
    data_fork_refs[d] = *filespecp;
    *refnump = d;
    return set_error(noErr);
}

OSErr Executor::C_CloseMovieFile(INTEGER refnum)
{
    auto it = data_fork_refs.find(refnum);
    if(it != data_fork_refs.end())
    {
        data_fork_refs.erase(it);
        return set_error(FSClose(refnum));
    }
    CloseResFile(refnum);
    return set_error(ResError());
}

OSErr Executor::C_NewMovieFromFile(GUEST<Movie> *moviep, INTEGER refnum, GUEST<INTEGER> *residp,
                                   StringPtr resnamep, INTEGER flags, Boolean *datarefwaschangedp)
{
    if(!moviep)
        return set_error(paramErr);
    *moviep = nullptr;
    if(datarefwaschangedp)
        *datarefwaschangedp = false;
    FSSpec spec;
    if(!spec_of_refnum(refnum, &spec))
        return set_error(rfNumErr);
    auto self = file_for(spec);
    INTEGER want = residp ? (INTEGER)*residp : 0;
    Bytes bytes;
    INTEGER used = movieInDataForkResID;
    bool from_resource = false;
    if(!data_fork_refs.count(refnum) && want != movieInDataForkResID)
    {
        INTEGER save = CurResFile();
        UseResFile(refnum);
        Handle h = want ? Get1Resource("moov"_4, want) : Get1IndResource("moov"_4, 1);
        if(h)
        {
            GUEST<INTEGER> id;
            GUEST<ResType> type;
            Str255 name;
            GetResInfo(h, &id, &type, name);
            used = id;
            if(resnamep)
                memcpy(resnamep, name, name[0] + 1);
            LoadResource(h);
            bytes.assign((uint8_t *)*h, (uint8_t *)*h + GetHandleSize(h));
            ReleaseResource(h);
            from_resource = true;
        }
        UseResFile(save);
    }
    if(!from_resource)
    {
        OSErr err = read_data_fork_movie(spec, 0, &bytes);
        if(err != noErr)
            return set_error(err);
        if(resnamep)
            resnamep[0] = 0;
    }
    MovieS *m;
    OSErr err = movie_from_bytes(&m, bytes.data(), bytes.size(), self, flags);
    if(err != noErr)
        return set_error(err);
    if(residp)
        *residp = used;
    *moviep = (Movie)m->guest;
    return set_error(noErr);
}

OSErr Executor::C_NewMovieFromHandle(GUEST<Movie> *theMovie, Handle h, INTEGER newMovieFlags,
                                     Boolean *dataRefWasChanged)
{
    if(!theMovie || !h)
        return set_error(paramErr);
    *theMovie = nullptr;
    if(dataRefWasChanged)
        *dataRefWasChanged = false;
    Bytes bytes((uint8_t *)*h, (uint8_t *)*h + GetHandleSize(h));
    MovieS *m;
    OSErr err = movie_from_bytes(&m, bytes.data(), bytes.size(), nullptr, newMovieFlags);
    if(err != noErr)
        return set_error(err);
    *theMovie = (Movie)m->guest;
    return set_error(noErr);
}

OSErr Executor::C_NewMovieFromDataFork(GUEST<Movie> *theMovie, INTEGER fRefNum, LONGINT fileOffset,
                                       INTEGER newMovieFlags, Boolean *dataRefWasChanged)
{
    if(!theMovie)
        return set_error(paramErr);
    *theMovie = nullptr;
    if(dataRefWasChanged)
        *dataRefWasChanged = false;
    FSSpec spec;
    if(!spec_of_refnum(fRefNum, &spec))
        return set_error(rfNumErr);
    Bytes bytes;
    OSErr err = read_data_fork_movie(spec, fileOffset, &bytes);
    if(err != noErr)
        return set_error(err);
    MovieS *m;
    err = movie_from_bytes(&m, bytes.data(), bytes.size(), file_for(spec), newMovieFlags);
    if(err != noErr)
        return set_error(err);
    *theMovie = (Movie)m->guest;
    return set_error(noErr);
}

Movie Executor::C_NewMovie(LONGINT flags)
{
    MovieS *m = new_movie();
    if(!m)
    {
        set_error(memFullErr);
        return nullptr;
    }
    m->active = flags & newMovieToolbox_Active;
    m->set_bounds();
    set_error(noErr);
    return (Movie)m->guest;
}

OSErr Executor::C_CreateMovieFile(const FSSpec *fileSpec, OSType creator, INTEGER scriptTag,
                                  LONGINT createMovieFileFlags, GUEST<INTEGER> *resRefNum, GUEST<Movie> *newmovie)
{
    if(!fileSpec)
        return set_error(paramErr);
    uint32_t flags = createMovieFileFlags;
    if(resRefNum)
        *resRefNum = 0;
    if(newmovie)
        *newmovie = nullptr;
    if(flags & createMovieFileDeleteCurFile)
    {
        OSErr err = FSpDelete(mut(fileSpec));
        if(err != noErr && err != fnfErr)
            return set_error(err);
    }
    OSErr err = FSpCreate(mut(fileSpec), creator, "MooV"_4, scriptTag);
    if(err != noErr)
        return set_error(err);
    if(!(flags & createMovieFileDontCreateResFile))
    {
        FSpCreateResFile(mut(fileSpec), creator, "MooV"_4, scriptTag);
        if(ResError() != noErr)
            return set_error(ResError());
    }
    if(!(flags & createMovieFileDontOpenFile) && resRefNum && !(flags & createMovieFileDontCreateResFile))
    {
        INTEGER rn = FSpOpenResFile(mut(fileSpec), fsRdWrPerm);
        if(rn == -1)
            return set_error(ResError());
        *resRefNum = rn;
    }
    if(!(flags & createMovieFileDontCreateMovie) && newmovie)
    {
        MovieS *m = new_movie();
        if(!m)
            return set_error(memFullErr);
        m->self = file_for(*fileSpec);
        m->active = true;
        m->set_bounds();
        *newmovie = (Movie)m->guest;
    }
    return set_error(noErr);
}

OSErr Executor::C_DeleteMovieFile(const FSSpec *fileSpec)
{
    if(!fileSpec)
        return set_error(paramErr);
    return set_error(FSpDelete(mut(fileSpec)));
}

namespace
{
/* the movie as a 'moov' resource in file rn */
OSErr put_movie_resource(MovieS &m, INTEGER rn, INTEGER *id, ConstStringPtr name, bool replace)
{
    Bytes b = write_moov(m);
    Handle h = NewHandle(b.size());
    if(!h)
        return memFullErr;
    memcpy(*h, b.data(), b.size());
    INTEGER save = CurResFile();
    UseResFile(rn);
    if(replace)
        if(Handle old = Get1Resource("moov"_4, *id))
        {
            RemoveResource(old);
            DisposeHandle(old);
        }
    if(!replace && *id == 0)
        *id = UniqueID("moov"_4);
    if(*id == 0 || *id == movieInDataForkResID)
        *id = 128;
    Str255 nothing = { 0 };
    AddResource(h, "moov"_4, *id, name ? name : nothing);
    OSErr err = ResError();
    if(err == noErr)
        UpdateResFile(rn);
    UseResFile(save);
    return err;
}

/* the movie and its media's data into one file: 'moov' first in the data
   fork (to_data_fork), or as a resource; its chunks in one 'mdat' */
OSErr flatten(MovieS &m, const FSSpec &spec, OSType creator, INTEGER script, uint32_t create_flags,
              bool to_data_fork, INTEGER *res_id, ConstStringPtr res_name)
{
    if(create_flags & createMovieFileDeleteCurFile)
    {
        OSErr err = FSpDelete(mut(&spec));
        if(err != noErr && err != fnfErr)
            return err;
    }
    OSErr err = FSpCreate(mut(&spec), creator, "MooV"_4, script);
    if(err != noErr && err != dupFNErr)
        return err;
    /* the new offsets: the chunks one after another in mdat */
    std::vector<std::vector<int64_t>> offsets(m.tracks.size());
    int64_t data_bytes = 0;
    for(size_t ti = 0; ti < m.tracks.size(); ti++)
        if(MediaS *md = m.tracks[ti]->media.get())
            for(const Chunk &c : md->chunks)
            {
                offsets[ti].push_back(data_bytes);
                data_bytes += md->chunk_bytes(c);
            }
    int64_t mdat_at = 0;
    if(to_data_fork)
        mdat_at = write_moov(m, offsets).size();
    for(auto &o : offsets)
        for(int64_t &x : o)
            x += mdat_at + 8;
    Bytes moov = write_moov(m, offsets);

    DataFile out;
    out.spec = spec;
    err = out.open(true);
    if(err != noErr)
        return err;
    SetEOF(out.rn, 0);
    Buffer buf;
    auto put = [&](const uint8_t *p, uint32_t n) -> OSErr {
        Ptr g = buf.get(n);
        if(!g)
            return memFullErr;
        memcpy(g, p, n);
        int64_t where;
        return out.append(g, n, &where);
    };
    if(to_data_fork && (err = put(moov.data(), moov.size())) != noErr)
        return err;
    Bytes head;
    put32(head, data_bytes + 8);
    put32(head, "mdat"_4);
    if((err = put(head.data(), head.size())) != noErr)
        return err;
    for(auto &t : m.tracks)
        if(MediaS *md = t->media.get())
            for(const Chunk &c : md->chunks)
            {
                uint32_t n = md->chunk_bytes(c);
                Ptr g = buf.get(n);
                if(!g)
                    return memFullErr;
                if(c.dref < 1 || c.dref > md->drefs.size() || !md->drefs[c.dref - 1])
                    return couldNotResolveDataRef;
                if((err = md->drefs[c.dref - 1]->read(c.offset, n, g)) != noErr)
                    return err;
                int64_t where;
                if((err = out.append(g, n, &where)) != noErr)
                    return err;
            }
    out.close();
    if(!to_data_fork)
    {
        FSpCreateResFile(mut(&spec), creator, "MooV"_4, script);
        INTEGER rn = FSpOpenResFile(mut(&spec), fsRdWrPerm);
        if(rn == -1)
            return ResError();
        Handle h = NewHandle(moov.size());
        if(!h)
        {
            CloseResFile(rn);
            return memFullErr;
        }
        memcpy(*h, moov.data(), moov.size());
        INTEGER save = CurResFile();
        UseResFile(rn);
        INTEGER id = res_id && *res_id ? *res_id : 128;
        Str255 nothing = { 0 };
        AddResource(h, "moov"_4, id, res_name ? res_name : nothing);
        err = ResError();
        UseResFile(save);
        CloseResFile(rn);
        if(res_id)
            *res_id = id;
    }
    else if(res_id)
        *res_id = movieInDataForkResID;
    return err;
}
}

OSErr Executor::C_AddMovieResource(Movie theMovie, INTEGER resRefNum, GUEST<INTEGER> *resId,
                                   ConstStr255Param resName)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return invalidMovie;
    for(auto &t : m->tracks)
        if(t->media)
            for(auto &d : t->media->drefs)
                if(d)
                    d->close();
    INTEGER id = resId ? (INTEGER)*resId : 0;
    OSErr err = put_movie_resource(*m, resRefNum, &id, resName, false);
    if(err == noErr && resId)
        *resId = id;
    return set_error(err);
}

OSErr Executor::C_UpdateMovieResource(Movie theMovie, INTEGER resRefNum, INTEGER resId, ConstStr255Param resName)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return invalidMovie;
    INTEGER id = resId;
    return set_error(put_movie_resource(*m, resRefNum, &id, resName, true));
}

void Executor::C_FlattenMovie(Movie theMovie, LONGINT movieFlattenFlags, const FSSpec *theFile, OSType creator,
                              INTEGER scriptTag, LONGINT createMovieFileFlags, GUEST<INTEGER> *resId,
                              ConstStr255Param resName)
{
    MovieS *m = movie_of(theMovie);
    if(!m || !theFile)
        return;
    INTEGER id = resId ? (INTEGER)*resId : 0;
    OSErr err = flatten(*m, *theFile, creator, scriptTag, createMovieFileFlags,
                        movieFlattenFlags & flattenAddMovieToDataFork, &id, resName);
    if(err == noErr && resId)
        *resId = id;
    set_error(err);
}

Movie Executor::C_FlattenMovieData(Movie theMovie, LONGINT movieFlattenFlags, const FSSpec *theFile,
                                   OSType creator, INTEGER scriptTag, LONGINT createMovieFileFlags)
{
    MovieS *m = movie_of(theMovie);
    if(!m || !theFile)
        return nullptr;
    bool to_data_fork = movieFlattenFlags & flattenAddMovieToDataFork;
    INTEGER id = 0;
    OSErr err = flatten(*m, *theFile, creator, scriptTag, createMovieFileFlags, to_data_fork, &id, nullptr);
    if(err != noErr)
    {
        set_error(err);
        return nullptr;
    }
    /* the movie as the new file has it */
    Bytes bytes;
    if(to_data_fork)
        err = read_data_fork_movie(*theFile, 0, &bytes);
    else
        bytes = write_moov(*m);
    MovieS *nm = nullptr;
    if(err == noErr)
        err = movie_from_bytes(&nm, bytes.data(), bytes.size(), file_for(*theFile), newMovieToolbox_Active);
    set_error(err);
    return nm ? (Movie)nm->guest : nullptr;
}

OSErr Executor::C_PutMovieIntoHandle(Movie theMovie, Handle publicMovie)
{
    MovieS *m = movie_of(theMovie);
    if(!m)
        return invalidMovie;
    if(!publicMovie)
        return set_error(paramErr);
    Bytes b = write_moov(*m);
    SetHandleSize(publicMovie, b.size());
    if(MemError() != noErr)
        return set_error(MemError());
    memcpy(*publicMovie, b.data(), b.size());
    return set_error(noErr);
}

/* --- the rest of Executor ------------------------------------------------- */

/* Every $AAAA selector Universal Interfaces 3.4's Movies.h names, its
   arguments' and result's bytes (Pascal: a word or less takes 2, the rest
   4): a selector QuickTime.yaml doesn't declare pops its arguments and
   answers paramErr or nil (traps.cpp), as the ICM's do. Generated from the
   header's declarations; $273 and $274, which it doesn't name, from their
   call sites in QuickTime 2.5's own components (the tune player's, which
   Cythera opens): $273 takes a pointer it fills, $274 that value and a
   Boolean, both answer an OSErr, and the caller gives up when $273 fails.
   The high bit of a selector ($8273) is masked off. */
namespace
{
struct MovieSelector
{
    uint16_t sel;
    uint8_t arg_bytes, result_bytes;
    const char *name;
};
const MovieSelector movie_selectors[] = {
    { 0x0001, 0, 2, "EnterMovies" },
    { 0x0002, 0, 0, "ExitMovies" },
    { 0x0003, 0, 2, "GetMoviesError" },
    { 0x0004, 0, 2, "GetMoviesStickyError" },
    { 0x0005, 8, 0, "MoviesTask" },
    { 0x0006, 12, 2, "PrerollMovie" },
    { 0x0007, 16, 2, "LoadMovieIntoRam" },
    { 0x0008, 16, 2, "LoadMediaIntoRam" },
    { 0x0009, 6, 0, "SetMovieActive" },
    { 0x000A, 4, 2, "GetMovieActive" },
    { 0x000B, 4, 0, "StartMovie" },
    { 0x000C, 4, 0, "StopMovie" },
    { 0x000D, 4, 0, "GoToBeginningOfMovie" },
    { 0x000E, 4, 0, "GoToEndOfMovie" },
    { 0x000F, 4, 2, "GetMoviePreviewMode" },
    { 0x0010, 6, 0, "SetMoviePreviewMode" },
    { 0x0011, 4, 0, "ShowMoviePoster" },
    { 0x0012, 4, 4, "GetMovieTimeBase" },
    { 0x0015, 12, 0, "GetMovieGWorld" },
    { 0x0016, 12, 0, "SetMovieGWorld" },
    { 0x001D, 8, 4, "GetMoviePict" },
    { 0x001E, 8, 4, "GetTrackPict" },
    { 0x001F, 4, 2, "UpdateMovie" },
    { 0x0022, 8, 2, "PutMovieIntoHandle" },
    { 0x0023, 4, 0, "DisposeMovie" },
    { 0x0026, 4, 4, "GetMovieCreationTime" },
    { 0x0027, 4, 4, "GetMovieModificationTime" },
    { 0x0029, 4, 4, "GetMovieTimeScale" },
    { 0x002A, 8, 0, "SetMovieTimeScale" },
    { 0x002B, 4, 4, "GetMovieDuration" },
    { 0x002C, 4, 4, "GetMovieRate" },
    { 0x002D, 8, 0, "SetMovieRate" },
    { 0x002E, 4, 2, "GetMovieVolume" },
    { 0x002F, 6, 0, "SetMovieVolume" },
    { 0x0031, 8, 0, "GetMovieMatrix" },
    { 0x0032, 8, 0, "SetMovieMatrix" },
    { 0x0033, 12, 0, "GetMoviePreviewTime" },
    { 0x0034, 12, 0, "SetMoviePreviewTime" },
    { 0x0035, 4, 4, "GetMoviePosterTime" },
    { 0x0036, 8, 0, "SetMoviePosterTime" },
    { 0x0037, 12, 0, "GetMovieSelection" },
    { 0x0038, 12, 0, "SetMovieSelection" },
    { 0x0039, 8, 4, "GetMovieTime" },
    { 0x003C, 8, 0, "SetMovieTime" },
    { 0x003D, 8, 0, "SetMovieTimeValue" },
    { 0x003E, 4, 4, "GetMovieUserData" },
    { 0x003F, 4, 4, "GetMovieTrackCount" },
    { 0x0040, 8, 4, "GetMovieTrack" },
    { 0x0042, 4, 0, "DisposeMovieTrack" },
    { 0x0043, 4, 4, "GetTrackCreationTime" },
    { 0x0044, 4, 4, "GetTrackModificationTime" },
    { 0x0045, 4, 2, "GetTrackEnabled" },
    { 0x0046, 6, 0, "SetTrackEnabled" },
    { 0x0047, 4, 4, "GetTrackUsage" },
    { 0x0048, 8, 0, "SetTrackUsage" },
    { 0x004B, 4, 4, "GetTrackDuration" },
    { 0x004C, 4, 4, "GetTrackOffset" },
    { 0x004D, 8, 0, "SetTrackOffset" },
    { 0x0050, 4, 2, "GetTrackLayer" },
    { 0x0051, 6, 0, "SetTrackLayer" },
    { 0x0052, 4, 4, "GetTrackAlternate" },
    { 0x0053, 8, 0, "SetTrackAlternate" },
    { 0x0054, 4, 2, "GetTrackVolume" },
    { 0x0055, 6, 0, "SetTrackVolume" },
    { 0x0056, 8, 0, "GetTrackMatrix" },
    { 0x0057, 8, 0, "SetTrackMatrix" },
    { 0x005D, 12, 0, "GetTrackDimensions" },
    { 0x005E, 12, 0, "SetTrackDimensions" },
    { 0x005F, 4, 4, "GetTrackUserData" },
    { 0x0061, 4, 0, "DisposeTrackMedia" },
    { 0x0062, 4, 4, "GetTrackMedia" },
    { 0x0066, 4, 4, "GetMediaCreationTime" },
    { 0x0067, 4, 4, "GetMediaModificationTime" },
    { 0x0068, 4, 4, "GetMediaTimeScale" },
    { 0x0069, 8, 0, "SetMediaTimeScale" },
    { 0x006A, 4, 4, "GetMediaDuration" },
    { 0x006B, 4, 2, "GetMediaLanguage" },
    { 0x006C, 6, 0, "SetMediaLanguage" },
    { 0x006D, 4, 2, "GetMediaQuality" },
    { 0x006E, 6, 0, "SetMediaQuality" },
    { 0x006F, 16, 0, "GetMediaHandlerDescription" },
    { 0x0070, 4, 4, "GetMediaUserData" },
    { 0x0071, 4, 4, "GetMediaHandler" },
    { 0x0072, 4, 2, "BeginMediaEdits" },
    { 0x0073, 4, 2, "EndMediaEdits" },
    { 0x0074, 20, 2, "AddEmptyTrackToMovie" },
    { 0x0077, 4, 4, "GetMediaSampleDescriptionCount" },
    { 0x0078, 12, 0, "GetMediaSampleDescription" },
    { 0x0079, 4, 4, "GetMediaSampleCount" },
    { 0x007A, 16, 0, "SampleNumToMediaTime" },
    { 0x007B, 20, 0, "MediaTimeToSampleNum" },
    { 0x007C, 34, 2, "AddMediaSample" },
    { 0x007D, 30, 2, "AddMediaSampleReference" },
    { 0x007E, 48, 2, "GetMediaSample" },
    { 0x007F, 44, 2, "GetMediaSampleReference" },
    { 0x0085, 20, 2, "InsertTrackSegment" },
    { 0x0086, 20, 2, "InsertMovieSegment" },
    { 0x0087, 12, 2, "InsertEmptyTrackSegment" },
    { 0x0088, 12, 2, "InsertEmptyMovieSegment" },
    { 0x0089, 12, 2, "DeleteTrackSegment" },
    { 0x008A, 12, 2, "DeleteMovieSegment" },
    { 0x008B, 16, 2, "ScaleTrackSegment" },
    { 0x008C, 16, 2, "ScaleMovieSegment" },
    { 0x008D, 4, 4, "CutMovieSelection" },
    { 0x008E, 4, 4, "CopyMovieSelection" },
    { 0x008F, 8, 0, "PasteMovieSelection" },
    { 0x0096, 8, 4, "TrackTimeToMediaTime" },
    { 0x0098, 12, 4, "GetMovieDataSize" },
    { 0x0099, 12, 4, "GetMediaDataSize" },
    { 0x009A, 8, 2, "PtInMovie" },
    { 0x009B, 8, 2, "PtInTrack" },
    { 0x009C, 8, 0, "SetMovieLanguage" },
    { 0x009D, 20, 0, "SetTrackGWorld" },
    { 0x009E, 16, 2, "GetUserData" },
    { 0x009F, 12, 2, "AddUserData" },
    { 0x00A0, 12, 2, "RemoveUserData" },
    { 0x00A5, 0, 4, "NewTimeBase" },
    { 0x00A6, 12, 4, "GetTimeBaseTime" },
    { 0x00A7, 8, 0, "SetTimeBaseTime" },
    { 0x00A8, 12, 0, "SetTimeBaseValue" },
    { 0x00A9, 4, 4, "GetTimeBaseRate" },
    { 0x00AA, 8, 0, "SetTimeBaseRate" },
    { 0x00AB, 12, 4, "GetTimeBaseStartTime" },
    { 0x00AC, 8, 0, "SetTimeBaseStartTime" },
    { 0x00AD, 12, 4, "GetTimeBaseStopTime" },
    { 0x00AE, 8, 0, "SetTimeBaseStopTime" },
    { 0x00AF, 4, 4, "GetTimeBaseMasterTimeBase" },
    { 0x00B0, 4, 4, "GetTimeBaseMasterClock" },
    { 0x00B1, 4, 4, "GetTimeBaseFlags" },
    { 0x00B2, 8, 0, "SetTimeBaseFlags" },
    { 0x00B3, 12, 0, "SetTimeBaseMasterClock" },
    { 0x00B4, 12, 0, "SetTimeBaseMasterTimeBase" },
    { 0x00B5, 8, 0, "ConvertTime" },
    { 0x00B6, 4, 0, "DisposeTimeBase" },
    { 0x00B7, 8, 0, "ConvertTimeScale" },
    { 0x00B8, 24, 2, "CallMeWhen" },
    { 0x00B9, 4, 0, "CancelCallBack" },
    { 0x00C5, 4, 4, "GetMediaTrack" },
    { 0x00CB, 20, 2, "PasteHandleIntoMovie" },
    { 0x00CC, 4, 4, "IsScrapMovie" },
    { 0x00D0, 4, 4, "GetTrackMovie" },
    { 0x00D5, 2, 2, "CloseMovieFile" },
    { 0x00D7, 14, 2, "AddMovieResource" },
    { 0x00D8, 12, 2, "UpdateMovieResource" },
    { 0x00D9, 4, 2, "HasMovieChanged" },
    { 0x00DD, 4, 2, "IsMovieDone" },
    { 0x00DE, 0, 0, "ClearMoviesStickyError" },
    { 0x00E1, 4, 0, "ClearMovieSelection" },
    { 0x00E2, 22, 0, "GetTrackNextInterestingTime" },
    { 0x00EB, 6, 4, "NewCallBack" },
    { 0x00EC, 4, 0, "DisposeCallBack" },
    { 0x00ED, 4, 2, "GetCallBackType" },
    { 0x00EE, 4, 4, "GetCallBackTimeBase" },
    { 0x00EF, 8, 0, "SetMoviesErrorProc" },
    { 0x00F0, 20, 2, "NewMovieFromFile" },
    { 0x00F1, 14, 2, "NewMovieFromHandle" },
    { 0x00F2, 12, 0, "PlayMoviePreview" },
    { 0x00F3, 4, 4, "GetMoviePreferredRate" },
    { 0x00F4, 8, 0, "SetMoviePreferredRate" },
    { 0x00F5, 4, 2, "GetMoviePreferredVolume" },
    { 0x00F6, 6, 0, "SetMoviePreferredVolume" },
    { 0x00F7, 4, 4, "GetMoviePosterPict" },
    { 0x00F9, 8, 0, "GetMovieBox" },
    { 0x00FA, 8, 0, "SetMovieBox" },
    { 0x00FB, 4, 4, "GetMovieDisplayBoundsRgn" },
    { 0x00FC, 4, 4, "GetMovieDisplayClipRgn" },
    { 0x00FD, 8, 0, "SetMovieDisplayClipRgn" },
    { 0x00FE, 4, 4, "GetMovieBoundsRgn" },
    { 0x00FF, 4, 4, "GetTrackMovieBoundsRgn" },
    { 0x0100, 4, 4, "GetMovieClipRgn" },
    { 0x0101, 8, 0, "SetMovieClipRgn" },
    { 0x0102, 4, 4, "GetTrackClipRgn" },
    { 0x0103, 8, 0, "SetTrackClipRgn" },
    { 0x0104, 4, 4, "NewMovieEditState" },
    { 0x0105, 8, 2, "UseMovieEditState" },
    { 0x0106, 4, 2, "DisposeMovieEditState" },
    { 0x0107, 4, 4, "NewTrackEditState" },
    { 0x0108, 8, 2, "UseTrackEditState" },
    { 0x0109, 4, 2, "DisposeTrackEditState" },
    { 0x010B, 8, 4, "GetTimeBaseStatus" },
    { 0x010C, 8, 0, "AddTime" },
    { 0x010D, 8, 0, "SubtractTime" },
    { 0x010E, 28, 0, "GetMovieNextInterestingTime" },
    { 0x0111, 4, 4, "GetTrackBoundsRgn" },
    { 0x0112, 4, 4, "GetTrackDisplayBoundsRgn" },
    { 0x0113, 4, 0, "ClearMovieChanged" },
    { 0x0115, 4, 4, "GetTrackMatte" },
    { 0x0116, 8, 0, "SetTrackMatte" },
    { 0x0117, 8, 4, "GetMovieIndTrack" },
    { 0x0121, 12, 2, "SetMediaShadowSync" },
    { 0x0122, 12, 2, "GetMediaShadowSync" },
    { 0x0123, 8, 4, "GetTrackEditRate" },
    { 0x0124, 4, 4, "GetTimeBaseEffectiveRate" },
    { 0x0126, 20, 2, "GetUserDataItem" },
    { 0x0127, 4, 4, "GetTrackID" },
    { 0x0128, 8, 0, "SetTimeBaseZero" },
    { 0x0129, 4, 2, "AddCallBackToTimeBase" },
    { 0x012A, 4, 2, "RemoveCallBackFromTimeBase" },
    { 0x012B, 4, 4, "GetFirstCallBack" },
    { 0x012C, 4, 4, "GetNextCallBack" },
    { 0x012D, 4, 0, "ExecuteCallBack" },
    { 0x012E, 20, 2, "SetUserDataItem" },
    { 0x012F, 4, 2, "NewUserData" },
    { 0x0130, 4, 2, "DisposeUserData" },
    { 0x0131, 8, 2, "NewUserDataFromHandle" },
    { 0x0132, 8, 2, "PutUserDataIntoHandle" },
    { 0x0149, 12, 4, "GetTrackDataSize" },
    { 0x014A, 4, 0, "DisposeMatte" },
    { 0x014B, 8, 2, "CountUserDataType" },
    { 0x014C, 18, 2, "AddUserDataText" },
    { 0x014D, 18, 2, "GetUserDataText" },
    { 0x014E, 14, 2, "RemoveUserDataText" },
    { 0x0152, 8, 0, "AddMovieSelection" },
    { 0x0153, 8, 2, "CopyTrackSettings" },
    { 0x0154, 8, 2, "CopyMovieSettings" },
    { 0x015C, 12, 0, "SetMovieActiveSegment" },
    { 0x015D, 12, 0, "GetMovieActiveSegment" },
    { 0x015E, 6, 0, "SetAutoTrackAlternatesEnabled" },
    { 0x015F, 4, 0, "SelectMovieAlternates" },
    { 0x0167, 12, 0, "SetMovieMasterTimeBase" },
    { 0x0168, 12, 0, "SetMovieMasterClock" },
    { 0x016B, 12, 4, "GetTrackSegmentDisplayBoundsRgn" },
    { 0x016C, 12, 4, "GetMovieSegmentDisplayBoundsRgn" },
    { 0x016D, 22, 0, "GetMediaNextInterestingTime" },
    { 0x016E, 16, 2, "LoadTrackIntoRam" },
    { 0x016F, 8, 0, "GetPosterBox" },
    { 0x0170, 8, 0, "SetPosterBox" },
    { 0x0172, 4, 4, "GetTrackStatus" },
    { 0x0173, 8, 4, "GetMovieStatus" },
    { 0x0175, 4, 2, "DeleteMovieFile" },
    { 0x0176, 4, 2, "RemoveMovieResource" },
    { 0x0179, 16, 0, "SetMovieCoverProcs" },
    { 0x0183, 20, 2, "InsertMediaIntoTrack" },
    { 0x0187, 4, 4, "NewMovie" },
    { 0x0188, 14, 4, "NewMovieTrack" },
    { 0x018A, 12, 4, "NewMovieController" },
    { 0x018B, 4, 0, "DisposeMovieController" },
    { 0x018C, 8, 2, "PutMovieOnScrap" },
    { 0x018D, 4, 4, "NewMovieFromScrap" },
    { 0x018E, 20, 4, "NewTrackMedia" },
    { 0x0190, 8, 2, "SetMediaHandler" },
    { 0x0191, 22, 2, "CreateMovieFile" },
    { 0x0192, 10, 2, "OpenMovieFile" },
    { 0x0197, 18, 2, "GetMediaDataRef" },
    { 0x0198, 16, 2, "AddMediaDataRef" },
    { 0x0199, 8, 2, "GetMediaDataRefCount" },
    { 0x019A, 12, 0, "SetMovieProgressProc" },
    { 0x019B, 30, 0, "FlattenMovie" },
    { 0x019C, 22, 4, "FlattenMovieData" },
    { 0x019E, 18, 0, "GetMediaDataHandlerDescription" },
    { 0x019F, 6, 4, "GetMediaDataHandler" },
    { 0x01A0, 10, 2, "SetMediaDataHandler" },
    { 0x01A1, 12, 0, "SetMoviePlayHints" },
    { 0x01A2, 12, 0, "SetMediaPlayHints" },
    { 0x01A5, 8, 4, "GetNextUserDataType" },
    { 0x01B3, 16, 2, "NewMovieFromDataFork" },
    { 0x01B4, 14, 2, "PutMovieIntoDataFork" },
    { 0x01C1, 12, 2, "SetMovieDefaultDataRef" },
    { 0x01C9, 14, 2, "SetMediaDataRef" },
    { 0x01CA, 10, 2, "SetMediaDataRefAttributes" },
    { 0x01CB, 34, 2, "ConvertFileToMovieFile" },
    { 0x01CC, 34, 2, "ConvertMovieToFile" },
    { 0x01CD, 32, 2, "PutMovieIntoTypedHandle" },
    { 0x01D0, 12, 2, "SetMediaSampleDescription" },
    { 0x01D2, 12, 2, "GetMovieDefaultDataRef" },
    { 0x01DD, 16, 2, "GetMovieCoverProcs" },
    { 0x01DE, 16, 0, "SetMovieDrawingCompleteProc" },
    { 0x01E0, 6, 2, "SetMediaDefaultDataRefIndex" },
    { 0x01E3, 20, 0, "SetTrackLoadSettings" },
    { 0x01E4, 20, 0, "GetTrackLoadSettings" },
    { 0x01EC, 26, 2, "NewMovieFromUserProc" },
    { 0x01ED, 12, 4, "GetDataHandler" },
    { 0x01F0, 16, 2, "AddTrackReference" },
    { 0x01F1, 12, 2, "DeleteTrackReference" },
    { 0x01F2, 16, 2, "SetTrackReference" },
    { 0x01F3, 12, 4, "GetTrackReference" },
    { 0x01F4, 8, 4, "GetNextTrackReferenceType" },
    { 0x01F5, 8, 4, "GetTrackReferenceCount" },
    { 0x01F7, 20, 2, "AddMediaSampleReferences" },
    { 0x01F8, 8, 2, "SetMediaPreferredChunkSize" },
    { 0x01F9, 8, 2, "GetMediaPreferredChunkSize" },
    { 0x01FA, 8, 4, "GetNextTrackForCompositing" },
    { 0x01FB, 8, 4, "GetPrevTrackForCompositing" },
    { 0x01FF, 12, 2, "QTSwapAtoms" },
    { 0x0200, 16, 2, "QTNextChildAnyType" },
    { 0x0201, 10, 2, "QTNewAlias" },
    /* QuickTime 3.0.2's 'base' image codec, before a band: a {long,
       routine} record of its own; it drops the result */
    { 0x0203, 4, 2, "(private $203: QuickTime 3's base codec, a record and routine)" },
    { 0x0205, 8, 2, "SetMovieColorTable" },
    { 0x0206, 8, 2, "GetMovieColorTable" },
    { 0x0207, 28, 2, "MovieSearchText" },
    { 0x0208, 16, 4, "GetMovieIndTrackType" },
    { 0x0209, 12, 0, "ShowMovieInformation" },
    { 0x020C, 4, 2, "QTNewAtomContainer" },
    { 0x020D, 4, 2, "QTDisposeAtomContainer" },
    { 0x020E, 12, 4, "QTGetNextChildType" },
    { 0x020F, 12, 2, "QTCountChildrenOfType" },
    { 0x0210, 18, 4, "QTFindChildByIndex" },
    { 0x0211, 16, 2, "QTSetAtomData" },
    { 0x0212, 12, 2, "QTCopyAtomDataToHandle" },
    { 0x0213, 22, 2, "QTCopyAtomDataToPtr" },
    { 0x0214, 12, 2, "QTCopyAtom" },
    { 0x0215, 4, 2, "QTLockContainer" },
    { 0x0216, 16, 2, "QTGetAtomDataPtr" },
    { 0x0217, 4, 2, "QTUnlockContainer" },
    { 0x0218, 30, 2, "QTInsertChild" },
    { 0x0219, 12, 2, "QTInsertChildren" },
    { 0x021A, 8, 2, "QTRemoveAtom" },
    { 0x021B, 8, 2, "QTRemoveChildren" },
    { 0x021C, 16, 2, "QTReplaceAtom" },
    { 0x021D, 20, 4, "QTFindChildByID" },
    { 0x0220, 18, 2, "NewMovieFromDataRef" },
    { 0x022A, 8, 2, "InvalidateMovieRegion" },
    { 0x022C, 8, 0, "GetMovieNaturalBoundsRect" },
    { 0x022E, 8, 2, "SetMediaPropertyAtom" },
    { 0x022F, 8, 2, "GetMediaPropertyAtom" },
    { 0x0231, 12, 2, "QTSetAtomID" },
    { 0x0232, 16, 2, "QTGetAtomTypeAndID" },
    { 0x0233, 28, 2, "BeginFullScreen" },
    { 0x0234, 8, 2, "EndFullScreen" },
    { 0x0235, 32, 2, "GetMediaSampleReferences" },
    { 0x0239, 20, 2, "NewSpriteWorld" },
    { 0x023A, 4, 0, "DisposeSpriteWorld" },
    { 0x023B, 8, 2, "SetSpriteWorldClip" },
    { 0x023C, 8, 2, "SetSpriteWorldMatrix" },
    { 0x023D, 12, 2, "SpriteWorldIdle" },
    { 0x023E, 8, 2, "InvalidateSpriteWorld" },
    { 0x023F, 4, 0, "DisposeAllSprites" },
    { 0x0240, 24, 2, "NewSprite" },
    { 0x0241, 4, 0, "DisposeSprite" },
    { 0x0242, 4, 0, "InvalidateSprite" },
    { 0x0243, 12, 2, "SetSpriteProperty" },
    { 0x0244, 12, 2, "GetSpriteProperty" },
    { 0x0246, 16, 2, "SpriteWorldHitTest" },
    { 0x0247, 16, 2, "SpriteHitTest" },
    { 0x0249, 8, 2, "GetMediaInputMap" },
    { 0x024A, 8, 2, "SetMediaInputMap" },
    { 0x0263, 8, 2, "GetTrackDisplayMatrix" },
    { 0x0273, 4, 2, "QuickTime private $273" },
    { 0x0274, 6, 2, "QuickTime private $274" },
    { 0x027A, 14, 2, "ITextAddString" },
    { 0x027B, 14, 2, "ITextRemoveString" },
    { 0x027C, 18, 2, "ITextGetString" },
    { 0x0282, 8, 2, "GetTrackSoundLocalizationSettings" },
    { 0x0283, 8, 2, "SetTrackSoundLocalizationSettings" },
    { 0x0284, 8, 2, "SetMoviePropertyAtom" },
    { 0x0285, 8, 2, "GetMoviePropertyAtom" },
    { 0x029D, 16, 2, "QTNewTween" },
    { 0x029E, 24, 2, "QTDoTween" },
    { 0x029F, 4, 2, "QTDisposeTween" },
    { 0x02B2, 4, 4, "GetMediaSyncSampleCount" },
    { 0x02B3, 12, 2, "QTGetAccessKeys" },
    { 0x02B4, 12, 2, "QTRegisterAccessKey" },
    { 0x02B5, 12, 2, "QTUnregisterAccessKey" },
    { 0x02BE, 28, 2, "MakeTrackTimeTable" },
    { 0x02BF, 28, 2, "MakeMediaTimeTable" },
    { 0x02C0, 8, 2, "GetMaxLoadedTimeInMovie" },
    { 0x02C2, 12, 2, "SetSpriteWorldFlags" },
    { 0x02C3, 8, 2, "QTMovieNeedsTimeTable" },
    { 0x02C6, 16, 2, "QTGetDataRefMaxFileOffset" },
    { 0x02C7, 16, 2, "GetMovieImporterForDataRef" },
    { 0x02C9, 16, 2, "QTGetEffectsList" },
    { 0x02CA, 16, 2, "QTCreateStandardParameterDialog" },
    { 0x02CB, 8, 2, "QTIsStandardParameterDialogEvent" },
    { 0x02CC, 4, 2, "QTDismissStandardParameterDialog" },
    { 0x02CD, 12, 2, "QTStandardParameterDialogDoAction" },
    { 0x02CE, 8, 0, "GetMediaPlayHints" },
    { 0x02CF, 12, 2, "AddSoundDescriptionExtension" },
    { 0x02D0, 12, 2, "GetSoundDescriptionExtension" },
    { 0x02D1, 8, 2, "RemoveSoundDescriptionExtension" },
    { 0x02D2, 8, 2, "QTGetEffectSpeed" },
    { 0x02D4, 8, 2, "GetQuickTimePreference" },
    { 0x02D5, 8, 2, "SetQuickTimePreference" },
    { 0x02D9, 12, 2, "SetSpriteWorldGraphicsMode" },
    { 0x02DA, 8, 0, "CheckQuickTimeRegistration" },
    { 0x02DB, 12, 2, "QTTextToNativeText" },
    { 0x02E8, 20, 2, "AddMediaSampleReferences64" },
    { 0x02E9, 32, 2, "GetMediaSampleReferences64" },
    { 0x02EA, 16, 2, "PutMovieIntoDataFork64" },
    { 0x02EB, 16, 2, "GetMovieDataSize64" },
    { 0x02EC, 16, 2, "GetTrackDataSize64" },
    { 0x02ED, 16, 2, "GetMediaDataSize64" },
    { 0x02EE, 18, 2, "NewMovieFromDataFork64" },
    { 0x02EF, 8, 4, "QTGetAtomParent" },
    { 0x02F5, 20, 2, "QTBandwidthRequest" },
    { 0x02F6, 8, 2, "QTBandwidthRelease" },
    { 0x02F7, 20, 2, "PrePrerollMovie" },
    { 0x02F8, 6, 0, "AbortPrePrerollMovie" },
    { 0x02FA, 22, 2, "CreateShortcutMovieFile" },
    { 0x0300, 12, 0, "GetMovieProgressProc" },
    { 0x0302, 12, 2, "AddMovieExecuteWiredActionsProc" },
    { 0x0303, 12, 2, "RemoveMovieExecuteWiredActionsProc" },
    { 0x0304, 12, 2, "MovieExecuteWiredActions" },
    { 0x0310, 20, 2, "QTScheduledBandwidthRequest" },
    { 0x0311, 8, 2, "QTScheduledBandwidthRelease" },
    { 0x0314, 4, 4, "GetMovieLoadState" },
    { 0x0315, 12, 2, "SetMovieAnchorDataRef" },
    { 0x0316, 16, 2, "GetMovieAnchorDataRef" },
    { 0x0318, 24, 2, "QTBandwidthRequestForTimeBase" },
    { 0x0319, 16, 2, "QTParseTextHREF" },
    { 0x031C, 28, 2, "OpenADataHandler" },
    { 0x033E, 28, 2, "CanQuickTimeOpenFile" },
    { 0x033F, 24, 2, "CanQuickTimeOpenDataRef" },
    { 0x0340, 8, 0, "SetMovieVideoOutput" },
    { 0x0344, 16, 2, "AddClonedTrackToMovie" },
    { 0x036A, 18, 2, "QTGetMIMETypeInfo" },
};
}

bool Executor::ROMlib_movies_unknown_selector(uint32_t sel, uint32_t *arg_bytes, uint32_t *result_bytes,
                                              const char **name)
{
    for(const MovieSelector &s : movie_selectors)
        if(s.sel == (sel & 0xFFFF))
        {
            *arg_bytes = s.arg_bytes;
            *result_bytes = s.result_bytes;
            *name = s.name;
            set_error(paramErr);
            return true;
        }
    return false;
}


void Executor::ROMlib_movies_process_exit(uint32_t lo, uint32_t hi)
{
    std::vector<MovieS *> gone;
    for(auto &e : reg.movies)
    {
        uint32_t g = US_TO_SYN68K_CHECK0(e.second->guest);
        if(g >= lo && g < hi)
            gone.push_back(e.second);
    }
    for(MovieS *m : gone)
    {
        /* its blocks went with the heap: forget them without disposing */
        for(auto &t : m->tracks)
        {
            sound_dispose(*t);   /* in the System heap */
            t->seq_desc_handle = nullptr;
            t->buffer = nullptr;
            if(t->seq)
                CDSequenceEnd(t->seq);
            t->seq = 0;
        }
        m->clip = m->display_clip = m->bounds_rgn = nullptr;
        m->ctab = nullptr;
        std::vector<void *> blocks = { m->guest, m->tb.guest, m->udta.guest };
        for(auto &t : m->tracks)
        {
            blocks.push_back(t->guest);
            blocks.push_back(t->udta.guest);
            if(t->media)
                blocks.push_back(t->media->guest);
        }
        uint32_t id = id_of(m->guest);
        for(void *b : blocks)
        {
            uint32_t bid = id_of(b);
            reg.movies.erase(bid);
            reg.tracks.erase(bid);
            reg.medias.erase(bid);
            reg.timebases.erase(bid);
            reg.userdatas.erase(bid);
        }
        reg.owned.erase(id);
    }
    for(auto it = data_fork_refs.begin(); it != data_fork_refs.end();)
        it = data_fork_refs.erase(it);
    if(sample_buffer.h)
    {
        uint32_t g = US_TO_SYN68K_CHECK0(sample_buffer.h);
        if(g >= lo && g < hi)
            sample_buffer.h = nullptr;
    }
}
