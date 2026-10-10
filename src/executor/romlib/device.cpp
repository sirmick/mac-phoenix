/* Copyright 1989 - 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DeviceMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <vector>
#include <DeviceMgr.h>
#include <DeskMgr.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <ResourceMgr.h>
#include <MenuMgr.h>
#include <ToolboxEvent.h>
#include <mman/mman.h>
#include <hfs/hfs.h>
#include <rsys/device.h>
#include <rsys/hostdisk.h>
#include <file/file.h>
#include <rsys/serial.h>
#include <rsys/mactcp.h>
#include <base/cpu.h>
#include <base/functions.impl.h>
#include <set>
#include <algorithm>

using namespace Executor;

/* MacPhoenix: the drivers Executor implements in C++ (umacdriver records
   made by ROMlib_driveropen). Any other non-RAM-based DCE holds a pointer
   to a 68k driver (DrvrInstall). */
static std::set<void *> native_drivers;

enum { unitTblFullErr = -29 };

/*
 * NOTE:  The device manager now executes "native code" and code read
 *	  in from resources.  The latter is "RAM" based, the former is
 *	  our version of "ROM" based.  We use the same bit that the Mac
 *	  uses to distinguish between the two, but our implementation of
 *	  ROM based is incompatible with theirs.  NOTE: even in our
 *	  incompatible ROM based routines, we still byte swap the pointers
 *	  that get us to the routines.
 */

/* RAM drivers' requests being executed, innermost last (for JIODone). */
static std::vector<ParmBlkPtr> io_in_progress;

void Executor::ROMlib_io_done(OSErr err)
{
    if(!io_in_progress.empty())
        io_in_progress.back()->ioParam.ioResult = err;
}

OSErr Executor::ROMlib_dispatch(ParmBlkPtr p, Boolean async,
                                DriverRoutineType routine,
                                INTEGER trapn) /* INTERNAL */
{
    int devicen;
    ramdriverhand ramdh;
    DCtlHandle h;
    OSErr retval;
    DriverUPP procp;

    devicen = -p->cntrlParam.ioCRefNum - 1;
    if(devicen < 0 || devicen >= NDEVICES)
        retval = badUnitErr;
    else if(LM(UTableBase) == guest_cast<DCtlHandlePtr>(-1) || (h = LM(UTableBase)[devicen]) == nullptr || *h == nullptr)
        retval = unitEmptyErr;
    else
    {
        HLock((Handle)h);
        p->ioParam.ioTrap = trapn;
        if(async)
            p->ioParam.ioTrap |= asyncTrpBit;
        else
            p->ioParam.ioTrap |= noQueueBit;
        if(!((*h)->dCtlFlags & RAMBASEDBIT) && native_drivers.count((*h)->dCtlDriver))
        {
            switch(routine)
            {
                case Open:
                    retval = ((*h)->dCtlDriver->udrvrOpen)(p, *h);
                    break;
                case Prime:
                    retval = ((*h)->dCtlDriver->udrvrPrime)(p, *h);
                    break;
                case Ctl:
                    retval = ((*h)->dCtlDriver->udrvrCtl)(p, *h);
                    break;
                case Stat:
                    retval = ((*h)->dCtlDriver->udrvrStatus)(p, *h);
                    break;
                case Close:
                    retval = ((*h)->dCtlDriver->udrvrClose)(p, *h);
                    break;
                default:
                    retval = fsDSIntErr;
                    break;
            }
        }
        else
        {
            /* A 68k driver: a RAM-based one from its handle, else (MacPhoenix)
               the pointer DrvrInstall was given. */
            bool ram_based = ((*h)->dCtlFlags & RAMBASEDBIT) != 0;
            ramdriverptr drvr;
            ramdh = nullptr;
            if(ram_based)
            {
                ramdh = (ramdriverhand)(*h)->dCtlDriver;
                LoadResource((Handle)ramdh);
                /* MacPhoenix: the routine offsets are in bytes (they were
                   added to a ramdriver pointer, scaled by its size). */
                HLock((Handle)ramdh);
                drvr = *ramdh;
            }
            else
                drvr = (ramdriverptr)(*h)->dCtlDriver;
            switch(routine)
            {
                case Open:
                    procp = (DriverUPP)((Ptr)drvr + drvr->drvrOpen);
                    break;
                case Prime:
                    procp = (DriverUPP)((Ptr)drvr + drvr->drvrPrime);
                    break;
                case Ctl:
                    procp = (DriverUPP)((Ptr)drvr + drvr->drvrCtl);
                    break;
                case Stat:
                    procp = (DriverUPP)((Ptr)drvr + drvr->drvrStatus);
                    break;
                case Close:
                    procp = (DriverUPP)((Ptr)drvr + drvr->drvrClose);
                    break;
                default:
                    procp = 0;
                    break;
            }
            if(procp)
            {
                LONGINT saved1, saved2, saved3, saved4, saved5, saved6, saved7,
                    savea2, savea3, savea4, savea5, savea6;

                savea2 = EM_A2;
                EM_A0 = US_TO_SYN68K(p);
                EM_A1 = US_TO_SYN68K(*h);
                EM_A2 = US_TO_SYN68K((ProcPtr)procp); /* for compatibility with above */
                saved1 = EM_D1;
                saved2 = EM_D2;
                saved3 = EM_D3;
                saved4 = EM_D4;
                saved5 = EM_D5;
                saved6 = EM_D6;
                saved7 = EM_D7;
                savea3 = EM_A3;
                savea4 = EM_A4;
                savea5 = EM_A5;
                savea6 = EM_A6;
                /* Open and Close return their result in D0; Prime,
                   Control and Status end in JIODone, which sets ioResult
                   (IMII-193). */
                p->ioParam.ioResult = 1;
                io_in_progress.push_back(p);
                execute68K(US_TO_SYN68K((ProcPtr)procp));
                io_in_progress.pop_back();
                if(routine == Open || routine == Close || p->ioParam.ioResult == 1)
                    p->ioParam.ioResult = (OSErr)EM_D0;
                EM_D1 = saved1;
                EM_D2 = saved2;
                EM_D3 = saved3;
                EM_D4 = saved4;
                EM_D5 = saved5;
                EM_D6 = saved6;
                EM_D7 = saved7;
                EM_A3 = savea3;
                EM_A4 = savea4;
                EM_A5 = savea5;
                EM_A6 = savea6;
                retval = EM_D0;
                EM_A2 = savea2;
            }
            else
                retval = fsDSIntErr;
            if(routine == Open)
                (*h)->dCtlFlags |= DRIVEROPENBIT;
            else if(routine == Close)
            {
                (*h)->dCtlFlags &= ~DRIVEROPENBIT;
                HUnlock((Handle)h);
                if(ramdh)
                    HUnlock((Handle)ramdh);
                LM(MBarEnable) = 0;
                /* NOTE: It's not clear whether we should zero out this
		   field or just check for DRIVEROPEN bit up above and never
		   send messages except open to non-open drivers.  */
                /* MacPhoenix: a DrvrInstall'ed driver keeps its entry
                   until DrvrRemove, as on a Mac. */
                if(ram_based)
                    LM(UTableBase)[devicen] = nullptr;
            }

            if(routine < Close)
                retval = p->ioParam.ioResult; /* see II-193 */
        }
    }
    fs_err_hook(retval);
    return retval;
}

/* PBOpen, PBClose, PBRead and PBWrite are part of the file manager */

OSErr Executor::PBControl(ParmBlkPtr pbp, Boolean a) /* IMII-186 */
{
    OSErr err;

    if(pbp->cntrlParam.ioCRefNum == OURHFSDREF && ROMlib_image_drive_control(pbp, &err))
        return err;
    err = ROMlib_dispatch(pbp, a, Ctl, 0);
    fs_err_hook(err);
    return err;
}

OSErr Executor::PBStatus(ParmBlkPtr pbp, Boolean a) /* IMII-186 */
{
    OSErr err;

    err = ROMlib_dispatch(pbp, a, Stat, 0);
    fs_err_hook(err);
    return err;
}

OSErr Executor::PBKillIO(ParmBlkPtr pbp, Boolean a) /* IMII-187 */
{
    OSErr err;

    pbp->cntrlParam.csCode = killCode;
    err = ROMlib_dispatch(pbp, a, Ctl, 0);
    fs_err_hook(err);
    return err;
}

/*
 * OpenDriver is defined below ROMlib_driveropen.
 * CloseDriver is defined below ROMlib_driverclose.
 */

/* FSRead, FSWrite are part of the file manager */

OSErr Executor::Control(INTEGER rn, INTEGER code, const void *param) /* IMII-179 */
{
    ParamBlockRec pb;
    OSErr err;

    pb.cntrlParam.ioVRefNum = 0;
    pb.cntrlParam.ioCRefNum = rn;
    pb.cntrlParam.csCode = code;
    if(param)
        BlockMoveData(param, (Ptr)pb.cntrlParam.csParam,
                      (Size)sizeof(pb.cntrlParam.csParam));
    err = PBControl(&pb, false);
    fs_err_hook(err);
    return err;
}

OSErr Executor::Status(INTEGER rn, INTEGER code, Ptr param) /* IMII-179 */
{
    ParamBlockRec pb;
    OSErr retval;

    pb.cntrlParam.ioVRefNum = 0;
    pb.cntrlParam.ioCRefNum = rn;
    pb.cntrlParam.csCode = code;
    retval = PBStatus(&pb, false);
    if(param)
        BlockMoveData((Ptr)pb.cntrlParam.csParam, param,
                      (Size)sizeof(pb.cntrlParam.csParam));
    fs_err_hook(retval);
    return retval;
}

OSErr Executor::KillIO(INTEGER rn) /* IMII-179 */
{
    ParamBlockRec pb;
    OSErr err;

    pb.cntrlParam.ioCRefNum = rn;
    err = PBKillIO(&pb, false);
    fs_err_hook(err);
    return err;
}

DCtlHandle Executor::GetDCtlEntry(INTEGER rn)
{
    int devicen;

    devicen = -rn - 1;
    return (devicen < 0 || devicen >= NDEVICES) ? 0
                                                : LM(UTableBase)[devicen];
}

static std::vector<driverinfo> knowndrivers;

void Executor::RegisterDriver(const driverinfo& di)
{
    knowndrivers.push_back(di);
}

/* Re-enter emulated code to run a driver call's completion routine.
 * A0 = param block, A1 = the routine, D0 = result. */
void Executor::callcomp(ParmBlkPtr pbp, ProcPtr comp, OSErr err)
{
    EM_A0 = US_TO_SYN68K(pbp);
    EM_A1 = US_TO_SYN68K(comp);
    EM_D0 = (unsigned short)err; /* TODO: unsigned short ? */
    execute68K((syn68k_addr_t)(uintptr_t)comp);
}

static void InitBuiltinDrivers()
{
    InitSerialDriver();
    InitMacTCPDriver();
    InitHostDiskDriver();
}

/*
 * ROMlib_driveropen will be called by PBOpen if it encounters a name
 * beginning with * a period.
 */
OSErr Executor::ROMlib_driveropen(ParmBlkPtr pbp, Boolean a) /* INTERNAL */
{
    if(knowndrivers.empty())
        InitBuiltinDrivers();

    OSErr err;
    INTEGER devicen;
    umacdriverptr up;
    DCtlHandle h;
    ramdriverhand ramdh;
    GUEST<ResType> typ;
    Boolean alreadyopen;

    TheZoneGuard guard(LM(SysZone));

    err = noErr;

    if((ramdh = (ramdriverhand)GetNamedResource("DRVR"_4,
                                                pbp->ioParam.ioNamePtr)))
    {
        LoadResource((Handle)ramdh);
        GUEST<INTEGER> resid;
        GetResInfo((Handle)ramdh, &resid, &typ, (StringPtr)0);
        devicen = resid;
        /* MacPhoenix: a driver of that name already in the table is the
           one (opened again if closed); otherwise the resource ID is the
           unit, unless another driver holds it, in which case the first
           free unit from 48 on (where extensions put theirs). */
        if(int found = ROMlib_unit_of_driver(pbp->ioParam.ioNamePtr); found >= 0)
            devicen = found;
        else if(devicen < 0 || devicen >= NDEVICES
                || (LM(UTableBase)[devicen] && *LM(UTableBase)[devicen]))
        {
            devicen = ROMlib_free_unit(48);
            if(devicen < 0)
            {
                fs_err_hook(unitTblFullErr);
                return unitTblFullErr; /* -29 */
            }
        }
        h = LM(UTableBase)[devicen];
        alreadyopen = h && ((*h)->dCtlFlags & DRIVEROPENBIT);
        if(!h && !(h = LM(UTableBase)[devicen] = (DCtlHandle)NewHandle(sizeof(DCtlEntry))))
            err = MemError();
        else if(!alreadyopen)
        {
            memset((char *)*h, 0, sizeof(DCtlEntry));
            (*h)->dCtlDriver = (umacdriverptr)ramdh;
            (*h)->dCtlFlags = (*ramdh)->drvrFlags | RAMBASEDBIT;
            (*h)->dCtlRefNum = -(devicen + 1);
            (*h)->dCtlDelay = (*ramdh)->drvrDelay;
            (*h)->dCtlEMask = (*ramdh)->drvrEMask;
            (*h)->dCtlMenu = (*ramdh)->drvrMenu;
            if((*h)->dCtlFlags & NEEDTIMEBIT)
                (*h)->dCtlCurTicks = TickCount() + (*h)->dCtlDelay;
            else
                (*h)->dCtlCurTicks = 0x7FFFFFFF;
            /*
	    * NOTE: this code doesn't check to see if something is already open.
	    *	 TODO:  fix this
	    */
            pbp->cntrlParam.ioCRefNum = (*h)->dCtlRefNum;
            err = ROMlib_dispatch(pbp, a, Open, 0);
        }
        else
        {
            pbp->cntrlParam.ioCRefNum = (*h)->dCtlRefNum;
            err = noErr;
        }
    }
    else
    {
        auto dip = knowndrivers.begin(), edip = knowndrivers.end();
        for(;
            dip != edip && !EqualString(dip->name, pbp->ioParam.ioNamePtr, false, true);
            dip++)
            ;
        
        if(dip != edip)
        {
            devicen = -dip->refnum - 1;
            if(devicen < 0 || devicen >= NDEVICES)
                err = badUnitErr;
            else if(LM(UTableBase)[devicen])
                err = noErr; /* note:  if we choose to support desk */
            /*	  accessories, we will have to */
            /*	  check to see if this is one and */
            /*	  call the open routine if it is */
            else
            {
                if(!(h = LM(UTableBase)[devicen] = (DCtlHandle)NewHandle(sizeof(DCtlEntry))))
                    err = MemError();
                else
                {
                    memset((char *)*h, 0, sizeof(DCtlEntry));
                    up = (umacdriverptr)NewPtrClear(sizeof(umacdriver));
                    if(!((*h)->dCtlDriver = up))
                        err = MemError();
                    else
                    {
                        native_drivers.insert(up);
                        up->udrvrOpen = dip->open;
                        up->udrvrPrime = dip->prime;
                        up->udrvrCtl = dip->ctl;
                        up->udrvrStatus = dip->status;
                        up->udrvrClose = dip->close;
                        str255assign(up->udrvrName, dip->name);
                        err = noErr;
                    }
                }
            }
            if(err == noErr)
            {
                pbp->cntrlParam.ioCRefNum = dip->refnum;
                err = ROMlib_dispatch(pbp, a, Open, 0);
            }
        }
        else
            err = fnfErr;
    }

    fs_err_hook(err);
    return err;
}

/* MacPhoenix: a driver's name, wherever its code is. */
static ConstStringPtr driver_name(DCtlHandle h)
{
    if(!h || !*h || !(*h)->dCtlDriver)
        return nullptr;
    if((*h)->dCtlFlags & RAMBASEDBIT)
    {
        ramdriverhand ramdh = (ramdriverhand)(*h)->dCtlDriver;
        return *ramdh ? (ConstStringPtr)&(*ramdh)->drvrName : nullptr;
    }
    if(native_drivers.count((*h)->dCtlDriver))
        return (ConstStringPtr)(*h)->dCtlDriver->udrvrName;
    return (ConstStringPtr)&((ramdriverptr)(*h)->dCtlDriver)->drvrName;
}

/* The unit holding the driver named name, or -1. */
int Executor::ROMlib_unit_of_driver(ConstStringPtr name)
{
    if(!name || !name[0])
        return -1;
    for(int i = 0; i < NDEVICES; i++)
    {
        ConstStringPtr n = driver_name(LM(UTableBase)[i]);
        if(n && EqualString(n, name, false, true))
            return i;
    }
    return -1;
}

/* The first empty unit from `from` on, or -1 when the table is full. */
int Executor::ROMlib_free_unit(int from)
{
    for(int i = std::max(from, 0); i < NDEVICES; i++)
        if(!LM(UTableBase)[i])
            return i;
    return -1;
}

/* DrvrInstall ($A03D) / RDrvrInstall ($A04F): a DCE for the 68k driver at
   drvr (a pointer; an extension that keeps its driver in a handle sets
   dRAMBased and dCtlDriver itself afterwards, IM Devices 1-62) in the unit
   of refnum. */
OSErr Executor::ROMlib_drvr_install(Ptr drvr, INTEGER refnum)
{
    int devicen = -refnum - 1;
    if(!drvr || devicen < 0 || devicen >= NDEVICES)
        return badUnitErr;
    TheZoneGuard guard(LM(SysZone));
    DCtlHandle h = LM(UTableBase)[devicen];
    if(!h)
    {
        h = (DCtlHandle)NewHandleClear(sizeof(DCtlEntry));
        if(!h)
            return MemError();
        LM(UTableBase)[devicen] = h;
    }
    else
        memset((char *)*h, 0, sizeof(DCtlEntry));
    ramdriverptr dp = (ramdriverptr)drvr;
    (*h)->dCtlDriver = (umacdriverptr)drvr;
    (*h)->dCtlFlags = dp->drvrFlags & ~(RAMBASEDBIT | DRIVEROPENBIT);
    (*h)->dCtlRefNum = refnum;
    (*h)->dCtlDelay = dp->drvrDelay;
    (*h)->dCtlEMask = dp->drvrEMask;
    (*h)->dCtlMenu = dp->drvrMenu;
    (*h)->dCtlCurTicks = ((*h)->dCtlFlags & NEEDTIMEBIT) ? TickCount() + (*h)->dCtlDelay
                                                          : 0x7FFFFFFF;
    return noErr;
}

OSErr Executor::ROMlib_drvr_remove(INTEGER refnum)
{
    int devicen = -refnum - 1;
    if(devicen < 0 || devicen >= NDEVICES)
        return badUnitErr;
    DCtlHandle h = LM(UTableBase)[devicen];
    if(!h)
        return badUnitErr;
    if(*h && ((*h)->dCtlFlags & DRIVEROPENBIT))
        return qErr; /* IM: can't remove an open driver */
    if(*h && (*h)->dCtlDriver && native_drivers.count((*h)->dCtlDriver))
        return badUnitErr;
    LM(UTableBase)[devicen] = nullptr;
    DisposeHandle((Handle)h);
    return noErr;
}

/* SystemTask's part for device drivers (Executor's desk accessories are
   their processes' business, desk.cpp): every open driver with dNeedTime
   whose time has come gets accRun. */
void Executor::ROMlib_drivers_give_time()
{
    LONGINT now = TickCount();
    for(int i = 0; i < NDEVICES; i++)
    {
        DCtlHandle h = LM(UTableBase)[i];
        if(!h || !*h)
            continue;
        INTEGER flags = (*h)->dCtlFlags;
        if(!(flags & DRIVEROPENBIT) || !(flags & NEEDTIMEBIT) || now < (*h)->dCtlCurTicks)
            continue;
        ConstStringPtr name = driver_name(h);
        if(!name || name[0] == 0 || name[1] != '.')
            continue; /* a desk accessory */
        INTEGER rn = (*h)->dCtlRefNum;
        static std::set<INTEGER> announced;
        if(announced.insert(rn).second)
            fprintf(stderr, "[Executor] accRun to %.*s (unit %d) every %d ticks\n",
                    name[0], (const char *)name + 1, i, (int)(*h)->dCtlDelay);
        Control(rn, accRun, (Ptr)0);
        h = LM(UTableBase)[i];
        if(h && *h)
            (*h)->dCtlCurTicks = TickCount() + (*h)->dCtlDelay;
    }
}

OSErr Executor::OpenDriver(ConstStringPtr name, GUEST<INTEGER> *rnp) /* IMII-178 */
{
    ParamBlockRec pb;
    OSErr retval;

    pb.ioParam.ioRefNum = 0; /* so we can't get garbage for ioRefNum. */
    pb.ioParam.ioPermssn = fsCurPerm;
    pb.ioParam.ioNamePtr = (StringPtr)name;
    retval = ROMlib_driveropen(&pb, false);
    *rnp = pb.ioParam.ioRefNum;
    fs_err_hook(retval);
    return retval;
}

OSErr Executor::CloseDriver(INTEGER rn) /* IMII-178 */
{
    ParamBlockRec pb;
    OSErr err;

    pb.cntrlParam.ioCRefNum = rn;
    err = PBClose(&pb, false);
    fs_err_hook(err);
    return err;
}
