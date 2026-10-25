# Apple's own items on Executor: fix list

Triage of 2026-10-24: every application, control panel, desk accessory and
AppleScript applet on the stock `7.5.5.img` and `7.6.1.img` was opened and
closed under `--backend executor --executor-system <image>`, with a SimpleText
launch and quit between each. SimpleText never failed, so nothing below leaves
the system unusable unless it crashes Executor.

This file lists what **should work** (a Quadra 650 with the same disk runs
it) and doesn't. Out of scope: items that need hardware the machine doesn't
have (Energy Saver, Token Ring, Brightness, Trackpad, PowerBook panels, Serial
Switch, Apple Video Player, AppleCD Audio Player, Battery Recondition, ...) and
PowerPC-only Graphing Calculator. When in doubt about an item, compare with
the ROM machine on the same disk (`--backend uae`, `~/roms/quadra.rom`, a boot
*copy* of the image).

Same result on 7.5.5 and 7.6.1 unless marked.

## 1. Crashes (fatal: Executor stops)

| # | Item | Log | Notes |
|---|------|-----|-------|
| 1 | ~~**Keyboard** control panel~~ | `Unknown selector 0x30 for trap Pack6` | **Fixed**: `0x30` is a private TextOrder (`IUTextOrderPrivate`, `romlib/iu.cpp`) |
| 2 | ~~**Monitors** control panel; **Change Monitor to 256** applet (More Automated Tasks)~~ | `Unknown selector 0xf3 for trap DisplayDispatch` | **Fixed**: see below |
| 3 | ~~**Apple System Profiler** (7.6.1, Apple Menu Items)~~ | `Unknown selector 0x12 for trap FSDispatch` | **Fixed**: see below |
| 4 | ~~**TCP/IP** control panel (policy-disabled; opened by hand)~~ | `[ot] Open Transport: OTOpenConfigurationsDB (OTLib$CfigLib) is not implemented` | **Fails cleanly** (7.6.1): see below. On 7.5.5 (no OT installed) it says it needs Open Transport |
| 5 | **Password Security** control panel, intermittent | `host signal 11 at guest pc $c0cc9bf4`, last A-trap `$A949` | Seen once, opened right after **Numbers** had been opened and closed. Opened alone it correctly says "cannot be used with this Macintosh". The PC is in the host stub area (`7101` words). Try Numbers then Password Security repeatedly |

Unknown selectors (`romlib/base/traps.cpp`, `unknown_selector_68k`): each
one a session meets is logged once as `[unimplemented] ...` and appended,
with the date and the application, to `<storage>/executor/unimplemented.log`,
the list to consider implementing from. Where the selector says how many
argument bytes the caller pushed, the call is then answered as the Mac's
dispatcher answers a selector it has no routine for: Apple's common
dispatcher (Palette, Menu, Thread, Layer, Display, GestaltValue, Dialog;
D0's high byte the parameter size) pops them and returns paramErr;
ScriptUtil (sizes in its selector long) answers 0. Every other dispatcher
still stops the session. Open Transport's missing functions are logged the
same way (C ones answer kOTNotSupportedErr, Pascal ones stop).

## 2. Opens but wrong

| # | Item | What happens | Expected |
|---|------|--------------|----------|
| 6 | ~~**Sound** control panel~~ | Window opens with empty content: no alert-sound list, no volume | **Fixed**: see below |
| 7 | ~~**Text** control panel~~ | "The control panel "Text" cannot be used now, because not enough memory is available." | **Fixed**: ScriptUtil no longer hidden from `GetTrapAddress`; popups from a `'MENU'` resource draw the menu the cdev fills |
| 8 | ~~**Launcher** control panel~~ | Window opens empty (blue) | **Fixed**: see below. Its background is dithered at 256 colours (the ROM machine compared ran at millions) |
| 9 | ~~**Labels** control panel~~ | Colour swatches all black (last one brown); names upside down | **Fixed**: see below |
| 10 | **Speech** control panel (7.6.1) | Partly drawn; voice popup empty | Voice name, rate slider |
| 11 | ~~**SimpleSound**~~ (**Fixed**, STRAGGLERS 45) | "Sorry, this program requires Sound Manager 3.0 or later." | Runs. Check `SndSoundManagerVersion` (`romlib/sound/soundIMVI.cpp`) and `gestaltSoundAttr` (`romlib/gestalt.cpp`) |
| 12 | ~~**AppleTalk** control panel (an application)~~ | Says it needs AppleTalk from Open Transport; `/api/quit` while that alert is up leaves it running | **Not a bug** (checked 2026-10-24): Executor's OT has no AppleTalk ('otan' $33: TCP only), so the alert is right; a modal alert doesn't take the Quit Apple Event (nor on a Mac), and `/api/quit` only reports the event delivered. OK (Return) and it quits by itself |
| 13 | ~~**Finder**, first open of a folder window~~ (**Fixed**, STRAGGLERS 47: after the zoom box) | Icons drawn twice, overlapping (old and new positions), until the window is closed and reopened | Clean layout |

## 3. Interaction

| # | Problem | Repro |
|---|---------|-------|
| 14 | ~~Clicking a background process's modal dialog doesn't bring that process forward~~ | **Fixed**: see below |
| 15 | ~~Escape doesn't cancel the "Choose a folder" Standard File dialog~~ | **Fixed** (STRAGGLERS 48). Run **Synchronize Folders** or **Alert When Folder Changes**, press Escape |
| 16 | ~~`/api/launch` of a desk accessory (`dfil`)~~ | **Fixed** (STRAGGLERS 49: BridgeAgent asks the Finder to open what isn't an application). Was: `{"path":"Macintosh HD:System Folder:Apple Menu Items:Calculator"}` puts up "That application appears damaged (lacks CODE and cfrg)" (`romlib/wind/windInit.cpp:183`). The same DA opens fine from the Apple menu |
| 17 | ~~`/api/launch` of a control panel (`cdev`)~~ | **Fixed** (STRAGGLERS 49). Was: Returns `launched`; nothing opens. From the Finder (double-click) control panels open fine |

## 4. Blocked by Executor's extension policy (decide first)

Plan (2026-10-24): next session, turn these on one at a time in the policy
and tackle what each runs into.

These work on a real Mac, but Executor's `src/executor/res/extension-policy.txt`
turns off what they need. Each is a policy decision before it is a fix:

- **Network Extension** (denied: it aborts on `$A08E`, see STRAGGLERS #8):
  File Sharing Monitor, Sharing Setup, Users & Groups, and the owner-name
  prompt in Apple IR File Exchange
- **QuickTime**: QuickTime™ Settings (7.6.1), MoviePlayer (7.6.1)
- ~~**Control Strip**~~: on since 2026-10-25 (STRAGGLERS 52)
- ~~**CloseView**~~: on since 2026-10-25 (magnification draws wrong, STRAGGLERS 52)
- **AutoRemounter, Easy Open, PC Exchange** (7.6.1), see STRAGGLERS 52 for what each needs:
  say "not installed" when opened by hand
- **Network Software Selector** (7.5.5): "Could not get information on the
  currently running networking code"

## Fixed

- **1 Keyboard**: Pack6 `0x30` takes IUTextOrder's arguments (two texts,
  lengths, scripts, languages; word result). The System's sorting code
  (`'ptch'` 27, `'lpch'` 31, `'scod'` -16463) and the Keyboard and Text panels
  call it. Implemented with IUMagString's comparison, as are IUTextOrder,
  IUMagPString and IUMagIDPString (they returned 0: "equal").
- **7 Text**: the panel checks `GetTrapAddress($B5)` against Unimplemented;
  Executor hid ScriptUtil (and `gestaltScriptMgrVersion`) unless the
  PretendScript preference was set. Both are visible now, Gestalt saying
  `0x0750` like `smVersion`. Then its popups drew empty: the popup CDEF put
  the `'MENU'` resource in the menu list but gave the control a copy, which
  the panel filled. The copy is what goes in the list now.

- **2 Monitors**: DisplayDispatch -13 (`DMGetModeBounds`, name a guess:
  the screen's bounds in a mode, which the panel draws its arrangement
  from) and the public calls the panel makes (DMEnableDisplay,
  DMSetMainDisplay, DMGetNextMirroredDevice, DMMoveDisplay, DMGetNameByAVID,
  DMSaveScreenPrefs, DMIsMirroringOn), for one screen. The video driver
  answers Status 12, 14 and 16 (connection, mode base address, preferred
  configuration). Choosing a depth then crashed in three ways, all fixed:
  GetCPixel had no 16/32-bit case; the screen's ports kept the old
  framebuffer's address after SetDepth allocated a new one, so the first
  redraw at Thousands wrote past its end over the next process's stack;
  and every InitGraf made a new, empty PortList, so the ports opened before
  the last application started (Finder's desktop window) were never moved
  (desktop icons vanished). The PortList is now made once; a quitting
  process's ports leave it.

- **3 Apple System Profiler**: FSDispatch `$12` is PBXGetVolInfo
  (System 7.5.2): PBHGetVInfo's record plus 64-bit size and free bytes. Its
  volume report then hung on "Gathering Volume Info...": the SCSI Manager
  stub answered "busy" to every call (and popped 2 bytes whatever the call),
  which the Profiler retried forever. It now answers as an empty bus does
  (SCSIGet succeeds, a selection gets scCommErr). All six reports work;
  the disk image's "Where" says External Floppy Drive.

- **4 TCP/IP** (fails cleanly; not working): a missing OT function with a C
  name (OT's sets name Pascal ones in capitals) now answers
  kOTNotSupportedErr instead of ending the session. The panel then says
  "TCP/IP Preferences" could not be opened and quits. Whether its calls
  reached Executor's OT or Apple's depended on whether anything had asked
  Gestalt 'otan' before the panel made its library manager: Apple's ASLM
  now gets Executor's wrappers before each application starts.
  **Open:** the configuration database (`OTLib$CfigLib`, 27 private
  functions: OTOpenConfigurationsDB, OTGetActiveConfigID,
  OTGetAttributeValue, ...) for the panel to show and change settings.

- **14 Background dialog clicks**: the check for a click in another
  process's window ran at the start of GetNextEvent, but the click could
  reach the queue during the call (the host posts input meanwhile), and
  the front process got it: BridgeAgent ignored clicks on the Finder's
  alert, which stayed up and blocked every applet after it ("AppleEvent
  timed out"). GetNextEvent now checks the mouse-down it hands out as well.
  The triage's chain started with Share a Folder: after its first dialog
  the Finder puts up a second ("Check the Sharing Setup control panel..."),
  which the sweep's single Escape/Return didn't reach.

- **9 Labels**: the window is the Finder's. It colours each swatch with
  PmForeColor from a palette it fills with SetEntryColor, entry -1 (label 0,
  none) included; Executor wrote that entry into the palette's header,
  zeroing its entry count, so every swatch but entry 0 stayed black. The
  palette calls now ignore entries outside the palette. GetLabel numbered
  the labels backwards (Essential is label 7) from a table of its own; it
  reads the System's 'lstr' and 'rgb ' -16392 + label now, which the
  Labels panel edits.

- **6 Sound**: four parts. (a) AppendDITL put items relative to (0,0), not
  the dialog's top-left: the Control Panel's window has its origin at
  (89,1), so every page was drawn 89 pixels left (fixed for all three
  methods, and the window growth). (b) The pages ask a sound output device
  component ('sdev'); the System's stand for sound hardware Executor never
  drives. Executor registers its own, native (component.cpp can now host C++
  components: ROMlib_register_native_component), named and iconed after the
  System's first, answering volume/mute ('hvol' 'svol' 'hmut' 'smut'
  'hstp') and the format sent out (48 kHz 16-bit stereo, 'srat' 'ssiz'
  'chan' and their lists). The volume and mute apply to what Executor sends
  out; Get/SetDefaultOutputVolume and Get/SetSysBeepVolume keep their values
  (were fixed stubs). (c) The pages call the device by its Component ID:
  CallComponent now takes one, going to its open instance. (d) Private
  SoundDispatch $021C0018 (name a guess, GetSoundOutputDevice) answers the
  current device. Plus: a maskless 'ICON' passed as 'ICN#' draws. All four
  pages match the ROM machine (8-bit and mono are off: one format).

- **8 Launcher**: the System Folder copy (`hfs_sideload.cpp`) took a fixed
  list of folders without Launcher Items (aliases to the applications);
  it takes Launcher Items and Shutdown Items now. Copies made before keep
  the old contents: delete `<storage>/executor-systems/<image>*` to redo.
  Found on the way, worse: **the Finder unblessed the System Folder of
  every disk image Executor mounted writable** (the image then no longer
  boots on the ROM machines). It checks a volume's blessed folder for files
  named by FinderName and SysResName: FinderName said "Browser" (Executor's
  shell) and SysResName had been reset to $FF bytes per launch. FinderName
  is the Finder's own name when Apple's Finder runs; SysResName is kept.

## Works (no action)

Control panels: Apple Menu Options, Color, ColorSync System Profile, Date &
Time, Desktop Patterns, Extensions Manager, General Controls, Map, Mouse,
Numbers, Power Macintosh Card, Startup Disk, Views, WindowShade, Cache Switch,
Memory, Easy Access (7.6.1). Desk accessories from the Apple menu: Calculator,
Chooser, Key Caps; Apple menu Shut Down powers off cleanly. Applications:
SimpleText, Find File, Jigsaw Puzzle, Note Pad, Scrapbook, Stickies, Script
Editor, File Assistant, Disk Copy, StuffIt Expander. Applets: Add Alias to
Apple Menu, Find Original from Alias, Share a Folder, Start File Sharing, Turn
Sound Off/On, Alert When Folder Changes, Hide/Show Folder Sizes, Synchronize
Folders.

## How the triage was run

Use a private copy of each image, so the built images and other people's runs
aren't touched:

```bash
cp --reflink=auto ~/storage/images/7.5.5.img ~/storage/images/triage-7.5.5.img
./build/mac-phoenix --backend executor --executor-system triage-7.5.5.img \
    --executor-writable-images --config /dev/null --bridge --network none \
    --port 18760 --extfs <host dir>
curl -X POST localhost:18760/api/emulator/start
# when done: rm ~/storage/images/triage-7.5.5.img; rm -rf ~/storage/executor-systems/triage-7.5.5.img*
```

- On the desktop, **"Macintosh HD" is the disk image**. The volume named after
  the image file (`triage-7.5.5.img`) is Executor's own System volume (a host
  directory under `~/storage/executor-systems/`). Bridge paths therefore start
  with `Macintosh HD:`; `™` in paths (`AppleScript™`, `StuffIt Expander™`)
  goes through as UTF-8 in the JSON.
- `/api/text`, `/api/ui` and `/api/windows` don't answer under Executor:
  drive by screenshots, `/api/app`, `/api/mouse` and `/api/keypress`.
- **Control panels**: Apple menu → Control Panels (y=150 on 7.5.5, 168 on
  7.6.1), click the zoom box (397,36), double-click icons, Cmd-W to close. If
  Cmd-W changes nothing, an alert is up: press Return. Some control panels
  are applications (AppleTalk, Desktop Patterns, Energy Saver, Extensions
  Manager on 7.6.1): quit them with `/api/quit`.
- **Policy-disabled panels**: with Executor running, move
  `executor-systems/<image>/System Folder/Control Panels (Disabled)` to the
  volume root and open it from the Executor volume's window.
- **Desk accessories**: Apple menu or `/api/launch`; `/api/quit`
  closes them through the DA Handler.
- **Applets** act on the Finder's selection: click empty desktop first. Their
  dialogs belong to the Finder, so answer them while the Finder is in front
  (item 14).
- Crash cause: `/api/status` → `child_exit.log`, or the emulator log; each
  fatal error also writes `~/storage/snapshots/executor-fatal`.

The driver scripts used (`tri.py`, `cpsweep.py`, `appsweep.py`,
`dasweep.py`) and every screenshot are in `~/storage/tmp/triage/` (scratch,
not in the repo).
