# Executor scoreboard

The target applications on Executor (Apple's 7.6.1 and 8.6 System Folders)
beside the ROM machines running the same System and application disk
(`tools/executor/scoreboard.py run` / `table`; grids, screenshots and logs under
`~/storage/executor-grid/m1/<column>/<disk set>/`, screenshot paths relative to
it; `scoreboard.py sheets` puts the columns side by side in `m1/sheets/`).

Columns: **e761** Executor on 7.6.1, 68k code (`--no-ppc`); **e761ppc** the same
with `--executor-ppc`, fat applications only; **q761** Quadra 650 ROM + a fresh
copy of 7.6.1 (games at `--depth 8`, as Executor's screen); **e86** Executor on
8.6 (`--executor-ppc`); **g86** G3 ROM + 8.6. Every session gets a fresh, writable
copy of the application disk. BridgeAgent launches the application from the
Finder; 20 s later the grid takes a screenshot and reads the log.

**runs**: the screen matches the reference's (checked by eye, sheet by sheet).
**partial**: up, but drawn wrong or a wrong behaviour seen (or, unchecked, an
unimplemented call logged). **fails**: crash, fatal error, SysError, an alert the
reference doesn't show, or never up. A note in brackets is what the log says about
a program that runs (unimplemented calls, CFM imports left unresolved, exceptions).
**First blocker**: the worst Executor column's. **Owner**: fix-prod, fix-dev,
fix-games by disk; fix-86 for faults on 8.6 only; platinum for 8.x frames, menus
and controls; "(ref fails too)" when the ROM machine doesn't run it either.
Visual verdicts and owner overrides: `tools/executor/scoreboard_notes.json`.

| System | Column | runs | partial | fails |
|---|---|---|---|---|
| 7.6.1 | e761 | 76 | 7 | 22 |
| 7.6.1 | e761ppc | 17 | 3 | 12 |
| 8.6 | e86 | 69 | 5 | 34 |

Gaps in the references and the method:

- The G3 starts in millions of colours and has no `--depth` (the PPC video
  driver publishes 32-bit modes only, `src/cpu/kpx/video_ppc.cpp`), so many
  games refuse to start there (g86 *fails*, "needs 256 colours"); their e86
  verdicts are judged on their own.
- Silicon Beach's black-and-white games (Dark Castle & co.) want a 1-bit screen:
  the Quadra at 256 colours shows their alert; Executor crashes or stays blank.
- Executor 8.6's desktop (no desktop picture, sparse Finder icons, no Control
  Strip) is not counted against any row.
- Full-screen games often leave `/api/app` without an answer; the screenshot
  decides ("runs?" where it wasn't checked).

Rows with a fault, by owner:

| System | Owner | rows |
|---|---|---|
| 7.6.1 | fix-dev | 5 |
| 7.6.1 | fix-games | 25 |
| 7.6.1 | fix-prod | 2 |
| 7.6.1 | — | 5 |
| 8.6 | fix-86 | 6 |
| 8.6 | fix-dev | 6 |
| 8.6 | fix-games | 21 |
| 8.6 | fix-prod | 1 |
| 8.6 | platinum | 1 |
| 8.6 | — | 4 |

## 7.6.1

| Disk | App | Code | e761 | e761ppc | q761 | First blocker | Screenshot | Owner |
|---|---|---|---|---|---|---|---|---|
| productivity-system-7 | AppleWorks 5.0.3 ƒ:AppleWorks | fat | runs | fails | runs | e761ppc: PowerPC exception 7 at 00000000 (lr 00815470) after unimplemented PowerPC OpenWD | e761ppc/prod/001.png | fix-prod |
| productivity-system-7 | ClarisWorks 4.0v4 ƒ:ClarisWorks | 68k | fails |  | runs | e761: alert "This copy of ClarisWorks has been modified by another program or virus" at launch (disk writable; Quadra shows the Registration Assistant) | e761/prod/002.png | fix-prod |
| dev-tools-system-7 | MPW:MPW Shell | fat | runs | fails | runs | e761ppc: host signal 11 at guest pc $c105ef84, address (nil) | e761ppc/dev/003.png | fix-dev |
| dev-tools-system-7 | CodeWarrior Pro 4 ƒ:Metrowerks CodeWarrior:CodeWarrior IDE 3.2 | fat | fails | fails | fails | e761: not in front; unresolved imports CountDragItemFlavors, WaitMouseMoved, SetDragSendProc (+21) | e761/dev/001.png | fix-dev (ref fails too) |
| dev-tools-system-7 | THINK C 6.0 ƒ:THINK C:THINK Project Manager | 68k | partial |  | runs | e761: Standard File opens in the boot volume's System Folder, not the application's folder (Quadra: THINK C folder) | e761/dev/006.png | fix-dev |
| dev-tools-system-7 | Languages ƒ:THINK Pascal 4.5d4 ƒ:Think Pascal:THINK Pascal 4.5d4:THINK Pascal 4.5 | 68k | partial |  | runs | e761: Standard File opens in the boot volume's System Folder, not the application's folder (Quadra: THINK Pascal 4.5d4 folder) | e761/dev/004.png | fix-dev |
| dev-tools-system-7 | Languages ƒ:HyperCard 2.4.1 ƒ:HyperCard | 68k | runs |  | runs |  | e761/dev/002.png |  |
| dev-tools-system-7 | Languages ƒ:MacPerl 5.2.0r4 ƒ:MacPerl | fat | runs | runs | runs | (e761ppc: unimplemented PowerPC equalstring) | e761ppc/dev/002.png |  |
| dev-tools-system-7 | Tools ƒ:ResEdit 2.1.3 ƒ:ResEdit 2.1.3:ResEdit | 68k | runs |  | runs |  | e761/dev/008.png |  |
| dev-tools-system-7 | Tools ƒ:BBEdit Lite 4.1 ƒ:BBEdit Lite 4.1 | fat | runs | fails | runs | e761ppc: host signal 11 at guest pc $c105eee0, address (nil) | e761ppc/dev/004.png | fix-dev |
| games | A-Train ƒ:A-Train | 68k | runs |  | fails |  | e761/games/001.png |  |
| games | A-Train ƒ:AT Construction Set | 68k | runs |  | fails |  | e761/games/002.png |  |
| games | A-Train ƒ:ATrain Editor 1.0Beta | 68k | runs |  | runs |  | e761/games/003.png |  |
| games | Afterlife 1.0 ƒ:Afterlife | fat | fails | fails | fails | e761: alert "Afterlife has detected that you are running an out-of-date version of the Sound Manager that is incompatible with Afterlife." (ref fails too: "Please insert the Afterlife CD-ROM and try running Afterlife again.") | e761/games/004.png | fix-games (ref fails too) |
| games | Apeiron 1.0.2 ƒ:Apeiron | 68k | runs |  | runs |  | e761/games/005.png |  |
| games | Ares 1.2.0 ƒ:Ares | fat | fails | fails | runs | e761: alert "Without QuickTime 2.1 or later, you can't see any of the movies for “Ares.”" (Quit/Continue); ref shows title screen | e761/games/006.png | fix-games |
| games | Ares 1.2.0 ƒ:GameRanger Folder:GameRanger | ppc | runs |  | n/a | (e761: latent: unresolved imports OTCancelUReply, OTRcvURequest, OTSndURequest (+13)) | e761/games/007.png |  |
| games | Ares 1.2.0 ƒ:Hera 1.0.0 ƒ:Hera | ppc | fails |  | n/a | e761: alert "Initialization Error: An error of type -5551 occurred. Appearance Manager 1.0.1 (part of MacOS 8.1) or later is required." (no ref; PPC-only app, a real 7.6.1 lacks Appearance too) | e761/games/008.png | — (needs Appearance Manager 1.0.1, Mac OS 8.1: right on 7.6.1) |
| games | Avara 1.0.2 ƒ:Avara | 68k | runs |  | runs |  | e761/games/009.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Avara Commuter | fat | runs | runs | runs |  | e761/games/010.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Avara MicroTracker | 68k | runs |  | runs |  | e761/games/011.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Mission Editing/Creating ƒ:BSPSplitter 0.6.4FAT | fat | runs | runs | runs |  | e761/games/012.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Mission Editing/Creating ƒ:BSPSplitter 0.6.4LC | 68k | runs |  | runs |  | e761/games/013.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Mission Editing/Creating ƒ:BSPViewer | 68k | runs |  | runs |  | e761/games/014.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Mission Editing/Creating ƒ:SoundComp 0.8.4(v2) | 68k | runs |  | runs |  | e761/games/015.png |  |
| games | Barrack 1.0.4 ƒ:Barrack | fat | runs | runs | runs | (e761ppc: latent: unresolved imports HGetVol, UpperText) | e761ppc/games/005.png |  |
| games | Bolo 0.99.7 ƒ:Bolo 0.99.7 | 68k | runs |  | runs |  | e761/games/017.png |  |
| games | Bubble Trouble 1.0.1 ƒ:Bubble Trouble | fat | runs | runs | runs | (e761ppc: unimplemented PowerPC HGetVol) | e761ppc/games/006.png |  |
| games | Chiral 1.0.4 ƒ:Chiral | fat | runs | runs | runs | (e761ppc: latent: unresolved imports HGetVol) | e761ppc/games/007.png |  |
| games | Chiral 1.0.4 ƒ:Chiral Basics | 68k | runs |  | runs |  | e761/games/020.png |  |
| games | Civilization 1.0.6 ƒ:Civilization™ | 68k | runs |  | runs? | (e761: front process unknown) | e761/games/021.png |  |
| games | Civilization II ƒ:Civ II Map Editor | 68k | partial |  | runs | e761: desktop around the map window drawn black instead of grey; window and dialog otherwise match | e761/games/022.png | fix-games |
| games | Civilization II ƒ:Civilization II | 68k | runs |  | runs |  | e761/games/023.png |  |
| games | Civilization II ƒ:Civilization II Files:Civilopedia:Civilopedia | fat | fails | fails | fails | e761: just the desktop (File menu only); ref shows full-screen black Civilopedia screen | e761/games/024.png | fix-games (ref fails too) |
| games | Cythera 1.0.4 ƒ:Cythera | fat | fails | fails | runs | e761: alert "Sorry - you need to install the QuickTime 3.0 or later"; ref shows main menu | e761/games/025.png | fix-games |
| games | Digital Messiah ƒ:Digital Messiah 2.0 | 68k | runs |  | runs |  | e761/games/026.png |  |
| games | Escape Velocity 1.0.5 ƒ:Escape Velocity | fat | runs | runs | runs | (e761ppc: PowerPC exception 6 logged, title screen up as on the reference) | e761ppc/games/010.png |  |
| games | EV Override 1.0.2 ƒ:EV Override | fat | runs | runs | runs | (e761ppc: latent: unresolved imports UpperString, HGetVol) | e761ppc/games/011.png |  |
| games | Exile 2.0.1 ƒ:Exile Editor v1.1.2:Exile.editor v1.1.2 | 68k | runs |  | runs |  | e761/games/029.png |  |
| games | Exile 2.0.1 ƒ:Exile v2.0.1 | 68k | runs |  | runs |  | e761/games/030.png |  |
| games | Exile II 2.0.3 ƒ:Exile II Editor v2.0.1 | 68k | runs |  | runs |  | e761/games/031.png |  |
| games | Exile II 2.0.3 ƒ:Exile II v2.0.3 | 68k | runs |  | runs |  | e761/games/032.png |  |
| games | Exile III 1.0.3 ƒ:Exile III (fat) v1.0.3 | fat | runs | fails | runs | e761ppc: not in front; unimplemented PowerPC HGetVol | e761ppc/games/012.png | fix-games |
| games | Exile III 1.0.3 ƒ:Exile III Editor v1.0.2 | 68k | runs |  | runs |  | e761/games/034.png |  |
| games | Flashback 1.0 ƒ:Flashback | fat | runs | runs | runs | (e761ppc: latent: unresolved imports GetWDInfo, MIDISignOut, MIDISignIn (+4)) | e761ppc/games/013.png |  |
| games | Glider PRO 1.1.2 ƒ:Glider PRO™ | fat | runs | runs | runs? | (e761: front process unknown) | e761/games/036.png |  |
| games | Glider PRO 1.1.2 ƒ:Utilities:Foundation Mover | fat | runs | runs | runs |  | e761/games/037.png |  |
| games | Glider PRO 1.1.2 ƒ:Utilities:Glider Edit 1.1.1 | 68k | runs |  | runs |  | e761/games/038.png |  |
| games | Glider PRO 1.1.2 ƒ:Utilities:Moving Van | 68k | runs |  | runs |  | e761/games/039.png |  |
| games | Lemmings 1.5.2 ƒ:Lemmings | 68k | runs |  | runs |  | e761/games/040.png |  |
| games | Maelstrom 1.4.2 ƒ:Maelstrom | 68k | runs |  | runs |  | e761/games/041.png |  |
| games | Marathon 1.2 ƒ:Marathon v1.2 | fat | runs | runs | runs | (e761ppc: unimplemented PowerPC HGetVol) | e761ppc/games/016.png |  |
| games | Marathon 2 ƒ:Marathon 2 | fat | runs | runs | runs | (e761ppc: latent: unresolved imports CrsrDevMoveTo, CrsrDevNextDevice, SGetCString (+14)) | e761ppc/games/017.png |  |
| games | Marathon Infinity 1.5 ƒ:Marathon ∞ 1.5 | fat | runs | runs | runs? | (e761: front process unknown) | e761/games/044.png |  |
| games | Master of Orion ƒ:Master of Orion | 68k | runs |  | runs |  | e761/games/045.png |  |
| games | Monkey Island 2 ƒ:LeChuck's Revenge | 68k | fails |  | fails | e761: alert "Sorry, the file 'Monkey2.005' could not be found. Please be sure that the data files are in the same folder as the application." on black screen | e761/games/046.png | — (disk: 'Monkey2.005' missing, the ROM machines fail alike) |
| games | Oids 2.0.1 ƒ:OIDS | ppc | fails |  | n/a | e761: alert "OIDS requires a newer version of SoundManager (try version 3.0) in order to run properly." on black screen (PPC-only, no reference) | e761/games/047.png | fix-games |
| games | Pararena 2.06 ƒ:Pararena 2.06M | 68k | runs |  | fails |  | e761/games/048.png |  |
| games | Pathways into Darkness 1.1 ƒ:Pathways ƒ:Pathways Into Darkness | 68k | runs |  | runs |  | e761/games/049.png |  |
| games | Populous II ƒ:MacPopulousII:MacPopulousII | 68k | runs |  | fails |  | e761/games/050.png |  |
| games | Populous II ƒ:MacPopulousII:MacPopulousII (Mono) | 68k | runs |  | fails |  | e761/games/051.png |  |
| games | Populous II ƒ:MacPopulousII:MacPopulousII (SC) | 68k | runs |  | fails |  | e761/games/052.png |  |
| games | Prince of Persia 2 ƒ:Prince of Persia 2 | 68k | runs |  | runs |  | e761/games/053.png |  |
| games | Prince of Persia ƒ:Prince of Persia | 68k | runs |  | runs |  | e761/games/054.png |  |
| games | SimAnt ƒ:E-Doc 1.0.20 | fat | runs | partial | runs | e761ppc: Standard File's directory popup drawn as a plain box without its arrow, list without icons | e761ppc/games/019.png | fix-games |
| games | SimAnt ƒ:SimAnt™ Color | 68k | runs |  | fails | (e761: front process unknown) | e761/games/056.png |  |
| games | SimCity 2000® Collection ƒ:SimCity 2000® 1.2 | fat | runs | runs | runs? | (e761: front process unknown) | e761/games/057.png |  |
| games | SimCity 2000® Collection ƒ:Urban Renewal Kit | fat | runs | runs | runs |  | e761/games/058.png |  |
| games | SimEarth 2.0 ƒ:SimEarth Black & White | 68k | runs |  | runs |  | e761/games/059.png |  |
| games | SimEarth 2.0 ƒ:SimEarth Color | 68k | runs |  | runs |  | e761/games/060.png |  |
| games | SimFarm ƒ:SimFarm | 68k | fails |  | runs | e761: crashed: blank white screen (host signal 11) | e761/games/061.png | fix-games |
| games | SimLife 1.0.1 ƒ:SimLife™ Color | 68k | runs |  | fails | (e761: front process unknown) | e761/games/062.png |  |
| games | SimLife 1.0.1 ƒ:SimLife™ Monochrome | 68k | runs |  | runs? | (e761: front process unknown) | e761/games/063.png |  |
| games | SimTower 1.2 ƒ:SimTower | fat | runs | fails | runs | e761ppc: crashed: blank white screen (host signal 11) | e761ppc/games/022.png | fix-games |
| games | Spectre 1.1 ƒ:Spectre 1.1 | 68k | runs |  | runs |  | e761/games/065.png |  |
| games | Swoop 1.0.2 ƒ:Swoop | 68k | runs |  | runs |  | e761/games/066.png |  |
| games | The Secret of Monkey Island ƒ:Monkey Island | 68k | runs |  | runs |  | e761/games/067.png |  |
| games | Warcraft II 1.22 ƒ:Editors:Warcraft II Map Editor | fat | runs | fails | fails | e761ppc: desktop only, empty menu bar; the reference's "You must have the Warcraft II CD in order to run the map editor" alert never appears (PowerPC exception 7) | e761ppc/games/023.png | fix-games (ref fails too) |
| games | Warcraft II 1.22 ƒ:Editors:Warcraft II Sound Editor | fat | runs | partial | runs | e761ppc: Standard File's directory popup drawn without its arrow, list without icons | e761ppc/games/024.png | fix-games |
| games | Warcraft II 1.22 ƒ:Warcraft II v.1.22 | 68k | runs |  | runs |  | e761/games/070.png |  |
| games | Wolfenstein 3D 1.0.2 ƒ:Wolfenstein 3D™ | fat | runs | runs | runs? | (e761: front process unknown) | e761/games/071.png |  |
| games-classic | Airborne! ƒ:Airborne! | 68k | fails |  | runs | e761: Fatal error alert: "init.cpp:147: in 'syn68k_addr_t unhandled_trap(syn68k_addr_t, void*)' Fatal error: unhandled trap 14 at pc 0x3C0AE88 (Format error)" | e761/classic/001.png | fix-games |
| games-classic | Apache Strike 1.2 ƒ:Apache Strike | 68k | fails |  | fails | e761: blank screen (not in front) | e761/classic/002.png | fix-games (ref fails too) |
| games-classic | Beyond Dark Castle 1.1 ƒ:Behind Dark Castle | 68k | runs |  | runs |  | e761/classic/003.png |  |
| games-classic | Beyond Dark Castle 1.1 ƒ:Beyond Dark Castle | 68k | fails |  | fails | e761: crashed, blank screen | e761/classic/004.png | fix-games (ref fails too) |
| games-classic | Beyond Dark Castle 1.1 ƒ:Beyond Dark Castle Editor  | 68k | fails |  | fails | e761: launch failed (err -43), same as reference | e761/classic/005.png | — (the same on the Quadra) |
| games-classic | Crystal Quest 2.2.5 ƒ:CritterEditor 1.0.4M | 68k | runs |  | runs |  | e761/classic/006.png |  |
| games-classic | Crystal Quest 2.2.5 ƒ:Crystal Quest | 68k | partial |  | runs | e761: drawn in black and white instead of colour | e761/classic/007.png | fix-games |
| games-classic | Crystal Quest 2.2.5 ƒ:Crystal Quest Sample | 68k | partial |  | runs | e761: drawn in black and white instead of colour | e761/classic/008.png | fix-games |
| games-classic | Dark Castle ƒ:Dark Castle | 68k | fails |  | fails | e761: crashed, blank screen | e761/classic/009.png | fix-games (ref fails too) |
| games-classic | Enchanted Scepters ƒ:Scepters | 68k | fails |  | fails | e761: blank screen (not in front) | e761/classic/010.png | fix-games (ref fails too) |
| games-classic | Glider 4.0.4 ƒ:Editor 1.01 | 68k | runs |  | runs |  | e761/classic/011.png |  |
| games-classic | Glider 4.0.4 ƒ:Glider™ | 68k | runs |  | runs | (e761: front process unknown) | e761/classic/012.png |  |
| games-classic | Oregon Trail ƒ:Oregon Trail | 68k | runs |  | runs |  | e761/classic/013.png |  |
| games-classic | Rescue! 2.0.4 ƒ:Rescue! 2.0.4 | 68k | fails |  | runs | e761: crashed, blank screen | e761/classic/014.png | fix-games |
| games-classic | Shufflepuck Café ƒ:Shufflepuck | 68k | partial |  | runs | e761: game screen right but menu bar empty (reference has Game/Paddle/Blocker/Cheat menus) | e761/classic/015.png | fix-games |
| games-classic | SimCity 1.3c ƒ:SimCity 1.3c | 68k | fails |  | fails | e761: desktop only (menu bar shows just File), same as reference | e761/classic/016.png | — (the same on the Quadra) |
| games-classic | SimCity 1.3c ƒ:SimCity Editor | 68k | runs |  | runs |  | e761/classic/017.png |  |
| games-classic | SimCity 1.3c ƒ:TerrainEdit1.1 | 68k | fails |  | fails | e761: desktop only (menu bar shows just File), same as reference | e761/classic/018.png | — (the same on the Quadra) |
| games-classic | Spaceward Ho! 3.0.1 ƒ:Spaceward Ho! 3.0.1 | 68k | runs |  | runs |  | e761/classic/019.png |  |
| games-ppc | EV Nova 1.0.10 ƒ:EV Nova | fat | partial | partial | fails | e761: only a File menu over the desktop, no game window | e761/gppc/001.png | fix-games (ref fails too) |
| games-ppc | Ferazel's Wand 1.0.3 ƒ:Ferazel's Wand | ppc | runs |  | n/a | (e761: latent: unresolved imports UpperString, SleepQRemove, SleepQInstall) | e761/gppc/002.png |  |
| games-ppc | Mars Rising 1.0.2 ƒ:Mars Rising | ppc | fails |  | n/a | e761: alert: "InputSprocket Not Loaded: Sorry, but I couldn't initialise Apple's InputSprocket Shared Library..." | e761/gppc/003.png | fix-games |
| games-ppc | Master of Orion II ƒ:Master of Orion II:MOO II | ppc | fails |  | n/a | e761: desktop only, empty menu bar, no game window | e761/gppc/004.png | fix-games |
| games-ppc | Slithereens 1.0.1 ƒ:Slithereens | fat | fails | fails | fails | e761: green error dialog: "-7004 main" (Quit) | e761/gppc/005.png | fix-games (ref fails too) |

## 8.6

| Disk | App | Code | e86 | g86 | First blocker | Screenshot | Owner |
|---|---|---|---|---|---|---|---|
| productivity-system-7 | AppleWorks 5.0.3 ƒ:AppleWorks | fat | fails | runs | e86: no menu titles, no New Document dialog: unimplemented PowerPC StandardMBDF (G3: New Document dialog) | e86/prod/001.png | platinum |
| productivity-system-7 | ClarisWorks 4.0v4 ƒ:ClarisWorks | 68k | fails | runs | e86: alert "This copy of ClarisWorks has been modified by another program or virus" at launch, as on 7.6.1 | e86/prod/002.png | fix-prod |
| dev-tools-system-7 | MPW:MPW Shell | fat | fails | runs | e86: host signal 11 at guest pc $c105ef84, address (nil) | e86/dev/005.png | fix-dev |
| dev-tools-system-7 | CodeWarrior Pro 4 ƒ:Metrowerks CodeWarrior:CodeWarrior IDE 3.2 | fat | fails | runs | e86: host signal 11 at guest pc $c105eee0, address 0x30 | e86/dev/001.png | fix-dev |
| dev-tools-system-7 | THINK C 6.0 ƒ:THINK C:THINK Project Manager | 68k | fails | runs | e86: TranslationDispatch selector $24: no size in the selector, session stopped (called from $555f6) | e86/dev/006.png | fix-dev |
| dev-tools-system-7 | Languages ƒ:THINK Pascal 4.5d4 ƒ:Think Pascal:THINK Pascal 4.5d4:THINK Pascal 4.5 | 68k | fails | runs | e86: TranslationDispatch selector $24: no size in the selector, session stopped (called from $555f6) | e86/dev/004.png | fix-dev |
| dev-tools-system-7 | Languages ƒ:HyperCard 2.4.1 ƒ:HyperCard | 68k | fails | runs | e86: fatal: failed assertion `unknown_1 == 16' (AE_hdlr.cpp:165) | e86/dev/002.png | fix-86 |
| dev-tools-system-7 | Languages ƒ:MacPerl 5.2.0r4 ƒ:MacPerl | fat | runs | runs | (e86: unimplemented PowerPC equalstring) | e86/dev/003.png |  |
| dev-tools-system-7 | Tools ƒ:ResEdit 2.1.3 ƒ:ResEdit 2.1.3:ResEdit | 68k | fails | runs | e86: ResourceDispatch selector $7: no size in the selector, session stopped (called from $3ba8b76) | e86/dev/008.png | fix-86 |
| dev-tools-system-7 | Tools ƒ:BBEdit Lite 4.1 ƒ:BBEdit Lite 4.1 | fat | fails | runs | e86: alert "There is not enough memory to complete this command" at launch (G3: untitled window) | e86/dev/007.png | fix-dev |
| dev-tools-macos-8-9 | MPW:MPW Shell | fat | fails | runs | e86: PowerPC exception 7 at 00000000 (lr 034c2f54) | e86/dev89/002.png | fix-dev |
| dev-tools-macos-8-9 | Languages ƒ:MacPerl 5.6.1r2 ƒ:MacPerl | ppc | runs | runs | (e86: unimplemented PowerPC ICStart) | e86/dev89/001.png |  |
| dev-tools-macos-8-9 | Tools ƒ:BBEdit Lite 6.1.2 ƒ:BBEdit Lite 6.1 | fat | fails | runs | e86: not in front; unresolved imports RealScript, Font2RealScript, DeactivateTSMDocument (+13) | e86/dev89/003.png | fix-86 |
| games | A-Train ƒ:A-Train | 68k | runs | runs |  | e86/games/001.png |  |
| games | A-Train ƒ:AT Construction Set | 68k | runs | runs |  | e86/games/002.png |  |
| games | A-Train ƒ:ATrain Editor 1.0Beta | 68k | fails | runs | e86: crashed: blank screen (TranslationDispatch selector $24, session stopped) | e86/games/003.png | fix-86 |
| games | Afterlife 1.0 ƒ:Afterlife | fat | fails | fails | e86: alert: "Afterlife has detected that you are running an out-of-date version of the Sound Manager that is incompatible with Afterlife." (reference fails differently, on the CD-ROM driver) | e86/games/004.png | fix-games (ref fails too) |
| games | Apeiron 1.0.2 ƒ:Apeiron | 68k | runs | fails |  | e86/games/005.png |  |
| games | Ares 1.2.0 ƒ:Ares | fat | partial | fails | e86: alert: "Without QuickTime 2.1 or later, you can't see any of the movies for "Ares."" (Quit/Continue); no QuickTime on Executor | e86/games/006.png | fix-games (ref fails too) |
| games | Ares 1.2.0 ƒ:GameRanger Folder:GameRanger | ppc | runs | runs | (e86: latent: unresolved imports OTExitPatchPriv, OTLeaveNotifier, OTEnterNotifier (+46)) | e86/games/007.png |  |
| games | Ares 1.2.0 ƒ:Hera 1.0.0 ƒ:Hera | ppc | fails | runs | e86: crashed: blank screen (host signal 11 at $c105eee0) | e86/games/008.png | fix-games |
| games | Avara 1.0.2 ƒ:Avara | 68k | runs | runs |  | e86/games/009.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Avara Commuter | fat | runs | runs |  | e86/games/010.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Avara MicroTracker | 68k | runs | runs |  | e86/games/011.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Mission Editing/Creating ƒ:BSPSplitter 0.6.4FAT | fat | runs | runs |  | e86/games/012.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Mission Editing/Creating ƒ:BSPSplitter 0.6.4LC | 68k | runs | runs |  | e86/games/013.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Mission Editing/Creating ƒ:BSPViewer | 68k | runs | runs |  | e86/games/014.png |  |
| games | Avara 1.0.2 ƒ:Extras ƒ:Mission Editing/Creating ƒ:SoundComp 0.8.4(v2) | 68k | runs | runs |  | e86/games/015.png |  |
| games | Barrack 1.0.4 ƒ:Barrack | fat | runs | fails | (e86: latent: unresolved imports HGetVol, UpperText) | e86/games/016.png |  |
| games | Bolo 0.99.7 ƒ:Bolo 0.99.7 | 68k | runs | runs |  | e86/games/017.png |  |
| games | Bubble Trouble 1.0.1 ƒ:Bubble Trouble | fat | runs | fails | (e86: unimplemented PowerPC HGetVol) | e86/games/018.png |  |
| games | Chiral 1.0.4 ƒ:Chiral | fat | runs | fails | (e86: latent: unresolved imports HGetVol) | e86/games/019.png |  |
| games | Chiral 1.0.4 ƒ:Chiral Basics | 68k | runs | fails |  | e86/games/020.png |  |
| games | Civilization 1.0.6 ƒ:Civilization™ | 68k | runs | runs? | (e86: front process unknown) | e86/games/021.png |  |
| games | Civilization II ƒ:Civ II Map Editor | 68k | runs | fails |  | e86/games/022.png |  |
| games | Civilization II ƒ:Civilization II | 68k | runs | fails |  | e86/games/023.png |  |
| games | Civilization II ƒ:Civilization II Files:Civilopedia:Civilopedia | fat | fails | fails | e86: just the desktop with a File menu; Civilopedia never comes up (not in front, unresolved imports) | e86/games/024.png | fix-games (ref fails too) |
| games | Cythera 1.0.4 ƒ:Cythera | fat | fails | fails | e86: crashed: blank screen (host signal 11 at $c105eee0) | e86/games/025.png | fix-games (ref fails too) |
| games | Digital Messiah ƒ:Digital Messiah 2.0 | 68k | runs | fails |  | e86/games/026.png |  |
| games | Escape Velocity 1.0.5 ƒ:Escape Velocity | fat | runs | fails | (e86: PowerPC exception 6 logged, title screen up as on the reference) | e86/games/027.png |  |
| games | EV Override 1.0.2 ƒ:EV Override | fat | runs | fails | (e86: latent: unresolved imports UpperString, HGetVol) | e86/games/028.png |  |
| games | Exile 2.0.1 ƒ:Exile Editor v1.1.2:Exile.editor v1.1.2 | 68k | runs | fails |  | e86/games/029.png |  |
| games | Exile 2.0.1 ƒ:Exile v2.0.1 | 68k | runs | fails |  | e86/games/030.png |  |
| games | Exile II 2.0.3 ƒ:Exile II Editor v2.0.1 | 68k | runs | fails |  | e86/games/031.png |  |
| games | Exile II 2.0.3 ƒ:Exile II v2.0.3 | 68k | runs | fails |  | e86/games/032.png |  |
| games | Exile III 1.0.3 ƒ:Exile III (fat) v1.0.3 | fat | fails | fails | e86: just the desktop, no game (PowerPC exception 7 after unimplemented HGetVol) | e86/games/033.png | fix-games (ref fails too) |
| games | Exile III 1.0.3 ƒ:Exile III Editor v1.0.2 | 68k | runs | fails |  | e86/games/034.png |  |
| games | Flashback 1.0 ƒ:Flashback | fat | runs | fails | (e86: latent: unresolved imports GetWDInfo, MIDISignOut, MIDISignIn (+4)) | e86/games/035.png |  |
| games | Glider PRO 1.1.2 ƒ:Glider PRO™ | fat | runs | fails | (e86: front process unknown) | e86/games/036.png |  |
| games | Glider PRO 1.1.2 ƒ:Utilities:Foundation Mover | fat | runs | runs |  | e86/games/037.png |  |
| games | Glider PRO 1.1.2 ƒ:Utilities:Glider Edit 1.1.1 | 68k | runs | runs |  | e86/games/038.png |  |
| games | Glider PRO 1.1.2 ƒ:Utilities:Moving Van | 68k | runs | runs |  | e86/games/039.png |  |
| games | Lemmings 1.5.2 ƒ:Lemmings | 68k | runs | fails |  | e86/games/040.png |  |
| games | Maelstrom 1.4.2 ƒ:Maelstrom | 68k | runs | fails |  | e86/games/041.png |  |
| games | Marathon 1.2 ƒ:Marathon v1.2 | fat | runs | fails | (e86: unimplemented PowerPC HGetVol) | e86/games/042.png |  |
| games | Marathon 2 ƒ:Marathon 2 | fat | runs | fails | (e86: latent: unresolved imports CrsrDevMoveTo, CrsrDevNextDevice, SGetCString (+14)) | e86/games/043.png |  |
| games | Marathon Infinity 1.5 ƒ:Marathon ∞ 1.5 | fat | fails | fails | e86: crash, blank screen (host signal 11 at guest pc $c105eee0) | e86/games/044.png | fix-86 (ref fails too) |
| games | Master of Orion ƒ:Master of Orion | 68k | runs | runs |  | e86/games/045.png |  |
| games | Monkey Island 2 ƒ:LeChuck's Revenge | 68k | fails | fails | e86: alert: "Sorry, the file 'Monkey2.005' could not be found. Please be sure that the data files are in the same folder as the application." | e86/games/046.png | — (disk: 'Monkey2.005' missing, the ROM machines fail alike) |
| games | Oids 2.0.1 ƒ:OIDS | ppc | fails | fails | e86: alert: "OIDS requires a newer version of SoundManager (try version 3.0) in order to run properly." | e86/games/047.png | fix-games (ref fails too) |
| games | Pararena 2.06 ƒ:Pararena 2.06M | 68k | runs | runs | (e86: asks for 16 colours: Executor's screen is 8-bit; the Quadra at 256 colours asks the same) | e86/games/048.png |  |
| games | Pathways into Darkness 1.1 ƒ:Pathways ƒ:Pathways Into Darkness | 68k | runs | fails |  | e86/games/049.png |  |
| games | Populous II ƒ:MacPopulousII:MacPopulousII | 68k | runs | fails |  | e86/games/050.png |  |
| games | Populous II ƒ:MacPopulousII:MacPopulousII (Mono) | 68k | runs | fails |  | e86/games/051.png |  |
| games | Populous II ƒ:MacPopulousII:MacPopulousII (SC) | 68k | runs | fails |  | e86/games/052.png |  |
| games | Prince of Persia 2 ƒ:Prince of Persia 2 | 68k | fails | fails | e86: alert: "Error = 10351 in OpenMohawkRsrcVerify()." | e86/games/053.png | fix-86 (ref fails too) |
| games | Prince of Persia ƒ:Prince of Persia | 68k | runs | fails |  | e86/games/054.png |  |
| games | SimAnt ƒ:E-Doc 1.0.20 | fat | runs | runs |  | e86/games/055.png |  |
| games | SimAnt ƒ:SimAnt™ Color | 68k | runs | runs? | (e86: asks for 16 colours: Executor's screen is 8-bit; the Quadra at 256 colours asks the same) | e86/games/056.png |  |
| games | SimCity 2000® Collection ƒ:SimCity 2000® 1.2 | fat | runs | fails | (e86: front process unknown) | e86/games/057.png |  |
| games | SimCity 2000® Collection ƒ:Urban Renewal Kit | fat | runs | fails |  | e86/games/058.png |  |
| games | SimEarth 2.0 ƒ:SimEarth Black & White | 68k | runs | runs |  | e86/games/059.png |  |
| games | SimEarth 2.0 ƒ:SimEarth Color | 68k | runs | fails |  | e86/games/060.png |  |
| games | SimFarm ƒ:SimFarm | 68k | fails | fails | e86: crash, blank screen (host signal 11 at guest pc $c1060970) | e86/games/061.png | fix-games (ref fails too) |
| games | SimLife 1.0.1 ƒ:SimLife™ Color | 68k | runs | fails | (e86: front process unknown) | e86/games/062.png |  |
| games | SimLife 1.0.1 ƒ:SimLife™ Monochrome | 68k | runs | runs? | (e86: front process unknown) | e86/games/063.png |  |
| games | SimTower 1.2 ƒ:SimTower | fat | fails | fails | e86: crash, blank screen (host signal 11 at guest pc $c105eee0) | e86/games/064.png | fix-games (ref fails too) |
| games | Spectre 1.1 ƒ:Spectre 1.1 | 68k | runs | fails |  | e86/games/065.png |  |
| games | Swoop 1.0.2 ƒ:Swoop | 68k | runs | fails |  | e86/games/066.png |  |
| games | The Secret of Monkey Island ƒ:Monkey Island | 68k | runs | fails |  | e86/games/067.png |  |
| games | Warcraft II 1.22 ƒ:Editors:Warcraft II Map Editor | fat | fails | runs | e86: just the desktop, no app (PowerPC exception 7 after unimplemented UpperText); reference shows "You must have the WarCraft II CD in order to run the map editor" | e86/games/068.png | fix-games |
| games | Warcraft II 1.22 ƒ:Editors:Warcraft II Sound Editor | fat | runs | fails |  | e86/games/069.png |  |
| games | Warcraft II 1.22 ƒ:Warcraft II v.1.22 | 68k | runs | fails |  | e86/games/070.png |  |
| games | Wolfenstein 3D 1.0.2 ƒ:Wolfenstein 3D™ | fat | runs | fails | (e86: front process unknown) | e86/games/071.png |  |
| games-classic | Airborne! ƒ:Airborne! | 68k | fails | runs | e86: Fatal error alert: "init.cpp:147: in 'syn68k_addr_t unhandled_trap(syn68k_addr_t, void*)': Fatal error: unhandled trap 14 at pc 0x3B99838 (Format error)" (Restart) | e86/classic/001.png | fix-games |
| games-classic | Apache Strike 1.2 ƒ:Apache Strike | 68k | fails | fails | e86: blank screen (not in front) | e86/classic/002.png | fix-games (ref fails too) |
| games-classic | Beyond Dark Castle 1.1 ƒ:Behind Dark Castle | 68k | runs | runs |  | e86/classic/003.png |  |
| games-classic | Beyond Dark Castle 1.1 ƒ:Beyond Dark Castle | 68k | fails | fails | e86: crashed, blank screen | e86/classic/004.png | fix-games (ref fails too) |
| games-classic | Beyond Dark Castle 1.1 ƒ:Beyond Dark Castle Editor  | 68k | fails | fails | e86: launch failed, Finder only | e86/classic/005.png | — (the same on the ROM machines) |
| games-classic | Crystal Quest 2.2.5 ƒ:CritterEditor 1.0.4M | 68k | runs | runs |  | e86/classic/006.png |  |
| games-classic | Crystal Quest 2.2.5 ƒ:Crystal Quest | 68k | runs | runs |  | e86/classic/007.png |  |
| games-classic | Crystal Quest 2.2.5 ƒ:Crystal Quest Sample | 68k | runs | runs |  | e86/classic/008.png |  |
| games-classic | Dark Castle ƒ:Dark Castle | 68k | fails | fails | e86: crashed, blank screen | e86/classic/009.png | fix-games (ref fails too) |
| games-classic | Enchanted Scepters ƒ:Scepters | 68k | fails | runs | e86: blank screen (not in front); reference has Scepters menu bar | e86/classic/010.png | fix-games |
| games-classic | Glider 4.0.4 ƒ:Editor 1.01 | 68k | runs | fails |  | e86/classic/011.png |  |
| games-classic | Glider 4.0.4 ƒ:Glider™ | 68k | runs | fails | (e86: front process unknown) | e86/classic/012.png |  |
| games-classic | Oregon Trail ƒ:Oregon Trail | 68k | runs | runs |  | e86/classic/013.png |  |
| games-classic | Rescue! 2.0.4 ƒ:Rescue! 2.0.4 | 68k | fails | fails | e86: crashed, blank screen | e86/classic/014.png | fix-games (ref fails too) |
| games-classic | Shufflepuck Café ƒ:Shufflepuck | 68k | partial | runs | e86: game screen drawn but menu bar has no menu titles (reference: File Edit Game Paddle Blocker Cheat Help) | e86/classic/015.png | fix-games |
| games-classic | SimCity 1.3c ƒ:SimCity 1.3c | 68k | partial | fails | e86: app in front with only a File menu, no window | e86/classic/016.png | — (the same on the ROM machines) |
| games-classic | SimCity 1.3c ƒ:SimCity Editor | 68k | runs | runs |  | e86/classic/017.png |  |
| games-classic | SimCity 1.3c ƒ:TerrainEdit1.1 | 68k | partial | fails | e86: app in front with only a File menu, no window | e86/classic/018.png | — (the same on the ROM machines) |
| games-classic | Spaceward Ho! 3.0.1 ƒ:Spaceward Ho! 3.0.1 | 68k | runs | runs |  | e86/classic/019.png |  |
| games-ppc | EV Nova 1.0.10 ƒ:EV Nova | fat | partial | fails | e86: only a File menu over the desktop, no game window | e86/gppc/001.png | fix-games (ref fails too) |
| games-ppc | Ferazel's Wand 1.0.3 ƒ:Ferazel's Wand | ppc | runs | fails | (e86: latent: unresolved imports UpperString, SleepQRemove, SleepQInstall) | e86/gppc/002.png |  |
| games-ppc | Mars Rising 1.0.2 ƒ:Mars Rising | ppc | fails | fails | e86: crashed, blank screen | e86/gppc/003.png | fix-games (ref fails too) |
| games-ppc | Master of Orion II ƒ:Master of Orion II:MOO II | ppc | fails | fails | e86: alert: "Unable to activate the context. -50" | e86/gppc/004.png | fix-games (ref fails too) |
| games-ppc | Slithereens 1.0.1 ƒ:Slithereens | fat | fails | fails | e86: crashed, blank screen | e86/gppc/005.png | fix-games (ref fails too) |
