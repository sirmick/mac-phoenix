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

What the Finder imports, by library (8.6 and 9.0.4 together), and who
provides each on a real Old World 8.6/9 Mac:

| Library | Unresolved | On a Mac | Executor |
|---|---|---|---|
| InterfaceLib | 41 of 664 | ROM / System patches | Executor's |
| AppearanceLib | 52 | ROM / System patches (Appearance Manager) | missing: not in the definitions |
| PrivateInterfaceLib | 35 | same | 10 implemented, under InterfaceLib only |
| DragLib | 29 | same | defined as `DragDispatch` traps; Apple's 68k Drag Manager answers them on 7.x; no PowerPC export |
| IconServicesLib | 26 | same | missing |
| Translation | 17 | same (Translation Manager / Easy Open) | missing |
| WindowsLib | 17 | same (Window Manager 2.0) | missing (`InvalWindowRect` C side only) |
| ThreadsLib | 14 | same | all 14 implemented, exported from InterfaceLib only |
| ObjectSupportLib | 7 | same | all 7 implemented (CFM-68K only) |
| ControlsLib, MenusLib, MacDialogsLib | 6, 5, 1 | same | missing (32-bit control values, menu item enabling, ...) |
| Memory Manager (MM*), FontManager, FontObjects, ToolboxUtils, DesktopItemsLib | 5, 2, 2, 1, 1 | same | missing |
| other fragments of the Finder | 11 | the Finder's own | `FragRelease`, `GetProcessTrapAddress`, AppleTalk (`ATEvent`, `MPPOpen`), `RealScript` |
| SoundLib, TextCommon, UnicodeConverter, QuickdrawLib, Math64Lib, VideoServicesLib, PCCardLib, ... | — | `'nlib'` resources in the System file (21 on 8.6, 28 on 9.0.4) | not registered (SoundLib is Executor's since STRAGGLERS 69) |
| ContextualMenu, FileSharingLib, ColorPickerLib, AppleGuideGlueLib, ... | — | Extensions (`'cfrg'`) | Apple's file, when the policy allows it |

So the Toolbox libraries are Executor's job, as InterfaceLib is: what
ships in files on 8.6/9 is extensions' and the System file's `'nlib'`s.

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
* The unresolved imports of a session: `CFM: unresolved import NAME` in
  the log; calls to them: `[unimplemented] PowerPC NAME`.
