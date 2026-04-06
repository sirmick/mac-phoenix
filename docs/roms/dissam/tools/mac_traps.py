# -*- coding: utf-8 -*-
# Macintosh A-line trap name table.
#
# Maps 16-bit A-line opcodes ($A000-$AFFF) to their Toolbox/OS trap names.
# Source: legacy/cxmon/src/mon_atraps.h + Inside Macintosh.
#
# Used by load_rom.py (Ghidra analysis) and export_listing.py (listing export).

# Subset of the most important traps for ROM analysis.  The full table has
# ~1200 entries; this keeps the ones you'll actually see in boot-flow code.
TRAP_NAMES = {
    # --- OS traps ($A0xx) ---
    0xA000: "_Open", 0xA001: "_Close", 0xA002: "_Read", 0xA003: "_Write",
    0xA004: "_Control", 0xA005: "_Status", 0xA006: "_KillIO",
    0xA007: "_GetVolInfo", 0xA008: "_Create", 0xA009: "_Delete",
    0xA00A: "_OpenRF", 0xA00B: "_Rename",
    0xA00C: "_GetFileInfo", 0xA00D: "_SetFileInfo",
    0xA00E: "_UnmountVol", 0xA00F: "_MountVol",
    0xA010: "_Allocate", 0xA011: "_GetEOF", 0xA012: "_SetEOF",
    0xA013: "_FlushVol", 0xA014: "_GetVol", 0xA015: "_SetVol",
    0xA017: "_Eject", 0xA018: "_GetFPos",
    0xA019: "_InitZone", 0xA01B: "_SetZone", 0xA01C: "_FreeMem",
    0xA01E: "_NewPtr", 0xA01F: "_DisposePtr", 0xA020: "_SetPtrSize",
    0xA021: "_GetPtrSize", 0xA022: "_NewHandle",
    0xA023: "_DisposeHandle", 0xA024: "_SetHandleSize",
    0xA025: "_GetHandleSize", 0xA027: "_ReallocHandle",
    0xA029: "_HLock", 0xA02A: "_HUnlock",
    0xA02C: "_InitApplZone", 0xA02D: "_SetApplLimit",
    0xA02E: "_BlockMove", 0xA02F: "_PostEvent",
    0xA030: "_OSEventAvail", 0xA031: "_GetOSEvent",
    0xA032: "_FlushEvents", 0xA033: "_VInstall", 0xA034: "_VRemove",
    0xA036: "_MoreMasters", 0xA037: "_ReadParam", 0xA038: "_WriteParam",
    0xA039: "_ReadDateTime", 0xA03A: "_SetDateTime", 0xA03B: "_Delay",
    0xA03D: "_DrvrInstall", 0xA03E: "_DrvrRemove",
    0xA040: "_ResrvMem", 0xA044: "_SetFPos", 0xA045: "_FlushFile",
    0xA047: "_SetTrapAddress",
    0xA049: "_HPurge", 0xA04A: "_HNoPurge", 0xA04B: "_SetGrowZone",
    0xA04C: "_CompactMem", 0xA04D: "_PurgeMem",
    0xA04E: "_AddDrive", 0xA04F: "_RDrvrInstall",
    0xA051: "_ReadXPRam", 0xA052: "_WriteXPRam",
    0xA055: "_StripAddress", 0xA057: "_SetApplBase",
    0xA058: "_InsTime", 0xA059: "_RmvTime", 0xA05A: "_PrimeTime",
    0xA05B: "_PowerOff", 0xA05D: "_SwapMMUMode",
    0xA060: "_FSDispatch", 0xA063: "_MaxApplZone", 0xA064: "_MoveHHi",
    0xA069: "_HGetState", 0xA06A: "_HSetState",
    0xA072: "_DoVBLTask", 0xA07C: "_ADBOp",
    0xA090: "_SysEnvirons", 0xA093: "_Microseconds",
    0xA0AD: "_Gestalt",
    # --- OS traps with variant flags ---
    0xA11E: "_NewPtr", 0xA122: "_NewHandle",
    0xA128: "_RecoverHandle", 0xA146: "_GetTrapAddress",
    0xA162: "_PurgeSpace", 0xA1AD: "_Gestalt",
    0xA200: "_HOpen", 0xA209: "_HDelete", 0xA20A: "_HOpenRF",
    0xA247: "_SetOSTrapAddress",
    0xA260: "_HFSDispatch",
    0xA31E: "_NewPtrClear", 0xA322: "_NewHandleClear",
    0xA346: "_GetOSTrapAddress",
    0xA43D: "_DrvrInstallRsrvMem",
    0xA51E: "_NewPtrSys", 0xA522: "_NewHandleSys",
    0xA53D: "_DrvrInstallRsrvMem",
    0xA647: "_SetToolTrapAddress",
    0xA71E: "_NewPtrSysClear", 0xA722: "_NewHandleSysClear",
    0xA746: "_GetToolTrapAddress",
    # --- Toolbox traps ($A8xx+) ---
    0xA815: "_SCSIDispatch",
    0xA850: "_InitCursor", 0xA851: "_SetCursor",
    0xA852: "_HideCursor", 0xA853: "_ShowCursor",
    0xA860: "_WaitNextEvent",
    0xA86E: "_InitGraf", 0xA873: "_SetPort", 0xA874: "_GetPort",
    0xA87B: "_ClipRect", 0xA87D: "_ClosePort",
    0xA884: "_DrawString", 0xA88A: "_TextSize",
    0xA891: "_LineTo", 0xA893: "_MoveTo",
    0xA895: "_ShutDown",
    0xA8A1: "_FrameRect", 0xA8A2: "_PaintRect",
    0xA8A3: "_EraseRect", 0xA8A5: "_FillRect",
    0xA8EC: "_CopyBits", 0xA8F3: "_OpenPicture",
    0xA8F6: "_DrawPicture",
    0xA8FE: "_InitFonts",
    0xA904: "_DrawGrowIcon",
    0xA912: "_InitWindows", 0xA913: "_NewWindow",
    0xA914: "_DisposeWindow", 0xA915: "_ShowWindow",
    0xA924: "_FrontWindow", 0xA92C: "_FindWindow",
    0xA930: "_InitMenus", 0xA931: "_NewMenu",
    0xA935: "_InsertMenu", 0xA937: "_DrawMenuBar",
    0xA93D: "_MenuSelect", 0xA93E: "_MenuKey",
    0xA954: "_NewControl",
    0xA970: "_GetNextEvent", 0xA972: "_GetMouse",
    0xA975: "_TickCount", 0xA976: "_GetKeys",
    0xA97B: "_InitDialogs", 0xA97C: "_GetNewDialog",
    0xA985: "_Alert", 0xA986: "_StopAlert",
    0xA991: "_ModalDialog",
    0xA994: "_CurResFile", 0xA995: "_InitResources",
    0xA997: "_OpenResFile", 0xA998: "_UseResFile",
    0xA99A: "_CloseResFile", 0xA99B: "_SetResLoad",
    0xA99C: "_CountResources", 0xA99D: "_GetIndResource",
    0xA9A0: "_GetResource", 0xA9A1: "_GetNamedResource",
    0xA9A2: "_LoadResource", 0xA9A3: "_ReleaseResource",
    0xA9A5: "_SizeRsrc", 0xA9AF: "_ResError",
    0xA9B4: "_SystemTask", 0xA9C8: "_SysBeep",
    0xA9C9: "_SysError",
    0xA9CB: "_TEGetText", 0xA9CC: "_TEInit",
    0xA9F0: "_LoadSeg", 0xA9F2: "_Launch",
    0xA9F4: "_ExitToShell",
    0xA9FD: "_GetScrap", 0xA9FE: "_PutScrap",
    0xAA00: "_OpenCPort",
    0xAA90: "_InitPalettes",
}


def trap_name(opcode):
    """Return the trap mnemonic for a 16-bit A-line opcode, or None."""
    if 0xA000 <= opcode <= 0xAFFF:
        return TRAP_NAMES.get(opcode)
    return None


def trap_mnemonic(opcode):
    """Return a display string like '_InitZone' or '$A019'."""
    name = trap_name(opcode)
    if name:
        return name
    if 0xA000 <= opcode <= 0xAFFF:
        return "$%04X" % opcode
    return None
