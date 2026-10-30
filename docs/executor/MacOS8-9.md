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

**Where it is now:** the 8.6 Finder runs and stays up: its menu bar
(File, Edit, View, Special, the Help and Application menus), the desktop
pattern and all its icons (both volumes, the aliases, the Trash) with
their names. Its windows open (2026-10-30): double-clicking the disk
opens "Macintosh HD" with its folders and "9 items, 302.4 MB available",
in Apple's WindowDefinitions and ControlDefinitions on Executor's
Appearance Manager drawing, Window Manager 2.0 and Control Manager 8.0
(docs/executor/Appearance.md). What it took besides those:
`VM_GetHoldableBytes` (the Finder sizes its offscreen drawing by it; 0
threw `memFullErr` and nothing was drawn) and Gestalt `'thds'` bit 2
(ThreadsLib present): without it the Finder never takes its thread task
reference, and the thread that fills a window waits on that reference
forever.
The 9.0.4 Finder runs as far (no extensions), then says 'The disk ""
cannot be used, because an error of type -35 occurred'.

Called and still unanswered (8.6, 2026-10-30): `GetQDGlobals`,
`QDGet`/`SetPatternOrigin`, `LMGetBrianBits`, `LMGetCurLayer`,
`IsMenuItemEnabled`, `SetSubmenuParentChoosable`, MenuDispatch $13; Icon
Services' `SetIconDevice`; the PPC Toolbox (`PPCOpenSync`,
`PPCInformAsync`), `GetWDInfo`, `CRMSearch`, OSDispatch $75 (14 argument
bytes, after `GetProcessInformation` on the Finder itself), a trap called
through its address (procInfo $02671822). At startup the Finder also
writes to a file Executor opened read-only on the host (162 `short write`
lines, `EBADF`).

## The 8.6 grid (2026-10-30)

`tools/executor/grid.py 8.6.img --jobs 3` runs everything on the image
that can be opened, one item per fresh session on the 8.6 System
(`--executor-ppc --bridge`, BridgeAgent launching it by path; a control
panel or desk accessory goes to the Finder as an `'odoc'`, a double-click):
105 items (control panels, desk accessories, applications, AppleScript
applets). It records the front process, a screenshot, a crash and the
calls Executor answered as unimplemented; results in
`~/storage/executor-grid/8.6/` (`grid.md`, `grid.json`, `NNN.png`,
`NNN.log`). The screenshots are the verdict: "runs" only means no crash.

Second run, by looking at the screenshots:

* **Work** (their windows, controls drawn): control panels Map,
  Monitors, Sound, ColorSync, DialAssist, General Controls, Launcher,
  Modem, Mouse, Numbers, Speech, Startup Disk, TCP/IP, Text; Calculator
  and Chooser; SimpleText, Note Pad, Scrapbook, SimpleSound, Stickies, Disk
  First Aid, Drive Setup, Web Sharing, Graphing Calculator, Netscape
  Import, Address Book Export, DropStuff, Microsoft Internet First Run.
* **Say what this Mac lacks, as a Mac would:** no Zip drive (Iomega
  Tools), FM tuner, CD-ROM drive (AppleCD Audio Player), Display Enabler
  (DigitalColor Meter, Monitors & Sound), QuickTime (MoviePlayer,
  PictureViewer, QuickTime Settings), the extensions Executor's policy
  leaves out (Internet Config, Location Manager, Control Strip, Remote
  Access, AppleTalk; AppleScript for Script Editor and the 31 applets;
  Extensions Manager's own startup extension, its Gestalt `'8INI'`, which
  it reports as "must be running System 8.0").
* **Executor's to fix:** host crashes in Mac OS Setup Assistant, Register
  StuffIt Expander, Appearance, Energy Saver, Keyboard and Sherlock (an
  application heap that runs out: unchecked `NewHandle`s in QuickDraw's
  region code); Network Browser (a trap it finds unimplemented); Internet
  Explorer and Outlook Express (their shared libraries don't load); File
  Exchange's preferences, the Users & Groups Data File (File Sharing,
  Users & Groups); Desktop Printer Utility (error -50); applications that
  start and show nothing (Disk Copy, StuffIt Expander 6, Netscape
  Communicator, Talkback, the Internet Setup assistants, Memory, Apple
  Menu Options); dimmed static text drawn as hatching (Modem, TCP/IP).

Third run (`~/storage/executor-grid/8.6-r3/`): no host crashes.

* **Partitions** are what 8.6 gives: the SIZE preferred size + 56K, the
  stack `DefltStack` + 16K (9.0.4: + 68K; 7.5.5 keeps + 16K), measured on
  the G3 by `tests/guest/partprobe` (ctest `partition_rom_ppc_86`,
  `partition_executor_86`). With 7.5.5's rule small applications ran out
  of heap at their first alert: Register StuffIt Expander (100K, a 64K
  buffer of its own) now shows its alert as on the G3. Where a heap is
  full anyway, Executor fails as a Mac does instead of crashing:
  `NewWindow`, `GetCTable`, `NewRgn` answer nil, QuickDraw's region
  builder and Icon Utilities' scratch fall back to the system heap,
  `AppendMenu`/`SetMenuItemText` don't write past a menu that can't grow.
  `--executor-debug allocs` logs every failed allocation (zone size, free)
  and each partition's layout.
* **Calls that left their outputs alone.** An unimplemented PowerPC
  import answers 0, which callers take for noErr with the outputs set:
  the Appearance panel took stale stack for a desktop pixpat and for a
  theme variant list, which it then disposed of (one of its own
  controls). WindowsLib's desktop calls and AppearanceLib's theme calls
  are Executor's now (signatures from the panel's call sites); Appearance
  opens, its tabs not drawn right yet (the tab control, panes overlapping).
* Energy Saver says its extension isn't running (as on a Mac without it);
  Disk Copy shows its menus (`SetMenuBar`'s copy reused the old master
  pointer, so the bar looked drawn); Mac OS Setup Assistant and Sherlock
  start, no window yet.
* User Profile Manager launches Communicator with an `'appa'` coercion of
  a `'MOSS'/'prfl'` event (Executor's coercion was an old private format
  that read only `'odoc'` lists); an application's fragment carries its
  `'cfrg'` name, which the libraries in Communicator's file import from.
  Communicator itself still quits: its allocator's pools don't fit in the
  12.5MB partition once its ~10MB of code is loaded there. Open: whether
  8.6's Process Manager adds a PowerPC application's code to the
  partition with virtual memory off (Communicator runs on the G3).
* Date & Time: the menu bar clock calls (MenusLib, weak imports it makes
  from 8.2 on) are answered; it now spins in Apple's WindowDefinitions
  drawing its title (no traps: needs PC sampling). With Charcoal (the
  PC sampler, 2026-10-30): in the System file's GlyphsCache (Apple Type
  Services, `'cfrg'` 52), section 0 +$14..+$2c, a skip-list search that
  never meets its end node: the glyph cache ATS keeps isn't set up as 8.6
  sets it up (FMSharedCaches, FontObjects); its menus show, no window.

Differential probes (docs/executor/DiffTest.md) now ask Apple's 8.6
libraries and Executor's the same questions: `appr` (the Appearance
Manager) found GetThemeSoundMask writing a long too many into its
caller's frame, the Code Fragment Manager's calls missing under
`CodeFragmentMgr` (Retro68's stub library), and the gaps listed in
`tests/guest/difftest/expected/appr-executor-8.6.txt` (regions Executor
draws as rectangles, the theme collection's missing items, invalid
brush and text colour numbers it answers, the platinum drawing). The
census (`libraries.py --census`) ranks what 8.6 sessions call that
Executor lacks: GetWDInfo, SetSubmenuParentChoosable, IsMenuItemEnabled,
GetQDGlobals and the PPC Toolbox first.

Fonts (2026-10-30, the `text` probe: every line as 8.6's). 8.6's system
font is Charcoal, from the Roman `'itlb'`'s extended record
(`itlbSysFond`, which also gives the Script Manager's font variables);
Charcoal is TrueType only, and Chicago, Monaco and Palatino have outlines
for the sizes they have no strike of. Executor scales TrueType now
(`quickdraw/sfnt.cpp`, from the TrueType Reference Manual: no hinting,
pixel centres with dropout control): an outline at a size becomes an
NFNT strike, so QuickDraw draws and measures it as a bitmap font; widths
come from `'hdmx'`, metrics are 8.6's (descent rounded half down, widMax
the advance maximum rounded up). The Font Manager looks at every FOND of a
family (8.6's System file has its own Chicago, Geneva and Monaco FONDs,
which hid the suitcases' sizes and outlines), takes styles' extra widths
from the FOND's style property table when its flags say so (Chicago 12
shadowed is 1 wider, not 2), and FMSwapFont answers the strike's metrics
with the style in bold/shadow/extra (GetFontInfo adds it). 8.x measures a
carriage return as 0 and keeps a bitmap glyph of no width so in any style.

Gestalt, FindFolder and the classic controls (2026-10-30, `sys` and
`ctl`). The per-version table (gestalt.cpp's `since_table`, and
`gestalt_set_system_version` for selectors 7.x answers too) gives 8.6's
values where Executor has what they claim: the 8.5 Menu Manager, Mixed
Mode, partial resources and override maps, the Script Manager at the
System's version, Apple events with the Object Support Library in the
System, Gestalt 7, no MFS, HFS Plus volumes (8.1); the extended Time
Manager and the Notification Manager on every version. Not yet: `'wind'`
(Window Manager 2.0 isn't complete: SetWindowBounds, WindowPathSelect),
QuickDraw 2.5 (QDGetPatternOrigin), the Folder Manager's descriptors. An
8.x startup makes Application Support, Contextual Menu Items and Cleanup
At Startup, as 8.6 has them. The classic push button, check box and radio
button take the 8.0 messages as 8.6's do (features, data, best rectangle:
the font's line plus 4 or 6, title plus 16 or 20, 58 at least for a push
button).

Still open: Keyboard ("Keyboard Preferences file is in use", after it
scans the Preferences folder: its 68k code decides why); Memory (no
window; its RAM Disk trap `$A074`, selectors 9-13, is handled as absent
by the panel); Network Browser (Apple's Navigation Services calls OS trap
`$A08E`, selector 1 with a 40-byte block, unknown); StuffIt Expander 6 is
a Carbon application and the image has no CarbonLib (on a Mac it
wouldn't open either; Executor should fail the launch with "CarbonLib
could not be found" rather than stub every import); the rest of the
list above.

What the first run needed fixed (none of the classic control panels or
desk accessories opened): the Finder never got the bridge's `'odoc'`
(Executor's `AESend` dropped events for other processes; and the
Finder's desktop window had endless update events, Executor's
`DMDrawDesktopRegion` making the desktop an update again while the
Finder drew it); PrivateInterfaceLib's resource map calls the Finder
opens a control panel with (`OpenResFileUnderSystemMap`, `GetMap`,
override attributes); Apple event records with a null descriptor; Gestalt
`'sysa'` (PowerPC on 8.x) and `NewFatRoutineDescriptor`; the Dialog
Manager's 8.0 calls (`StandardAlert`, `NewFeaturesDialog`, items as
controls, `AppendDialogItemList`, timeouts, event masks; DialogMgr8.yaml).

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

4. **Tools** (done 2026-10-28). Executor's export tables as CFM binds
   them, written by the executor_tests export test
   (`build/executor-exports.json`), which `tools/macdecode/libraries.py`
   reads instead of guessing from the YAML (it undercounted by about 800:
   `lowmem:` entries' `LMGet`/`LMSet`, PB calls' `Sync`/`Async`, trap flag
   variants). Internal names are no longer exported (`_NewPtr_flags`,
   bare `PBRead`, `INVALID_GetTrapAddress`: `~` in `variants:`).
   `libraries.py --coverage --rom ROM --system FOLDER`: against Apple's
   libraries in the user's ROM and System file, read each time. See
   LIBRARIES.md, "Seeing where things stand".
5. **Data exports in the YAML** (done 2026-10-28). `data:` entries: name,
   type, library and a getter for the storage; `extern` in the C headers
   (`Components.h`: `extern UniversalProcPtr CallComponentUPP;`).
   `CallComponentUPP` and StdCLib's and MathLib's data (`errno`, `_iob`,
   `pi`, ...) are declared there now; nothing is exported from C++ alone.
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

   Found on the way (2026-10-28): the System's PowerPC libraries call 68k
   traps through the trap tables in low memory (OS traps at $0400, Toolbox
   traps at $0E00: ATSSupport reads $0690, `_HeapDispatch`; CollectionsLib
   $1DD8, `_CollectionMgr`) with `CallOSTrapUniversalProc` /
   `CallUniversalProc`. Done: `CallOSTrapUniversalProc` (MixedMode.yaml);
   `_HeapDispatch` selector $20 (a heap for system-wide allocations, from
   how ATSSupport uses it: Executor answers temporary memory;
   `export: false`, the name being ours); Mixed Mode logs a PowerPC call to
   a trap Executor lacks (procInfo, first argument, caller) and answers
   paramErr instead of `_Unimplemented`'s fatal alert.

   The Collection Manager (done 2026-10-28; `_CollectionMgr` $ABF6,
   CollectionsLib's 39 routines; Collections.yaml, collections.cpp,
   docs/executor/CollectionManager.md). 7.6.1's System file has no
   Collections or CollectionsLib (8.6's has both), and nothing on 7.x has
   called $ABF6 on Executor: Apple's 8.x libraries, run as they are, bring
   it in. CollectionTest (tests/guest/collections, Retro68 PowerPC) passes
   49/49 on Executor 7.6.1 and on the G3 with 9.0.4
   (`collections_ppc_executor_761`, `collections_rom_ppc_904`); Apple's
   9.0.4 settled what Inside Macintosh leaves open (items in tag and ID
   order; replacing keeps the attributes; the lock protects the data only;
   a flattened stream read through its offsets). Apple's 9.0.4 writes
   wrong offsets for an item replaced by larger data; Executor's are right.

   Then: `BlockZero` (MemoryMgr.yaml; its stub not zeroing ended in a
   corrupted heap); `ExpandMem`'s accessors from PrivateInterfaceLib
   (`library:` on a `lowmem:` entry; Executor called it `AE_info`);
   `HiliteColor` before `InitGraf` does nothing (AppearanceLib, prepared
   when the Finder loads it, calls it before the Finder's InitGraf). The
   8.6 Finder then reaches its event loop, calling Window Manager 2.0's
   and the Menu Manager's routines (step 7) on the way.

   Then `PortList` (QuickDraw's open ports, $D66): made at startup in the
   system heap, not by the first InitGraf, and its accessors are
   PrivateInterfaceLib's. AppearanceLib walks it to set each port's
   highlight colour; it got 0 and wrote a colour into the driver unit
   table (`--watch` found it: its report now gives the PowerPC's state at
   its last call out). For CFM-68K code, a library Executor answers with
   InterfaceLib's table (PrivateInterfaceLib & co.) has its own entries
   merged in. Now the 8.6 Finder runs its event loop (update and activate
   events) for a second, then quits by itself: no Apple event, no alert,
   an orderly teardown (`--executor-logtraps --executor-logtraps-nesting 8`:
   its work runs on Thread Manager threads). Next: what it decides that
   on. Also seen: a font file's resource map written back through a
   read-only fork (`Fonts/%Andale Mono`: "short write ... errno 9"), which
   a Mac would refuse with a permission error.

   Then, why the Finder quits (2026-10-28; `--executor-logtraps-filter`
   keeps the slowdown small enough that the race stays, and PowerPC
   calls in the log now show their caller):
   - Gestalt answers per System version (gestalt.cpp, since_table):
     `'appr'` 1, `'apvr'` $110, `'icon'` $1F (8.5), `'cfrg'` 1, `'cltn'`
     $01108000 on 8.x, values as 9.0.4 gives them (a probe on the G3).
   - `_CodeFragmentDispatch` ($AA5A) is there for 68k code on 8.x (a
     PowerPC-only System; 7.x keeps going without it). The Color Picker
     INIT then calls its private selector -20 with its file's FSSpec: CFM
     is told of a file with fragments; answered noErr
     (`CFragRegisterFilePrivate`, `export: false`).
   - `InsXTime`. `GetThreadStateGivenTaskRef` and
     `SetThreadReadyGivenTaskRef` answer threadProtocolErr for a task
     reference no process has, as 9.0.4 does (the Finder polls its main
     thread with 0).
   - The quit itself: the Finder's startup throws -35 (nsvErr) and its
     main function, catching it, tears down without an alert (9.0.4's
     Finder shows the alert, same error). The -35 comes from Icon
     Services: every call fails (-35, -50). IconServicesLib is glue to
     `$AA75`, which ToolboxUtils' init points at the System's native
     Icon Services code; that code's state is set up by the System's own
     startup, which Executor doesn't run, so it works from nil globals
     (an FSSpec read at $28). Decided: Executor's own Icon Services.

   Icon Services (done 2026-10-28; `_IconServicesDispatch` $AA75,
   IconServicesLib's 37 documented routines; IconServices.yaml from
   Icons.h, iconservices.cpp, docs/executor/IconServices.md): IconRefs
   over Executor's records of icon families, the System's icons from the
   System file's icon resources, applications' from the desktop database,
   files' and folders' custom icons, drawing and hit testing through Icon
   Utilities' suites. IconTest (tests/guest/iconservices) passes 34/34 on
   Executor 7.6.1 and on the G3 with 9.0.4 (`icons_ppc_executor_761`,
   `icons_rom_ppc_904`); 9.0.4 settled the owner counts (system icons are
   shared; registering twice is one more owner; unregistering invalidates
   at once; a composite holds its parts), the generic icon for an unknown
   creator/type, paramErr for a missing family element. With it the 8.6
   Finder stays up (menu bar, desktop pattern); it learns of volumes
   through calls still missing (RequestVolumeNotification, the PPC
   Toolbox, ...). Also Window Manager 2.0's
   InvalWindowRect/Rgn and ValidWindowRect/Rgn (WindowsLib, found on 8.x
   only).

   Where the desktop stands (2026-10-28): the Finder finds the mounted
   volumes in the VCB queue (GetVCBQHdr; volumes -1 and -2), registers
   for volume notifications (RequestVolumeNotification: a routine,
   procInfo $E0, given a block with an event code at 4 and the vRefNum at
   6; accepted, nothing sent yet), gets the startup disk's icon
   (GetIconRefFromFolder) and draws it at the top right, in its desktop
   window (the screen, its visible region the whole desktop). It then
   repaints that column's background (DMDrawDesktopRegion, a region that
   includes the icon), invalidates its desktop window, and draws nothing
   between BeginUpdate and EndUpdate: what it draws on an update is the
   next thing to find. The Trash and the other volume are never drawn.
   Update (2026-10-29): `GetIconRefByName` was the missing piece (private
   IconServicesLib selector $06). It answers from the registered IconRefs,
   and for `'macs'` from the System's icon of that type, registering it;
   the Finder's desktop code asks for `'trsh'`, the System's startup for
   `'docu'`, `'fldr'`, `'APPL'` and the rest. With the selector exported
   but answering `noSuchIconErr` those lookups failed and the Finder quit
   ~2 s after launch; unresolved it got 0 and kept going without icons.
   Now the Finder stays up, its desktop keeps its icons (the same frame at
   6 s and 12 s), and it plots the startup disk's icon at
   (576,28)-(608,60).
   That run's icons came from an out-of-memory alert's update, though;
   without it the desktop stayed empty. Three things (2026-10-29):
   - `SysZoneFloatSizes` (PrivateInterfaceLib, no trap): the Finder's
     memory check, before every operation, wants the System heap's
     largest block or its growth room above ExpandMem+$1AE (Executor's
     reserve, 64K) plus the request, else "There is not enough memory".
     A probe on 9.0.4 (G3) settled the two results: the free block at
     the top of the System heap (not MaxBlock) and how far the heap can
     grow (about TempFreeMem); either pointer may be nil; returns 0.
     Executor answers its top free block and `ROMlib_syszone_reserve_left`.
   - MDEF 63, 8.6's menu definition for every menu: an accelerated
     resource (a routine descriptor, relative and "needs preparing", over a
     small PEF fragment importing MenusLib's `StandardMDEF`). Mixed Mode
     prepared it in place at the first call, and the purgeable resource
     then moved (the Menu Manager locks it only for the call): the
     record's transition vector pointed at the old code, and the next
     CalcMenuSize ran heap data as PowerPC code, the Finder's main thread
     lost for good, the desktop never redrawn. Mixed Mode now records
     where it prepared each record and prepares it again where it is
     found next. `StandardMDEF` is Executor's MDEF 0 (MenusLib).
   - `LMGetEventQueue` returned a copy of the queue header through an
     argument (the generated form for a struct), Apple's returns its
     address (`QHdrPtr LMGetEventQueue(void)`): the Finder's idle code
     wrote through nil (a host crash). The queue headers' accessors
     (EventQueue, VBLQueue, DTQueue, DrvQHdr, VCBQHdr, FSQHdr) are
     `accessor: pointer` now. Also `Microseconds` and `GetTime` for
     PowerPC callers.
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
* `--executor-debug pcsample`: the PowerPC PC at the end of the core's
  time slices (every 5 ms at most); every 5 s the twelve most frequent
  addresses as `Fragment sect N+$off (Export+$off)` (cfm.cpp's
  `ROMlib_cfm_describe`, on CountSymbols/GetIndSymbol). For code that spins
  without calling the Toolbox; the fragment's container (its `'cfrg'`
  entry's offset and length) goes to `pef.py --dis`.
* `tools/executor/grid.py IMAGE --rom g3`: the grid on Apple's code (the
  ROM machine, a fresh copy of the image per item), the reference
  screenshots; `grid_compare.py REF EXECUTOR --html FILE` puts the two
  side by side.
