#pragma once

#include <ExMacTypes.h>
#include <FileMgr.h>

namespace Executor
{
    class Volume
    {
    protected:
        VCB& vcb;
    public:
        Volume(VCB& vcb) : vcb(vcb) {}
        virtual ~Volume();

        virtual void PBGetVInfo(ParmBlkPtr pb);
        virtual void PBHGetVInfo(HParmBlkPtr pb);

        virtual void PBHRename(HParmBlkPtr pb) = 0;
        virtual void PBHCreate(HParmBlkPtr pb) = 0;
        virtual void PBDirCreate(HParmBlkPtr pb) = 0;
        virtual void PBHDelete(HParmBlkPtr pb) = 0;
        virtual void PBHOpenDF(HParmBlkPtr pb) = 0;
        virtual void PBHOpenRF(HParmBlkPtr pb) = 0;
        virtual void PBGetCatInfo(CInfoPBPtr pb) = 0;
        virtual void PBSetCatInfo(CInfoPBPtr pb) = 0;
        virtual void PBCatMove(CMovePBPtr pb) = 0;
        virtual void PBHGetFInfo(HParmBlkPtr pb) = 0;
        virtual void PBOpenDF(ParmBlkPtr pb) = 0;
        virtual void PBOpenRF(ParmBlkPtr pb) = 0;
        virtual void PBCreate(ParmBlkPtr pb) = 0;
        virtual void PBDelete(ParmBlkPtr pb) = 0;
        virtual void PBOpenWD(WDPBPtr pb) = 0;
        virtual void PBGetFInfo(ParmBlkPtr pb) = 0;
        virtual void PBSetFInfo(ParmBlkPtr pb) = 0;
        virtual void PBHSetFInfo(HParmBlkPtr pb) = 0;
        virtual void PBSetFLock(ParmBlkPtr pb) = 0;
        virtual void PBHSetFLock(HParmBlkPtr pb) = 0;
        virtual void PBRstFLock(ParmBlkPtr pb) = 0;
        virtual void PBHRstFLock(HParmBlkPtr pb) = 0;
        virtual void PBSetFVers(ParmBlkPtr pb) = 0;
        virtual void PBRename(ParmBlkPtr pb) = 0;
        virtual void PBSetVInfo(HParmBlkPtr pb) = 0;
        virtual void PBFlushVol(ParmBlkPtr pb) = 0;
        virtual void PBUnmountVol(ParmBlkPtr pb) = 0;
        virtual void PBEject(ParmBlkPtr pb) = 0;
        virtual void PBOffLine(ParmBlkPtr pb) = 0;

        virtual void PBRead(ParmBlkPtr pb) = 0;
        virtual void PBWrite(ParmBlkPtr pb) = 0;
        virtual void PBClose(ParmBlkPtr pb) = 0;
        virtual void PBAllocate(ParmBlkPtr pb) = 0;
        virtual void PBAllocContig(ParmBlkPtr pb) = 0;
        virtual void PBSetEOF(ParmBlkPtr pb) = 0;
        virtual void PBLockRange(ParmBlkPtr pb) = 0;
        virtual void PBUnlockRange(ParmBlkPtr pb) = 0;
        virtual void PBGetFPos(ParmBlkPtr pb) = 0;
        virtual void PBSetFPos(ParmBlkPtr pb) = 0;
        virtual void PBGetEOF(ParmBlkPtr pb) = 0;
        virtual void PBFlushFile(ParmBlkPtr pb) = 0;

    };
}
