#include <file/volume.h>
#include <rsys/macros.h>
#include <MemoryMgr.h>

using namespace Executor;

Volume::~Volume()
{
    
}

void Volume::PBGetVInfo(ParmBlkPtr pb)
{
    // TODO: derived class should keep fields up to date
    
    if(pb->volumeParam.ioNamePtr)
        str255assign(pb->volumeParam.ioNamePtr, vcb.vcbVN);
    pb->volumeParam.ioVRefNum = vcb.vcbVRefNum;
    pb->volumeParam.ioVCrDate = vcb.vcbCrDate;
    pb->volumeParam.ioVLsBkUp = vcb.vcbVolBkUp;
    pb->volumeParam.ioVAtrb = vcb.vcbAtrb;
    pb->volumeParam.ioVNmFls = vcb.vcbNmFls;    // FIXME: should refer to WD if vrefnum is a WD
    // ioVDirSt     // FIXME
    // ioVBlLen     // FIXME
    pb->volumeParam.ioVNmAlBlks = vcb.vcbNmAlBlks;
    pb->volumeParam.ioVAlBlkSiz = vcb.vcbAlBlkSiz;
    pb->volumeParam.ioVClpSiz = vcb.vcbClpSiz;
    pb->volumeParam.ioAlBlSt = vcb.vcbAlBlSt;
    pb->volumeParam.ioVNxtFNum = vcb.vcbNxtCNID;
    pb->volumeParam.ioVFrBlk = vcb.vcbFreeBks;
}
void Volume::PBHGetVInfo(HParmBlkPtr pb)
{
    PBGetVInfo(reinterpret_cast<ParmBlkPtr>(pb));

    pb->volumeParam.ioVSigWord = vcb.vcbSigWord;
    pb->volumeParam.ioVDrvInfo = vcb.vcbDrvNum;
    pb->volumeParam.ioVDRefNum = vcb.vcbDRefNum;
    pb->volumeParam.ioVFSID = vcb.vcbFSID;
    pb->volumeParam.ioVBkUp = vcb.vcbVolBkUp;
    pb->volumeParam.ioVSeqNum = vcb.vcbVSeqNum;
    pb->volumeParam.ioVWrCnt = vcb.vcbWrCnt;
    pb->volumeParam.ioVFilCnt = vcb.vcbFilCnt;
    pb->volumeParam.ioVDirCnt = vcb.vcbDirCnt;
    memcpy(pb->volumeParam.ioVFndrInfo, vcb.vcbFndrInfo, sizeof(vcb.vcbFndrInfo));

#if 0
        // copied from ufs - system folder?
    if(ISWDNUM(LM(BootDrive)))
    {
        wdp = WDNUMTOWDP(LM(BootDrive));
        if(wdp->vcbp == vcbp)
            pb->volumeParam.ioVFndrInfo[1] = wdp->dirid;
    }
#endif
}
