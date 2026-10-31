# High-resolution rendering for Executor (design)

Executor is QuickDraw: there is no Mac OS ROM, and every drawing call any
program makes, 68k or PowerPC, Apple's AppearanceLib included, ends in
Executor's C++. That lets Executor render the Mac's screen at any
resolution (2x, 3x, fractional) with anti-aliased text and vector chrome,
while programs keep seeing the 640x480-style 1x screen they were written
for. The ROM machines can't do this: their QuickDraw is Apple's code in
ROM.

Status: phase 1 (text on the screen) implemented, 2026-10-31: see
"Phase 1, as built" at the end. The rest is design.

## The idea in one paragraph

The **1x framebuffer stays the truth**: Executor's blitters keep drawing
it exactly as now, and every pixel a program reads comes from it. Beside
it, a **presentation surface** at the display's scale is drawn with Skia
from the same calls, recorded at Executor's QuickDraw bottlenecks. Every
guest object that has a 1x truth (region, pattern, picture, GWorld, icon)
gets a host-side **twin** at high resolution, keyed by the guest object
and **validated by a hash of its 1x bytes**: when they agree the twin is
drawn, when they don't (a program changed the bytes behind QuickDraw's
back) the 1x truth is upscaled. A different look from 8.6's is acceptable
(anti-aliased text, smooth curves, grey for dimmed text); a different
*layout* is not: positions, widths, hit testing all stay 1x.

## Goals and non-goals

Goals:
- The standard interface (window frames, controls, menus, dialogs, lists,
  system and document text) crisp at any display scale.
- Zero behaviour change for programs: measurement, hit testing, pixels
  read back are the 1x ones.
- A fallback that is never worse than today: anything not understood is
  the 1x pixels, upscaled.
- Fractional scales (1.5x, 2.25x): the vector side is a matrix; bitmaps
  are generated large and resampled.

Non-goals:
- Pure pixel engines (Marathon, Doom, Quake, DrawSprocket games), video
  (QuickTime, Myst, Riven), paint documents (MacPaint, Photoshop, Kid
  Pix): upscaled 1x, integer or pixel-art filters. That is what they
  should look like anyway.
- Pixel-exact equality with Apple's rendering at 2x (there is none).

## Pieces

### 1. Recording at the bottlenecks

QuickDraw's drawing reaches a small set of entry points, each already
Executor's code:

| Entry | Executor | Twin drawing |
|---|---|---|
| `StdText` | quickdraw/qStdText.cpp | glyph runs (section 4) |
| `StdLine`, `StdRect`, `StdRRect`, `StdOval`, `StdArc`, `StdPoly`, `StdRgn` | quickdraw/qStd*.cpp | Skia paths, stroke or fill, verb = frame / paint / erase / invert / fill |
| `StdBits` (`CopyBits`) | quickdraw/qStdBits.cpp | copy from the source's twin (GWorld shadow, icon twin) or upscaled 1x |
| `CopyMask`, `CopyDeepMask` | quickdraw/qIMIV.cpp | as `StdBits`, with the mask's twin |
| `ScrollRect` | quickdraw/qBit.cpp | shift the presentation surface too |
| `SetCPixel`, direct pixel writes | qIMV.cpp, memory | mark raw (section 5) |
| `StdComment`, pictures | `DrawPicture` replays through the above | free |

Programs that install their own `grafProcs` usually call back into the
`Std*` ones; those that don't draw their own pixels, which section 5
catches. An audit is needed for Executor-internal paths that blit without
going through a `Std*` (the text helper, the Window Manager's desktop and
frame painting, the menu bar).

Each recorded call carries the state that decides its pixels: port and
its origin, clip and vis regions (their twins), pen size, pattern, mode,
fore/back/op colours, text font/size/face/mode. The presentation surface
is drawn as the calls happen (immediate mode), not kept as a list; only
GWorlds keep their twins (section 3).

### 2. Twins keyed by the 1x truth

A host-side table: guest object (handle or pointer) -> twin + hash of the
1x bytes when the twin was made. Used for:

- **Regions.** Every region operation is Executor's (`RectRgn`,
  `OpenRgn`/`CloseRgn` recording ovals, round rects and lines, `Union`,
  `Sect`, `Diff`, `Xor`, `Offset`, `Inset`, the window definitions'
  structure and content regions). Each builds the 1x region and the twin,
  an `SkPath` (curves stay curves), in the same call. Clipping at the
  display scale uses the twin when the hash still matches, else the 1x
  region scaled (staircase edges). `PtInRgn` and every other test stays
  1x. Regions from `BitMapToRegion` are 1x unless the bitmap's GWorld has
  a twin.
- **Patterns** (`PixPat`, old-style patterns). 8x8 pixel art: drawn at
  one device pixel per pattern pixel or scaled up, a per-pattern choice
  (grey "disabled" patterns read better as a solid grey; desktop and
  fill patterns as pixel art).
- **Pictures**: replay at the display scale; the twin is just the picture
  handle (its bytes are its own hash check).
- **Icons and other stable bitmaps**: section 6.

### 3. GWorld shadows

Most well-written programs draw offscreen and `CopyBits` the result:
the screen only sees a bitmap copy, and that is where much text (browser
pages, word processors, spreadsheets) reaches the screen. So each GWorld
gets a shadow surface at the display scale; drawing into the GWorld is
replayed into the shadow; `CopyBits` from a GWorld (to the screen or to
another GWorld, scaled, masked) copies the matching area of the shadow.

A shadow is valid only as long as nobody wrote the GWorld's pixels
behind QuickDraw: image decoders, sprites, fades, custom blitters do,
through `GetPixBaseAddr`. Detection is section 5.

### 4. Text

The largest visible gain, with the least risk:

- **Layout stays 1x.** Pen positions come from the 1x advances (Executor's
  Font Manager now matches 8.6's widths on every key of the `text` probe),
  scaled; each glyph is placed individually. Caret, selection,
  `TruncString`, line breaks are unchanged because the program never sees
  the high-resolution rendering. With `FractEnable`, fractional positions,
  better than QuickDraw's rounding.
- **Glyphs from outlines**, anti-aliased, ppem = size x scale, light or no
  hinting (Skia over FreeType). 8.6 has outlines for Charcoal, Chicago,
  Geneva, Monaco, New York, Times, Helvetica, Courier, Palatino, Symbol and
  the Microsoft core fonts. Bitmap-only fonts: a substitute outline font
  from a table, else the 1x strike upscaled. Where the 1x advances come
  from a bitmap strike (Geneva 9), the outline glyph may look slightly
  tight or loose in its advance; scaling each glyph horizontally to fill
  it is an option.
- **QuickDraw's styles synthesised**: bold as embolden, italic as skew,
  outline and shadow as stroke and offset fill (at device pixels),
  underline at the scaled `ulOffset`; condense and extend are in the 1x
  advances already.
- **Text modes**: `srcOr` anti-aliased; `srcCopy` fills the background box
  first; `grayishTextOr` (dimmed) as grey text instead of a dither;
  `srcXor` and `notSrcXor` not anti-aliased (programs rely on XOR text
  erasing itself exactly).
- **Gap: ATSUI.** Text drawn through ATSUI (8.6's window titles via
  WindowDefinitions, Unicode-aware programs) is rendered by Apple's scaler
  and blitted: 1x unless Executor provides ATSUI's glyph rendering. ATS is
  also what hangs Date & Time (MacOS8-9.md), so it is worth owning.

### 5. Knowing when a twin is stale

Guest memory is host memory, so **host page protection** catches every
writer: the 68k core, the PowerPC core's compiled code, Executor's C++.
No CPU-core hooks or emulated MMU are needed (core write taps would cost
per-store checks in every core and still miss host writes).

- `NewGWorld` is Executor's: it page-aligns and pads pixel buffers, so a
  protected page belongs to one GWorld and `rowBytes` maps it to a band of
  rows.
- A GWorld whose shadow is in sync has its pixel pages read-only. The
  first guest write faults; the handler marks those rows raw, unprotects
  the page and resumes (Executor's SIGSEGV handling already resumes on
  guest memory, `--watch`).
- At the next `CopyBits` from the GWorld, raw bands are re-made from the
  1x pixels (upscaled, or sent to the image upscaler when the content is
  stable, section 6), then the pages are protected again.
- **Thrash guard**: a GWorld that faults every frame (a game's back
  buffer) stops being protected and stays raw.
- The screen framebuffer (outside guest RAM, `$41000000` on Executor) is
  host memory too: the same mechanism marks areas a program wrote
  directly.
- A cheaper first version, without protection: hash the 1x pixels of the
  copied rectangle at `CopyBits` time and compare with the hash at the
  last sync.

### 6. Icons and stable bitmaps

- **Key**: hash of the icon's 1x bits and mask (`ICN#`, `ics#`, `icl8`,
  `ics8`, `cicn`, Icon Services' families), or of a `PICT`'s bytes.
- **Order**: a real larger size the System or program has (8.5's `icns`,
  up to 128x128); else the image-upscaler cache; while that is made, a
  pixel-art filter (xBR/ScaleFX) placeholder.
- **Generation** is asynchronous, never in a draw call: the first draw
  shows the placeholder, the result replaces it when ready.
- **Stored large** (256 px for icons) on disk, keyed by hash and shared by
  every session; resampled to the actual display scale. A batch pass over
  a disk's resources can fill most of it ahead of time (icons are a finite
  set).
- **Where the model runs**: locally by default (Real-ESRGAN class): a
  remote service would publish the user's images.
- **Opt-in per kind**: icons and art yes; images containing text, and the
  user's own documents, no (an upscaler invents detail).
- Not for per-frame content (games, video): latency, flicker between
  frames, cost.

### 7. The theme, semantically

Platinum on 8.6 is mostly lines: AppearanceLib, which draws every window
frame and control for WindowDefinitions and ControlDefinitions, makes
about 800 `LineTo`/`Line`/`MoveTo` calls against 29 bitmap copies
(`CopyBits`, `SmartCopyMask`; which parts those are is not traced yet).
Recorded at the primitives, that chrome is already vector. Better still,
the `DrawTheme*` calls (`DrawThemeWindowFrame`, `DrawThemeButton`,
`DrawThemeTrack`, ...) say *what* is drawn, a pressed default button, so
the presentation surface can draw it its own way: platinum redrawn with
gradients, or another theme altogether. Executor's own window and control
definitions route through `DrawTheme*` on 8.x (MacOS8-9.md, "One
Toolbox"), so this covers both. The 1x truth keeps Apple's pixels (or
Executor's approximation of them).

### 8. Compositing

The display is the presentation surface. Areas marked raw (direct pixel
writes, raw GWorld bands, pure pixel engines, video) show the 1x pixels
upscaled instead; the dirty-rectangle tracking the video output already
does is where the two meet. The video path (triple buffer, encoders,
`/api/screenshot`) gets frames at the display size; `/api/screenshot`
keeps a 1x option for the test tools.

## Library

Skia (BSD-3): immediate-mode canvas; paths and boolean path operations;
`SkRegion` (QuickDraw-like scanline regions); image shaders for patterns;
blend modes plus arithmetic and runtime blenders for QuickDraw's transfer
and arithmetic modes; image filters (blur, drop shadow) for effects; text
over FreeType with hinting; CPU raster, GPU (Ganesh/Graphite) and
WebAssembly (CanvasKit) back-ends. Cost: a large dependency (GN build,
several MB). Runner-up Blend2D (zlib): small and fast, but no regions, no
filters, simple text, no JIT in wasm.

New code is written from documentation and behaviour (LICENSING.md);
FreeType is FTL (BSD-style) when used through Skia.

## Phases

1. **Text only, screen only.** Record `StdText` on the screen port; draw
   glyph runs with Skia over a 2x upscale of the 1x framebuffer. Measure
   the gain on Finder windows, dialogs, menus, SimpleText.
2. **Primitives and region twins** on the screen port: the chrome.
3. **`DrawTheme*` semantic drawing.**
4. **GWorld shadows** with hash-at-blit staleness; then page protection.
5. **Icon cache**: big Icon Services sizes, then the upscaler.
6. **Fractional scales**, GPU back-end, wasm.

## Testing

- The 1x truth must not change: the difftest probes and the grid compare
  as today (`tools/difftest`, `tools/executor/grid_compare.py`).
- The presentation surface, downscaled, should match the 1x truth closely
  except where a different look is intended (text anti-aliasing, dimmed
  text): a per-session check that flags areas where the two disagree
  beyond a threshold, which finds missed bottlenecks and stale twins.
- Grid screenshots at 2x beside 1x (`grid.py` with a scale option).

## Programs that won't benefit

- Pixel engines: Marathon, Doom, Quake, Escape Velocity, SimCity 2000,
  Lemmings (upscaled; arguably right).
- Video and pre-rendered images: QuickTime, MoviePlayer, Myst, Riven,
  Director titles.
- Paint documents: MacPaint, Photoshop, Kid Pix, ClarisWorks paint;
  HyperCard's card art (1-bit) and bitmap-font card text (fields and
  buttons do benefit).
- Browsers' images (their text can, through GWorld shadows).
- Kaleidoscope schemes (window frames blitted from the scheme's images).
- Documents set in bitmap-only fonts.

## Open questions

- Which of AppearanceLib's 29 bitmap copies are chrome (check marks, radio
  dots, thumb grips?) and need twins.
- Executor-internal blits that bypass the `Std*` entry points.
- Pattern policy: which patterns become solid colours, which stay pixel
  art.
- Owning ATSUI's rendering (also the Date & Time hang).
- Cost of Skia at full redraw rates on the CPU back-end; when to use the
  GPU.

## Phase 1, as built (2026-10-31)

### Building and running

Skia is optional and off by default: `tools/skia/fetch.sh` fetches the
prebuilt static Skia of JetBrains' skia-pack (release `m144-e2e6623374-4`,
SHA-256 pinned in the script) into `~/storage/deps/skia-m144-linux-x64`, where
`-DEXECUTOR_SKIA=ON` looks for it (`-DSKIA_DIR=...` otherwise). It links
`libskia.a` and Skia's own `libfreetype2.a` (FreeType, FTL; static and not
exported, so the system's FreeType that Qt loads is untouched); the binary
grows by about 6 MB. Building Skia ourselves (GN, depot_tools, a vendored
subset) was not needed: skia-pack also has `linux-Release-arm64` (for
`tools/xarch` builds; `fetch.sh arm64`) and `wasm-Release-wasm` static
libraries built with Emscripten (`fetch.sh wasm`), which the wasm build
could link instead of CanvasKit (CanvasKit is Skia's JavaScript API; we
draw from C++, so the static libraries are what we'd want; whether their
Emscripten version links with ours is untried).

```
tools/skia/fetch.sh && cmake -B build -DEXECUTOR_SKIA=ON && cmake --build build -j4
./build/mac-phoenix --backend executor --executor-system 8.6.img --executor-ppc --executor-scale 2
curl -o 2x.png localhost:11000/api/screenshot           # the presentation surface
curl -o 1x.png 'localhost:11000/api/screenshot?scale=1' # the Mac's own pixels
```

`--executor-debug hidpi` logs each piece of text StdText draws on the screen
and what became of it (recorded, replaced, or left 1x and why).

### The pieces

- `romlib/hidpi/hidpi.h`: the interface; inline no-ops without
  `EXECUTOR_SKIA`, so QuickDraw's code has no `#if`s.
- `romlib/hidpi/hidpi.cpp` (emulator thread): `text_helper`
  (quickdraw/qStdText.cpp) hands over, for text drawn on the screen port
  at 1:1 (no numer/denom scaling), each character with the pen position
  the 1x layout gives it, the font the Font Manager chose (`WidthTable`
  `fID`, size, face), and StdBits' call as a function. Around that call the
  framebuffer's rows under its rectangle are copied (before, after): a
  **run**. The family's outline is found through the Font Manager's own
  search (`ROMlib_family_outline`, every FOND of the family) and copied
  out of the guest once per font; an outline the Font Manager hadn't
  loaded is loaded and emptied again, its error globals put back.
- `romlib/hidpi/hidpi_skia.cpp` (render thread; no Executor headers): for
  each rectangle the video driver redraws, the 1x pixels scaled up
  (nearest neighbour; pixel edges `ceil(x * scale)`, so fractional scales
  work), then each run over that rectangle that is still on the screen:
  the pixels its 1x text changed back to what they were before it, then its
  glyphs from the outline (anti-aliased, slight hinting, subpixel
  positions) at ppem = size x scale, clipped to the port's vis and clip
  regions (1x, scaled) and a pixel round the run.
- `PhoenixVideoDriver` keeps the presentation surface beside the 1x frame
  and composes it after each conversion; `executor_child` publishes both
  in the IPC slot (the surface, then the 1x frame: `scale_milli`,
  `base_width`, `base_height` in `IPCBuffer`). The parent sizes its video
  frames for the largest mode then, divides the page's absolute mouse
  positions by the scale, sends the scale with the cursor image (the page
  draws the cursor at the Mac's size), and `/api/screenshot?scale=1`
  answers the 1x frame.

### Runs and staleness

A run is shown while the screen still has what it left: the pixels it
changed, and a pixel round them (a selected label's black box erased under
its white srcBic text changes only those), or for srcCopy its whole box.
What draws over a run without being text (an erase, a highlight, a window
moving over it) makes it stale and the 1x pixels show; text drawn over a
run updates what the run left (it is drawn after it). A run that a later
one draws again glyph for glyph is replaced, the later one taking its
"before" where its text is still underneath: TextEdit erases and draws the
whole line again at each keystroke, and the two drawn together darkened
the anti-aliasing; the caret blinking over a run is enough to make it
stale, so replacement looks at stale runs too. srcXor drawn twice in the
same place (erasing itself) removes both. At most 4096 runs; stale ones go
when text is drawn over them.

Text modes: srcOr, srcBic (the text in the background colour: selected
icon labels), srcCopy (the box filled first), grayishTextOr (grey, the
foreground and background colours mixed, instead of the dither), srcXor
(aliased glyphs inverting the surface). The others stay 1x. Styles: bold
as QuickDraw makes it (the glyph again `FMOutput.bold` pixels to the
right, at the device's pixels: Skia's embolden was far lighter than
QuickDraw's bold), italic as a skew of 0.25, underline at the baseline + 1,
`ulThick` thick, outline and shadow as strokes. An outline of the style
asked for is used when the family has one.

Each glyph is **centred in its 1x advance**: a bitmap strike's
proportions aren't the outline's (Geneva 12's i, l and j are wide cells in
the strike and thin glyphs in the outline, so placed at the pen they left
uneven gaps, "qui ck").

### What it looks like (2026-10-31, 8.6 and 7.5.5 at 2x)

- **Menus** (titles and items, dimmed items as grey, command-key glyphs),
  window titles (Charcoal on 8.6), the BridgeAgent window, **General
  Controls** (Geneva 9 and 9 bold), **SimpleText** (Geneva 12 in plain,
  bold, italic, underline), dialogs: from outlines.
- **7.5.5's Finder** draws its windows on the screen: icon labels and the
  window headers are from outlines too. **8.6's Finder** draws window
  contents offscreen and copies them: they stay 1x (GWorld shadows,
  phase 4). The desktop's labels are drawn on the screen in both.
- **Chicago** looks almost the same at 2x: its TrueType outline is drawn in
  pixel steps like the bitmap. Charcoal, Geneva, New York and Monaco gain
  the most.
- Not covered (1x): transfer modes other than the five above, text scaled
  by numer/denom (pictures), runs with a character the outline lacks,
  bitmap-only families (no substitution table yet), everything drawn
  offscreen.
- Proportions: outline glyphs centred in bitmap advances read well;
  scaling glyphs to fill the advance wasn't needed. Synthesised italic
  (QuickDraw slants about the strike's bottom, Skia about the baseline) is
  shifted by about descent/2 pixels from the 1x one, inside the run's box.
- Clipping: runs are clipped to the 1x vis and clip regions, scaled
  (staircase edges at 2x: region twins are phase 2); no overflow past
  window edges was seen.
- `/api/text`, `/api/ui`, measurement, hit testing: unchanged (the 1x truth).

### Open after phase 1

- Text through GWorlds (8.6's Finder windows, most modern programs):
  phase 4's shadows.
- Selection highlighting (InvertRect over text) and other non-text drawing
  over text show the 1x text until it is drawn again.
- A substitute outline for bitmap-only families; ATSUI (8.6's
  WindowDefinitions titles: Executor draws its own titles today).
- The render thread's cost: every redrawn rectangle is scaled and its runs
  drawn again (no cache of drawn runs). Measured (`--executor-debug hidpi`
  prints it every 5 s) on 8.6 at 640x480 x 2, typing in SimpleText and
  opening General Controls: at most 1.3 ms a frame, 30-40 ms per 5 s
  (under 1% of a core), 25-44 runs on the screen. The GPU back-end can
  wait.
