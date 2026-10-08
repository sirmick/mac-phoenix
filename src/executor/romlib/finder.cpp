/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* The Desktop Manager (IM VI 9-44..9-60), MacPhoenix implementation.
 *
 * One database per volume, opened by PBDTGetPath/PBDTOpenInform; its
 * reference number is -vRefNum. On a host-folder volume it lives in the
 * hidden file ".desktopdb" at the volume root and is written by PBDTFlush
 * and PBDTCloseDown; other volumes get an in-memory database. Finder fills
 * an empty database itself (it walks the volume adding every application's
 * bundle), so all this keeps is what it was given. */

#include <base/common.h>

#include <Finder.h>
#include <FileMgr.h>

#include <file/file.h>
#include <hfs/hfs.h>
#include <file/localvolume/localvolume.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Executor;

namespace
{
enum
{
    afpItemNotFound = -5012,
    afpIconTypeError = -5030,
};

struct DTIcon
{
    uint32_t creator, type, tag;
    int8_t iconType;
    std::vector<uint8_t> data;
};

struct DTAppl
{
    uint32_t creator, crDate;
    int32_t parID;
    std::string name; // MacRoman, no length byte
};

struct DesktopDB
{
    short vRefNum = 0;
    fs::path file; // empty: not persisted
    bool existed = false;
    bool dirty = false;
    std::vector<DTIcon> icons;
    std::vector<DTAppl> appls;
    std::map<int32_t, std::string> comments; // by CNID

    void load();
    void save();
    void clear()
    {
        icons.clear();
        appls.clear();
        comments.clear();
        dirty = true;
    }
};

std::map<short, DesktopDB> databases; // by vRefNum

/* Host-only file, so host byte order; the magic catches anything else. */
const char kMagic[8] = { 'X', 'D', 'T', 'D', 'B', '0', '0', '1' };

template<class T> void put(std::ostream& o, T v) { o.write((const char *)&v, sizeof v); }
template<class T> bool get(std::istream& i, T& v) { return (bool)i.read((char *)&v, sizeof v); }

void putString(std::ostream& o, const std::string& s)
{
    put<uint32_t>(o, s.size());
    o.write(s.data(), s.size());
}

bool getString(std::istream& i, std::string& s)
{
    uint32_t n;
    if(!get(i, n) || n > 0x10000)
        return false;
    s.resize(n);
    return (bool)i.read(s.data(), n);
}

void DesktopDB::load()
{
    std::ifstream in(file.string(), std::ios::binary);
    if(!in)
        return;
    existed = true;
    char magic[8];
    uint32_t n;
    if(!in.read(magic, sizeof magic) || memcmp(magic, kMagic, sizeof magic))
        return;
    std::vector<DTIcon> ic;
    std::vector<DTAppl> ap;
    std::map<int32_t, std::string> co;
    if(!get(in, n))
        return;
    while(n--)
    {
        DTIcon e;
        uint32_t len;
        if(!get(in, e.creator) || !get(in, e.type) || !get(in, e.tag)
           || !get(in, e.iconType) || !get(in, len) || len > 0x10000)
            return;
        e.data.resize(len);
        if(!in.read((char *)e.data.data(), len))
            return;
        ic.push_back(std::move(e));
    }
    if(!get(in, n))
        return;
    while(n--)
    {
        DTAppl a;
        if(!get(in, a.creator) || !get(in, a.crDate) || !get(in, a.parID)
           || !getString(in, a.name))
            return;
        ap.push_back(std::move(a));
    }
    if(!get(in, n))
        return;
    while(n--)
    {
        int32_t cnid;
        std::string c;
        if(!get(in, cnid) || !getString(in, c))
            return;
        co[cnid] = std::move(c);
    }
    icons = std::move(ic);
    appls = std::move(ap);
    comments = std::move(co);
}

void DesktopDB::save()
{
    dirty = false;
    if(file.empty())
        return;
    fs::path tmp = file;
    tmp += ".new";
    {
        std::ofstream out(tmp.string(), std::ios::binary | std::ios::trunc);
        if(!out)
            return;
        out.write(kMagic, sizeof kMagic);
        put<uint32_t>(out, icons.size());
        for(const auto& e : icons)
        {
            put(out, e.creator);
            put(out, e.type);
            put(out, e.tag);
            put(out, e.iconType);
            put<uint32_t>(out, e.data.size());
            out.write((const char *)e.data.data(), e.data.size());
        }
        put<uint32_t>(out, appls.size());
        for(const auto& a : appls)
        {
            put(out, a.creator);
            put(out, a.crDate);
            put(out, a.parID);
            putString(out, a.name);
        }
        put<uint32_t>(out, comments.size());
        for(const auto& [cnid, c] : comments)
        {
            put(out, cnid);
            putString(out, c);
        }
        if(!out)
            return;
    }
    boost::system::error_code ec;
    fs::rename(tmp, file, ec);
}

/* The volume named by ioNamePtr/ioVRefNum; opens (or creates) its database. */
DesktopDB *openDB(DTPBPtr dtp, OSErr& err)
{
    LONGINT dir;
    HVCB *vcbp = ROMlib_findvcb(dtp->ioVRefNum, dtp->ioNamePtr, &dir, false);
    if(!vcbp)
    {
        err = nsvErr;
        return nullptr;
    }
    short vref = vcbp->vcbVRefNum;
    auto [it, created] = databases.try_emplace(vref);
    DesktopDB& db = it->second;
    if(created)
    {
        db.vRefNum = vref;
        if(auto *lv = dynamic_cast<LocalVolume *>(((VCBExtra *)vcbp)->volume))
        {
            db.file = lv->getRoot() / ".desktopdb";
            db.load();
        }
    }
    err = noErr;
    return &db;
}

DesktopDB *findDB(DTPBPtr dtp, OSErr& err)
{
    auto it = databases.find(-dtp->ioDTRefNum);
    if(dtp->ioDTRefNum <= 0 || it == databases.end())
    {
        err = rfNumErr;
        return nullptr;
    }
    err = noErr;
    return &it->second;
}

std::string pstring(ConstStringPtr p)
{
    return p ? std::string((const char *)p + 1, p[0]) : std::string();
}

/* Catalog info for ioNamePtr in ioDirID (the directory itself if no name). */
OSErr catInfo(DesktopDB& db, DTPBPtr dtp, CInfoPBRec& cpb)
{
    Str255 name;
    memset(&cpb, 0, sizeof cpb);
    if(dtp->ioNamePtr && dtp->ioNamePtr[0])
    {
        memcpy(name, dtp->ioNamePtr, dtp->ioNamePtr[0] + 1);
        cpb.hFileInfo.ioNamePtr = name;
        cpb.hFileInfo.ioFDirIndex = 0;
    }
    else
    {
        cpb.hFileInfo.ioNamePtr = nullptr;
        cpb.hFileInfo.ioFDirIndex = -1;
    }
    cpb.hFileInfo.ioVRefNum = db.vRefNum;
    cpb.hFileInfo.ioDirID = dtp->ioDirID;
    return PBGetCatInfo(&cpb, false);
}

OSErr finish(DTPBPtr dtp, OSErr err)
{
    dtp->ioResult = err;
    return err;
}
}

OSErr
Executor::PBDTGetPath(DTPBPtr dtp)
{
    OSErr err;
    DesktopDB *db = openDB(dtp, err);
    dtp->ioDTRefNum = db ? -db->vRefNum : 0;
    return finish(dtp, err);
}

OSErr
Executor::PBDTOpenInform(DTPBPtr dtp)
{
    OSErr err;
    DesktopDB *db = openDB(dtp, err);
    dtp->ioDTRefNum = db ? -db->vRefNum : 0;
    if(db)
        /* low bit 0: just created (empty), so Finder rebuilds it */
        dtp->ioTagInfo = db->existed ? 1 : 0;
    return finish(dtp, err);
}

OSErr
Executor::PBDTCloseDown(DTPBPtr dtp)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        if(db->dirty)
            db->save();
        databases.erase(db->vRefNum);
    }
    return finish(dtp, err);
}

OSErr
Executor::PBDTGetIcon(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        auto it = std::find_if(db->icons.begin(), db->icons.end(), [&](const DTIcon& e) {
            return e.creator == dtp->ioFileCreator && e.type == dtp->ioFileType
                && e.iconType == dtp->ioIconType;
        });
        if(it == db->icons.end())
            err = afpItemNotFound;
        else
        {
            size_t n = std::min<size_t>(it->data.size(), std::max<int32_t>(dtp->ioDTReqCount, 0));
            if(n)
                memcpy(dtp->ioDTBuffer, it->data.data(), n);
            dtp->ioDTActCount = it->data.size();
            dtp->ioTagInfo = it->tag;
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTGetIconInfo(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        int index = dtp->ioIndex;
        err = afpItemNotFound;
        for(const auto& e : db->icons)
            if(e.creator == dtp->ioFileCreator && --index == 0)
            {
                dtp->ioIconType = e.iconType;
                dtp->ioFileType = e.type;
                dtp->ioDTActCount = e.data.size();
                dtp->ioTagInfo = e.tag;
                err = noErr;
                break;
            }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTGetAPPL(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        const DTAppl *found = nullptr;
        int index = dtp->ioIndex;
        for(const auto& a : db->appls)
        {
            if(a.creator != dtp->ioFileCreator)
                continue;
            if(index == 0)
            {
                /* the "first choice": the most recently created copy */
                if(!found || a.crDate > found->crDate)
                    found = &a;
            }
            else if(--index == 0)
            {
                found = &a;
                break;
            }
        }
        if(!found)
            err = afpItemNotFound;
        else
        {
            if(dtp->ioNamePtr)
            {
                dtp->ioNamePtr[0] = std::min<size_t>(found->name.size(), 255);
                memcpy(dtp->ioNamePtr + 1, found->name.data(), dtp->ioNamePtr[0]);
            }
            dtp->ioAPPLParID = found->parID;
            dtp->ioTagInfo = found->crDate;
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTGetComment(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        CInfoPBRec cpb;
        err = catInfo(*db, dtp, cpb);
        if(err == noErr)
        {
            auto it = db->comments.find(cpb.dirInfo.ioDrDirID);
            if(it == db->comments.end())
                err = afpItemNotFound;
            else
            {
                size_t n = std::min<size_t>(it->second.size(), 200);
                memcpy(dtp->ioDTBuffer, it->second.data(), n);
                dtp->ioDTActCount = n;
            }
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTAddIcon(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        size_t n = std::max<int32_t>(dtp->ioDTReqCount, 0);
        const uint8_t *p = (const uint8_t *)(Ptr)dtp->ioDTBuffer;
        auto it = std::find_if(db->icons.begin(), db->icons.end(), [&](const DTIcon& e) {
            return e.creator == dtp->ioFileCreator && e.type == dtp->ioFileType
                && e.iconType == dtp->ioIconType;
        });
        if(it != db->icons.end() && it->data.size() != n)
            err = afpIconTypeError;
        else
        {
            if(it == db->icons.end())
            {
                db->icons.push_back({ (uint32_t)dtp->ioFileCreator, (uint32_t)dtp->ioFileType, 0,
                                      dtp->ioIconType, {} });
                it = db->icons.end() - 1;
            }
            it->tag = dtp->ioTagInfo;
            it->data.assign(p, p + n);
            db->dirty = true;
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTAddAPPL(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        CInfoPBRec cpb;
        err = catInfo(*db, dtp, cpb);
        if(err == noErr)
        {
            std::string name = pstring(dtp->ioNamePtr);
            int32_t parID = dtp->ioDirID;
            auto& v = db->appls;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const DTAppl& a) {
                        return a.creator == dtp->ioFileCreator && a.parID == parID
                            && a.name == name;
                    }),
                    v.end());
            v.push_back({ (uint32_t)dtp->ioFileCreator, (uint32_t)cpb.hFileInfo.ioFlCrDat, parID, name });
            db->dirty = true;
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTSetComment(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        CInfoPBRec cpb;
        err = catInfo(*db, dtp, cpb);
        if(err == noErr)
        {
            size_t n = std::clamp<int32_t>(dtp->ioDTReqCount, 0, 200);
            const char *p = (const char *)(Ptr)dtp->ioDTBuffer;
            db->comments[cpb.dirInfo.ioDrDirID].assign(p, n);
            db->dirty = true;
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTRemoveAPPL(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        std::string name = pstring(dtp->ioNamePtr);
        auto& v = db->appls;
        auto end = std::remove_if(v.begin(), v.end(), [&](const DTAppl& a) {
            return a.creator == dtp->ioFileCreator && a.parID == dtp->ioDirID
                && a.name == name;
        });
        if(end == v.end())
            err = afpItemNotFound;
        else
        {
            v.erase(end, v.end());
            db->dirty = true;
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTRemoveComment(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        CInfoPBRec cpb;
        err = catInfo(*db, dtp, cpb);
        if(err == noErr)
        {
            if(db->comments.erase(cpb.dirInfo.ioDrDirID))
                db->dirty = true;
            else
                err = afpItemNotFound;
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTFlush(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        if(db->dirty)
            db->save();
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTGetInfo(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
    {
        boost::system::error_code ec;
        auto size = db->file.empty() ? 0 : fs::file_size(db->file, ec);
        if(ec)
            size = 0;
        dtp->ioVRefNum = db->vRefNum;
        dtp->ioDirID = 2; /* at the volume root */
        dtp->ioDTLgLen = size;
        dtp->ioDTPyLen = (size + 511) & ~511;
    }
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTReset(DTPBPtr dtp, Boolean async)
{
    OSErr err;
    if(DesktopDB *db = findDB(dtp, err))
        db->clear();
    FAKEASYNC(dtp, async, err);
    return err;
}

OSErr
Executor::PBDTDelete(DTPBPtr dtp, Boolean async)
{
    /* by volume (ioNamePtr/ioVRefNum), and only when it isn't open */
    OSErr err;
    LONGINT dir;
    HVCB *vcbp = ROMlib_findvcb(dtp->ioVRefNum, dtp->ioNamePtr, &dir, false);
    if(!vcbp)
        err = nsvErr;
    else if(databases.count(vcbp->vcbVRefNum))
        err = fBsyErr;
    else
    {
        err = noErr;
        if(auto *lv = dynamic_cast<LocalVolume *>(((VCBExtra *)vcbp)->volume))
        {
            boost::system::error_code ec;
            fs::remove(lv->getRoot() / ".desktopdb", ec);
        }
    }
    FAKEASYNC(dtp, async, err);
    return err;
}
