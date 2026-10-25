#pragma once
/* hfsplusvolume.h -- an HFS+ volume for the File Manager
 * (docs/executor/HFSPlus.md): the classic (HFS) calls over hfs/plus/, with
 * names, IDs and dates as Mac OS 8.1-9 give them; read-write on a writable
 * image, else locked. */
#include <file/volume.h>
#include <file/file.h>
#include <hfs/drive_flags.h>
#include <hfs/plus/reader.h>

#include <map>
#include <memory>
#include <vector>

namespace Executor
{
class HFSPlusVolume : public Volume
{
public:
    HFSPlusVolume(VCB& vcb, std::unique_ptr<hfsplus::Reader> reader);
    ~HFSPlusVolume() override;

    hfsplus::Reader& reader() { return *reader_; }

    /* FSpExchangeFiles on this volume, as Mac OS 9.0.4 does it */
    OSErr exchangeFiles(FSSpecPtr src, FSSpecPtr dst);

    void PBGetVInfo(ParmBlkPtr pb) override;
    void PBGetCatInfo(CInfoPBPtr pb) override;
    void PBGetFInfo(ParmBlkPtr pb) override;
    void PBHGetFInfo(HParmBlkPtr pb) override;

    void PBHOpenDF(HParmBlkPtr pb) override;
    void PBHOpenRF(HParmBlkPtr pb) override;
    void PBOpenDF(ParmBlkPtr pb) override;
    void PBOpenRF(ParmBlkPtr pb) override;
    void PBOpenWD(WDPBPtr pb) override;

    void PBRead(ParmBlkPtr pb) override;
    void PBClose(ParmBlkPtr pb) override;
    void PBGetFPos(ParmBlkPtr pb) override;
    void PBSetFPos(ParmBlkPtr pb) override;
    void PBGetEOF(ParmBlkPtr pb) override;
    void PBFlushFile(ParmBlkPtr pb) override;
    void PBFlushVol(ParmBlkPtr pb) override;

    /* What a read-only volume refuses */
    void PBHRename(HParmBlkPtr pb) override;
    void PBHCreate(HParmBlkPtr pb) override;
    void PBDirCreate(HParmBlkPtr pb) override;
    void PBHDelete(HParmBlkPtr pb) override;
    void PBSetCatInfo(CInfoPBPtr pb) override;
    void PBCatMove(CMovePBPtr pb) override;
    void PBCreate(ParmBlkPtr pb) override;
    void PBDelete(ParmBlkPtr pb) override;
    void PBSetFInfo(ParmBlkPtr pb) override;
    void PBHSetFInfo(HParmBlkPtr pb) override;
    void PBSetFLock(ParmBlkPtr pb) override;
    void PBHSetFLock(HParmBlkPtr pb) override;
    void PBRstFLock(ParmBlkPtr pb) override;
    void PBHRstFLock(HParmBlkPtr pb) override;
    void PBSetFVers(ParmBlkPtr pb) override;
    void PBRename(ParmBlkPtr pb) override;
    void PBSetVInfo(HParmBlkPtr pb) override;
    void PBWrite(ParmBlkPtr pb) override;
    void PBAllocate(ParmBlkPtr pb) override;
    void PBAllocContig(ParmBlkPtr pb) override;
    void PBSetEOF(ParmBlkPtr pb) override;
    void PBLockRange(ParmBlkPtr pb) override;
    void PBUnlockRange(ParmBlkPtr pb) override;

    void PBUnmountVol(ParmBlkPtr pb) override;
    void PBEject(ParmBlkPtr pb) override;
    void PBOffLine(ParmBlkPtr pb) override;

private:
    using Entry = hfsplus::CatalogEntry;
    enum class InfoKind
    {
        FInfo,
        HFInfo,
        CatInfo
    };
    struct OpenFork;
    struct ForkState;

    Entry root();
    Entry resolveDir(short vRef, long dirID);
    Entry resolve(StringPtr name, short vRef, long dirID);
    Entry resolveRelative(Entry base, const std::string& rest);
    std::optional<Entry> child(uint32_t dirID, const std::string& macName);
    Entry resolveForInfo(StringPtr name, short vRef, long dirID, short index, bool includeDirectories);
    std::string classicName(const Entry& e);
    uint32_t localDate(uint32_t gmt) const;
    /* the longest fork the classic calls open: whole blocks under 2 GB */
    uint64_t forkLimit() const;
    void getInfoCommon(CInfoPBPtr pb, InfoKind kind);
    void openCommon(GUEST<short>& refNum, const Entry& item, bool resource, int8_t permission);
    OpenFork& openFork(short refNum);
    void setFPosCommon(ParmBlkPtr pb, bool checkEOF);
    void updateVCB();

    /* writing */
    bool writable() const { return reader_->writable(); }
    void check(hfsplus::Error e, OSErr notFound = fnfErr);
    void refresh(ForkState& s);
    void refreshFile(uint32_t cnid);
    uint32_t gmtDate(uint32_t local) const;
    std::u16string newName(ConstStringPtr name, size_t max = 31);
    std::pair<Entry, std::string> resolveForCreate(StringPtr name, short vRef, long dirID);
    bool isOpen(uint32_t cnid);
    void createCommon(StringPtr name, short vRef, long dirID, bool folder, GUEST<LONGINT> *newDirID);
    void deleteCommon(StringPtr name, short vRef, long dirID);
    void renameCommon(StringPtr name, short vRef, long dirID, ConstStringPtr newName);
    void setInfoCommon(CInfoPBPtr pb, long dirID, InfoKind kind);
    void lockCommon(StringPtr name, short vRef, long dirID, bool lock);
    void refuseWrite(OpenFork& f);
    void stampWritten(OpenFork& f);

    std::unique_ptr<hfsplus::Reader> reader_;
    std::vector<std::unique_ptr<OpenFork>> forks_;     /* by FCB index */
    std::map<uint64_t, std::weak_ptr<ForkState>> states_;   /* (CNID, fork): what its paths share */
    unsigned blockScale_ = 1;                           /* reported blocks: this many of the volume's */
};

/* Mount the HFS+ volume (bare, or in an HFS wrapper) at offset in fd as
   drive pb->volumeParam.ioVRefNum; the old HFS mount's counterpart. */
OSErr hfsPlusMountVol(ParmBlkPtr pb, int fd, uint64_t offset, drive_flags_t flags, DrvQExtra *dqp);

/* Whether there's an HFS+ volume at offset in fd. */
bool isHFSPlusVolume(int fd, uint64_t offset);

} // namespace Executor
