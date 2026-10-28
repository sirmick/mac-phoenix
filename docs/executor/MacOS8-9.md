# Mac OS 8.6 and 9.0.4 on Executor — recon and plan

Executor runs Apple's 7.5.5 and 7.6.1 System Folders with its own Toolbox
(`--executor-system`). Mac OS 8.6 and 9.0.4 are PowerPC-only, but nothing
in the design rules them out: Executor is the Toolbox (no Mac OS ROM, no
nanokernel), and PowerPC code runs on mame-ppc (`--executor-ppc`). This
is where they stand (2026-10-28) and the work, in order.

```bash
cp ~/storage/images/8.6.img ~/storage/images/os86-scratch.img    # a scratch copy
./build/mac-phoenix --backend executor --executor-system os86-scratch.img --executor-ppc --bridge
```

## Where it stands

Tried: 8.6 (`8.6.img`) with Executor's extension policy, 9.0.4
(`9.0.4.img`) with no extensions at all, 9.0.4 (`tests-9.0.4.img`) with
the policy.

* The System Folder is extracted from the HFS+ image and its System file
  opens; startup runs. 8.6's allowed extensions load cleanly (Color
  Picker, Speech Manager, General Controls, Launcher, Mouse, Speech).
  9.0.4's Speech INIT 0 calls `AliasDispatch $2F` (Alias Manager, 8.5 and
  later), which stops the session.
* Fixed on the way:
  * Executor told the Finder it was System 7.0 ("could not start up
    because the version of the system software installed was too old"):
    it knew Apple's System file by its `'lpch'` resources, which 8 and 9
    no longer have. A System file with `'boot'` resources is Apple's too
    (`version.cpp`); Gestalt `'sysv'` now answers 8.6 and 9.0.4.
  * The Finder "could not start up because the following system library
    could not be found" (no name): its launcher loads the Finder's own
    1.8 MB library, Executor's CFM allocated it in the 930 KB application
    heap and answered "not found". CFM now falls back to temporary memory
    (`cfm.cpp`, `cfm_new_ptr`), as Apple's does.
* Now: the Finder's library loads and runs, with 233 (8.6) and 266
  (9.0.4) of its imports unresolved (answered 0 when called; called so
  far: `MMNewHeap`, 9.0.4's `OpenResFileUnderSystemMap`), and Executor's
  About box comes up (its Apple-menu entry, which the Finder seems to
  open). Same picture on 8.6 and 9.0.4.

## Where the libraries come from

Measured on the G3 (mame-ppc) with Apple's 8.6 and 9.0.4, extensions off
(Shift held through startup: `test_guest_suite.sh --hold-key 56`), by
LibProbe (`tests/guest/libprobe`): for each library name, whether CFM has
it loaded already, whether it loads, its exports and where its code is;
then each one's source: the System file's `'nlib'` resources, an
Extensions file's `'cfrg'` (CFM finds those with extensions off too), or
neither, i.e. the system's own code (ROM, or the System file's patches).

| Source | 8.6 | 9.0.4 | On Executor |
|---|---|---|---|
| ROM | InterfaceLib, PrivateInterfaceLib, NameRegistryLib | InterfaceLib | Executor's |
| The system's own (no file) | AppearanceLib (217 exports), WindowsLib (129), ControlsLib (83), MenusLib (45), MacDialogsLib (6), DragLib (43), IconServicesLib (50), Translation (58), ThreadsLib (31), ObjectSupportLib (13), FontManager (77), FontObjects (41), ToolboxUtils (135), NavigationLib (19), FoldersLib (14), CollectionsLib (39), ControlStripLib (26), ATSUnicodeLib (101), LocalesLib (13), DriverServicesLib (224), DebugLib, StdCLib, MathLib | the same (AppearanceLib 218, WindowsLib 135, ControlsLib 99, MenusLib 50, MacDialogsLib 10, IconServicesLib 53, FontManager 114, FontObjects 67), and PrivateInterfaceLib (1852), NameRegistryLib (70), DesktopItemsLib (4), StringCompare, NumberFormatting | Executor's job: StdCLib and MathLib are; the rest are missing or under InterfaceLib only |
| System file `'nlib'` | TextCommon, UnicodeConverter, SoundLib, QuickdrawLib, Math64Lib, VideoServicesLib, DriverLoaderLib, CursorDevicesLib, UTCUtils, StringCompare | the same and PPCToolbox, DebugLib (not StringCompare) | not registered (SoundLib is Executor's) |
| Extensions file | ContextualMenu, FileSharingLib, ColorPickerLib, AppleGuideGlueLib, AppleScriptLib, OTClientLib & co., QuickTimeLib, TextEncodingConverter | the same | Apple's file when the policy allows it; OT's and AppleScriptLib's are Executor's |
| Not there | DialogsLib, DesktopItemsLib, PCCard, PPCToolbox, CarbonLib (-2821) | DialogsLib, PCCard, CarbonLib | — |

Loading "Memory Manager" (the Finder's `MMNewHeap` & co.) hangs
`GetSharedLibrary` on both, so it is probed last.

LibProbe on Executor (as the start application, `--executor-app`, with
8.6's and 9.0.4's System Folders and no extensions) loads 11 of these:
InterfaceLib, StdCLib, MathLib, SoundLib, AppleScriptLib, OTClientLib,
OTUtilityLib (Executor's), and FileSharingLib, TextCommon,
UnicodeConverter, TextEncodingConverter (Apple's files in Extensions,
found by CFM there). Everything in the "system's own" row is missing.

What the Finder imports and Executor lacks, by library (8.6 and 9.0.4
together): AppearanceLib 52, InterfaceLib 41 (of 664), PrivateInterfaceLib
35 (10 implemented under InterfaceLib only), DragLib 29 (defined as
`DragDispatch` traps, which Apple's 68k Drag Manager answers on 7.x; no
PowerPC export), IconServicesLib 26, Translation 17, WindowsLib 17,
ThreadsLib 14 (all implemented, exported from InterfaceLib only),
ObjectSupportLib 7 (all implemented, CFM-68K only), ControlsLib 6,
MenusLib 5, Memory Manager 5, FontManager 2, FontObjects 2, MacDialogsLib,
ToolboxUtils, DesktopItemsLib 1 each, and 11 from the Finder's other
fragments (`FragRelease`, `GetProcessTrapAddress`, AppleTalk's `ATEvent`
and `MPPOpen`, `RealScript`).

## The work

1. **Library names for PowerPC.** Export what Executor has under 8/9's
   library names: `library:` on the definitions (`LIBRARY_ALSO`, as
   SoundLib's): ThreadsLib (14), ObjectSupportLib (7, PowerPC too),
   PrivateInterfaceLib (10). Check: the unresolved list shrinks by 31.
2. **Trap-routed exports.** A definition with a trap or selector but no
   Executor routine (the Drag Manager's 29: Apple's 68k code answers them
   on 7.x; `InsXTime`) gets a PowerPC and CFM-68K export that calls
   through the trap table, as InterfaceLib's glue does. Then DragLib.
3. **The System's `'nlib'`s.** Register the System file's native
   libraries with CFM at startup, as the System does (TextCommon,
   UnicodeConverter, Math64Lib, ...), each one checked: Apple's code or
   Executor's (SoundLib, QuickdrawLib, the sound hardware plug-ins: not
   Apple's).
4. **InterfaceLib's 41.** The Text Services Manager's document calls, the
   PPC Toolbox and NBP (answer as a Mac without AppleTalk), the File
   Manager utilities (`UTLocateFCB` & co., `GetFSInfo`), `SleepQInstall`,
   `GetTime`, `Microseconds`, `CallOSTrapUniversalProc`,
   `CompareAndSwap`, `BlockZero`, ...: many are definitions without a
   PowerPC export (raw 68k traps, C side only).
5. **Mac OS 8's Toolbox.** The Appearance Manager (52: theme drawing,
   control embedding, `GetControlData`/`SetControlData`, bevel buttons,
   `StandardAlert`, theme sounds and cursors), Window Manager 2.0 (17:
   proxy icons, `TransitionWindow`, `WindowPathSelect`, `SetWindowBounds`),
   ControlsLib, MenusLib, MacDialogsLib: new definitions from Apple's
   documentation (Mac OS 8 Toolbox Reference) and C++ routines. The
   biggest piece; on 7.x they answer as a Mac without Appearance does.
6. **Icon Services** (26), the **Translation Manager** (17, the Finder's
   private Easy Open calls), **PrivateInterfaceLib**'s other 25 (resource
   maps under the System map, layers, volume notification), the MM*
   heaps, font objects, `RegisterToolboxListener`.
7. **The Finder running.** What it calls once the imports are there, as
   for 7.6.1's Finder (the About box first), checked against 8.6 and
   9.0.4 on the G3 (mame-ppc); a Finder test cell on each.
8. **Extensions** (the 7.6.1 round again): Speech (`AliasDispatch $2F`),
   then what each of 8/9's extensions needs.

## Tools

* `tools/macdecode/pef.py`: a PEF container's imports by library and
  symbol, TOC slots, an annotated PowerPC disassembly.
* `tests/guest/libprobe` (LibProbe, Retro68 PowerPC, `build-retro68.sh`):
  which libraries a system provides and where their code is;
  `MACEMU_ROM=~/storage/roms/g3.rom tests/test_guest_suite.sh --backend kpx
  --core mame-ppc --jit --image 8.6 --app tests/guest/libprobe/LibProbePPC.bin
  --results libprobe.txt --hold-key 56` (Executor: `--backend executor`).
* The unresolved imports of a session: `CFM: unresolved import NAME` in
  the log; calls to them: `[unimplemented] PowerPC NAME`.
