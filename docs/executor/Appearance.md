# The Appearance Manager on Executor

Mac OS 8's look and its programming interface: themes, their colours
("brushes"), fonts and measurements, and the drawing of windows, controls
and menus in them. Executor's own, for 8.x and 9.x Systems (and later, as
an option, 7.x). Plan: [MacOS8-9.md](MacOS8-9.md) step 7.

## What it is on a Mac (8.6)

Three layers, all in the System file's data fork (its `'cfrg'`s):

| Layer | Size | Role |
|---|---|---|
| AppearanceLib | 316 KB PowerPC | The Appearance Manager itself, not glue. 217 exports: Appearance.h's 83 calls; the Control Manager's 8.0 calls (32: embedding, keyboard focus, control data, `HandleControlClick`/`Key`, `IdleControls`); Menus.h's per-item calls (19); Dialogs.h's (6: `StandardAlert`, `GetDialogItemAsControl`, `AutoSizeDialog`, ...); window collapsing (6); and 71 undocumented ones for the Appearance control panel (themes, variants, sound sets, system fonts) and the bevel button / image well helpers Controls.h has as inline `SetControlData` wrappers. |
| WindowDefinitions, ControlDefinitions | 22 KB, 150 KB | The 8.x window kinds (WDEF 63-67: pop-up, document, dialog, utility, side utility) and the Appearance controls (CDEF 2-27: bevel button, slider, disclosure triangle, progress bar, little arrows, chasing arrows, tabs, separator, group box, image well, pop-up arrow, placard, clock, user pane, edit text, static text, picture, icon, Finder header, list box, push button, scroll bar, pop-up button, radio group, scrolling text). The System's WDEF and CDEF resources of those IDs are accelerated stubs into these (as MDEF 63 is into MenusLib's `StandardMDEF`). They draw by calling AppearanceLib. |
| WindowsLib, ControlsLib, MenusLib | | Window Manager 2.0, the 32-bit control calls, the menus: Executor's already. |

68k code reaches the Appearance 1.0 calls through `_AppearanceDispatch`
(`$AA74`, the selector a word in D0: `move.w #sel, D0`). The later calls
(1.0.1, 1.1: `DrawThemeWindowFrame`, `DrawThemeButton`, the tracks, the
fonts) are AppearanceLib's only.

## Executor's

Decided 2026-10-30: Executor's own AppearanceLib, with Apple's
WindowDefinitions and ControlDefinitions on top of it at first (they need
AppearanceLib's documented calls, ToolboxUtils, Window Manager 2.0 and
ATSUI for titles), Executor's definitions later, one by one. Apple's
AppearanceLib can't be used: it calls the Window Manager's, the Menu
Manager's and QuickDraw's private hooks, needs the state the System's own
startup sets up, and brings its own Control Manager embedding, which would
clash with Executor's Control Manager.

* Definitions: `multiversal/defs/Appearance.yaml`, from Appearance.h
  (Universal Interfaces 3.4) by `tools/multiversal/uih2yaml.py`, edited.
  A library call without `executor:` is declared and not exported, so an
  import of one Executor lacks stays unresolved and logged.
* AppearanceLib is Executor's from 8.0 on (`libraryInThisSystem`); on 7.x
  no AppearanceLib, as on a Mac without the Appearance extension.
* The look is the one fork ("One Toolbox for 7, 8 and 9"): System 7's
  look first, platinum after, both from the same drawing calls.

## Reference values (9.0.4 on the G3)

`tests/guest/appearance/ApprProbe.c` asks a real Mac for the answers;
`reference-9.0.4.txt` has them (649 lines). What they settle:

* Gestalt `'appr'` 1 (present), `'apvr'` $0111 (1.1.1).
* `RegisterAppearanceClient` noErr, again `themeProcessRegisteredErr`
  (-30561); `UnregisterAppearanceClient` noErr, again
  `themeProcessNotRegisteredErr` (-30562). `IsAppearanceClient` follows.
* Brushes -2 (white) and -1 (black), 1-48; 0 and 49 on
  `themeInvalidBrushErr` (-30560), the colour untouched. Platinum's grays:
  dialog background $DDDD, $EEEE, $8888, $5555 ..., the accent $6666/$6666/$CCCC.
  Text colours likewise (`TEXT` lines).
* `IsThemeInColor`: true at 8 and 32 bits (colour device or not), false at 1.
* Accent colours: 8 entries, $EEEE gray to black through the blues.
* Fonts: system Charcoal 12, small system Geneva 10, small emphasized
  Geneva 10 bold, views Geneva 10; every other ID `paramErr` in 1.1.1.
* Menus: bar 20, separator 6, item extra width 4 (28 for a hierarchical
  item), title extra 13 (9 squished).
* Scroll bar thumb style 1 (proportional), arrow style 1 (double), check
  box style 1 (check mark).
* The shapes of windows (`WINREGION`: every theme window type and region
  code around a content rectangle), buttons (`BUTTON`: region, content and
  background bounds by kind), tracks, tabs, grow boxes and menu
  backgrounds.

## Where it stands (2026-10-30)

Executor's AppearanceLib is the one 8.6 uses; the 8.6 Finder's desktop is
as it was with Apple's (icons, labels). Implemented (`appearance_mgr.cpp`),
matching the reference where it measures them:

* Clients (`Register`/`UnregisterAppearanceClient`, `IsAppearanceClient`).
* Brushes and text colours: `GetThemeBrushAsColor`, `GetThemeTextColor`,
  `SetThemePen`/`Background`/`TextColor`, `SetThemeWindowBackground`,
  `SetThemeTextColorForWindow`, `IsThemeInColor`, `GetThemeAccentColors`
  (the reference's tables at 32, 8 and 1 bits).
* Fonts: `GetThemeFont`, `UseThemeFont`, and `GetViewsFont` (not in the
  header: the Finder's, `(script, name, &size)`).
* Menus' sizes, the scroll bar and check box styles, `SetThemeCursor`
  (the System's I-beam, cross, plus and watch; the arrow for the rest).
* `GetThemeWindowRegion`: every window type's frame, title bar, boxes,
  grow box and shadow from the reference. Not yet: the title text and
  proxy icon regions, the movable dialog's diagonal grow box, the pop-up
  window's tab.
* The drawing state (`Get`/`Set`/`Normalize`/`DisposeThemeDrawingState`).
* AppearanceLib's window and menu calls the Finder makes: `GetWindowRegion`
  (structure, content, update, port), `IsWindowCollapsed`/`Collapsable`
  (false: no collapsing yet), `SetMenuItemKeyGlyph` (accepted).

Next: the drawing calls (System 7's look first), then the Control Manager's
8.0 calls, so the Finder can open a window.
