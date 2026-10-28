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
* Then (steps 1 and 2 below, "The Finder starting"): the 8.6 Finder ran
  to its menu bar and desktop pattern with 173 imports unresolved.
* Now (step 3, 2026-10-28): CFM finds 8.x's Toolbox libraries in the
  System file. With Executor keeping the core managers, Apple's
  IconServicesLib, AppearanceLib, ToolboxUtils, PreferencesLib,
  Translation, FontManager, FontObjects, ATSUI, NavigationLib,
  CollectionsLib and TextCommon/UnicodeConverter prepare and initialize;
  the session then stops in `NewPtr` with no current zone (a library's init
  setting the zone from an accessor Executor lacks). They import about 170
  routines Executor lacks between them, most of them the ROM's (step 6).

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
| The system's own (no file: in fact the System file's data fork, step 3) | AppearanceLib (217 exports), WindowsLib (129), ControlsLib (83), MenusLib (45), MacDialogsLib (6), DragLib (43), IconServicesLib (50), Translation (58), ThreadsLib (31), ObjectSupportLib (13), FontManager (77), FontObjects (41), ToolboxUtils (135), NavigationLib (19), FoldersLib (14), CollectionsLib (39), ControlStripLib (26), ATSUnicodeLib (101), LocalesLib (13), DriverServicesLib (224), DebugLib, StdCLib, MathLib | the same (AppearanceLib 218, WindowsLib 135, ControlsLib 99, MenusLib 50, MacDialogsLib 10, IconServicesLib 53, FontManager 114, FontObjects 67), and PrivateInterfaceLib (1852), NameRegistryLib (70), DesktopItemsLib (4), StringCompare, NumberFormatting | Executor's job: StdCLib and MathLib are; the rest are missing or under InterfaceLib only |
| System file `'nlib'` | TextCommon, UnicodeConverter, SoundLib, QuickdrawLib, Math64Lib, VideoServicesLib, DriverLoaderLib, CursorDevicesLib, UTCUtils, StringCompare | the same and PPCToolbox, DebugLib (not StringCompare) | not registered (SoundLib is Executor's) |
| Extensions file | ContextualMenu, FileSharingLib, ColorPickerLib, AppleGuideGlueLib, AppleScriptLib, OTClientLib & co., QuickTimeLib, TextEncodingConverter | the same | Apple's file when the policy allows it; OT's and AppleScriptLib's are Executor's |
| Not there | DialogsLib, DesktopItemsLib, PCCard, PPCToolbox, CarbonLib (-2821) | DialogsLib, PCCard, CarbonLib | — |

Loading "Memory Manager" (the Finder's `MMNewHeap` & co.) hangs
`GetSharedLibrary` on both, so it is probed last.

By version (LibProbe on the G3, extensions off, 7.6.1 too: the table
the version gating of "One Toolbox for 7, 8 and 9" follows; numbers are
exports):

| | Libraries |
|---|---|
| On 7.6.1 already | InterfaceLib (2539), PrivateInterfaceLib (929), DragLib (40), ThreadsLib (33), ObjectSupportLib (13), Translation (34), NameRegistryLib (32), StdCLib, MathLib, SoundLib, QuickdrawLib, Math64Lib, DriverServicesLib, DriverLoaderLib, VideoServicesLib, CursorDevicesLib, ColorPickerLib, AppleGuideGlueLib, AppleScriptLib, Open Transport's, QuickTimeLib |
| New in 8.x (8.6) | AppearanceLib, WindowsLib, ControlsLib, MenusLib, MacDialogsLib, IconServicesLib, FontManager, FontObjects, ToolboxUtils, NavigationLib, FoldersLib, CollectionsLib, ControlStripLib, ATSUnicodeLib, LocalesLib, TextCommon, UnicodeConverter, TextEncodingConverter, ContextualMenu, FileSharingLib, UTCUtils, StringCompare, DebugLib |
| New in 9.0.4 | DesktopItemsLib, PPCToolbox, NumberFormatting |
| Grown | PrivateInterfaceLib 929 → 1852 (9.0.4), Translation 34 → 58 and FontManager 77 → 114 (8.x), NameRegistryLib 32 → 70 (9.0.4) |

So steps 1 and 2 below (ThreadsLib, ObjectSupportLib, PrivateInterfaceLib,
DragLib) are libraries 7.x has too: Executor answers them on every
System. The ones new in 8.x and 9 are to be found only when Executor runs
that System (CFM answers "not found" on 7.x, as a 7.6.1 Mac does).

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

## The Finder starting: what it needed (2026-10-28)

The minimum, found by running the 8.6 Finder until it stops and taking
what it called last (unresolved imports answer 0 and log
`[unimplemented] PowerPC NAME` when called):

* **The "Memory Manager" library** (new in 8.x: 8.6 has it, 7.6.1 hasn't;
  `defs/MemoryManagerLib.yaml`, `mman/mmlib.cpp`): `MMNewHeap` (the
  "Finder Object Heap"; 0 makes the Finder call `SysError(413)`),
  `MMAllocateIn`, `MMAllocateClearIn`, `MMFree`, `MMGetFreeSpace`,
  `MMGetDefaultHeap`, their forms from the Finder's calls. The first
  library the version gating applies to (builtinlibs: found from System
  8.0). The generator takes a library name that isn't an identifier
  (`LIBRARY_FUNCTION_NAMED`).
* **Thread Manager threads in PowerPC code.** A thread's entry, switcher
  and termination procedures from a PowerPC caller are transition vectors
  (their first word in a loaded PowerPC fragment's code:
  `ROMlib_cfm_ppc_code`), run with `callPPC`, the entry on the thread's
  stack. A thread switched away from inside PowerPC code keeps its
  registers, mode and depth, and the core's execution in progress:
  `cpu::Core::save_exec_context` (MAME's x86-64 back-end keeps the host
  stack it entered on and FPU bookkeeping in shared fields, which nested
  calls restore in order but coroutines resume out of order; arm64 the
  same but for the stack). Process switches keep it too. The first
  process holds the Process Manager's baton from the start, so its
  threads can switch before any process has. ThreadTest
  (`tests/guest/threads`: two threads yielding, switchers, a terminator,
  results) passes 11/11 on Executor and on the G3 (`threads_ppc_executor_761`,
  `threads_rom_ppc_761`).
* **Accelerated code resources.** The Finder's MDEF is a routine
  descriptor before a PEF container (routine flags
  `kProcDescriptorIsRelative`, `kFragmentNeedsPreparing`, now in
  `MixedMode.yaml`): Mixed Mode prepares the fragment at its first call
  and the record then names its main routine (`mixed_mode.cpp`).
* **`NewDesktopLayerOwner`** (PrivateInterfaceLib): 8.x's
  `TakeDesktopLayer`, the desk layer's window for the Finder's desktop.
* **Low-memory arrays' accessors** (`LMGetSysResName`, `LMGetCurApName`
  and 13 others) exported from InterfaceLib, giving the address as
  Apple's do.

* **A selector Executor lacks, through Mixed Mode**: PowerPC code calls
  some traps with `CallUniversalProc` on the trap's address (8.6 calls
  `OSDispatch` selector $75 that way, from the desktop printing libraries
  the Finder loads). Mixed Mode knows the call's argument and result
  sizes from the procinfo, so a dispatcher it calls directly with an
  unknown selector answers paramErr and pops them, as a Mac's does,
  logged, instead of stopping the session (`traps::mixed_mode_call`).

**Where it is now:** the 8.6 Finder runs: its menu bar (File, Edit,
View, Special) and the desktop pattern, no disk icons yet (Icon
Services), a black area where a window's Appearance drawing should be.
The 9.0.4 Finder runs as far (no extensions), then says 'The disk ""
cannot be used, because an error of type -35 occurred'.

Called and still unanswered (8.6, 36 calls): `RegisterAppearanceClient`,
`GetViewsFont`, `SetThemeTextColor`, `SetThemeBackground`, `StandardMDEF`,
`SetMenuItemKeyGlyph`, `SetSubmenuParentChoosable`, `PopupWindowEntry`;
the window calls `GetWindowRegion`, `InvalWindowRect`/`Rgn`,
`ValidWindowRect`, `SetWindowModified`, `Get`/`SetWindowProxyIcon`,
`SetWindowProxyFSSpec`; Icon Services' `GetIconRefByName`,
`GetIconRefFromFolder`, `RegisterIconRefFromIconFamily`, `CompositeIconRef`,
`PlotIconRef`, `IconRefToRgn`, `SetIconDevice`; `RegisterToolboxListener`,
`RequestVolumeNotification`, the PPC Toolbox (`PPCOpenSync`,
`PPCInformAsync`), `InsXTime`, `GetWDInfo`, `CRMSearch`,
`CallOSTrapUniversalProc`. Then a host crash in `EqualString` with a nil
string (before the accessors).

## One Toolbox for 7, 8 and 9

Executor stays one Toolbox, a superset; it doesn't grow an implementation
per System version.

* **New managers are additions.** The Appearance Manager, Window Manager
  2.0, Icon Services, the Translation Manager, the 32-bit control and menu
  calls came beside System 7's calls, which kept working. Each is
  implemented once; 7.x code doesn't call them (applications ask Gestalt
  first).
* **What the System says is per version.** Gestalt and the libraries CFM
  finds follow the System Executor runs on (`'sysv'` from its `'vers'`):
  a table of which feature exists from which version (Appearance 8.0,
  HFS+ 8.1, Icon Services, Window Manager 2.0 and built-in Navigation
  8.5, ...), not separate code. On 7.x they stay absent, so 7.x
  applications see what they see now (offering them there, as a Mac with
  the extensions would, is a later choice).
* **The look is the one fork.** 8 and 9 draw with the Appearance
  Manager's theme (platinum); Executor's window, control and menu
  definition procedures draw System 7's. The theme drawing calls
  (`DrawThemeWindowFrame` & co.) do the drawing, and the definition
  procedures call them when the theme is on, as Apple's do: one drawing
  layer with two styles. 8/9 run with System 7's look first; platinum
  comes after.
* **Behaviour is checked per version.** Where Apple's changed between
  versions, Executor follows the System it runs on, checked against that
  version on a ROM machine (8.6 and 9.0.4 on the G3, as 7.6.1 against the
  Quadra), as the extension policy's `@ 7.6` already does.
* **What ships as a file stays Apple's.** Extensions, the System's
  `'nlib'`s, the Finder: Apple's code on Executor's Toolbox, as on 7.x.
  8/9 put more there (TextCommon, UnicodeConverter, ContextualMenu), which
  is less for Executor to write.

## The work

Done:

1. **Library names for PowerPC** (2026-10-28). What Executor has, under
   8/9's library names, by family: `library:` on the definitions
   (`LIBRARY_ALSO`, as SoundLib's; a list for a library routine exported
   by more than one): the Thread Manager (ThreadsLib, 21), the object
   support calls (ObjectSupportLib, 13, InterfaceLib too), the layer
   calls, `TypeSelect*`, `Begin/EndSystemMode`, `AddAppleMenuItem`
   (PrivateInterfaceLib, 20). The 8.6 Finder's unresolved imports: 233 →
   202.
2. **Trap-routed exports** for selectors (2026-10-28). A subtrap's
   definition with `library:` and no Executor routine gets a PowerPC and
   CFM-68K export that sets the selector and calls through the
   dispatcher's table entry, as InterfaceLib's glue does (`TRAP_GLUE`).
   DragLib (39): Apple's 68k Drag Manager in the System answers them;
   DragTest (`tests/guest/drag`) passes 14/14 on Executor 7.6.1 and on the
   G3 with 7.6.1 and 9.0.4. The 8.6 Finder's unresolved imports: 202 → 173.
3. **The System file's own fragments** (2026-10-28). 8.x's Toolbox
   libraries "with no file" are PEF containers in the System file's data
   fork, listed by its `'cfrg'` resources (several, by machine):
   AppearanceLib, WindowsLib, IconServicesLib, Translation, FontManager,
   ... CFM looks there after Executor's own libraries. On the way: init
   routines run once the whole closure is prepared (8.x's libraries import
   each other in a cycle), a file a library is being prepared from stays
   open while its imports are looked up, the System's fragments go in
   temporary memory.

The direction is an Executor that eventually needs nothing of Apple's;
Apple's libraries from the System file are scaffolding until Executor's
own exist. Everything Executor exports is declared in the multiversal
YAML (the generated headers are Retro68's Multiversal Interfaces; the YAML
is the auditable list of what Executor implements).

Next, in order:

4. **Tools** (a small commit of its own). The generator writes the export
   list it builds (per library, PowerPC and CFM-68K: `lowmem:` entries as
   `LMGet`/`LMSet`, PB calls as `Sync`/`Async`, trap flag variants), and
   `tools/macdecode/libraries.py` reads it instead of guessing from the
   YAML (it undercounted by about 800). Internal names stop being exported
   (`_NewPtr_flags`, `_BlockMove_flags`, `INVALID_GetTrapAddress`, ...).
   Then coverage against Apple's export tables, read from the user's ROM
   and System file each time (not committed).
5. **Data exports in the YAML.** A declaration for a library's exported
   variable (`data:` with name, type, library), generated into the headers
   (`extern UniversalProcPtr CallComponentUPP;` as Retro68's
   `Components.h` has it) and the export list. `CallComponentUPP` moves
   to it from `cfm68k_glue.cpp`; `GetQDGlobals`, `gNotifyRectList` and the
   like use it.
6. **Everything the ROM gives.** What an Old World Mac's ROM (and the
   System's patches) exports, measured on the G3 ROM (InterfaceLib 2493
   exports, Executor 1763; PrivateInterfaceLib 928, Executor 16; the 8.6
   System adds about 230 to InterfaceLib, 9.0.4 more): InterfaceLib,
   PrivateInterfaceLib, NameRegistryLib, DriverServicesLib's application
   side (`UpTime`, `DurationToAbsolute`, atomic operations, `CStrCopy`).
   The hardware parts (ATA, Heathrow, ATI, PCI, power, the nanokernel's
   exceptions) answer as a Mac without the device; NQD is private
   (Executor's QuickDraw answers through InterfaceLib); Math64Lib is a
   System `'nlib'` (step 8). In batches, by how many containers on our
   images import each call (`libraries.py`):
   a. The open ones: the Finder's and Apple's libraries' unresolved
      imports; the Component Manager's calls for PowerPC code
      (`CallComponentFunction`, `CallComponentFunctionWithStorage`,
      `CallComponentFunctionWithStorageProcInfo`, `CallComponentCanDo`,
      `GetComponentVersion`, `ComponentSetTarget`, ...: Components.yaml);
      calls Executor implements for 68k code without a PowerPC export
      (`GetWDInfo`, `InsXTime`, `GetTime`, `PostEvent`, `UpperString`,
      `SleepQInstall`, `PBControlImmed`, ...); `Microseconds`,
      `BlockZero`, `Wide*`, `X2Fix`.
   b. The low-memory accessors (about 740 across both libraries):
      `lowmem:` entries, accessors under the right library.
   c. The rest our software imports: Text Services Manager documents,
      Mixed Mode helpers (`CallOSTrapUniversalProc`), CFM's `FragGet*`,
      NBP and the PPC Toolbox (as a Mac without AppleTalk), the emulator
      and Memory Manager internals (`SetEmulatorRegister`,
      `SysZoneFloatSizes`, `MakeExecutable`: nothing to do on Executor).
   d. The rest of the tables, with a test program where the behaviour
      isn't obvious.
7. **The Finder running**, with the add-on libraries selectable per
   library: Executor's (once finished) or Apple's from the System file.
   Executor keeps the managers 8.x's System replaces the ROM's with
   (WindowsLib, MenusLib, ControlsLib, EventsLib, MacDialogsLib, ListsLib,
   NQD: Apple's expect their private state; tried, they clash). Apple's
   for now: IconServicesLib, Translation, CollectionsLib, FontObjects and
   ATSUI, NavigationLib, TextCommon/UnicodeConverter, PreferencesLib,
   FoldersLib, ToolboxUtils. Each moves to Executor's column when Executor
   has it, tested against Apple's running on the same Executor (and on the
   G3). The Appearance Manager is Executor's own early (Apple's calls the
   Window Manager's and NQD's private hooks; the theme drawing is the one
   fork anyway, "One Toolbox for 7, 8 and 9"); then Window Manager 2.0 and
   the 32-bit control and menu calls in Executor's managers. On 7.x the
   choice exists where Executor's version does (or an extension file
   brings Apple's). A Finder test cell on 8.6 and 9.0.4.
8. **The System's `'nlib'`s.** Registered with CFM at startup, as the
   System does (TextCommon, UnicodeConverter, Math64Lib, ...), each
   checked: Apple's code or Executor's (SoundLib, QuickdrawLib, the sound
   hardware plug-ins: not Apple's).
9. **Extensions** (the 7.6.1 round again): Speech (`AliasDispatch $2F`),
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
