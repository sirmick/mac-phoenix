/* MacPhoenix: ".Disk", the driver behind host-folder volumes.
 *
 * A host folder has no blocks to read, but Finder still asks a volume's
 * driver what it is (Status 43 'devt', Control 23 drive info) and for its
 * icon (Control 21/22). This answers the way Basilisk II's .Disk driver
 * does (src/core/disk.cpp, refnum -63), so a host volume looks like the
 * fixed disk of the real reference boots. */

#include <base/common.h>

#include <DeviceMgr.h>
#include <FileMgr.h>
#include <Disk.h>
#include <MemoryMgr.h>
#include <DeskMgr.h>

#include <mman/mman.h>

#include <file/file.h>
#include <hfs/drive_flags.h>
#include <rsys/device.h>
#include <rsys/hostdisk.h>
#include <base/traps.impl.h>

#include <cstring>

using namespace Executor;

namespace
{
/* ICN#: a drive outline with a light, rows 19..27 (Basilisk's shape). */
Ptr drive_icon()
{
    static Ptr icon;
    if(icon)
        return icon;
    TheZoneGuard guard(LM(SysZone));
    icon = NewPtrClear(256);
    if(!icon)
        return nullptr;
    auto put = [](uint8_t *row, uint32_t v) {
        row[0] = v >> 24;
        row[1] = v >> 16;
        row[2] = v >> 8;
        row[3] = v;
    };
    uint8_t *data = (uint8_t *)icon, *mask = data + 128;
    for(int y = 19; y <= 27; y++)
    {
        uint32_t row = (y == 19 || y == 27) ? 0x7FFFFFFE
                     : y == 24 ? 0x8C000001 : 0x80000001;
        put(data + 4 * y, row);
        put(mask + 4 * y, (y == 19 || y == 27) ? 0x7FFFFFFE : 0xFFFFFFFF);
    }
    return icon;
}

OSErr done(ParmBlkPtr pbp, OSErr err)
{
    pbp->ioParam.ioResult = err;
    if((pbp->ioParam.ioTrap & asyncTrpBit) && pbp->ioParam.ioCompletion)
        callcomp(pbp, pbp->ioParam.ioCompletion, err);
    return err;
}

DrvQEl *find_drive(INTEGER drive)
{
    for(DrvQEl *dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
        if(dp->dQDrive == drive && dp->dQRefNum == kHostDiskRefNum)
            return dp;
    return nullptr;
}
}

static OSErr C_hostdisk_open(ParmBlkPtr pbp, DCtlPtr dcp);
REGISTER_FUNCTION_PTR(hostdisk_open, D0(A0, A1));
static OSErr C_hostdisk_prime(ParmBlkPtr pbp, DCtlPtr dcp);
REGISTER_FUNCTION_PTR(hostdisk_prime, D0(A0, A1));
static OSErr C_hostdisk_ctl(ParmBlkPtr pbp, DCtlPtr dcp);
REGISTER_FUNCTION_PTR(hostdisk_ctl, D0(A0, A1));
static OSErr C_hostdisk_status(ParmBlkPtr pbp, DCtlPtr dcp);
REGISTER_FUNCTION_PTR(hostdisk_status, D0(A0, A1));
static OSErr C_hostdisk_close(ParmBlkPtr pbp, DCtlPtr dcp);
REGISTER_FUNCTION_PTR(hostdisk_close, D0(A0, A1));

static OSErr C_hostdisk_open(ParmBlkPtr pbp, DCtlPtr dcp)
{
    /* As the reference's .Disk DCE: needs lock, status/control/read/write
       enabled; open; Driver Gestalt supported (low byte bit 2, which
       Finder checks before asking 'devt'). */
    dcp->dCtlFlags = 0x4F00 | DRIVEROPENBIT | 0x04;
    return done(pbp, noErr);
}

static OSErr C_hostdisk_close(ParmBlkPtr pbp, DCtlPtr dcp)
{
    return done(pbp, -24); /* closErr: like a ROM disk driver, it stays open */
}

/* No raw blocks behind a host folder. */
static OSErr C_hostdisk_prime(ParmBlkPtr pbp, DCtlPtr dcp)
{
    pbp->ioParam.ioActCount = 0;
    return done(pbp, ioErr);
}

static OSErr C_hostdisk_ctl(ParmBlkPtr pbp, DCtlPtr dcp)
{
    if(pbp->cntrlParam.csCode == accRun)
        return done(pbp, noErr);
    if(!find_drive(pbp->cntrlParam.ioVRefNum))
        return done(pbp, nsDrvErr);
    GUEST<int32_t> *param = (GUEST<int32_t> *)pbp->cntrlParam.csParam;
    switch(pbp->cntrlParam.csCode)
    {
        case 5: /* verify */
        case 7: /* eject: a fixed disk stays */
            return done(pbp, noErr);
        case 6: /* format */
            return done(pbp, wPrErr);
        case 21: /* drive icon */
        case 22: /* media icon */
            *param = US_TO_SYN68K(drive_icon());
            return done(pbp, noErr);
        case 23: /* drive info: unspecified fixed SCSI disk */
            *param = 0x0601;
            return done(pbp, noErr);
        default:
            return done(pbp, controlErr);
    }
}

static OSErr C_hostdisk_status(ParmBlkPtr pbp, DCtlPtr dcp)
{
    DrvQEl *dp = find_drive(pbp->cntrlParam.ioVRefNum);
    uint8_t *param = (uint8_t *)pbp->cntrlParam.csParam;
    auto put32 = [&](int at, uint32_t v) { *(GUEST<uint32_t> *)(param + at) = v; };
    auto put16 = [&](int at, uint16_t v) { *(GUEST<uint16_t> *)(param + at) = v; };

    switch(pbp->cntrlParam.csCode)
    {
        case 43: /* driver gestalt */
        {
            uint32_t sel = *(GUEST<uint32_t> *)param;
            switch(sel)
            {
                case "vers"_4: put32(4, 0x01008000); break;
                case "devt"_4: put32(4, "disk"_4); break;
                case "intf"_4: put32(4, "exec"_4); break;
                case "sync"_4: put32(4, 0x01000000); break;
                case "boot"_4:
                    put16(4, dp ? (uint16_t)dp->dQDrive : 0);
                    put16(6, (uint16_t)kHostDiskRefNum);
                    break;
                case "wide"_4: put16(4, 0x0100); break;
                case "purg"_4: put32(4, 0); break;
                case "ejec"_4: put32(4, 0x00030003); break;
                case "flus"_4: put16(4, 0); break;
                case "vmop"_4: put32(4, 0); break;
                default: return done(pbp, statusErr);
            }
            return done(pbp, noErr);
        }
        case 8: /* drive status */
        {
            if(!dp)
                return done(pbp, nsDrvErr);
            DrvSts *st = (DrvSts *)param;
            memset(st, 0, sizeof *st);
            st->writeProt = 0;
            st->diskInPlace = 8; /* fixed */
            st->installed = 1;
            st->sides = (SignedByte)0x80;
            st->dQDrive = dp->dQDrive;
            st->dQRefNum = dp->dQRefNum;
            st->dQFSID = dp->dQFSID;
            return done(pbp, noErr);
        }
        default:
            return done(pbp, dp ? statusErr : nsDrvErr);
    }
}

/* Disk images' drives (OURHFSDREF) have no driver unit behind them; the
   File Manager reads them itself. Finder still asks a volume's driver for
   its icons and drive info: answer those as .Disk does, so an image looks
   like the same fixed disk (as under Basilisk II). */
bool Executor::ROMlib_image_drive_control(ParmBlkPtr pbp, OSErr *err)
{
    GUEST<int32_t> *param = (GUEST<int32_t> *)pbp->cntrlParam.csParam;
    switch(pbp->cntrlParam.csCode)
    {
        case 21: /* drive icon */
        case 22: /* media icon */
            *param = US_TO_SYN68K(drive_icon());
            break;
        case 23: /* drive info: unspecified fixed SCSI disk */
            *param = 0x0601;
            break;
        default:
            return false;
    }
    *err = done(pbp, noErr);
    return true;
}

void Executor::InitHostDiskDriver()
{
    RegisterDriver({
        &hostdisk_open, &hostdisk_prime, &hostdisk_ctl,
        &hostdisk_status, &hostdisk_close, (StringPtr) "\005.Disk", kHostDiskRefNum,
    });
}

INTEGER Executor::ROMlib_add_host_drive(const char *path)
{
    GUEST<INTEGER> refnum;
    if(OpenDriver((StringPtr) "\005.Disk", &refnum) != noErr)
        return 0;
    DrvQExtra *dqp = ROMlib_addtodq(2048L * 2, path, 0, kHostDiskRefNum,
                                    DRIVE_FLAGS_FIXED, nullptr);
    return dqp ? (INTEGER)dqp->dq.dQDrive : 0;
}
