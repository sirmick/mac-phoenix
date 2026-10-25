/* fsref.cpp -- Mac OS 9.0's HFS Plus APIs: FSRefs, Unicode names, catalog
 * info, iterators and search, the fork calls with 64-bit positions, volume
 * info (docs/executor/HFSPlus.md). Every call answers as Mac OS 9.0.4 does
 * (tests/guest/hfsplus/FSRefTest.c, oracle-9.0.4/fsreftest.txt).
 *
 * On an HFS+ volume (HFSPlusVolume) items are found by CNID and named in
 * Unicode, forks of any length open; on any other volume (HFS, host
 * folders) the calls go through the classic File Manager: MacRoman names
 * of 31 characters at most, forks to 2 GB. An FSRef is ours: the volume,
 * then the CNID, or (classic) the parent directory, the name and the
 * item's ID, which still finds it after a move or rename as 9.0.4's do. */
#include <base/common.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <file/file.h>
#include <file/hfsplusvolume.h>
#include <file/localvolume/item.h>
#include <hfs/hfs.h>
#include <hfs/plus/unicode.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Executor;
using hfsplus::CatalogEntry;

namespace
{
enum : OSErr
{
    kSameFileErr = -1306,
    kNotAFileErr = -1302,
    kDiffVolErr = -1303,
    kFileBoundsErr = -1309,
    kFSDataTooBigErr = -1310,
    errFSBadFSRef = -1401,
    errFSBadForkName = -1402,
    errFSBadForkRef = -1404,
    errFSBadInfoBitmap = -1405,
    errFSNotAFolder = -1407,
    errFSForkNotFound = -1409,
    errFSNameTooLong = -1410,
    errFSMissingName = -1411,
    errFSBadPosMode = -1412,
    errFSBadAllocFlags = -1413,
    errFSNoMoreItems = -1417,
    errFSBadItemCount = -1418,
    errFSBadSearchParams = -1419,
    errFSRefsDifferent = -1420,
    errFSForkExists = -1421,
    errFSBadIteratorFlags = -1422,
    errFSIteratorNotFound = -1423,
    errFSIteratorNotSupported = -1424,
};

const uint32_t kGettable = 0x0003FFFF, kSettable = 0x00001FE3;
const uint16_t kNodeLocked = 0x01, kNodeDir = 0x10;
const char16_t kResourceForkName[] = u"RESOURCE_FORK";

/* ---- the classic calls, from here ----------------------------------------- */

/* While set, the classic calls take the fork calls' refnums (hfsXbar.cpp
   refuses them to applications, as 9.0.4 does: fsDataTooBigErr). */
int bypassDepth = 0;
std::set<short> forkAPIRefNums;          /* paths FSOpenFork opened, any volume */

struct Bypass
{
    Bypass() { bypassDepth++; }
    ~Bypass() { bypassDepth--; }
};

HFSPlusVolume *hfsPlusVolume(HVCB *vcb)
{
    return vcb ? dynamic_cast<HFSPlusVolume *>(((VCBExtra *)vcb)->volume) : nullptr;
}

HVCB *vcbFor(short vref)
{
    LONGINT dir;
    return vref ? ROMlib_findvcb(vref, nullptr, &dir, false) : nullptr;
}

std::string pstr(ConstStringPtr p)
{
    return p ? std::string((const char *)p + 1, p[0]) : std::string();
}

void putPStr(StringPtr out, const std::string& s, size_t max = 255)
{
    size_t n = std::min(s.size(), max);
    out[0] = (uint8_t)n;
    memcpy(out + 1, s.data(), n);
}

/* ---- names ---------------------------------------------------------------- */

std::u16string fromGuest(const GUEST<UniChar> *p, uint32_t n)
{
    std::u16string s;
    for(uint32_t i = 0; i < n; i++)
        s += char16_t((UniChar)p[i]);
    return s;
}

void putUnicode(HFSUniStr255Ptr out, const std::u16string& s)
{
    if(!out)
        return;
    size_t n = std::min<size_t>(s.size(), 255);
    out->length = (uint16_t)n;
    for(size_t i = 0; i < n; i++)
        out->unicode[i] = s[i];
}

/* An application's name as HFS+ stores names: MacRoman's precomposed
   characters decomposed (the rest as given). */
std::u16string decomposed(const std::u16string& s)
{
    std::u16string out;
    for(char16_t c : s)
    {
        bool done = false;
        if(c >= 0x80)
            for(int b = 0x80; b < 256 && !done; b++)
                if(hfsplus::macRomanCharacter(uint8_t(b)) == c)
                {
                    out += hfsplus::macRomanToUnicode(std::string(1, char(b)));
                    done = true;
                }
        if(!done)
            out += c;
    }
    return out;
}

/* A classic volume's name in Unicode: precomposed, as the Text Encoding
   Converter gives MacRoman. */
std::u16string classicToUnicode(const std::string& mac)
{
    std::u16string out;
    for(unsigned char b : mac)
        out += char16_t(hfsplus::macRomanCharacter(uint8_t(b)));
    return out;
}

/* A Unicode name for a classic volume: MacRoman, 31 characters at most
   (errFSNameTooLong past them; bdNamErr for a character MacRoman lacks).
   A ':' stays one (9.0.4 makes such an HFS name). */
OSErr classicName(const std::u16string& u, std::string *out)
{
    std::string mac;
    size_t from = 0;
    for(;;)
    {
        size_t colon = u.find(u':', from);
        bool lossy = false;
        mac += hfsplus::unicodeToMacRoman(decomposed(u.substr(from, colon - from)), &lossy);
        if(lossy)
            return bdNamErr;
        if(colon == std::u16string::npos)
            break;
        mac += ':';
        from = colon + 1;
    }
    if(mac.size() > 31)
        return errFSNameTooLong;
    *out = mac;
    return noErr;
}

/* ---- dates ---------------------------------------------------------------- */

void putUTC(UTCDateTime& d, uint32_t secs)
{
    d.highSeconds = 0;
    d.lowSeconds = secs;
    d.fraction = 0;
}

uint32_t utcFromLocal(uint32_t local)
{
    return local ? (uint32_t)(macToHostTime(local) + 2082844800) : 0;
}

uint32_t localFromUTC(uint32_t utc)
{
    return utc ? (uint32_t)hostToMacTime(int64_t(utc) - 2082844800) : 0;
}

/* ---- nodes and refs ------------------------------------------------------- */

struct Node
{
    short vref = 0;
    HVCB *vcb = nullptr;
    HFSPlusVolume *hp = nullptr;
    uint32_t id = 0, parent = 0;
    bool dir = false;
    std::string mac;                    /* the classic name ("" for a root on HFS+) */
    std::u16string name;                /* the Unicode name */
};

/* An FSRef: 'FRef', the volume, the kind (1 HFS+, 2 classic), the name's
   length, the ID, the parent, the classic name. */
const uint32_t kRefMagic = 0x46526566;

void toRef(const Node& n, FSRefPtr ref)
{
    if(!ref)
        return;
    uint8_t *h = ref->hidden;
    memset(h, 0, 80);
    auto put32 = [](uint8_t *p, uint32_t v) {
        p[0] = v >> 24;
        p[1] = v >> 16;
        p[2] = v >> 8;
        p[3] = v;
    };
    put32(h, kRefMagic);
    h[4] = uint8_t(n.vref >> 8);
    h[5] = uint8_t(n.vref);
    h[6] = n.hp ? 1 : 2;
    h[7] = (uint8_t)std::min<size_t>(n.mac.size(), 63);
    put32(h + 8, n.id);
    put32(h + 12, n.parent);
    memcpy(h + 16, n.mac.data(), h[7]);
}

/* the classic catalog info of (vref, dir, name), or by dirID alone */
OSErr classicInfo(short vref, LONGINT dir, const std::string& name, CInfoPBRec *c, Str255 nameOut)
{
    Bypass b;
    memset(c, 0, sizeof *c);
    putPStr(nameOut, name);
    c->hFileInfo.ioNamePtr = nameOut;
    c->hFileInfo.ioVRefNum = vref;
    c->hFileInfo.ioDirID = dir;
    c->hFileInfo.ioFDirIndex = name.empty() ? -1 : 0;
    return PBGetCatInfo(c, false);
}

void fillClassic(Node& n, const CInfoPBRec& c, const unsigned char *name)
{
    n.dir = c.hFileInfo.ioFlAttrib & ATTRIB_ISADIR;
    n.id = n.dir ? (uint32_t)c.dirInfo.ioDrDirID : (uint32_t)c.hFileInfo.ioDirID;
    n.parent = n.dir ? (uint32_t)c.dirInfo.ioDrParID : (uint32_t)c.hFileInfo.ioFlParID;
    n.mac = pstr(name);
    n.name = classicToUnicode(n.mac);
}

void fillPlus(Node& n, const CatalogEntry& e)
{
    n.id = e.cnid;
    n.parent = e.parentID;
    n.dir = e.folder;
    n.name = e.cnid == hfsplus::kRootFolderID ? n.hp->reader().volumeName() : e.name;
    n.mac = n.hp->shortName(e);
}

/* the node at (vref, dir, name) found with the classic calls */
OSErr classicNode(short vref, LONGINT dir, const std::string& name, Node *n)
{
    CInfoPBRec c;
    Str255 nm;
    OSErr err = classicInfo(vref, dir, name, &c, nm);
    if(err)
        return err;
    n->vref = vref;
    n->vcb = vcbFor(vref);
    fillClassic(*n, c, nm);
    if(n->id == 2)
        n->parent = 1;
    return noErr;
}

/* A classic file by its ID, the volume walked (its ID survives renames and
   moves; HFS has no file threads). */
OSErr classicFileByID(short vref, uint32_t id, Node *n)
{
    std::vector<LONGINT> dirs = { 2 };
    for(size_t k = 0; k < dirs.size() && k < 100000; k++)
        for(INTEGER i = 1;; i++)
        {
            CInfoPBRec c;
            Str255 nm;
            Bypass b;
            memset(&c, 0, sizeof c);
            nm[0] = 0;
            c.hFileInfo.ioNamePtr = nm;
            c.hFileInfo.ioVRefNum = vref;
            c.hFileInfo.ioDirID = dirs[k];
            c.hFileInfo.ioFDirIndex = i;
            if(PBGetCatInfo(&c, false) != noErr)
                break;
            if(c.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
                dirs.push_back(c.dirInfo.ioDrDirID);
            else if((uint32_t)c.hFileInfo.ioDirID == id)
            {
                n->vref = vref;
                n->vcb = vcbFor(vref);
                fillClassic(*n, c, nm);
                return noErr;
            }
        }
    return fnfErr;
}

OSErr fromRef(FSRefPtr ref, Node *n)
{
    if(!ref)
        return paramErr;
    const uint8_t *h = ref->hidden;
    auto get32 = [](const uint8_t *p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; };
    if(get32(h) != kRefMagic)
    {
        bool zero = std::all_of(h, h + 80, [](uint8_t b) { return b == 0; });
        return zero ? errFSBadFSRef : fnfErr;
    }
    *n = Node();
    n->vref = int16_t(h[4] << 8 | h[5]);
    n->vcb = vcbFor(n->vref);
    if(!n->vcb)
        return nsvErr;
    n->hp = hfsPlusVolume(n->vcb);
    uint32_t id = get32(h + 8), parent = get32(h + 12);
    if(h[6] == 1)
    {
        if(!n->hp)
            return errFSBadFSRef;
        auto e = n->hp->byID(id);
        if(!e)
            return fnfErr;
        fillPlus(*n, *e);
        return noErr;
    }
    if(h[6] != 2 || n->hp)
        return errFSBadFSRef;
    std::string name((const char *)h + 16, h[7]);
    short vref = n->vref;
    if(id == 2)
        return classicNode(vref, 2, "", n);
    /* by its name first: an exchange leaves the ID with the name on 9.0.4,
       where Executor's classic ExchangeFiles moves it with the contents */
    if(classicNode(vref, parent, name, n) == noErr)
        return noErr;
    /* moved or renamed: a directory by its ID, a file by a walk */
    Node byDir;
    if(classicNode(vref, id, "", &byDir) == noErr && byDir.dir && byDir.id == id)
    {
        *n = byDir;
        return noErr;
    }
    return classicFileByID(vref, id, n);
}

/* ---- catalog info --------------------------------------------------------- */

void infoFromPlus(const Node& n, const CatalogEntry& e, uint32_t which, FSCatalogInfoPtr ci)
{
    uint64_t bs = n.hp->reader().header().blockSize;
    if(which & 0x2)
        ci->nodeFlags = (e.flags & CatalogEntry::kLocked ? kNodeLocked : 0) | (e.folder ? kNodeDir : 0)
                        | (e.folder ? 0 : n.hp->openFlags(e.cnid));
    if(which & 0x4)
        ci->volume = n.vref;
    if(which & 0x8)
        ci->parentDirID = e.parentID;
    if(which & 0x10)
        ci->nodeID = e.cnid;
    if(which & 0x20)
        putUTC(ci->createDate, e.createDate);
    if(which & 0x40)
        putUTC(ci->contentModDate, e.contentModDate);
    if(which & 0x80)
        putUTC(ci->attributeModDate, e.attributeModDate);
    if(which & 0x100)
        putUTC(ci->accessDate, e.accessDate);
    if(which & 0x200)
        putUTC(ci->backupDate, e.backupDate);
    if(which & 0x400)
        for(int i = 0; i < 4; i++)
            ci->permissions[i] = e.raw.size() >= 48 ? hfsplus::be32(e.raw.data() + 32 + 4 * i) : 0;
    if(which & 0x800)
        memcpy(ci->finderInfo, e.userInfo, 16);
    if(which & 0x1000)
        memcpy(ci->extFinderInfo, e.finderInfo, 16);
    if(which & 0x2000)
        ci->valence = e.folder ? (uint32_t)e.valence : 0u;
    if(which & 0x4000)
    {
        ci->dataLogicalSize = e.folder ? 0 : e.dataFork.logicalSize;
        ci->dataPhysicalSize = e.folder ? 0 : e.dataFork.totalBlocks * bs;
    }
    if(which & 0x8000)
    {
        ci->rsrcLogicalSize = e.folder ? 0 : e.resourceFork.logicalSize;
        ci->rsrcPhysicalSize = e.folder ? 0 : e.resourceFork.totalBlocks * bs;
    }
    if(which & 0x10000)
        ci->sharingFlags = 0;
    if(which & 0x20000)
        ci->userPrivileges = 0;
    if(which & 0x1)
        ci->textEncodingHint = e.textEncoding;
}

/* an open file's fork lengths as its paths have them (9.0.4 reports
   them before a flush; Executor's HFS catalog has them at close) */
void openLengths(const Node& n, bool rsrc, uint32_t *eof, uint32_t *peof)
{
    if(n.dir || !n.vcb)
        return;
    int16_t total = *(GUEST<INTEGER> *)LM(FCBSPtr);
    for(int16_t ref = 2; ref < total; ref += 94)
    {
        fcbrec *f = (fcbrec *)((char *)LM(FCBSPtr) + ref);
        if((LONGINT)f->fdfnum == (LONGINT)n.id && (VCB *)f->fcvptr == (VCB *)n.vcb
           && bool(f->fcflags & fcfisres) == rsrc)
        {
            *eof = std::max<uint32_t>(*eof, (uint32_t)(LONGINT)f->fcleof);
            *peof = std::max<uint32_t>(*peof, (uint32_t)(LONGINT)f->fcPLen);
        }
    }
}

void infoFromClassic(const Node& n, const CInfoPBRec& c, uint32_t which, FSCatalogInfoPtr ci)
{
    bool dir = n.dir;
    uint8_t attrib = c.hFileInfo.ioFlAttrib;
    if(which & 0x2)
        ci->nodeFlags = (attrib & ATTRIB_ISLOCKED ? kNodeLocked : 0) | (dir ? kNodeDir : 0)
                        | (dir ? 0 : attrib & 0x8C);
    if(which & 0x4)
        ci->volume = n.vref;
    if(which & 0x8)
        ci->parentDirID = n.parent;
    if(which & 0x10)
        ci->nodeID = n.id;
    if(which & 0x20)
        putUTC(ci->createDate, utcFromLocal(dir ? c.dirInfo.ioDrCrDat : c.hFileInfo.ioFlCrDat));
    if(which & 0x40)
        putUTC(ci->contentModDate, utcFromLocal(dir ? c.dirInfo.ioDrMdDat : c.hFileInfo.ioFlMdDat));
    if(which & 0x80)
        putUTC(ci->attributeModDate, 0);
    if(which & 0x100)
        putUTC(ci->accessDate, 0);
    if(which & 0x200)
        putUTC(ci->backupDate, utcFromLocal(dir ? c.dirInfo.ioDrBkDat : c.hFileInfo.ioFlBkDat));
    if(which & 0x400)
        for(int i = 0; i < 4; i++)
            ci->permissions[i] = 0;
    if(which & 0x800)
        memcpy(ci->finderInfo, dir ? (const void *)&c.dirInfo.ioDrUsrWds : (const void *)&c.hFileInfo.ioFlFndrInfo, 16);
    if(which & 0x1000)
        memcpy(ci->extFinderInfo, dir ? (const void *)&c.dirInfo.ioDrFndrInfo : (const void *)&c.hFileInfo.ioFlXFndrInfo,
               16);
    if(which & 0x2000)
        ci->valence = dir ? (uint16_t)c.dirInfo.ioDrNmFls : 0;
    if(which & 0x4000)
    {
        uint32_t eof = dir ? 0 : (uint32_t)c.hFileInfo.ioFlLgLen, peof = dir ? 0 : (uint32_t)c.hFileInfo.ioFlPyLen;
        openLengths(n, false, &eof, &peof);
        ci->dataLogicalSize = eof;
        ci->dataPhysicalSize = peof;
    }
    if(which & 0x8000)
    {
        uint32_t eof = dir ? 0 : (uint32_t)c.hFileInfo.ioFlRLgLen, peof = dir ? 0 : (uint32_t)c.hFileInfo.ioFlRPyLen;
        openLengths(n, true, &eof, &peof);
        ci->rsrcLogicalSize = eof;
        ci->rsrcPhysicalSize = peof;
    }
    if(which & 0x10000)
        ci->sharingFlags = 0;
    if(which & 0x20000)
        ci->userPrivileges = 0;
    if(which & 0x1)
        ci->textEncodingHint = 0;
}

void specOf(const Node& n, FSSpecPtr spec)
{
    if(!spec)
        return;
    spec->vRefNum = n.vref;
    if(n.id == 2)
    {
        spec->parID = 1;
        putPStr(spec->name, pstr((ConstStringPtr)n.vcb->vcbVN), 31);
    }
    else
    {
        spec->parID = n.parent;
        putPStr(spec->name, n.mac, 63);
    }
}

OSErr catalogInfo(const Node& n, uint32_t which, FSCatalogInfoPtr ci, HFSUniStr255Ptr name, FSSpecPtr spec,
                  FSRefPtr parentRef)
{
    if(which & ~kGettable)
        return errFSBadInfoBitmap;
    if(ci && which)
    {
        if(n.hp)
        {
            auto e = n.hp->byID(n.id);
            if(!e)
                return fnfErr;
            infoFromPlus(n, *e, which, ci);
        }
        else
        {
            CInfoPBRec c;
            Str255 nm;
            OSErr err = n.id == 2 ? classicInfo(n.vref, 2, "", &c, nm) : classicInfo(n.vref, n.parent, n.mac, &c, nm);
            if(err)
                return err;
            infoFromClassic(n, c, which, ci);
        }
    }
    putUnicode(name, n.name);
    specOf(n, spec);
    if(parentRef)
    {
        Node p;
        if(n.id == 2)
            memset(parentRef->hidden, 0, 80);               /* the root's: none (9.0.4) */
        else if(n.hp)
        {
            p.vref = n.vref;
            p.vcb = n.vcb;
            p.hp = n.hp;
            if(auto e = n.hp->byID(n.parent))
                fillPlus(p, *e);
            toRef(p, parentRef);
        }
        else
        {
            if(classicNode(n.vref, n.parent, "", &p) == noErr)
                toRef(p, parentRef);
        }
    }
    return noErr;
}

/* a directory's child by its Unicode name */
OSErr childByName(const Node& dir, const std::u16string& name, Node *n)
{
    if(!dir.dir)
        return dirNFErr;
    if(name.empty())
    {
        *n = dir;
        return noErr;
    }
    if(dir.hp)
    {
        auto e = dir.hp->lookup(dir.id, decomposed(name));
        if(!e)
            return fnfErr;
        *n = dir;
        fillPlus(*n, *e);
        return noErr;
    }
    std::string mac;
    if(classicName(name, &mac) != noErr)
        return fnfErr;
    if(mac.find(':') != std::string::npos)
        return fnfErr;
    return classicNode(dir.vref, dir.id, mac, n);
}

/* ---- iterators ---------------------------------------------------------- */

struct Iterator
{
    Node container;
    bool subtree;
    uint32_t index = 0;                 /* flat: items returned */
    /* search: the catalog's leaf records in file order (HFS+), or a walk */
    std::vector<Node> found;
    size_t next = 0;
    bool searched = false;
};
std::map<uint32_t, Iterator> iterators;   /* by its guest block */

uint32_t guestAddr(Ptr p)
{
    return US_TO_SYN68K(p);
}

/* ---- forks ---------------------------------------------------------------- */

bool forkName(uint32_t len, const GUEST<UniChar> *name, bool *rsrc)
{
    if(len == 0)
    {
        *rsrc = false;
        return true;
    }
    if(!name)
        return false;
    std::u16string n = fromGuest(name, len);
    if(n == kResourceForkName)
    {
        *rsrc = true;
        return true;
    }
    return false;
}

/* position from a mode and offset; errFSBadPosMode for newline mode */
OSErr position(uint16_t mode, int64_t offset, int64_t mark, int64_t eof, int64_t *pos)
{
    if(mode & 0x80)
        return errFSBadPosMode;
    switch(mode & 3)
    {
        case fsAtMark: *pos = mark; break;
        case fsFromStart: *pos = offset; break;
        case fsFromLEOF: *pos = eof + offset; break;
        default: *pos = mark + offset; break;
    }
    return *pos < 0 ? posErr : noErr;
}

struct ForkPath
{
    short ref;
    HVCB *vcb = nullptr;
    HFSPlusVolume *hp = nullptr;
};

/* a refnum the fork calls opened */
OSErr forkPath(short ref, ForkPath *p)
{
    if(!forkAPIRefNums.count(ref))
        return ref > 0 && ref % 94 == 2 ? fnOpnErr : errFSBadForkRef;
    OSErr err;
    fcbrec *f = PRNTOFPERR(ref, &err);
    if(!f)
        return errFSBadForkRef;
    p->ref = ref;
    p->vcb = (HVCB *)(VCB *)f->fcvptr;
    p->hp = hfsPlusVolume(p->vcb);
    return noErr;
}

/* classic forks: what the FCB says (mark, EOF, allocation) */
void classicFork(short ref, int64_t *mark, int64_t *eof, int64_t *phys)
{
    OSErr err;
    fcbrec *f = PRNTOFPERR(ref, &err);
    *mark = f ? (uint32_t)(LONGINT)f->fcbCrPs : 0;
    *eof = f ? (uint32_t)(LONGINT)f->fcleof : 0;
    *phys = f ? (uint32_t)(LONGINT)f->fcPLen : 0;
}

OSErr classicSetMark(short ref, int64_t pos)
{
    Bypass b;
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ref;
    pb.ioParam.ioPosMode = fsFromStart;
    pb.ioParam.ioPosOffset = (LONGINT)pos;
    return PBSetFPos(&pb, false);
}

} // namespace

/* hfsXbar.cpp: the classic calls on a fork call's path (9.0.4: fsDataTooBigErr) */
bool Executor::ROMlib_forkAPIRefused(short refNum)
{
    return !bypassDepth && forkAPIRefNums.count(refNum);
}

void Executor::ROMlib_forkAPIClosed(short refNum)
{
    forkAPIRefNums.erase(refNum);
}

/* ---- FSRefParam calls ----------------------------------------------------- */

OSErr Executor::PBMakeFSRef(FSRefParamPtr pb, Boolean async)
{
    OSErr err;
    {
        short vref = pb->ioVRefNum;
        LONGINT dir = pb->ioDirID;
        std::string name = pstr(pb->ioNamePtr);
        HVCB *vcb = vcbFor(vref);
        if(!vcb && vref == 0)
        {
            LONGINT d;
            vcb = ROMlib_findvcb(0, pb->ioNamePtr, &d, true);
        }
        Node n;
        if(HFSPlusVolume *hp = hfsPlusVolume(vcb))
        {
            try
            {
                uint32_t id = hp->resolveClassic(pb->ioNamePtr, vref, dir);
                n.vref = vcb->vcbVRefNum;
                n.vcb = vcb;
                n.hp = hp;
                fillPlus(n, *hp->byID(id));
                err = noErr;
            }
            catch(OSErrorException& e)
            {
                err = e.code;
            }
        }
        else
        {
            CInfoPBRec c;
            Str255 nm;
            err = classicInfo(vref, dir, name, &c, nm);
            if(!err)
            {
                n.vref = c.hFileInfo.ioVRefNum;
                if(!n.vref || n.vref > 0)
                    n.vref = vcb ? (short)vcb->vcbVRefNum : vref;
                n.vcb = vcbFor(n.vref);
                fillClassic(n, c, nm);
                if(n.id == 2)
                    n.parent = 1;
                if(n.dir && name.empty())
                {
                    /* by dirID: its own name and parent */
                    Node self;
                    if(classicNode(n.vref, n.id, "", &self) == noErr)
                        n = self;
                }
            }
        }
        if(!err)
            toRef(n, pb->newRef);
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBMakeFSRefUnicode(FSRefParamPtr pb, Boolean async)
{
    Node dir, n;
    OSErr err = fromRef(pb->ref, &dir);
    if(!err)
        err = childByName(dir, fromGuest(pb->name, pb->nameLength), &n);
    if(!err)
        toRef(n, pb->newRef);
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBCompareFSRefs(FSRefParamPtr pb, Boolean async)
{
    Node a, b;
    OSErr err = fromRef(pb->ref, &a);
    if(!err)
        err = fromRef(pb->parentRef, &b);
    if(!err && (a.vref != b.vref || a.id != b.id))
        err = errFSRefsDifferent;
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBGetCatalogInfo(FSRefParamPtr pb, Boolean async)
{
    Node n;
    OSErr err = fromRef(pb->ref, &n);
    if(!err)
        err = catalogInfo(n, pb->whichInfo, pb->catInfo, pb->outName, pb->spec, pb->parentRef);
    FAKEASYNC(pb, async, err);
}

static OSErr setCatalogInfo(const Node& n, uint32_t which, FSCatalogInfoPtr ci)
{
    if(which & ~kSettable)
        return errFSBadInfoBitmap;
    if(!which)
        return noErr;
    if(!ci)
        return paramErr;
    if(n.hp)
    {
        try
        {
            auto e = n.hp->byID(n.id);
            if(!e)
                return fnfErr;
            if(which & 0x20)
                e->createDate = ci->createDate.lowSeconds;
            if(which & 0x40)
                e->contentModDate = ci->contentModDate.lowSeconds;
            if(which & 0x80)
                e->attributeModDate = ci->attributeModDate.lowSeconds;
            if(which & 0x100)
                e->accessDate = ci->accessDate.lowSeconds;
            if(which & 0x200)
                e->backupDate = ci->backupDate.lowSeconds;
            if(which & 0x800)
                memcpy(e->userInfo, ci->finderInfo, 16);
            if(which & 0x1000)
                memcpy(e->finderInfo, ci->extFinderInfo, 16);
            if(which & 0x1)
                e->textEncoding = ci->textEncodingHint;
            if(which & 0x2)
                e->flags = (uint16_t(ci->nodeFlags) & kNodeLocked) ? e->flags | CatalogEntry::kLocked
                                                                   : e->flags & ~CatalogEntry::kLocked;
            n.hp->setInfo(*e);
            return noErr;
        }
        catch(OSErrorException& ex)
        {
            return ex.code;
        }
    }
    CInfoPBRec c;
    Str255 nm;
    OSErr err = n.id == 2 ? classicInfo(n.vref, 2, "", &c, nm) : classicInfo(n.vref, n.parent, n.mac, &c, nm);
    if(err)
        return err;
    Bypass b;
    if(which & 0xBE0 & ~0x0400 & ~0x0180)
    {
        c.hFileInfo.ioNamePtr = nm;
        c.hFileInfo.ioDirID = n.id == 2 ? 2 : n.parent;
        if(n.dir)
        {
            if(which & 0x20)
                c.dirInfo.ioDrCrDat = localFromUTC(ci->createDate.lowSeconds);
            if(which & 0x40)
                c.dirInfo.ioDrMdDat = localFromUTC(ci->contentModDate.lowSeconds);
            if(which & 0x200)
                c.dirInfo.ioDrBkDat = localFromUTC(ci->backupDate.lowSeconds);
            if(which & 0x800)
                memcpy(&c.dirInfo.ioDrUsrWds, ci->finderInfo, 16);
            if(which & 0x1000)
                memcpy(&c.dirInfo.ioDrFndrInfo, ci->extFinderInfo, 16);
        }
        else
        {
            if(which & 0x20)
                c.hFileInfo.ioFlCrDat = localFromUTC(ci->createDate.lowSeconds);
            if(which & 0x40)
                c.hFileInfo.ioFlMdDat = localFromUTC(ci->contentModDate.lowSeconds);
            if(which & 0x200)
                c.hFileInfo.ioFlBkDat = localFromUTC(ci->backupDate.lowSeconds);
            if(which & 0x800)
                memcpy(&c.hFileInfo.ioFlFndrInfo, ci->finderInfo, 16);
            if(which & 0x1000)
                memcpy(&c.hFileInfo.ioFlXFndrInfo, ci->extFinderInfo, 16);
        }
        c.hFileInfo.ioFDirIndex = 0;
        if((err = PBSetCatInfo(&c, false)) != noErr)
            return err;
    }
    if((which & 0x2) && !n.dir)
    {
        HParamBlockRec h;
        memset(&h, 0, sizeof h);
        h.fileParam.ioNamePtr = nm;
        h.fileParam.ioVRefNum = n.vref;
        h.fileParam.ioDirID = n.parent;
        err = (uint16_t(ci->nodeFlags) & kNodeLocked) ? PBHSetFLock(&h, false) : PBHRstFLock(&h, false);
    }
    return err;
}

OSErr Executor::PBSetCatalogInfo(FSRefParamPtr pb, Boolean async)
{
    Node n;
    OSErr err = fromRef(pb->ref, &n);
    if(!err)
        err = setCatalogInfo(n, pb->whichInfo, pb->catInfo);
    FAKEASYNC(pb, async, err);
}

static OSErr createUnicode(FSRefParamPtr pb, bool folder)
{
    Node dir, n;
    OSErr err = fromRef(pb->ref, &dir);
    if(err)
        return err;
    if(!dir.dir)
        return dirNFErr;
    std::u16string name = fromGuest(pb->name, pb->name ? (uint32_t)pb->nameLength : 0u);
    if(name.empty())
        return errFSMissingName;
    if(name.size() > 255)
        return errFSNameTooLong;
    if(dir.hp)
    {
        try
        {
            if(dir.hp->lookup(dir.id, decomposed(name)))
                return dupFNErr;
            uint32_t id = dir.hp->createUnicode(dir.id, decomposed(name), folder);
            n = dir;
            fillPlus(n, *dir.hp->byID(id));
        }
        catch(OSErrorException& e)
        {
            return e.code;
        }
    }
    else
    {
        std::string mac;
        if((err = classicName(name, &mac)) != noErr)
            return err;
        if(mac.find(':') != std::string::npos)
            return bdNamErr;
        Bypass b;
        HParamBlockRec h;
        Str255 nm;
        putPStr(nm, mac);
        memset(&h, 0, sizeof h);
        h.fileParam.ioNamePtr = nm;
        h.fileParam.ioVRefNum = dir.vref;
        h.fileParam.ioDirID = dir.id;
        if((err = folder ? PBDirCreate(&h, false) : PBHCreate(&h, false)) != noErr)
            return err;
        if((err = classicNode(dir.vref, dir.id, mac, &n)) != noErr)
            return err;
    }
    if(pb->whichInfo && pb->catInfo)
    {
        uint32_t which = pb->whichInfo & kSettable & ~0x2u;
        if((err = setCatalogInfo(n, which, pb->catInfo)) != noErr)
            return err;
    }
    toRef(n, pb->newRef);
    specOf(n, pb->spec);
    if(folder)
        pb->ioDirID = n.id;
    return noErr;
}

OSErr Executor::PBCreateFileUnicode(FSRefParamPtr pb, Boolean async)
{
    OSErr err = createUnicode(pb, false);
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBCreateDirectoryUnicode(FSRefParamPtr pb, Boolean async)
{
    OSErr err = createUnicode(pb, true);
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBDeleteObject(FSRefParamPtr pb, Boolean async)
{
    Node n;
    OSErr err = fromRef(pb->ref, &n);
    if(!err && n.hp)
    {
        try
        {
            if(n.dir)
            {
                auto e = n.hp->byID(n.id);
                if(e && e->valence)
                    throw OSErrorException(fBsyErr);
            }
            n.hp->deleteID(n.id);
        }
        catch(OSErrorException& e)
        {
            err = e.code;
        }
    }
    else if(!err)
    {
        /* a locked file: fLckdErr (Executor's HFS code would delete it) */
        CInfoPBRec c;
        Str255 nm;
        if(!n.dir && classicInfo(n.vref, n.parent, n.mac, &c, nm) == noErr
           && (c.hFileInfo.ioFlAttrib & ATTRIB_ISLOCKED))
            err = fLckdErr;
        else
        {
            Bypass b;
            HParamBlockRec h;
            putPStr(nm, n.mac);
            memset(&h, 0, sizeof h);
            h.fileParam.ioNamePtr = nm;
            h.fileParam.ioVRefNum = n.vref;
            h.fileParam.ioDirID = n.parent;
            err = PBHDelete(&h, false);
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBMoveObject(FSRefParamPtr pb, Boolean async)
{
    Node n, to;
    OSErr err = fromRef(pb->ref, &n);
    if(!err)
        err = fromRef(pb->parentRef, &to);
    if(!err && n.vref != to.vref)
        err = kDiffVolErr;
    if(!err && !to.dir)
        err = errFSNotAFolder;
    if(!err && n.hp)
    {
        try
        {
            if(n.dir)                           /* not into itself or a child of it */
                for(uint32_t up = to.id; up > 1;)
                {
                    if(up == n.id)
                        throw OSErrorException(badMovErr);
                    auto e = n.hp->byID(up);
                    up = e ? e->parentID : 0;
                }
            n.hp->moveID(n.id, to.id);
            fillPlus(n, *n.hp->byID(n.id));
        }
        catch(OSErrorException& e)
        {
            err = e.code;
        }
    }
    else if(!err)
    {
        Bypass b;
        CMovePBRec m;
        Str255 nm;
        putPStr(nm, n.mac);
        memset(&m, 0, sizeof m);
        m.ioNamePtr = nm;
        m.ioVRefNum = n.vref;
        m.ioDirID = n.parent;
        m.ioNewDirID = to.id;
        if((err = PBCatMove(&m, false)) == noErr)
            err = classicNode(n.vref, to.id, n.mac, &n);
    }
    if(!err)
        toRef(n, pb->newRef);
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBRenameUnicode(FSRefParamPtr pb, Boolean async)
{
    Node n;
    OSErr err = fromRef(pb->ref, &n);
    std::u16string name = fromGuest(pb->name, pb->name ? (uint32_t)pb->nameLength : 0u);
    if(!err && name.empty())
        err = dupFNErr;                         /* (9.0.4) */
    if(!err && name.size() > 255)
        err = errFSNameTooLong;
    if(!err && n.hp)
    {
        try
        {
            std::u16string d = decomposed(name);
            if(n.id != hfsplus::kRootFolderID)
                if(auto other = n.hp->lookup(n.parent, d); other && other->cnid != n.id)
                    throw OSErrorException(dupFNErr);
            n.hp->renameUnicode(n.id, d);
            fillPlus(n, *n.hp->byID(n.id));
        }
        catch(OSErrorException& e)
        {
            err = e.code;
        }
    }
    else if(!err)
    {
        std::string mac;
        err = classicName(name, &mac);
        if(!err)
        {
            Bypass b;
            HParamBlockRec h;
            Str255 nm, to;
            putPStr(nm, n.mac);
            putPStr(to, mac);
            memset(&h, 0, sizeof h);
            h.fileParam.ioNamePtr = nm;
            h.fileParam.ioVRefNum = n.vref;
            h.fileParam.ioDirID = n.id == 2 ? 2 : n.parent;
            Node other;
            if(n.id != 2 && !EqualString(nm, to, false, true)
               && classicNode(n.vref, n.parent, mac, &other) == noErr)
                err = dupFNErr;                 /* (Executor's HFS code: bdNamErr) */
            else if(mac != n.mac && EqualString(nm, to, false, true))
            {
                /* only the case changes: Executor's HFS code would call the
                   name taken, so through a name of its own */
                Str255 tmp;
                putPStr(tmp, "\xA5" "fsref rename");
                h.ioParam.ioMisc = guest_cast<LONGINT>((StringPtr)tmp);
                if((err = PBHRename(&h, false)) == noErr)
                    h.fileParam.ioNamePtr = tmp;
            }
            h.ioParam.ioMisc = guest_cast<LONGINT>((StringPtr)to);
            if(!err && (err = PBHRename(&h, false)) == noErr)
                err = n.id == 2 ? classicNode(n.vref, 2, "", &n) : classicNode(n.vref, n.parent, mac, &n);
        }
    }
    if(!err)
        toRef(n, pb->newRef);
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBExchangeObjects(FSRefParamPtr pb, Boolean async)
{
    Node a, b;
    OSErr err = fromRef(pb->ref, &a);
    if(!err)
        err = fromRef(pb->parentRef, &b);
    if(!err && a.vref != b.vref)
        err = kDiffVolErr;
    if(!err && (a.dir || b.dir))
        err = kNotAFileErr;
    if(!err && a.id == b.id)
        err = kSameFileErr;
    if(!err && a.hp)
    {
        try
        {
            a.hp->exchangeIDs(a.id, b.id);
        }
        catch(OSErrorException& e)
        {
            err = e.code;
        }
    }
    else if(!err)
    {
        FSSpec sa, sb;
        specOf(a, &sa);
        specOf(b, &sb);
        Bypass by;
        err = FSpExchangeFiles(&sa, &sb);
    }
    FAKEASYNC(pb, async, err);
}

/* ---- iterators ------------------------------------------------------------ */

OSErr Executor::PBOpenIterator(FSCatalogBulkParamPtr pb, Boolean async)
{
    Node c;
    OSErr err = fromRef(pb->container, &c);
    uint32_t flags = pb->iteratorFlags;
    if(!err && (flags & ~1u))
        err = errFSBadIteratorFlags;
    if(!err && !c.dir)
        err = errFSNotAFolder;
    if(!err && (flags & 1) && c.id != 2)
        err = errFSBadIteratorFlags;            /* a subtree: the volume's only (9.0.4) */
    if(!err)
    {
        Ptr block = NewPtrSys(4);
        if(!block)
            err = memFullErr;
        else
        {
            Iterator it;
            it.container = c;
            it.subtree = flags & 1;
            iterators[guestAddr(block)] = it;
            pb->iterator = block;
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBCloseIterator(FSCatalogBulkParamPtr pb, Boolean async)
{
    Ptr block = pb->iterator;
    OSErr err = noErr;
    auto it = block ? iterators.find(guestAddr(block)) : iterators.end();
    if(it == iterators.end())
        err = fnOpnErr;                         /* (9.0.4, closed twice) */
    else
    {
        iterators.erase(it);
        DisposePtr(block);
    }
    FAKEASYNC(pb, async, err);
}

/* one item for the bulk calls' arrays */
static void bulkItem(FSCatalogBulkParamPtr pb, ItemCount i, const Node& n)
{
    uint32_t which = pb->whichInfo;
    FSCatalogInfoPtr ci = pb->catalogInfo;
    FSRefPtr refs = pb->refs;
    FSSpecPtr specs = pb->specs;
    HFSUniStr255Ptr names = pb->names;
    catalogInfo(n, which, ci ? ci + i : nullptr, names ? names + i : nullptr, specs ? specs + i : nullptr, nullptr);
    if(refs)
        toRef(n, refs + i);
}

OSErr Executor::PBGetCatalogInfoBulk(FSCatalogBulkParamPtr pb, Boolean async)
{
    OSErr err = noErr;
    Ptr block = pb->iterator;
    auto found = block ? iterators.find(guestAddr(block)) : iterators.end();
    pb->actualItems = 0;
    pb->containerChanged = false;
    if(found == iterators.end())
        err = errFSIteratorNotFound;
    else if(pb->whichInfo & ~kGettable)
        err = errFSBadInfoBitmap;
    else if(pb->maximumItems == 0)
        err = errFSBadItemCount;
    else if(found->second.subtree)
        err = errFSIteratorNotSupported;
    else
    {
        Iterator& it = found->second;
        ItemCount got = 0, max = pb->maximumItems;
        while(got < max)
        {
            Node n;
            bool have = false;
            if(it.container.hp)
            {
                if(auto e = it.container.hp->childAt(it.container.id, it.index + 1))
                {
                    n = it.container;
                    fillPlus(n, *e);
                    have = true;
                }
            }
            else
            {
                CInfoPBRec c;
                Str255 nm;
                Bypass b;
                memset(&c, 0, sizeof c);
                nm[0] = 0;
                c.hFileInfo.ioNamePtr = nm;
                c.hFileInfo.ioVRefNum = it.container.vref;
                c.hFileInfo.ioDirID = it.container.id;
                c.hFileInfo.ioFDirIndex = it.index + 1;
                if(PBGetCatInfo(&c, false) == noErr)
                {
                    n.vref = it.container.vref;
                    n.vcb = it.container.vcb;
                    fillClassic(n, c, nm);
                    have = true;
                }
            }
            if(!have)
            {
                err = errFSNoMoreItems;
                break;
            }
            bulkItem(pb, got, n);
            it.index++;
            got++;
        }
        pb->actualItems = got;
    }
    FAKEASYNC(pb, async, err);
}

/* whether a name holds another, case-insensitively as HFS+ orders names */
static bool partialMatch(const std::u16string& name, const std::u16string& part)
{
    if(part.empty())
        return true;
    for(size_t i = 0; i + part.size() <= name.size(); i++)
        if(hfsplus::fastUnicodeCompare(name.data() + i, part.size(), part.data(), part.size()) == 0)
            return true;
    return false;
}

OSErr Executor::PBCatalogSearch(FSCatalogBulkParamPtr pb, Boolean async)
{
    OSErr err = noErr;
    Ptr block = pb->iterator;
    auto found = block ? iterators.find(guestAddr(block)) : iterators.end();
    FSSearchParamsPtr sp = pb->searchParams;
    pb->actualItems = 0;
    pb->containerChanged = false;
    const uint32_t kPartialName = 1u << 0, kFullName = 1u << 1, kNegate = 1u << 14;
    if(found == iterators.end())
        err = errFSIteratorNotFound;
    else if(!found->second.subtree)
        err = errFSIteratorNotSupported;
    else if(pb->maximumItems == 0)
        err = errFSBadItemCount;
    else if(!sp || !sp->searchInfo1 || !sp->searchInfo2)
        err = errFSBadSearchParams;
    else
    {
        Iterator& it = found->second;
        if(!it.searched)
        {
            /* everything that matches, in the catalog's order on disk */
            it.searched = true;
            uint32_t bits = sp->searchBits;
            std::u16string want = decomposed(fromGuest(sp->searchName, sp->searchName ? (uint32_t)sp->searchNameLength : 0u));
            auto matches = [&](const std::u16string& name) {
                bool m = true;
                if(bits & kPartialName)
                    m = partialMatch(name, want);
                else if(bits & kFullName)
                    m = hfsplus::fastUnicodeCompare(name, want) == 0;
                return (bits & kNegate) ? !m : m;
            };
            const Node& c = it.container;
            if(c.hp)
                c.hp->reader().forEachLeafRecord([&](const CatalogEntry& e) {
                    if(matches(e.cnid == hfsplus::kRootFolderID ? c.hp->reader().volumeName() : e.name))
                    {
                        Node n = c;
                        fillPlus(n, e);
                        it.found.push_back(n);
                    }
                });
            else
            {
                std::vector<LONGINT> dirs = { 2 };
                for(size_t k = 0; k < dirs.size(); k++)
                    for(INTEGER i = 1;; i++)
                    {
                        CInfoPBRec ci;
                        Str255 nm;
                        Bypass b;
                        memset(&ci, 0, sizeof ci);
                        nm[0] = 0;
                        ci.hFileInfo.ioNamePtr = nm;
                        ci.hFileInfo.ioVRefNum = c.vref;
                        ci.hFileInfo.ioDirID = dirs[k];
                        ci.hFileInfo.ioFDirIndex = i;
                        if(PBGetCatInfo(&ci, false) != noErr)
                            break;
                        Node n;
                        n.vref = c.vref;
                        n.vcb = c.vcb;
                        fillClassic(n, ci, nm);
                        if(n.dir)
                            dirs.push_back(n.id);
                        if(matches(n.name))
                            it.found.push_back(n);
                    }
            }
        }
        ItemCount got = 0;
        while(got < pb->maximumItems && it.next < it.found.size())
            bulkItem(pb, got++, it.found[it.next++]);
        if(it.next >= it.found.size() && got < pb->maximumItems)
            err = errFSNoMoreItems;
        pb->actualItems = got;
    }
    FAKEASYNC(pb, async, err);
}

/* ---- forks ---------------------------------------------------------------- */

OSErr Executor::PBOpenFork(FSForkIOParamPtr pb, Boolean async)
{
    Node n;
    OSErr err = fromRef(pb->ref, &n);
    bool rsrc = false;
    if(!err && !forkName(pb->forkNameLength, pb->forkName, &rsrc))
        err = errFSBadForkName;
    if(!err && n.dir)
        err = errFSForkNotFound;
    short ref = 0;
    if(!err && n.hp)
    {
        try
        {
            ref = n.hp->openForkAPI(n.id, rsrc, pb->permissions);
        }
        catch(OSErrorException& e)
        {
            err = e.code;
        }
    }
    else if(!err)
    {
        Bypass b;
        HParamBlockRec h;
        Str255 nm;
        putPStr(nm, n.mac);
        memset(&h, 0, sizeof h);
        h.ioParam.ioNamePtr = nm;
        h.ioParam.ioVRefNum = n.vref;
        h.fileParam.ioDirID = n.parent;
        h.ioParam.ioPermssn = pb->permissions;
        err = rsrc ? PBHOpenRF(&h, false) : PBHOpenDF(&h, false);
        if(!err)
        {
            ref = h.ioParam.ioRefNum;
            OSErr e2;
            if(fcbrec *f = PRNTOFPERR(ref, &e2))
                f->fcflags = f->fcflags | 0x08; /* "large file" */
        }
    }
    if(!err)
    {
        forkAPIRefNums.insert(ref);
        pb->forkRefNum = ref;
    }
    FAKEASYNC(pb, async, err);
}

static OSErr readOrWrite(FSForkIOParamPtr pb, bool write)
{
    ForkPath p;
    OSErr err = forkPath(pb->forkRefNum, &p);
    pb->actualCount = 0;
    if(err)
        return err;
    int64_t mark, eof, phys, pos;
    if(p.hp)
    {
        try
        {
            auto st = p.hp->forkStatus(p.ref);
            mark = st.mark;
            eof = st.size;
        }
        catch(OSErrorException& e)
        {
            return e.code;
        }
    }
    else
        classicFork(p.ref, &mark, &eof, &phys);
    if((err = position(pb->positionMode, pb->positionOffset, mark, eof, &pos)) != noErr)
        return err;
    uint32_t n = pb->requestCount;
    if(!write && pos > eof)
        return eofErr;                          /* the mark where it was (9.0.4) */
    if(p.hp)
    {
        try
        {
            if(write)
            {
                p.hp->writeFork(p.ref, pos, n, pb->buffer);
                pb->actualCount = n;
            }
            else
            {
                uint32_t got = p.hp->readFork(p.ref, pos, n, pb->buffer);
                pb->actualCount = got;
                if(got < n)
                    err = eofErr;
            }
            pb->positionOffset = (int64_t)p.hp->forkStatus(p.ref).mark;
        }
        catch(OSErrorException& e)
        {
            err = e.code;
        }
        return err;
    }
    if(pos + int64_t(n) > 0x7FFFFFFF)
        return write ? kFileBoundsErr : eofErr;
    Bypass b;
    ParamBlockRec io;
    memset(&io, 0, sizeof io);
    io.ioParam.ioRefNum = p.ref;
    io.ioParam.ioBuffer = pb->buffer;
    io.ioParam.ioReqCount = n;
    io.ioParam.ioPosMode = fsFromStart;
    io.ioParam.ioPosOffset = (LONGINT)pos;
    err = write ? PBWrite(&io, false) : PBRead(&io, false);
    pb->actualCount = io.ioParam.ioActCount;
    pb->positionOffset = (int64_t)(uint32_t)(LONGINT)io.ioParam.ioPosOffset;
    return err;
}

OSErr Executor::PBReadFork(FSForkIOParamPtr pb, Boolean async)
{
    OSErr err = readOrWrite(pb, false);
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBWriteFork(FSForkIOParamPtr pb, Boolean async)
{
    OSErr err = readOrWrite(pb, true);
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBGetForkPosition(FSForkIOParamPtr pb, Boolean async)
{
    ForkPath p;
    OSErr err = forkPath(pb->forkRefNum, &p);
    if(!err)
    {
        int64_t mark, eof, phys;
        if(p.hp)
            mark = p.hp->forkStatus(p.ref).mark;
        else
            classicFork(p.ref, &mark, &eof, &phys);
        pb->positionOffset = mark;
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBSetForkPosition(FSForkIOParamPtr pb, Boolean async)
{
    ForkPath p;
    OSErr err = forkPath(pb->forkRefNum, &p);
    if(!err)
    {
        int64_t mark, eof, phys, pos;
        if(p.hp)
        {
            auto st = p.hp->forkStatus(p.ref);
            mark = st.mark;
            eof = st.size;
        }
        else
            classicFork(p.ref, &mark, &eof, &phys);
        err = position(pb->positionMode, pb->positionOffset, mark, eof, &pos);
        if(!err && pos > eof)
            err = eofErr;
        if(!err)
        {
            if(p.hp)
                p.hp->setForkMark(p.ref, pos);
            else
                err = classicSetMark(p.ref, pos);
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBGetForkSize(FSForkIOParamPtr pb, Boolean async)
{
    ForkPath p;
    OSErr err = forkPath(pb->forkRefNum, &p);
    if(!err)
    {
        int64_t mark, eof, phys;
        if(p.hp)
            eof = p.hp->forkStatus(p.ref).size;
        else
            classicFork(p.ref, &mark, &eof, &phys);
        pb->positionOffset = eof;
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBSetForkSize(FSForkIOParamPtr pb, Boolean async)
{
    ForkPath p;
    OSErr err = forkPath(pb->forkRefNum, &p);
    if(!err)
    {
        int64_t mark, eof, phys, size;
        if(p.hp)
        {
            auto st = p.hp->forkStatus(p.ref);
            mark = st.mark;
            eof = st.size;
        }
        else
            classicFork(p.ref, &mark, &eof, &phys);
        err = position(pb->positionMode, pb->positionOffset, mark, eof, &size);
        if(!err && p.hp)
        {
            try
            {
                p.hp->setForkSize(p.ref, size);
            }
            catch(OSErrorException& e)
            {
                err = e.code;
            }
        }
        else if(!err)
        {
            if(size > 0x7FFFFFFF)
                err = kFileBoundsErr;
            else
            {
                Bypass b;
                ParamBlockRec io;
                memset(&io, 0, sizeof io);
                io.ioParam.ioRefNum = p.ref;
                io.ioParam.ioMisc = (LONGINT)size;
                err = PBSetEOF(&io, false);
                if(!err && mark > size)
                    classicSetMark(p.ref, size);
            }
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBAllocateFork(FSForkIOParamPtr pb, Boolean async)
{
    ForkPath p;
    OSErr err = forkPath(pb->forkRefNum, &p);
    uint16_t flags = pb->allocationFlags;
    if(!err && (flags & 0xFFF8))
        err = errFSBadAllocFlags;
    if(!err)
    {
        int64_t mark, eof, phys = 0, start;
        if(p.hp)
        {
            auto st = p.hp->forkStatus(p.ref);
            mark = st.mark;
            eof = st.size;
            phys = st.physical;
        }
        else
            classicFork(p.ref, &mark, &eof, &phys);
        err = position(pb->positionMode, pb->positionOffset, mark, eof, &start);
        uint64_t end = start + (uint64_t)pb->allocationAmount;
        if(!err && p.hp)
        {
            try
            {
                pb->allocationAmount = p.hp->allocateFork(p.ref, end, flags & 1);
            }
            catch(OSErrorException& e)
            {
                err = e.code;
            }
        }
        else if(!err)
        {
            if(end > 0x7FFFFFFF)
                err = kFileBoundsErr;
            else if((int64_t)end > phys)
            {
                Bypass b;
                ParamBlockRec io;
                memset(&io, 0, sizeof io);
                io.ioParam.ioRefNum = p.ref;
                io.ioParam.ioReqCount = (LONGINT)(end - phys);
                err = (flags & 2) ? PBAllocContig(&io, false) : PBAllocate(&io, false);
                if(!err)
                    pb->allocationAmount = (uint32_t)(LONGINT)io.ioParam.ioActCount;
            }
            else
                pb->allocationAmount = 0;
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBFlushFork(FSForkIOParamPtr pb, Boolean async)
{
    ForkPath p;
    OSErr err = forkPath(pb->forkRefNum, &p);
    if(!err && p.hp)
    {
        try
        {
            p.hp->flushFork(p.ref);
        }
        catch(OSErrorException& e)
        {
            err = e.code;
        }
    }
    else if(!err)
    {
        Bypass b;
        ParamBlockRec io;
        memset(&io, 0, sizeof io);
        io.ioParam.ioRefNum = p.ref;
        err = PBFlushFile(&io, false);
        OSErr e2;
        if(fcbrec *f = PRNTOFPERR(p.ref, &e2))
            f->fcflags = f->fcflags & ~fcdirty;
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBCloseFork(FSForkIOParamPtr pb, Boolean async)
{
    ForkPath p;
    OSErr err = forkPath(pb->forkRefNum, &p);
    if(!err)
    {
        forkAPIRefNums.erase(p.ref);
        if(p.hp)
        {
            try
            {
                p.hp->closeFork(p.ref);
            }
            catch(OSErrorException& e)
            {
                err = e.code;
            }
        }
        else
        {
            Bypass b;
            ParamBlockRec io;
            memset(&io, 0, sizeof io);
            io.ioParam.ioRefNum = p.ref;
            err = PBClose(&io, false);
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBCreateFork(FSForkIOParamPtr pb, Boolean async)
{
    Node n;
    OSErr err = fromRef(pb->ref, &n);
    bool rsrc;
    if(!err && !forkName(pb->forkNameLength, pb->forkName, &rsrc))
        err = errFSBadForkName;
    if(!err)
        err = n.dir ? errFSForkNotFound : errFSForkExists;      /* both forks are always there */
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBDeleteFork(FSForkIOParamPtr pb, Boolean async)
{
    Node n;
    OSErr err = fromRef(pb->ref, &n);
    bool rsrc;
    if(!err && !forkName(pb->forkNameLength, pb->forkName, &rsrc))
        err = errFSBadForkName;
    if(!err && n.dir)
        err = errFSForkNotFound;
    if(!err)
    {
        /* emptied: the fork opened, cut to nothing, closed */
        FSForkIOParam o;
        memset(&o, 0, sizeof o);
        o.ref = pb->ref;
        o.forkNameLength = pb->forkNameLength;
        o.forkName = pb->forkName;
        o.permissions = fsRdWrPerm;
        if((err = PBOpenFork(&o, false)) == noErr)
        {
            o.positionMode = fsFromStart;
            o.positionOffset = 0;
            err = PBSetForkSize(&o, false);
            PBCloseFork(&o, false);
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBIterateForks(FSForkIOParamPtr pb, Boolean async)
{
    Node n;
    OSErr err = fromRef(pb->ref, &n);
    int32_t index = pb->forkIterator.initialize;
    if(!err && n.dir)
        err = errFSNoMoreItems;
    if(!err && index >= 2)
        err = errFSNoMoreItems;
    if(!err)
    {
        bool rsrc = index == 1;
        FSCatalogInfo ci;
        err = catalogInfo(n, rsrc ? 0x8000 : 0x4000, &ci, nullptr, nullptr, nullptr);
        if(!err)
        {
            putUnicode(pb->outForkName, rsrc ? std::u16string(kResourceForkName) : std::u16string());
            pb->positionOffset = (int64_t)(uint64_t)(rsrc ? ci.rsrcLogicalSize : ci.dataLogicalSize);
            pb->allocationAmount = rsrc ? ci.rsrcPhysicalSize : ci.dataPhysicalSize;
            pb->forkIterator.initialize = index + 1;
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBGetForkCBInfo(FSForkCBInfoParamPtr pb, Boolean async)
{
    OSErr err = noErr;
    short ref = pb->desiredRefNum;
    if(!ref)
    {
        /* the next open path (on the volume, if one is named) */
        INTEGER index = pb->iterator;
        FCBPBRec f;
        Str255 nm;
        for(;;)
        {
            memset(&f, 0, sizeof f);
            f.ioNamePtr = nm;
            f.ioVRefNum = pb->volumeRefNum;
            f.ioFCBIndx = ++index;
            if((err = PBGetFCBInfo(&f, false)) != noErr)
            {
                err = errFSNoMoreItems;
                break;
            }
            ref = f.ioRefNum;
            break;
        }
        pb->iterator = index;
    }
    fcbrec *f = nullptr;
    if(!err)
    {
        OSErr e2;
        f = PRNTOFPERR(ref, &e2);
        if(!f || !(LONGINT)f->fdfnum)
            err = rfNumErr;
    }
    if(!err)
    {
        pb->actualRefNum = ref;
        HVCB *vcb = (HVCB *)(VCB *)f->fcvptr;
        HFSPlusVolume *hp = hfsPlusVolume(vcb);
        uint8_t flags = f->fcflags;
        uint64_t pos = (uint32_t)(LONGINT)f->fcbCrPs, eof = (uint32_t)(LONGINT)f->fcleof,
                 peof = (uint32_t)(LONGINT)f->fcPLen;
        int8_t perm = (flags & fcwriteperm) ? fsRdWrPerm : fsRdPerm;
        bool rsrc = flags & fcfisres;
        if(hp)
            try
            {
                auto st = hp->forkStatus(ref);
                pos = st.mark;
                eof = st.size;
                peof = st.physical;
                if(st.permission)
                    perm = st.permission;
                flags = (flags & ~0x80) | (st.dirty ? 0x80 : 0);
            }
            catch(OSErrorException&)
            {
            }
        if(FSForkInfoPtr fi = pb->forkInfo)
        {
            fi->flags = flags;
            fi->permissions = perm;
            fi->volume = vcb ? (short)vcb->vcbVRefNum : 0;
            fi->reserved2 = 0;
            fi->nodeID = (LONGINT)f->fdfnum;
            fi->forkID = rsrc ? 1 : 0;
            fi->currentPosition = pos;
            fi->logicalEOF = eof;
            fi->physicalEOF = peof;
            fi->process = 0;
        }
        putUnicode(pb->forkName, rsrc ? std::u16string(kResourceForkName) : std::u16string());
        if(pb->ref)
        {
            Node n;
            n.vref = vcb ? (short)vcb->vcbVRefNum : 0;
            n.vcb = vcb;
            n.hp = hp;
            if(hp)
            {
                if(auto e = hp->byID((LONGINT)f->fdfnum))
                    fillPlus(n, *e), toRef(n, pb->ref);
            }
            else
            {
                unsigned char *nm = (unsigned char *)&f->fcname;
                if(classicNode(n.vref, (LONGINT)f->fcparid, pstr(nm), &n) == noErr)
                    toRef(n, pb->ref);
            }
        }
    }
    FAKEASYNC(pb, async, err);
}

/* ---- volumes -------------------------------------------------------------- */

OSErr Executor::PBGetVolumeInfo(FSVolumeInfoParamPtr pb, Boolean async)
{
    OSErr err = noErr;
    uint32_t which = pb->whichInfo;
    HVCB *vcb = nullptr;
    if(which & ~0xFFFFu)
        err = errFSBadInfoBitmap;
    else if(pb->volumeIndex > 0)
    {
        vcb = (HVCB *)ROMlib_indexqueue(&LM(VCBQHdr), (short)(uint32_t)pb->volumeIndex);
        if(!vcb)
            err = nsvErr;
    }
    else if(!(vcb = vcbFor(pb->ioVRefNum)))
        err = nsvErr;
    if(!err)
    {
        short vref = vcb->vcbVRefNum;
        pb->ioVRefNum = vref;
        HFSPlusVolume *hp = hfsPlusVolume(vcb);
        HParamBlockRec h;
        memset(&h, 0, sizeof h);
        h.volumeParam.ioVRefNum = vref;
        {
            Bypass b;
            err = PBHGetVInfo(&h, false);
        }
        if(FSVolumeInfoPtr vi = pb->volumeInfo; vi && !err && which)
        {
            if(hp)
            {
                const hfsplus::VolumeHeader& hd = hp->reader().header();
                putUTC(vi->createDate, utcFromLocal(hd.createDate));
                putUTC(vi->modifyDate, hd.modifyDate);
                putUTC(vi->backupDate, hd.backupDate);
                putUTC(vi->checkedDate, hd.checkedDate);
                vi->fileCount = hd.fileCount;
                vi->folderCount = hd.folderCount;
                vi->totalBytes = uint64_t(hd.totalBlocks) * hd.blockSize;
                vi->freeBytes = uint64_t(hd.freeBlocks) * hd.blockSize;
                vi->blockSize = hd.blockSize;
                vi->totalBlocks = hd.totalBlocks;
                vi->freeBlocks = hd.freeBlocks;
                vi->nextAllocation = hd.nextAllocation;
                vi->rsrcClumpSize = hd.rsrcClumpSize;
                vi->dataClumpSize = hd.dataClumpSize;
                vi->nextCatalogID = hd.nextCatalogID;
                for(int i = 0; i < 8; i++)
                {
                    uint32_t v = hd.finderInfo[i];
                    for(int k = 0; k < 4; k++)
                        vi->finderInfo[4 * i + k] = uint8_t(v >> (24 - 8 * k));
                }
                vi->signature = hp->reader().isHFSX() ? 0x4858 : 0x482B;
            }
            else
            {
                putUTC(vi->createDate, utcFromLocal(h.volumeParam.ioVCrDate));
                putUTC(vi->modifyDate, utcFromLocal(h.volumeParam.ioVLsMod));
                putUTC(vi->backupDate, utcFromLocal(h.volumeParam.ioVBkUp));
                putUTC(vi->checkedDate, 0);
                vi->fileCount = (LONGINT)h.volumeParam.ioVFilCnt;
                vi->folderCount = (LONGINT)h.volumeParam.ioVDirCnt;
                uint32_t bs = (LONGINT)h.volumeParam.ioVAlBlkSiz;
                vi->totalBytes = uint64_t((uint16_t)h.volumeParam.ioVNmAlBlks) * bs;
                vi->freeBytes = uint64_t((uint16_t)h.volumeParam.ioVFrBlk) * bs;
                vi->blockSize = bs;
                vi->totalBlocks = (uint16_t)h.volumeParam.ioVNmAlBlks;
                vi->freeBlocks = (uint16_t)h.volumeParam.ioVFrBlk;
                vi->nextAllocation = (uint16_t)h.volumeParam.ioVAllocPtr;
                vi->rsrcClumpSize = (LONGINT)h.volumeParam.ioVClpSiz;
                vi->dataClumpSize = (LONGINT)h.volumeParam.ioVClpSiz;
                vi->nextCatalogID = (LONGINT)h.volumeParam.ioVNxtCNID;
                memcpy(vi->finderInfo, &h.volumeParam.ioVFndrInfo, 32);
                vi->signature = (uint16_t)h.volumeParam.ioVSigWord;
            }
            vi->flags = (uint16_t)h.volumeParam.ioVAtrb;
            vi->filesystemID = (uint16_t)h.volumeParam.ioVFSID;
            vi->driveNumber = (uint16_t)h.volumeParam.ioVDrvInfo;
            vi->driverRefNum = (int16_t)h.volumeParam.ioVDRefNum;
        }
        Node root;
        if(!err)
        {
            if(hp)
            {
                root.vref = vref;
                root.vcb = vcb;
                root.hp = hp;
                if(auto e = hp->byID(hfsplus::kRootFolderID))
                    fillPlus(root, *e);
            }
            else
                classicNode(vref, 2, "", &root);
            putUnicode(pb->volumeName, root.name);
            toRef(root, pb->ref);
        }
    }
    FAKEASYNC(pb, async, err);
}

OSErr Executor::PBSetVolumeInfo(FSVolumeInfoParamPtr pb, Boolean async)
{
    OSErr err = noErr;
    uint32_t which = pb->whichInfo;
    HVCB *vcb = vcbFor(pb->ioVRefNum);
    if(which & ~0x3004u)
        err = errFSBadInfoBitmap;
    else if(!vcb)
        err = nsvErr;
    else if(FSVolumeInfoPtr vi = pb->volumeInfo)
    {
        HParamBlockRec h;
        Bypass b;
        memset(&h, 0, sizeof h);
        h.volumeParam.ioVRefNum = vcb->vcbVRefNum;
        if((err = PBHGetVInfo(&h, false)) == noErr)
        {
            h.volumeParam.ioNamePtr = nullptr;
            if(which & 0x4)
                h.volumeParam.ioVBkUp = localFromUTC(vi->backupDate.lowSeconds);
            if(which & 0x1000)
                memcpy(&h.volumeParam.ioVFndrInfo, vi->finderInfo, 32);
            if(which & 0x2000)
                h.volumeParam.ioVAtrb = (uint16_t)vi->flags;
            err = PBSetVInfo(&h, false);
        }
    }
    FAKEASYNC(pb, async, err);
}

/* ---- the high-level calls: parameter blocks filled in ------------------------ */

OSErr Executor::C_FSpMakeFSRef(FSSpecPtr source, FSRefPtr newRef)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    if(!source)
        return paramErr;
    pb.ioNamePtr = source->name;
    pb.ioVRefNum = source->vRefNum;
    pb.ioDirID = source->parID;
    pb.newRef = newRef;
    return PBMakeFSRef(&pb, false);
}

OSErr Executor::C_FSMakeFSRefUnicode(FSRefPtr parentRef, UniCharCount nameLength, GUEST<UniChar> *name,
                                     TextEncoding textEncodingHint, FSRefPtr newRef)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = parentRef;
    pb.nameLength = nameLength;
    pb.name = name;
    pb.textEncodingHint = textEncodingHint;
    pb.newRef = newRef;
    return PBMakeFSRefUnicode(&pb, false);
}

OSErr Executor::C_FSCompareFSRefs(FSRefPtr ref1, FSRefPtr ref2)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = ref1;
    pb.parentRef = ref2;
    return PBCompareFSRefs(&pb, false);
}

OSErr Executor::C_FSCreateFileUnicode(FSRefPtr parentRef, UniCharCount nameLength, GUEST<UniChar> *name,
                                      FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfo, FSRefPtr newRef,
                                      FSSpecPtr newSpec)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = parentRef;
    pb.nameLength = nameLength;
    pb.name = name;
    pb.whichInfo = whichInfo;
    pb.catInfo = catalogInfo;
    pb.newRef = newRef;
    pb.spec = newSpec;
    return PBCreateFileUnicode(&pb, false);
}

OSErr Executor::C_FSCreateDirectoryUnicode(FSRefPtr parentRef, UniCharCount nameLength, GUEST<UniChar> *name,
                                           FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfo,
                                           FSRefPtr newRef, FSSpecPtr newSpec, GUEST<uint32_t> *newDirID)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = parentRef;
    pb.nameLength = nameLength;
    pb.name = name;
    pb.whichInfo = whichInfo;
    pb.catInfo = catalogInfo;
    pb.newRef = newRef;
    pb.spec = newSpec;
    OSErr err = PBCreateDirectoryUnicode(&pb, false);
    if(!err && newDirID)
        *newDirID = (uint32_t)(int32_t)pb.ioDirID;
    return err;
}

OSErr Executor::C_FSDeleteObject(FSRefPtr ref)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = ref;
    return PBDeleteObject(&pb, false);
}

OSErr Executor::C_FSMoveObject(FSRefPtr ref, FSRefPtr destDirectory, FSRefPtr newRef)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = ref;
    pb.parentRef = destDirectory;
    pb.newRef = newRef;
    return PBMoveObject(&pb, false);
}

OSErr Executor::C_FSRenameUnicode(FSRefPtr ref, UniCharCount nameLength, GUEST<UniChar> *name,
                                  TextEncoding textEncodingHint, FSRefPtr newRef)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = ref;
    pb.nameLength = nameLength;
    pb.name = name;
    pb.textEncodingHint = textEncodingHint;
    pb.newRef = newRef;
    return PBRenameUnicode(&pb, false);
}

OSErr Executor::C_FSExchangeObjects(FSRefPtr ref, FSRefPtr destRef)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = ref;
    pb.parentRef = destRef;
    return PBExchangeObjects(&pb, false);
}

OSErr Executor::C_FSGetCatalogInfo(FSRefPtr ref, FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfo,
                                   HFSUniStr255Ptr outName, FSSpecPtr fsSpec, FSRefPtr parentRef)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = ref;
    pb.whichInfo = whichInfo;
    pb.catInfo = catalogInfo;
    pb.outName = outName;
    pb.spec = fsSpec;
    pb.parentRef = parentRef;
    return PBGetCatalogInfo(&pb, false);
}

OSErr Executor::C_FSSetCatalogInfo(FSRefPtr ref, FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfo)
{
    FSRefParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ref = ref;
    pb.whichInfo = whichInfo;
    pb.catInfo = catalogInfo;
    return PBSetCatalogInfo(&pb, false);
}

OSErr Executor::C_FSOpenIterator(FSRefPtr container, FSIteratorFlags iteratorFlags, GUEST<FSIterator> *iterator)
{
    FSCatalogBulkParam pb;
    memset(&pb, 0, sizeof pb);
    pb.container = container;
    pb.iteratorFlags = iteratorFlags;
    OSErr err = PBOpenIterator(&pb, false);
    if(iterator)
        *iterator = err ? nullptr : (Ptr)pb.iterator;
    return err;
}

OSErr Executor::C_FSCloseIterator(FSIterator iterator)
{
    FSCatalogBulkParam pb;
    memset(&pb, 0, sizeof pb);
    pb.iterator = iterator;
    return PBCloseIterator(&pb, false);
}

static OSErr bulkCall(bool search, FSIterator iterator, FSSearchParamsPtr criteria, ItemCount maximumObjects,
                      GUEST<ItemCount> *actualObjects, Boolean *containerChanged, FSCatalogInfoBitmap whichInfo,
                      FSCatalogInfoPtr catalogInfos, FSRefPtr refs, FSSpecPtr specs, HFSUniStr255Ptr names)
{
    FSCatalogBulkParam pb;
    memset(&pb, 0, sizeof pb);
    pb.iterator = iterator;
    pb.searchParams = criteria;
    pb.maximumItems = maximumObjects;
    pb.whichInfo = whichInfo;
    pb.catalogInfo = catalogInfos;
    pb.refs = refs;
    pb.specs = specs;
    pb.names = names;
    OSErr err = search ? PBCatalogSearch(&pb, false) : PBGetCatalogInfoBulk(&pb, false);
    if(actualObjects)
        *actualObjects = pb.actualItems;
    if(containerChanged)
        *containerChanged = pb.containerChanged;
    return err;
}

OSErr Executor::C_FSGetCatalogInfoBulk(FSIterator iterator, ItemCount maximumObjects, GUEST<ItemCount> *actualObjects,
                                       Boolean *containerChanged, FSCatalogInfoBitmap whichInfo,
                                       FSCatalogInfoPtr catalogInfos, FSRefPtr refs, FSSpecPtr specs,
                                       HFSUniStr255Ptr names)
{
    return bulkCall(false, iterator, nullptr, maximumObjects, actualObjects, containerChanged, whichInfo,
                    catalogInfos, refs, specs, names);
}

OSErr Executor::C_FSCatalogSearch(FSIterator iterator, FSSearchParamsPtr searchCriteria, ItemCount maximumObjects,
                                  GUEST<ItemCount> *actualObjects, Boolean *containerChanged,
                                  FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfos, FSRefPtr refs,
                                  FSSpecPtr specs, HFSUniStr255Ptr names)
{
    return bulkCall(true, iterator, searchCriteria, maximumObjects, actualObjects, containerChanged, whichInfo,
                    catalogInfos, refs, specs, names);
}

static void forkPB(FSForkIOParam& pb, FSRefPtr ref, UniCharCount nameLength, GUEST<UniChar> *name)
{
    memset(&pb, 0, sizeof pb);
    pb.ref = ref;
    pb.forkNameLength = nameLength;
    pb.forkName = name;
}

OSErr Executor::C_FSCreateFork(FSRefPtr ref, UniCharCount forkNameLength, GUEST<UniChar> *forkName)
{
    FSForkIOParam pb;
    forkPB(pb, ref, forkNameLength, forkName);
    return PBCreateFork(&pb, false);
}

OSErr Executor::C_FSDeleteFork(FSRefPtr ref, UniCharCount forkNameLength, GUEST<UniChar> *forkName)
{
    FSForkIOParam pb;
    forkPB(pb, ref, forkNameLength, forkName);
    return PBDeleteFork(&pb, false);
}

OSErr Executor::C_FSIterateForks(FSRefPtr ref, CatPositionRec *forkIterator, HFSUniStr255Ptr forkName,
                                 GUEST<int64_t> *forkSize, GUEST<uint64_t> *forkPhysicalSize)
{
    FSForkIOParam pb;
    forkPB(pb, ref, 0, nullptr);
    if(!forkIterator)
        return paramErr;
    pb.forkIterator = *forkIterator;
    pb.outForkName = forkName;
    OSErr err = PBIterateForks(&pb, false);
    *forkIterator = pb.forkIterator;
    if(!err && forkSize)
        *forkSize = (int64_t)pb.positionOffset;
    if(!err && forkPhysicalSize)
        *forkPhysicalSize = (uint64_t)pb.allocationAmount;
    return err;
}

OSErr Executor::C_FSOpenFork(FSRefPtr ref, UniCharCount forkNameLength, GUEST<UniChar> *forkName, int8_t permissions,
                             GUEST<int16_t> *forkRefNum)
{
    FSForkIOParam pb;
    forkPB(pb, ref, forkNameLength, forkName);
    pb.permissions = permissions;
    OSErr err = PBOpenFork(&pb, false);
    if(forkRefNum)
        *forkRefNum = (int16_t)pb.forkRefNum;
    return err;
}

static int64_t wide64(const wide& w)
{
    return int64_t(int32_t(w.hi)) << 32 | uint32_t(w.lo);
}

OSErr Executor::C_FSReadFork(int16_t forkRefNum, uint16_t positionMode, wide positionOffset, ByteCount requestCount,
                             Ptr buffer, GUEST<ByteCount> *actualCount)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    pb.positionMode = positionMode;
    pb.positionOffset = wide64(positionOffset);
    pb.requestCount = requestCount;
    pb.buffer = buffer;
    OSErr err = PBReadFork(&pb, false);
    if(actualCount)
        *actualCount = (uint32_t)pb.actualCount;
    return err;
}

OSErr Executor::C_FSWriteFork(int16_t forkRefNum, uint16_t positionMode, wide positionOffset, ByteCount requestCount,
                              Ptr buffer, GUEST<ByteCount> *actualCount)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    pb.positionMode = positionMode;
    pb.positionOffset = wide64(positionOffset);
    pb.requestCount = requestCount;
    pb.buffer = buffer;
    OSErr err = PBWriteFork(&pb, false);
    if(actualCount)
        *actualCount = (uint32_t)pb.actualCount;
    return err;
}

OSErr Executor::C_FSGetForkPosition(int16_t forkRefNum, GUEST<int64_t> *position)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    OSErr err = PBGetForkPosition(&pb, false);
    if(!err && position)
        *position = (int64_t)pb.positionOffset;
    return err;
}

OSErr Executor::C_FSSetForkPosition(int16_t forkRefNum, uint16_t positionMode, wide positionOffset)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    pb.positionMode = positionMode;
    pb.positionOffset = wide64(positionOffset);
    return PBSetForkPosition(&pb, false);
}

OSErr Executor::C_FSGetForkSize(int16_t forkRefNum, GUEST<int64_t> *forkSize)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    OSErr err = PBGetForkSize(&pb, false);
    if(!err && forkSize)
        *forkSize = (int64_t)pb.positionOffset;
    return err;
}

OSErr Executor::C_FSSetForkSize(int16_t forkRefNum, uint16_t positionMode, wide positionOffset)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    pb.positionMode = positionMode;
    pb.positionOffset = wide64(positionOffset);
    return PBSetForkSize(&pb, false);
}

OSErr Executor::C_FSAllocateFork(int16_t forkRefNum, FSAllocationFlags flags, uint16_t positionMode,
                                 wide positionOffset, wide requestCount, GUEST<uint64_t> *actualCount)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    pb.allocationFlags = flags;
    pb.positionMode = positionMode;
    pb.positionOffset = wide64(positionOffset);
    pb.allocationAmount = (uint64_t)wide64(requestCount);
    OSErr err = PBAllocateFork(&pb, false);
    if(actualCount)
        *actualCount = (uint64_t)pb.allocationAmount;
    return err;
}

OSErr Executor::C_FSFlushFork(int16_t forkRefNum)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    return PBFlushFork(&pb, false);
}

OSErr Executor::C_FSCloseFork(int16_t forkRefNum)
{
    FSForkIOParam pb;
    memset(&pb, 0, sizeof pb);
    pb.forkRefNum = forkRefNum;
    return PBCloseFork(&pb, false);
}

OSErr Executor::C_FSGetForkCBInfo(int16_t desiredRefNum, FSVolumeRefNum volume, GUEST<int16_t> *iterator,
                                  GUEST<int16_t> *actualRefNum, FSForkInfoPtr forkInfo, FSRefPtr ref,
                                  HFSUniStr255Ptr outForkName)
{
    FSForkCBInfoParam pb;
    memset(&pb, 0, sizeof pb);
    pb.desiredRefNum = desiredRefNum;
    pb.volumeRefNum = volume;
    pb.iterator = iterator ? (int16_t)*iterator : 0;
    pb.forkInfo = forkInfo;
    pb.ref = ref;
    pb.forkName = outForkName;
    OSErr err = PBGetForkCBInfo(&pb, false);
    if(iterator)
        *iterator = (int16_t)pb.iterator;
    if(actualRefNum)
        *actualRefNum = (int16_t)pb.actualRefNum;
    return err;
}

OSErr Executor::C_FSGetVolumeInfo(FSVolumeRefNum volume, ItemCount volumeIndex, GUEST<FSVolumeRefNum> *actualVolume,
                                  FSVolumeInfoBitmap whichInfo, FSVolumeInfoPtr info, HFSUniStr255Ptr volumeName,
                                  FSRefPtr rootDirectory)
{
    FSVolumeInfoParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ioVRefNum = volume;
    pb.volumeIndex = volumeIndex;
    pb.whichInfo = whichInfo;
    pb.volumeInfo = info;
    pb.volumeName = volumeName;
    pb.ref = rootDirectory;
    OSErr err = PBGetVolumeInfo(&pb, false);
    if(!err && actualVolume)
        *actualVolume = (int16_t)pb.ioVRefNum;
    return err;
}

OSErr Executor::C_FSSetVolumeInfo(FSVolumeRefNum volume, FSVolumeInfoBitmap whichInfo, FSVolumeInfoPtr info)
{
    FSVolumeInfoParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ioVRefNum = volume;
    pb.whichInfo = whichInfo;
    pb.volumeInfo = info;
    return PBSetVolumeInfo(&pb, false);
}

OSErr Executor::C_FSGetDataForkName(HFSUniStr255Ptr dataForkName)
{
    if(dataForkName)
        putUnicode(dataForkName, std::u16string());
    return noErr;
}

OSErr Executor::C_FSGetResourceForkName(HFSUniStr255Ptr resourceForkName)
{
    if(resourceForkName)
        putUnicode(resourceForkName, kResourceForkName);
    return noErr;
}

/* 68K: an SInt64 comes as a pointer to it (Pascal), PowerPC passes it */
OSErr Executor::C_FSReadFork68K(int16_t forkRefNum, uint16_t positionMode, wide *positionOffset,
                                ByteCount requestCount, Ptr buffer, GUEST<ByteCount> *actualCount)
{
    return C_FSReadFork(forkRefNum, positionMode, *positionOffset, requestCount, buffer, actualCount);
}

OSErr Executor::C_FSWriteFork68K(int16_t forkRefNum, uint16_t positionMode, wide *positionOffset,
                                 ByteCount requestCount, Ptr buffer, GUEST<ByteCount> *actualCount)
{
    return C_FSWriteFork(forkRefNum, positionMode, *positionOffset, requestCount, buffer, actualCount);
}

OSErr Executor::C_FSSetForkPosition68K(int16_t forkRefNum, uint16_t positionMode, wide *positionOffset)
{
    return C_FSSetForkPosition(forkRefNum, positionMode, *positionOffset);
}

OSErr Executor::C_FSSetForkSize68K(int16_t forkRefNum, uint16_t positionMode, wide *positionOffset)
{
    return C_FSSetForkSize(forkRefNum, positionMode, *positionOffset);
}

OSErr Executor::C_FSAllocateFork68K(int16_t forkRefNum, FSAllocationFlags flags, uint16_t positionMode,
                                    wide *positionOffset, wide *requestCount, GUEST<uint64_t> *actualCount)
{
    return C_FSAllocateFork(forkRefNum, flags, positionMode, *positionOffset, *requestCount, actualCount);
}
