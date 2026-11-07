/*
 * images.cpp - disk images in and out while Executor runs (session.h:
 * mount, mount-additions, mount-cd, eject; Hooks::ejected).
 *
 * Disk images going away, ejected here or by the Finder (Put Away, or
 * anything else that unmounts them), are told to the front end at the next
 * event call: the tab takes them out of its Disks panel.
 */
#include "internal.h"

#include <base/cpu.h>
#include <FileMgr.h>
#include <file/file.h>
#include <hfs/hfs.h>
#include <MemoryMgr.h>
#include <OSEvent.h>

#include <cstring>
#include <map>

namespace executor_host::session {

using namespace Executor;

namespace {

/* The disk images' drives (by path) and whether a volume is on each now. */
std::map<std::string, bool> image_drives()
{
    std::map<std::string, bool> out;
    const std::string &images = setup().images;
    for(DrvQEl *dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
    {
        auto *dqp = (DrvQExtra *)((char *)dp - sizeof(LONGINT));
        const char *name = (const char *)dqp->devicename;
        if(!name || strncmp(name, images.c_str(), images.size()) != 0)
            continue;
        bool &on = out[name];
        for(HVCB *v = (HVCB *)LM(VCBQHdr).qHead; v; v = (HVCB *)v->qLink)
            if(v->vcbDrvNum == dp->dQDrive)
                on = true;
    }
    return out;
}

/* A File Manager trap called as a program would, through the trap table:
   the System's patches on _MountVol and _UnmountVol are how the Finder
   hears of volumes coming and going. A0 the parameter block (in the
   system heap: the patches read it), D0 the result. */
OSErr fs_trap(uint16_t trap, INTEGER vref)
{
    static std::map<uint16_t, Ptr> stubs;
    static ParmBlkPtr pb = nullptr;
    Ptr &code = stubs[trap];
    if(!code)
    {
        code = NewPtrSys(4);
        ((GUEST<uint16_t> *)code)[0] = trap;          /* _MountVol, _UnmountVol */
        ((GUEST<uint16_t> *)code)[1] = 0x4E75;        /* RTS */
    }
    if(!pb)
        pb = (ParmBlkPtr)NewPtrSysClear(sizeof(ParamBlockRec));
    memset((void *)pb, 0, sizeof(ParamBlockRec));
    pb->volumeParam.ioVRefNum = vref;
    uint32_t d[8], a[7];
    for(int i = 0; i < 8; i++)
        d[i] = EM_DREG(i);
    for(int i = 0; i < 7; i++)
        a[i] = EM_AREG(i);
    EM_A0 = US_TO_SYN68K(pb);
    execute68K(US_TO_SYN68K(code));
    OSErr err = (int16_t)EM_D0;
    for(int i = 0; i < 8; i++)
        EM_DREG(i) = d[i];
    for(int i = 0; i < 7; i++)
        EM_AREG(i) = a[i];
    return err;
}

/* The volumes on the image's drive at PATH (its drive queue element in
   *DRIVE): on line, and with OFFLINE those ejected but still known (the
   Finder's Eject: no drive number, the driver's left) */
std::vector<INTEGER> image_volumes(const std::string &path, bool offline, DrvQEl **drive = nullptr)
{
    std::vector<INTEGER> out;
    for(DrvQEl *dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
    {
        auto *dqp = (DrvQExtra *)((char *)dp - sizeof(LONGINT));
        if(!dqp->devicename || path != (const char *)dqp->devicename)
            continue;
        if(drive)
            *drive = dp;
        for(HVCB *v = (HVCB *)LM(VCBQHdr).qHead; v; v = (HVCB *)v->qLink)
            if(v->vcbDrvNum == dp->dQDrive
               || (offline && !v->vcbDrvNum && (v->vcbDRefNum == dp->dQDrive || v->vcbDRefNum == -dp->dQDrive)))
                out.push_back(v->vcbVRefNum);
    }
    return out;
}

std::map<std::string, bool> seen;

}

/* The image's drive (ROMlib_image_drive: its flags worked out, locked for
   a CD), then _MountVol on it, as GetNextEvent would for a disk put in; a
   diskEvt for the program in front. -1: the image can't be opened. */
OSErr mount_image(const std::string &path)
{
    INTEGER drive = ROMlib_image_drive(path.c_str());
    if(!drive)
        return -1;
    OSErr err = fs_trap(0xA00F, drive);
    PPostEvent(diskEvt, ((LONGINT)err << 16) | (uint16_t)drive, nullptr);
    return err;
}

/* Through _UnmountVol, as the Finder's Put Away; one ejected by the
   Finder but still known too. */
OSErr eject_image(const std::string &path)
{
    DrvQEl *drive = nullptr;
    for(INTEGER vref : image_volumes(path, true, &drive))
        if(OSErr err = fs_trap(0xA00E, vref))
            return err;
    return drive ? noErr : nsvErr;
}

/* Drives that had a volume and have none now: ejected. A volume ejected
   but still known (offline: the Finder's Eject) is unmounted too, the
   image being gone from the front end, so no grey icon stays on the
   desktop. One that won't go (files open on it: an application running
   from it, the Finder's "could not be put away") stays, and so does the
   image: the tab dropping it left the Mac reading a file that wasn't
   there, and the disk added again was a second one. */
void watch_images()
{
    auto now = image_drives();
    for(auto &[path, on] : now)
    {
        if(seen[path] && !on)
        {
            for(INTEGER vref : image_volumes(path, true))
                fs_trap(0xA00E, vref);
            if(image_volumes(path, true).empty() && hooks().ejected)
                hooks().ejected(path);
        }
        seen[path] = on;
    }
}

}  // namespace executor_host::session
