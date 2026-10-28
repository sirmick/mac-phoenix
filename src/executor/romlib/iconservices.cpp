/* MacPhoenix: Icon Services (IconServices.yaml), from Universal
 * Interfaces' Icons.h and Apple's documentation (docs/executor/
 * IconServices.md), checked against Apple's on Mac OS 9.0.4 by IconTest
 * (tests/guest/iconservices):
 *
 * - An IconRef is Executor's record of an icon family: its elements ('ICN#',
 *   'icl8', 'ics#', ..., 'il32', 'l8mk'; the 'icns' layout), an owner count,
 *   and what it is registered under (creator and type).
 * - GetIconRef finds a registered ref, or makes one: the System's icons
 *   (kSystemIconsCreator, from the System file's icon resources by the IDs
 *   Icons.h gives), an application's (the volume's desktop database), and
 *   for a creator/type nobody has, the generic application or document
 *   icon (no error). Getting it again is the same ref and one more owner.
 * - Registering a creator/type twice is the same ref, one more owner;
 *   unregistering makes the ref invalid at once, whoever holds it.
 *   Registered refs stay at no owners; others go away.
 * - Files: a custom icon (kHasCustomIcon: the file's resources, a folder's
 *   "Icon\r"), else by type and creator; folders the folder icon (the
 *   System Folder its own), a volume's root the hard disk icon; the label
 *   from the Finder flags.
 * - Drawing and hit testing go through Icon Utilities' icon suites.
 * - A composite holds its two parts (one owner each).
 *
 * A ref is a block of the system heap, standing for the record; a ref no
 * longer valid keeps its block (an old pointer never names a new ref). */

#include <base/common.h>
#include <IconServices.h>
#include <Iconutil.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <Finder.h>
#include <AliasMgr.h>
#include <mman/mman.h>
#include <algorithm>
#include <map>
#include <vector>

using namespace Executor;

namespace
{
/* An icon family element's type and the selector bit that names it. */
struct ElementKind
{
    uint32_t type;
    uint32_t selector;
    bool classic; /* Icon Utilities knows it (an icon suite's) */
};
const ElementKind kinds[] = {
    { "ICN#"_4, 0x00000001, true }, { "icl4"_4, 0x00000002, true }, { "icl8"_4, 0x00000004, true },
    { "il32"_4, 0x00000008, false }, { "l8mk"_4, 0x00000010, false },
    { "ics#"_4, 0x00000100, true }, { "ics4"_4, 0x00000200, true }, { "ics8"_4, 0x00000400, true },
    { "is32"_4, 0x00000800, false }, { "s8mk"_4, 0x00001000, false },
    { "icm#"_4, 0x00010000, true }, { "icm4"_4, 0x00020000, true }, { "icm8"_4, 0x00040000, true },
    { "ich#"_4, 0x01000000, false }, { "ich4"_4, 0x02000000, false }, { "ich8"_4, 0x04000000, false },
    { "ih32"_4, 0x08000000, false }, { "h8mk"_4, 0x10000000, false },
};

const ElementKind *kindOf(uint32_t type)
{
    for(const ElementKind& k : kinds)
        if(k.type == type)
            return &k;
    return nullptr;
}

struct IconRecord
{
    std::map<uint32_t, std::vector<uint8_t>> elements;
    uint32_t owners = 1;
    bool registered = false;
    uint32_t creator = 0, type = 0;
    IconRef override = nullptr;
    IconRef background = nullptr, foreground = nullptr; /* a composite */
};

std::map<IconRef, IconRecord> records;
std::map<std::pair<uint32_t, uint32_t>, IconRef> registry;
std::map<INTEGER, bool> customIconsEnabled;

IconRecord *record(IconRef ref)
{
    auto it = records.find(ref);
    return it == records.end() ? nullptr : &it->second;
}

/* What draws: the override, if there is one. */
IconRecord *drawn(IconRef ref)
{
    IconRecord *r = record(ref);
    for(int i = 0; r && r->override && i < 8; i++)
        if(IconRecord *o = record(r->override))
            r = o;
        else
            break;
    return r;
}

IconRef newRef(IconRecord rec)
{
    Ptr p;
    {
        TheZoneGuard guard(LM(SysZone));
        p = NewPtrClear(4);
    }
    if(!p)
        return nullptr;
    IconRef ref = (IconRef)p;
    records[ref] = std::move(rec);
    return ref;
}

void release(IconRef ref);

void forget(IconRef ref)
{
    IconRecord *r = record(ref);
    if(!r)
        return;
    IconRef bg = r->background, fg = r->foreground, ov = r->override;
    if(r->registered)
        registry.erase({ r->creator, r->type });
    records.erase(ref);
    if(bg)
        release(bg);
    if(fg)
        release(fg);
    if(ov)
        release(ov);
}

void release(IconRef ref)
{
    IconRecord *r = record(ref);
    if(!r)
        return;
    if(r->owners > 0)
        r->owners--;
    if(r->owners == 0 && !r->registered)
        forget(ref);
}

std::vector<uint8_t> handleBytes(Handle h)
{
    std::vector<uint8_t> out;
    if(h && *h)
    {
        Size n = GetHandleSize(h);
        out.assign((uint8_t *)*h, (uint8_t *)*h + n);
    }
    return out;
}

Handle newHandleWith(const std::vector<uint8_t>& bytes)
{
    Handle h = NewHandle(bytes.size());
    if(h && !bytes.empty())
        memcpy(*h, bytes.data(), bytes.size());
    return h;
}

/* An 'icns' family's elements into rec. */
void readFamily(IconRecord& rec, const uint8_t *p, int32_t size)
{
    if(size < 8 || p[0] != 'i' || p[1] != 'c' || p[2] != 'n' || p[3] != 's')
        return;
    int32_t total = std::min<int32_t>(size, (int32_t)(p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7]));
    for(int32_t at = 8; at + 8 <= total;)
    {
        uint32_t type = (uint32_t)p[at] << 24 | p[at + 1] << 16 | p[at + 2] << 8 | p[at + 3];
        int32_t n = p[at + 4] << 24 | p[at + 5] << 16 | p[at + 6] << 8 | p[at + 7];
        if(n < 8 || at + n > total)
            break;
        rec.elements[type].assign(p + at + 8, p + at + n);
        at += n;
    }
}

void put32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(x >> 24);
    v.push_back(x >> 16);
    v.push_back(x >> 8);
    v.push_back(x);
}

/* The elements selected, as an 'icns' family. */
std::vector<uint8_t> writeFamily(const IconRecord& rec, uint32_t which)
{
    std::vector<uint8_t> out;
    put32(out, "icns"_4);
    put32(out, 0);
    for(const ElementKind& k : kinds)
    {
        auto it = rec.elements.find(k.type);
        if(it == rec.elements.end() || !(which & k.selector))
            continue;
        put32(out, k.type);
        put32(out, it->second.size() + 8);
        out.insert(out.end(), it->second.begin(), it->second.end());
    }
    uint32_t n = out.size();
    out[4] = n >> 24;
    out[5] = n >> 16;
    out[6] = n >> 8;
    out[7] = n;
    return out;
}

/* The icon resources with id in the current resource file (Get1) or the
   chain: an 'icns', else the classic types one by one. */
bool loadResources(IconRecord& rec, INTEGER id, bool onlyThisFile)
{
    auto get = [&](ResType t) { return onlyThisFile ? Get1Resource(t, id) : GetResource(t, id); };
    if(Handle h = get("icns"_4))
    {
        LoadResource(h);
        if(*h)
            readFamily(rec, (uint8_t *)*h, GetHandleSize(h));
    }
    for(const ElementKind& k : kinds)
        if(k.classic && !rec.elements.count(k.type))
            if(Handle h = get(k.type))
            {
                LoadResource(h);
                if(*h)
                    rec.elements[k.type] = handleBytes(h);
            }
    return !rec.elements.empty();
}

/* The System's icons: type, then the resource ID (Icons.h). */
const std::pair<uint32_t, INTEGER> systemIcons[] = {
    { "docu"_4, -4000 }, { "sdoc"_4, -3985 }, { "edtf"_4, -3989 }, { "APPL"_4, -3996 },
    { "APPD"_4, -3991 }, { "fldr"_4, -3999 }, { "prvf"_4, -3994 }, { "flpy"_4, -3998 },
    { "trsh"_4, -3993 }, { "ramd"_4, -3988 }, { "cddr"_4, -3987 }, { "desk"_4, -3992 },
    { "ofld"_4, -3997 }, { "hdsk"_4, -3995 }, { "srvr"_4, -3972 }, { "suit"_4, -3970 },
    { "movr"_4, -3969 }, { "pref"_4, -3971 }, { "qery"_4, -16506 }, { "INIT"_4, -16415 },
    { "macs"_4, -3983 }, { "help"_4, -20271 }, { "amnu"_4, -3982 }, { "strt"_4, -3981 },
    { "ownd"_4, -3980 }, { "dbox"_4, -3979 }, { "shfl"_4, -3978 }, { "mntd"_4, -3977 },
    { "ctrl"_4, -3976 }, { "prnt"_4, -3975 }, { "prf "_4, -3974 }, { "extn"_4, -3973 },
    { "font"_4, -3968 }, { "ftrh"_4, -3984 },
};

IconRef registerRecord(uint32_t creator, uint32_t type, IconRecord rec)
{
    rec.registered = true;
    rec.creator = creator;
    rec.type = type;
    rec.owners = 1;
    IconRef ref = newRef(std::move(rec));
    if(ref)
        registry[{ creator, type }] = ref;
    return ref;
}

IconRef findRegistered(uint32_t creator, uint32_t type)
{
    auto it = registry.find({ creator, type });
    return it == registry.end() ? nullptr : it->second;
}

/* The System's icon of type, registered under kSystemIconsCreator. */
IconRef systemIcon(uint32_t type)
{
    if(IconRef ref = findRegistered("macs"_4, type))
    {
        record(ref)->owners++;
        return ref;
    }
    for(const auto& [t, id] : systemIcons)
        if(t == type)
        {
            IconRecord rec;
            INTEGER save = CurResFile();
            UseResFile(LM(SysMap));
            loadResources(rec, id, true);
            UseResFile(save);
            if(rec.elements.empty())
                return nullptr;
            return registerRecord("macs"_4, type, std::move(rec));
        }
    return nullptr;
}

/* An application's icon for creator/type from a volume's desktop database. */
bool desktopIcons(IconRecord& rec, INTEGER vRefNum, uint32_t creator, uint32_t type)
{
    DTPBRec pb = {};
    pb.ioVRefNum = vRefNum;
    if(PBDTGetPath(&pb) != noErr)
        return false;
    static const std::pair<int, uint32_t> sizes[] = {
        { 1, "ICN#"_4 }, { 2, "icl4"_4 }, { 3, "icl8"_4 },
        { 4, "ics#"_4 }, { 5, "ics4"_4 }, { 6, "ics8"_4 },
    };
    uint8_t buf[1024];
    for(const auto& [kind, elementType] : sizes)
    {
        pb.ioDTBuffer = (Ptr)buf;
        pb.ioDTReqCount = sizeof buf;
        pb.ioIconType = kind;
        pb.ioFileCreator = creator;
        pb.ioFileType = type;
        if(PBDTGetIcon(&pb, false) == noErr && pb.ioDTActCount > 0)
            rec.elements[elementType].assign(buf, buf + std::min<int32_t>(pb.ioDTActCount, sizeof buf));
    }
    return !rec.elements.empty();
}

IconRef iconFor(INTEGER vRefNum, uint32_t creator, uint32_t type)
{
    if(IconRef ref = findRegistered(creator, type))
    {
        record(ref)->owners++;
        return ref;
    }
    if(creator == "macs"_4)
        if(IconRef ref = systemIcon(type))
            return ref;
    IconRecord rec;
    if(creator != "macs"_4 && desktopIcons(rec, vRefNum, creator, type))
        return registerRecord(creator, type, std::move(rec));
    /* Nobody's: the generic icon. */
    return systemIcon(type == "APPL"_4 ? "APPL"_4 : "docu"_4);
}

/* A file's custom icon (kHasCustomIcon: ID kCustomIconResource, -16455). */
IconRef customIcon(FSSpec *file)
{
    INTEGER save = CurResFile();
    INTEGER rn = FSpOpenResFile(file, fsRdPerm);
    if(rn == -1)
        return nullptr;
    IconRecord rec;
    loadResources(rec, -16455, true);
    CloseResFile(rn);
    UseResFile(save);
    if(rec.elements.empty())
        return nullptr;
    return newRef(std::move(rec));
}

/* The icon suite to draw (the caller disposes of it, not its data). */
Handle suiteOf(const IconRecord& rec)
{
    GUEST<Handle> suite;
    if(C_NewIconSuite(&suite) != noErr)
        return nullptr;
    for(const auto& [type, bytes] : rec.elements)
        if(const ElementKind *k = kindOf(type); k && k->classic)
            C_AddIconToSuite(newHandleWith(bytes), suite, type);
    return suite;
}

uint16_t labelOf(uint16_t finderFlags)
{
    return (finderFlags >> 1) & 7;
}
}

OSErr Executor::C_IconServicesInit(InitBlock *initBlockPtr)
{
    return noErr;
}

void Executor::C_IconServicesTerminate()
{
}

OSErr Executor::C_IconRefToIconFamily(IconRef theIconRef, IconSelectorValue whichIcons,
                                      GUEST<IconFamilyHandle> *iconFamily)
{
    IconRecord *r = drawn(theIconRef);
    if(!r)
        return invalidIconRefErr;
    std::vector<uint8_t> bytes = writeFamily(*r, whichIcons);
    Handle h = newHandleWith(bytes);
    if(!h)
        return memFullErr;
    *iconFamily = (IconFamilyHandle)h;
    return noErr;
}

OSErr Executor::C_IconFamilyToIconSuite(IconFamilyHandle iconFamily, IconSelectorValue whichIcons,
                                        GUEST<Handle> *iconSuite)
{
    if(!iconFamily || !*iconFamily)
        return paramErr;
    IconRecord rec;
    readFamily(rec, (uint8_t *)*iconFamily, GetHandleSize((Handle)iconFamily));
    for(auto it = rec.elements.begin(); it != rec.elements.end();)
    {
        const ElementKind *k = kindOf(it->first);
        if(!k || !(whichIcons & k->selector))
            it = rec.elements.erase(it);
        else
            ++it;
    }
    Handle suite = suiteOf(rec);
    if(!suite)
        return memFullErr;
    *iconSuite = suite;
    return noErr;
}

OSErr Executor::C_IconSuiteToIconFamily(Handle iconSuite, IconSelectorValue whichIcons,
                                        GUEST<IconFamilyHandle> *iconFamily)
{
    if(!iconSuite)
        return paramErr;
    IconRecord rec;
    for(const ElementKind& k : kinds)
    {
        if(!k.classic || !(whichIcons & k.selector))
            continue;
        GUEST<Handle> data;
        if(C_GetIconFromSuite(&data, iconSuite, k.type) == noErr && data)
            rec.elements[k.type] = handleBytes(data);
    }
    Handle h = newHandleWith(writeFamily(rec, whichIcons));
    if(!h)
        return memFullErr;
    *iconFamily = (IconFamilyHandle)h;
    return noErr;
}

OSErr Executor::C_SetIconFamilyData(IconFamilyHandle iconFamily, OSType iconType, Handle h)
{
    if(!iconFamily || !*iconFamily || !h)
        return paramErr;
    IconRecord rec;
    readFamily(rec, (uint8_t *)*iconFamily, GetHandleSize((Handle)iconFamily));
    rec.elements[iconType] = handleBytes(h);
    std::vector<uint8_t> bytes = writeFamily(rec, 0xFFFFFFFF);
    /* types the selectors don't name are kept too */
    for(const auto& [type, data] : rec.elements)
        if(!kindOf(type))
        {
            put32(bytes, type);
            put32(bytes, data.size() + 8);
            bytes.insert(bytes.end(), data.begin(), data.end());
        }
    uint32_t n = bytes.size();
    bytes[4] = n >> 24; bytes[5] = n >> 16; bytes[6] = n >> 8; bytes[7] = n;
    SetHandleSize((Handle)iconFamily, n);
    if(OSErr err = MemError())
        return err;
    memcpy(*iconFamily, bytes.data(), n);
    return noErr;
}

OSErr Executor::C_GetIconFamilyData(IconFamilyHandle iconFamily, OSType iconType, Handle h)
{
    if(!iconFamily || !*iconFamily || !h)
        return paramErr;
    IconRecord rec;
    readFamily(rec, (uint8_t *)*iconFamily, GetHandleSize((Handle)iconFamily));
    auto it = rec.elements.find(iconType);
    if(it == rec.elements.end())
        return paramErr;
    SetHandleSize(h, it->second.size());
    if(OSErr err = MemError())
        return err;
    if(!it->second.empty())
        memcpy(*h, it->second.data(), it->second.size());
    return noErr;
}

OSErr Executor::C_GetIconRefOwners(IconRef theIconRef, GUEST<uint16_t> *owners)
{
    IconRecord *r = record(theIconRef);
    if(!r)
        return invalidIconRefErr;
    *owners = r->owners;
    return noErr;
}

OSErr Executor::C_AcquireIconRef(IconRef theIconRef)
{
    IconRecord *r = record(theIconRef);
    if(!r)
        return invalidIconRefErr;
    r->owners++;
    return noErr;
}

OSErr Executor::C_ReleaseIconRef(IconRef theIconRef)
{
    if(!record(theIconRef))
        return invalidIconRefErr;
    release(theIconRef);
    return noErr;
}

OSErr Executor::C_GetIconRefFromFile(const FSSpec *theFile, GUEST<IconRef> *theIconRef,
                                     GUEST<INTEGER> *theLabel)
{
    if(!theFile || !theIconRef)
        return paramErr;
    CInfoPBRec pb = {};
    Str255 name;
    memcpy(name, theFile->name, theFile->name[0] + 1);
    pb.hFileInfo.ioNamePtr = name;
    pb.hFileInfo.ioVRefNum = theFile->vRefNum;
    pb.hFileInfo.ioDirID = theFile->parID;
    pb.hFileInfo.ioFDirIndex = name[0] ? 0 : -1;
    if(OSErr err = PBGetCatInfo(&pb, false))
        return err;
    bool folder = pb.hFileInfo.ioFlAttrib & 0x10;
    uint16_t flags = folder ? (uint16_t)pb.dirInfo.ioDrUsrWds.frFlags : (uint16_t)pb.hFileInfo.ioFlFndrInfo.fdFlags;
    if(theLabel)
        *theLabel = labelOf(flags);
    IconRef ref = nullptr;
    bool custom = (flags & 0x0400) /* kHasCustomIcon */
        && !(customIconsEnabled.count(theFile->vRefNum) && !customIconsEnabled[theFile->vRefNum]);
    if(folder)
    {
        LONGINT dirID = pb.dirInfo.ioDrDirID;
        if(custom)
        {
            FSSpec icon;
            if(FSMakeFSSpec(theFile->vRefNum, dirID, (StringPtr) "\005Icon\r", &icon) == noErr)
                ref = customIcon(&icon);
        }
        if(!ref)
            ref = C_GetIconRefFromFolder(theFile->vRefNum, pb.dirInfo.ioDrParID, dirID, pb.hFileInfo.ioFlAttrib,
                                         pb.hFileInfo.ioACUser, theIconRef) == noErr
                ? (IconRef)*theIconRef : nullptr;
    }
    else
    {
        if(custom)
            ref = customIcon(const_cast<FSSpec *>(theFile));
        if(!ref)
            ref = iconFor(theFile->vRefNum, pb.hFileInfo.ioFlFndrInfo.fdCreator, pb.hFileInfo.ioFlFndrInfo.fdType);
    }
    if(!ref)
        return noSuchIconErr;
    *theIconRef = ref;
    return noErr;
}

OSErr Executor::C_GetIconRef(INTEGER vRefNum, OSType creator, OSType iconType, GUEST<IconRef> *theIconRef)
{
    if(!theIconRef)
        return paramErr;
    IconRef ref = iconFor(vRefNum, creator, iconType);
    if(!ref)
        return noSuchIconErr;
    *theIconRef = ref;
    return noErr;
}

OSErr Executor::C_GetIconRefFromFolder(INTEGER vRefNum, LONGINT parentFolderID, LONGINT folderID,
                                       SignedByte attributes, SignedByte accessPrivileges,
                                       GUEST<IconRef> *theIconRef)
{
    if(!theIconRef)
        return paramErr;
    uint32_t type = "fldr"_4;
    if(folderID == 2)
        type = "hdsk"_4; /* a volume's root */
    else
    {
        GUEST<INTEGER> vref;
        GUEST<LONGINT> dir;
        if(FindFolder(vRefNum, kSystemFolderType, false, &vref, &dir) == noErr && dir == folderID)
            type = "macs"_4;
    }
    IconRef ref = systemIcon(type);
    if(!ref)
        ref = systemIcon("fldr"_4);
    if(!ref)
        return noSuchIconErr;
    *theIconRef = ref;
    return noErr;
}

OSErr Executor::C_RegisterIconRefFromIconFamily(OSType creator, OSType iconType, IconFamilyHandle iconFamily,
                                                GUEST<IconRef> *theIconRef)
{
    if(!iconFamily || !*iconFamily || !theIconRef)
        return paramErr;
    if(IconRef ref = findRegistered(creator, iconType))
    {
        record(ref)->owners++;
        *theIconRef = ref;
        return noErr;
    }
    IconRecord rec;
    readFamily(rec, (uint8_t *)*iconFamily, GetHandleSize((Handle)iconFamily));
    IconRef ref = registerRecord(creator, iconType, std::move(rec));
    if(!ref)
        return memFullErr;
    *theIconRef = ref;
    return noErr;
}

OSErr Executor::C_RegisterIconRefFromResource(OSType creator, OSType iconType, const FSSpec *resourceFile,
                                              INTEGER resourceID, GUEST<IconRef> *theIconRef)
{
    if(!resourceFile || !theIconRef)
        return paramErr;
    if(IconRef ref = findRegistered(creator, iconType))
    {
        record(ref)->owners++;
        *theIconRef = ref;
        return noErr;
    }
    INTEGER save = CurResFile();
    INTEGER rn = FSpOpenResFile(const_cast<FSSpec *>(resourceFile), fsRdPerm);
    if(rn == -1)
        return ResError() ? ResError() : fnfErr;
    IconRecord rec;
    loadResources(rec, resourceID, true);
    CloseResFile(rn);
    UseResFile(save);
    if(rec.elements.empty())
        return noSuchIconErr;
    IconRef ref = registerRecord(creator, iconType, std::move(rec));
    if(!ref)
        return memFullErr;
    *theIconRef = ref;
    return noErr;
}

OSErr Executor::C_UnregisterIconRef(OSType creator, OSType iconType)
{
    IconRef ref = findRegistered(creator, iconType);
    if(!ref)
        return noSuchIconErr;
    forget(ref);
    return noErr;
}

OSErr Executor::C_UpdateIconRef(IconRef theIconRef)
{
    return record(theIconRef) ? noErr : invalidIconRefErr;
}

OSErr Executor::C_OverrideIconRefFromResource(IconRef theIconRef, const FSSpec *resourceFile, INTEGER resourceID)
{
    IconRecord *r = record(theIconRef);
    if(!r || !resourceFile)
        return r ? paramErr : invalidIconRefErr;
    INTEGER save = CurResFile();
    INTEGER rn = FSpOpenResFile(const_cast<FSSpec *>(resourceFile), fsRdPerm);
    if(rn == -1)
        return ResError() ? ResError() : fnfErr;
    IconRecord rec;
    loadResources(rec, resourceID, true);
    CloseResFile(rn);
    UseResFile(save);
    if(rec.elements.empty())
        return noSuchIconErr;
    IconRef over = newRef(std::move(rec));
    if(!over)
        return memFullErr;
    r = record(theIconRef);
    if(r->override)
        release(r->override);
    r->override = over;
    return noErr;
}

OSErr Executor::C_OverrideIconRef(IconRef oldIconRef, IconRef newIconRef)
{
    IconRecord *r = record(oldIconRef);
    if(!r || !record(newIconRef))
        return invalidIconRefErr;
    record(newIconRef)->owners++;
    if(r->override)
        release(r->override);
    record(oldIconRef)->override = newIconRef;
    return noErr;
}

OSErr Executor::C_RemoveIconRefOverride(IconRef theIconRef)
{
    IconRecord *r = record(theIconRef);
    if(!r)
        return invalidIconRefErr;
    if(IconRef over = r->override)
    {
        r->override = nullptr;
        release(over);
    }
    return noErr;
}

OSErr Executor::C_CompositeIconRef(IconRef backgroundIconRef, IconRef foregroundIconRef,
                                   GUEST<IconRef> *compositeIconRef)
{
    if(!record(backgroundIconRef) || !record(foregroundIconRef))
        return invalidIconRefErr;
    if(!compositeIconRef)
        return paramErr;
    IconRecord rec;
    rec.background = backgroundIconRef;
    rec.foreground = foregroundIconRef;
    IconRef ref = newRef(std::move(rec));
    if(!ref)
        return memFullErr;
    record(backgroundIconRef)->owners++;
    record(foregroundIconRef)->owners++;
    *compositeIconRef = ref;
    return noErr;
}

OSErr Executor::C_IsIconRefComposite(IconRef compositeIconRef, GUEST<IconRef> *backgroundIconRef,
                                     GUEST<IconRef> *foregroundIconRef)
{
    IconRecord *r = record(compositeIconRef);
    if(!r)
        return invalidIconRefErr;
    if(backgroundIconRef)
        *backgroundIconRef = r->background;
    if(foregroundIconRef)
        *foregroundIconRef = r->foreground;
    return noErr;
}

Boolean Executor::C_IsValidIconRef(IconRef theIconRef)
{
    return record(theIconRef) != nullptr;
}

/* Draws ref's family (a composite: its background, then its foreground). */
static OSErr plot(const Rect *theRect, IconAlignmentType align, IconTransformType transform, IconRef ref, int depth = 0)
{
    IconRecord *r = drawn(ref);
    if(!r)
        return invalidIconRefErr;
    if(r->background || r->foreground)
    {
        if(depth > 4)
            return noErr;
        IconRef bg = r->background, fg = r->foreground;
        if(bg)
            plot(theRect, align, transform, bg, depth + 1);
        if(fg)
            plot(theRect, align, transform, fg, depth + 1);
        return noErr;
    }
    Handle suite = suiteOf(*r);
    if(!suite)
        return memFullErr;
    OSErr err = C_PlotIconSuite(theRect, align, transform, suite);
    C_DisposeIconSuite(suite, true);
    return err;
}

OSErr Executor::C_PlotIconRef(const Rect *theRect, IconAlignmentType align, IconTransformType transform,
                              IconServicesUsageFlags theIconServicesUsageFlags, IconRef theIconRef)
{
    return plot(theRect, align, transform, theIconRef);
}

/* The family to hit-test (a composite: its background's, which is under
   the other). */
static IconRecord *shape(IconRef ref)
{
    IconRecord *r = drawn(ref);
    for(int i = 0; r && (r->background || r->foreground) && i < 4; i++)
        r = drawn(r->background ? r->background : r->foreground);
    return r;
}

Boolean Executor::C_PtInIconRef(const GUEST<Point> *testPt, const Rect *iconRect, IconAlignmentType align,
                                IconServicesUsageFlags theIconServicesUsageFlags, IconRef theIconRef)
{
    IconRecord *r = shape(theIconRef);
    if(!r || !testPt)
        return false;
    Handle suite = suiteOf(*r);
    if(!suite)
        return false;
    Point pt = *testPt;
    Boolean in = C_PtInIconSuite(pt, iconRect, align, suite);
    C_DisposeIconSuite(suite, true);
    return in;
}

Boolean Executor::C_RectInIconRef(const Rect *testRect, const Rect *iconRect, IconAlignmentType align,
                                  IconServicesUsageFlags iconServicesUsageFlags, IconRef theIconRef)
{
    IconRecord *r = shape(theIconRef);
    if(!r || !testRect)
        return false;
    Handle suite = suiteOf(*r);
    if(!suite)
        return false;
    Boolean in = C_RectInIconSuite(testRect, iconRect, align, suite);
    C_DisposeIconSuite(suite, true);
    return in;
}

OSErr Executor::C_IconRefToRgn(RgnHandle theRgn, const Rect *iconRect, IconAlignmentType align,
                               IconServicesUsageFlags iconServicesUsageFlags, IconRef theIconRef)
{
    IconRecord *r = shape(theIconRef);
    if(!r)
        return invalidIconRefErr;
    Handle suite = suiteOf(*r);
    if(!suite)
        return memFullErr;
    OSErr err = C_IconSuiteToRgn(theRgn, iconRect, align, suite);
    C_DisposeIconSuite(suite, true);
    return err;
}

OSErr Executor::C_GetIconSizesFromIconRef(IconSelectorValue iconSelectorInput,
                                          GUEST<IconSelectorValue> *iconSelectorOutputPtr,
                                          IconServicesUsageFlags iconServicesUsageFlags, IconRef theIconRef)
{
    IconRecord *r = shape(theIconRef);
    if(!r)
        return invalidIconRefErr;
    uint32_t have = 0;
    for(const auto& [type, bytes] : r->elements)
        if(const ElementKind *k = kindOf(type))
            have |= k->selector;
    if(iconSelectorOutputPtr)
        *iconSelectorOutputPtr = have & iconSelectorInput;
    return noErr;
}

OSErr Executor::C_FlushIconRefs(OSType creator, OSType iconType)
{
    return noErr;
}

OSErr Executor::C_FlushIconRefsByVolume(INTEGER vRefNum)
{
    return noErr;
}

OSErr Executor::C_SetCustomIconsEnabled(INTEGER vRefNum, Boolean enableCustomIcons)
{
    customIconsEnabled[vRefNum] = enableCustomIcons;
    return noErr;
}

OSErr Executor::C_GetCustomIconsEnabled(INTEGER vRefNum, Boolean *customIconsEnabledPtr)
{
    if(!customIconsEnabledPtr)
        return paramErr;
    auto it = customIconsEnabled.find(vRefNum);
    *customIconsEnabledPtr = it == customIconsEnabled.end() || it->second;
    return noErr;
}

/* Empty: no large mask, or one with no bit set. */
Boolean Executor::C_IsIconRefMaskEmpty(IconRef iconRef)
{
    IconRecord *r = shape(iconRef);
    if(!r)
        return true;
    auto it = r->elements.find("ICN#"_4);
    if(it == r->elements.end() || it->second.size() < 256)
        return true;
    return std::all_of(it->second.begin() + 128, it->second.begin() + 256, [](uint8_t b) { return b == 0; });
}

IconRef Executor::C_GetIconRefVariant(IconRef inIconRef, OSType inVariant, GUEST<IconTransformType> *outTransform)
{
    if(outTransform)
        *outTransform = 0;
    return inIconRef;
}

/* An 'icns' file: the family in the data fork. */
OSErr Executor::C_ReadIconFile(const FSSpec *iconFile, GUEST<IconFamilyHandle> *iconFamily)
{
    if(!iconFile || !iconFamily)
        return paramErr;
    GUEST<INTEGER> rn;
    if(OSErr err = FSpOpenDF(const_cast<FSSpec *>(iconFile), fsRdPerm, &rn))
        return err;
    GUEST<LONGINT> eof;
    GetEOF(rn, &eof);
    Handle h = NewHandle(eof);
    if(!h)
    {
        FSClose(rn);
        return memFullErr;
    }
    GUEST<LONGINT> count = eof;
    OSErr err = FSRead(rn, &count, *h);
    FSClose(rn);
    if(err && err != eofErr)
    {
        DisposeHandle(h);
        return err;
    }
    *iconFamily = (IconFamilyHandle)h;
    return noErr;
}

OSErr Executor::C_WriteIconFile(IconFamilyHandle iconFamily, const FSSpec *iconFile)
{
    if(!iconFamily || !iconFile)
        return paramErr;
    GUEST<INTEGER> rn;
    OSErr err = FSpOpenDF(const_cast<FSSpec *>(iconFile), fsRdWrPerm, &rn);
    if(err == fnfErr)
    {
        FSpCreate(const_cast<FSSpec *>(iconFile), "????"_4, "icns"_4, 0);
        err = FSpOpenDF(const_cast<FSSpec *>(iconFile), fsRdWrPerm, &rn);
    }
    if(err)
        return err;
    GUEST<LONGINT> count = GetHandleSize((Handle)iconFamily);
    err = FSWrite(rn, &count, *iconFamily);
    SetEOF(rn, count);
    FSClose(rn);
    return err;
}

OSErr Executor::C_RegisterIconRefFromIconFile(OSType creator, OSType iconType, const FSSpec *iconFile,
                                              GUEST<IconRef> *theIconRef)
{
    GUEST<IconFamilyHandle> fam;
    if(OSErr err = C_ReadIconFile(iconFile, &fam))
        return err;
    OSErr err = C_RegisterIconRefFromIconFamily(creator, iconType, fam, theIconRef);
    DisposeHandle((Handle)(IconFamilyHandle)fam);
    return err;
}
