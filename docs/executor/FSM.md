# The File System Manager on Executor

STRAGGLERS item 52 (PC Exchange). PC Exchange mounts DOS (FAT) disks on a
Mac through the File System Manager (FSM): it installs a foreign file system
(FFS) with `InstallFS` and the FSM hands it the drives no Mac file system
reads, and then every File Manager call for the volumes it mounted. Executor
had no FSM, so PC Exchange declined to install ("not installed" in its
control panel). Now `src/executor/romlib/file/fsm.cpp` is one, written from
Apple's `FSM.h` (records, selectors, constants) and the behaviour of PC
Exchange 2.1.1 (7.6.1) and 2.0.5 (7.5.5): which calls it makes, in which
order, what it reads back.

```bash
ctest -R pcexchange_executor        # 7.6.1: a FAT16 image read, written,
                                    #   consistent after shutdown
./build/mac-phoenix --backend executor --executor-system 7.6.1.img \
    --disk dos.img [--executor-writable-images]
python3 tests/lib/fat_image.py make dos.img PCDISK   # a FAT16 test image
python3 tests/lib/fat_image.py check dos.img         # its consistency
```

The log says what happened: `[Executor] FSM: drive 5: no Mac volume, kept
for foreign file systems`, `FSM: DOS installed (FSID $4953)`, `FSM: drive 5
mounted by DOS: 0`. PC Exchange also installs a ProDOS file system (FSID
`$0100`).

## The pieces

**Gestalt** `'fs  '` has bit 2 (FSM present) and bit 3 (dynamic load of a
file system's code); PC Exchange 2.1.1 installs only with both.

**`_FSMDispatch` (`$A0AC`)**, register based: D0 the selector, arguments in
A0, D3, D4. Selector numbers come from the glue in PC Exchange and the
System's drivers:

| D0 | Call | Registers |
|---|---|---|
| 0 | InstallFS | A0 the FSD record |
| 1 | RemoveFS | A0 the FSID |
| 2 | GetFSInfo | D4 selector, D3 key, A0 buffer (its `fsdLength` the size); size copied back in D4 |
| 5 | SetFSInfo | D4 FSID, D3 size, A0 record |
| 6 | InformFSM | D3 message, A0 parameter block |
| 4 | ? | A0 = 1, from the disk drivers (`.Sony`, `.EDisk`): logged, noErr |

- `InstallFS` keeps a **copy** of the record: PC Exchange disposes of its
  own as soon as the call returns. It fills in `fileSystemSpec` with the
  current resource file when the file system left it empty (PC Exchange
  does, and opens its file again from there for its load message).
- `SetFSInfo` copies the record after the link and FSID. PC Exchange uses it
  to set `compInterfMask` (enable, dynamic load, resource loaded) and the
  HFS component's proc, log-to-physical proc and stack.
- `InformFSM(fsmDrvQElChangedMessage)` (PC Exchange sends it with nil right
  after installing) offers every unread drive to the file systems again;
  `fsmGetFSIconMessage` asks the drive's file system for its icon.
- **FSVars** (`$BB8`) `+$12` points at the FSM's own variables, `$54` bytes,
  length word first, the version at `+$18`. PC Exchange 2.0.5 carries an
  FSM of its own (`CUST` 1000, version `$012F`) and installs it over the
  System's unless that says `$012F` or later, so Executor's says `$012F`.

**Mounting.** A disk image Executor's HFS and HFS+ code don't recognise
(`try_to_mount_disk`) keeps its drive-queue element and its file open, its
size in blocks, and is answered `noMacDskErr`; driver-level `_Read`/`_Write`
on that drive (refnum -101) work without the direct-disk-access preference.
`ROMlib_fsm_mount` offers such a drive to each file system in turn: it
loads a dynamically loading one (comm proc `ffsLoadMessage`), reads the
sector its HFS component's `idSector` names, sends `ffsIDDiskMessage` with
that sector (PC Exchange looks for a FAT boot sector), and on noErr calls the
HFS component with `kFSMMountVol` (`$A00F`), `ioVRefNum` the drive. The
drive's `dQFSID` becomes the file system's. `PBMountVol` of a drive no
volume is on goes the same way. A `diskEvt` is posted for each volume.

**Routing.** Every File Manager entry point (`hfsXbar.cpp`, the desktop
database calls in `finder.cpp`) starts with `FSM_ROUTE`: the call's volume
is found from its parameter block (a path and `ioVRefNum`, an FCB, a volume
index, a working directory or a desktop database refnum); if it is a VCB a
file system made, the file system gets the call:
`HFSCIProc(vcb, selectCode, pb, globals, fsid)`, on its own stack
(`stackTop`), `ioTrap` set to the trap word (PC Exchange reads the call from
`ioTrap`; `selectCode` is the trap word, or for `_HFSDispatch` the selector).
The routing is inside the C++ functions, not the trap table, so Executor's
own callers (Resource Manager, Standard File, aliases) reach the file system
too. Two answers are adjusted:

- `PBHOpenDF` answered `paramErr` is retried as `PBHOpen`, as the `HOpenDF`
  glue does on a Mac (PC Exchange has no OpenDF).
- `GetVolInfo` by name or index gets the volume's `ioVRefNum`, as HFS
  answers; PC Exchange sets it only when the call gave one.

**VCBs.** PC Exchange keeps its own fields at fixed offsets from `$B2`, the
classic VCB size, whatever `sysVCBLength` says, so a foreign VCB is a plain
`$B2`-byte VCB plus the file system's part, not Executor's `VCBExtra`
(whose `Volume*` would land in PC Exchange's data). `ROMlib_volume_of()`
answers nullptr for such a VCB, and the code that cast to `VCBExtra` goes
through it.

**The HFS utility routines (`$A824`)**, Pascal, D0 the selector, all of
`FSM.h`'s: FCBs in Executor's FCB table, VCBs in the VCB queue (vRefNums
from `ROMlib_nextvrn`), working directories in the WDCB table, drives in the
drive queue, `UTDetermineVol`, `UTParsePathname`, `UTGetPathComponentName`
(`startOffset` a 0-based index into the name's characters; a partial path's
leading colon skipped; an empty component at a colon is `::`, the parent;
the caller moves on by `componentLength + 1` while `moreName`), and a block
cache: 64 blocks of 512 bytes, written through on release or `UTMarkDirty`,
blocks keyed by drive and physical block; `UTCacheReadIP`/`WriteIP` go to
the drive directly (a nil log-to-physical proc: the position is the
volume's). `UTIndexFCB` and `UTLocateFCB` end with `fnfErr` (PC Exchange
indexes until it).

**Icons.** Executor answers the drive and media icon `_Control` calls (21,
22) of its image drives itself; for a drive a foreign file system has, it
asks that file system (`ffsGetIconMessage`, `ICN#`), so a DOS disk shows PC
Exchange's icon.

**Shutdown.** `ShutDwnPower`/`ShutDwnStart` (and `ROMlib_OurClose`) unmount
every foreign volume through its file system, so what it keeps (the FAT,
its `FINDER.DAT`) is written back; a busy volume is flushed.

## Fixed on the way

- `PBControl` to an image drive (refnum -101) set no `ioTrap`, and the
  answer's completion test read the caller's: the Finder's stack parameter
  block had the asynchronous bit and a garbage completion pointer there
  when it asked the DOS drive for its icon, and Executor jumped to it.
- At the first launch (the Finder) Executor closed every named open file
  outside the System's resource chain; it now leaves a foreign file
  system's own paths alone.
- `$A074`, an OS trap no interface file names, called by PC Exchange 2.1.1's
  INIT with D0 = 4 and an 80-byte block (it keeps the long at `+$1C` as a
  transfer size above which its I/O goes another way, 0 without): answered
  `paramErr` (it was the fatal "unimplemented trap" alert).

## Not done

- **7.5.5.** The policy allows `PC Exchange @ 7.6`: 7.5.5's PC Exchange
  2.0.5 works on this FSM (its `CUST` 1000 FSM stays out, see FSVars) but
  with it in, the ASLM test crashes now and then under load (STRAGGLERS 62;
  its `_HFSDispatch` patch is the suspect).

- **The drive's icon kind.** PC Exchange picks its icon from the driver's
  `'devt'` (Driver Gestalt); Executor's image drives have no driver unit
  (refnum -101), so it shows its floppy-like default, where the ROM machine
  shows the hard-disk one.
- **Formatting.** The Disk Initialization component (`fsdDICI`, PC
  Exchange's `FRMT`) is recorded but nothing calls it: Erase Disk offers no
  DOS format.
- **Ejecting.** `UTEjectVol` takes the volume off line; the image stays in
  its drive (Executor has no eject for images).
- **Selector 4** of `_FSMDispatch` and **`$A074`**: what they do is unknown.
- **A log-to-physical proc without the HLL bit** (register based): not
  called (logged as unimplemented); PC Exchange passes nil.
- Desktop database calls for a foreign volume go to its file system (PC
  Exchange keeps them in `FINDER.DAT`); `gestaltDTMgrSupportsFSM` is not
  set.
