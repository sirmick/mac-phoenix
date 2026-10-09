#include <FileMgr.h>
#include <MemoryMgr.h>
#include <error/error.h>
#include <file/file.h>

using namespace Executor;

OSErr Executor::PBHGetLogInInfo(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHGetDirAccess(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHCopyFile(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHMapName(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHMapID(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHSetDirAccess(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHMoveRename(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

/* MacPhoenix: multiversal has no FIDParam; its fields as Inside Macintosh
   lays them out after the I/O header. */
OSErr Executor::PBExchangeFiles(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;
    char *p = (char *)pb;
    FSSpec src, dst;

    StringPtr src_name = pb->ioParam.ioNamePtr;
    StringPtr dst_name = *(GUEST<StringPtr> *)(p + 28);   /* ioDestNamePtr */
    LONGINT dst_dir = *(GUEST<LONGINT> *)(p + 36);        /* ioDestDirID */
    LONGINT src_dir = *(GUEST<LONGINT> *)(p + 48);        /* ioSrcDirID */

    retval = FSMakeFSSpec(pb->ioParam.ioVRefNum, src_dir, src_name, &src);
    if(retval == noErr)
        retval = FSMakeFSSpec(pb->ioParam.ioVRefNum, dst_dir, dst_name, &dst);
    if(retval == noErr)
        retval = FSpExchangeFiles(&src, &dst);
    FAKEASYNC(pb, async, retval);
}

OSErr Executor::PBCatSearch(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    FAKEASYNC(pb, async, retval);
}

/* MacPhoenix: file ID references (IM Files 2-...). A file's ID is its
   catalog number (what PBGetCatInfo returns in ioDirID for a file), so
   creating one only looks the file up; resolving one finds the file with
   that number again. AppleScript registers its scripting additions by file
   ID and gives up on them when PBCreateFileIDRef fails.

   FIDParam: ioNamePtr 18, ioVRefNum 22, ioSrcDirID 48, ioFileID 54. */
namespace
{
enum
{
    fidNotFound = -1300,
    fidExists = -1301,
    notAFileErr = -1302,
};

GUEST<int32_t> &fid_src_dir(ParmBlkPtr pb) { return *(GUEST<int32_t> *)((char *)pb + 48); }
GUEST<int32_t> &fid_file_id(ParmBlkPtr pb) { return *(GUEST<int32_t> *)((char *)pb + 54); }

struct FileIDKey
{
    INTEGER vref;
    int32_t id;
    bool operator<(const FileIDKey &o) const { return vref != o.vref ? vref < o.vref : id < o.id; }
};
struct FileIDPlace
{
    int32_t parID;
    std::string name; /* Pascal bytes, no length */
};
std::map<FileIDKey, FileIDPlace> file_ids; /* the ones created this run */

/* Catalog info of name in dir (a guest-addressable block). */
OSErr cat_info(INTEGER vref, int32_t dir, ConstStringPtr name, CInfoPBRec *cpb, StringPtr namebuf)
{
    memset(cpb, 0, sizeof *cpb);
    memcpy(namebuf, name, name[0] + 1);
    cpb->hFileInfo.ioNamePtr = namebuf;
    cpb->hFileInfo.ioVRefNum = vref;
    cpb->hFileInfo.ioDirID = dir;
    cpb->hFileInfo.ioFDirIndex = 0;
    return PBGetCatInfo(cpb, false);
}

/* The file numbered id on vref: (parent, name) by walking its catalog. */
bool find_file_id(INTEGER vref, int32_t id, FileIDPlace &out)
{
    struct Block
    {
        CInfoPBRec cpb;
        Str255 name;
    };
    Block *b = (Block *)NewPtrSysClear(sizeof(Block));
    if(!b)
        return false;
    bool found = false;
    std::vector<int32_t> dirs = { 2 };
    while(!dirs.empty() && !found)
    {
        int32_t dir = dirs.back();
        dirs.pop_back();
        for(INTEGER i = 1; !found; i++)
        {
            memset(&b->cpb, 0, sizeof b->cpb);
            b->cpb.hFileInfo.ioNamePtr = b->name;
            b->cpb.hFileInfo.ioVRefNum = vref;
            b->cpb.hFileInfo.ioDirID = dir;
            b->cpb.hFileInfo.ioFDirIndex = i;
            if(PBGetCatInfo(&b->cpb, false) != noErr)
                break;
            if(b->cpb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
                dirs.push_back(b->cpb.dirInfo.ioDrDirID);
            else if(b->cpb.hFileInfo.ioDirID == id)
            {
                out.parID = dir;
                out.name.assign((const char *)b->name + 1, b->name[0]);
                found = true;
            }
        }
    }
    DisposePtr((Ptr)b);
    return found;
}
}

OSErr Executor::PBCreateFileIDRef(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;
    struct Block
    {
        CInfoPBRec cpb;
        Str255 name;
    };
    Block *b = (Block *)NewPtrSysClear(sizeof(Block));
    ConstStringPtr name = pb->ioParam.ioNamePtr;
    if(!b)
        retval = MemError();
    else if(!name)
        retval = paramErr;
    else
    {
        INTEGER vref = pb->ioParam.ioVRefNum;
        retval = cat_info(vref, fid_src_dir(pb), name, &b->cpb, b->name);
        if(retval == noErr && (b->cpb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR))
            retval = notAFileErr;
        if(retval == noErr)
        {
            int32_t id = b->cpb.hFileInfo.ioDirID;
            fid_file_id(pb) = id;
            /* The volume's own reference number, for later lookups. */
            HParamBlockRec *vpb = (HParamBlockRec *)NewPtrSysClear(sizeof(HParamBlockRec));
            INTEGER vol = vref;
            if(vpb)
            {
                vpb->volumeParam.ioVRefNum = vref;
                if(PBHGetVInfo(vpb, false) == noErr)
                    vol = vpb->volumeParam.ioVRefNum;
                DisposePtr((Ptr)vpb);
            }
            const unsigned char *leaf = b->name; /* GetCatInfo leaves the leaf name */
            file_ids[{ vol, id }] = { b->cpb.hFileInfo.ioFlParID,
                                      std::string((const char *)leaf + 1, leaf[0]) };
        }
    }
    if(b)
        DisposePtr((Ptr)b);
    FAKEASYNC(pb, async, retval);
}

OSErr Executor::PBDeleteFileIDRef(ParmBlkPtr pb, Boolean async)
{
    /* IDs are catalog numbers; there is nothing to remove. */
    OSErr retval = noErr;
    FAKEASYNC(pb, async, retval);
}

OSErr Executor::PBResolveFileIDRef(ParmBlkPtr pb, Boolean async)
{
    OSErr retval = fidNotFound;
    INTEGER vref = pb->ioParam.ioVRefNum;
    HParamBlockRec *vpb = (HParamBlockRec *)NewPtrSysClear(sizeof(HParamBlockRec));
    if(vpb)
    {
        vpb->volumeParam.ioVRefNum = vref;
        if(PBHGetVInfo(vpb, false) == noErr)
            vref = vpb->volumeParam.ioVRefNum;
        DisposePtr((Ptr)vpb);
    }
    int32_t id = fid_file_id(pb);
    FileIDPlace place;
    auto it = file_ids.find({ vref, id });
    bool found = false;
    if(it != file_ids.end())
    {
        /* Still there under that name? (it may have been renamed/moved) */
        struct Block
        {
            CInfoPBRec cpb;
            Str255 name;
            Str255 query;
        };
        Block *b = (Block *)NewPtrSysClear(sizeof(Block));
        if(b)
        {
            b->query[0] = std::min<size_t>(it->second.name.size(), 255);
            memcpy(b->query + 1, it->second.name.data(), b->query[0]);
            if(cat_info(vref, it->second.parID, b->query, &b->cpb, b->name) == noErr
               && b->cpb.hFileInfo.ioDirID == id)
            {
                place = it->second;
                found = true;
            }
            DisposePtr((Ptr)b);
        }
    }
    if(!found)
        found = find_file_id(vref, id, place);
    if(found)
    {
        file_ids[{ vref, id }] = place;
        if(StringPtr out = pb->ioParam.ioNamePtr)
        {
            out[0] = std::min<size_t>(place.name.size(), 255);
            memcpy(out + 1, place.name.data(), out[0]);
        }
        fid_src_dir(pb) = place.parID;
        retval = noErr;
    }
    FAKEASYNC(pb, async, retval);
}
