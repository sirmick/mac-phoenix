# High-resolution rendering for Executor (design)

Executor is QuickDraw: there is no Mac OS ROM, and every drawing call any
program makes, 68k or PowerPC, Apple's AppearanceLib included, ends in
Executor's C++. That lets Executor render the Mac's screen at any
resolution (2x, 3x, fractional) with anti-aliased text and vector chrome,
while programs keep seeing the 640x480-style 1x screen they were written
for. The ROM machines can't do this: their QuickDraw is Apple's code in
ROM.

Status: phase 1 (text on the screen) implemented, 2026-10-31; phase 2
(chrome) and phase 4 (GWorld shadows) started 2026-11-01: see
"Phase 1, as built" and the sections after it at the end. The rest is
design.

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
draw from C++, so the static libraries are what we'd want).

**wasm link: skia-pack's prebuilt doesn't link; build Skia ourselves
instead (2026-10-31).** `-DEXECUTOR_SKIA=ON` configures `build-wasm`
cleanly (the `find_library()` calls needed `NO_CMAKE_FIND_ROOT_PATH`:
Emscripten's toolchain file restricts `find_library` to its own sysroot
otherwise), but linking skia-pack's wasm package fails: `libskia.a`'s
FreeType/libpng objects are undefined on `__wasm_longjmp`/`__c_longjmp`.
skia-pack's wasm build was compiled with clang 21 using Emscripten's newer
wasm-native setjmp/longjmp (`-sSUPPORT_LONGJMP=wasm`, paired with
`-fwasm-exceptions`); our emsdk (clang 23, `~/emsdk`) defaults to the
older JS-polyfill SjLj, paired with the JS-polyfill exception model we
already build the rest of Executor's wasm target with. Passing
`-sSUPPORT_LONGJMP=wasm` at link time alone just moves the break (our own
`romlib` objects that call `setjmp`/`longjmp`, e.g. `launch.cpp`, were
compiled expecting the old runtime's `emscripten_longjmp`, and the
Emscripten `libc++.a` picked for the new mode is missing symbols the old
one didn't need either); matching it at compile time too makes `clang++`
refuse the combination outright (`'-wasm-enable-sjlj' not allowed with
'-enable-emscripten-cxx-exceptions'`), because the new longjmp model
requires the new wasm-exceptions model too — not a Skia-local flag, but a
switch of Executor's *whole* wasm build's exception model (Asyncify,
which our process switching relies on, doesn't support `-fwasm-exceptions`
either, so that path is closed).

So: `tools/skia/build-wasm.sh`, beside `fetch.sh`, builds Skia from
source with *our* emsdk instead of using the prebuilt. A minimal config
for what phase 1 actually needs — core, paths, fonts/glyphs from
outlines (FreeType), raster backend — with no GPU, no image codecs
(decoding isn't needed: fonts arrive as bytes already in memory, handed
to Skia via `SkData`), no SVG/shaping/ICU. The one surprise: FreeType's
own `BUILD.gn` depends on libpng (for color-emoji glyph support)
unconditionally, and libpng on zlib, regardless of codec flags — those
three stay, built minimally too (no codec use otherwise). GN
(`bin/fetch-gn`, a direct download, no `depot_tools`/CIPD needed) and
three `git fetch --depth 1` clones at DEPS-pinned commits (Skia itself
pinned to `fetch.sh`'s same `m144-e2e6623374-4` revision; FreeType,
libpng, zlib at the commits Skia's `DEPS` pins for it) are the only
inputs; `ninja -j3 skia` builds in about 90 seconds. The script writes
its output into `fetch.sh`'s own wasm destination
(`~/storage/deps/skia-m144-wasm-wasm`), same directory layout, so
`-DEXECUTOR_SKIA=ON`'s existing `SKIA_DIR` default picks it up with no
further CMake changes for path resolution — `tools/skia/fetch.sh wasm`
is superseded for this target; run `build-wasm.sh` instead. One addition
*was* needed in `CMakeLists.txt`: the native/arm64 prebuilt's extra
FreeType dependencies (libpng, zlib, skcms) get their symbols for free
from the system zlib/libpng that Qt already links; the wasm build links
no such thing, so `libpng`/`zlib`/`skcms` are now found and linked
explicitly alongside `libskia`/`libfreetype2` (harmless if unreferenced:
a static archive only pulls in the object files a symbol reference
actually needs).

Built this way, `executor-web.wasm` (Release, `source
~/emsdk/emsdk_env.sh && emcmake cmake -B build-wasm -DEXECUTOR_SKIA=ON
-DCMAKE_BUILD_TYPE=Release && cmake --build build-wasm -j3 --target
executor-web`) is 63.8 MB (6.67 MB gzipped) versus 59.8 MB (5.39 MB
gzipped) with `-DEXECUTOR_SKIA=OFF` — Skia costs about 4.0 MB
uncompressed, 1.27 MB gzipped, in this minimal configuration.

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
same place (erasing itself) removes both. The replacing run is also drawn
inside the replaced one's clip (AppleWorks draws its font pop-up's name
again through a clip of its right half). Text whose drawing the port's
clip leaves nothing of is not a run and changes none (a dialog's items
drawn again outside its update region). A run drawn over until no pixel
it changed is left (a button drawn up again over its pressed look) goes:
it could never be found stale. One whose drawing changed nothing (chrome
drawn again over the same pixels) is stale at any change in its box. At
most 4096 runs; stale ones go when text is drawn over them. A run knows its port: when the Window Manager computes that window's
visRgn again (CalcVis: a window put in front), the run is clipped to it,
and when the window is hidden or closed its runs go. The pixels alone
can't tell: an alert's white over a text window's white left slivers of
the outline glyphs inside the alert, and a closed alert's pressed Cancel
(a grey box, its title white) came back over a document's white
(`tests/test_hidpi_windows.sh`). Runs a menu's saved bits carry back keep
their window's port.
`tests/test_hidpi_chrome.sh` (ChromeGallery) has a case for each.

Text modes: srcOr, srcBic (the text in the background colour: selected
icon labels), srcCopy (the box filled first), grayishTextOr (grey, the
foreground and background colours mixed, instead of the dither), srcXor
(aliased glyphs inverting the surface). The others stay 1x. Styles: bold
as QuickDraw makes it (the glyph again `FMOutput.bold` pixels to the
right, at the device's pixels: Skia's embolden was far lighter than
QuickDraw's bold), italic as a skew of 0.25, underline at the baseline + 1,
`ulThick` thick, outline and shadow as strokes. An outline of the style
asked for is used when the family has one.

Each **word keeps its 1x place**, its glyphs evenly spaced in it: a
bitmap strike's proportions aren't the outline's (Geneva 12's i, l and j
are wide cells in the strike and thin glyphs in the outline, so placed at
the pen they left uneven gaps, "qui ck"). Each glyph centred in its own 1x
advance still left them ("P ro", "Regis ter"); now a word (glyphs each
starting where the last's 1x advance ended, up to a space) takes the
outline's own advances and spreads what's left of its 1x width evenly
between them (`glyph_lefts`, hidpi_internal.h; a lone glyph is centred).

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
- Proportions: outline glyphs evenly spaced in each word's 1x width read
  well; scaling glyphs to fill the advance wasn't needed. Synthesised italic
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

### The wasm tab

Built with `tools/skia/build-wasm.sh` (above) and `--scale` on
`executor-web`'s own argv at start (`executor/web/main.js`: the
`display.scale` setting or `?scale=`, default `devicePixelRatio`;
`executor/web/worker.js` passes it through), then changed live (below).
`PhoenixVideoDriver::render()`'s `rectsHook` branch (wasm's only path)
now calls `hidpi::compose()` and publishes the whole `scaled_` surface
through the same `rectsHook` every time (`compose()` doesn't report how
far outline-glyph padding extended the dirty area, and this matches
`scaledFrameHook`'s own full-frame-every-publish cost on native, not a
regression). `guestPoint()`'s mouse mapping needed no change: the
canvas element's `width`/`height` IDL attributes freeze at the 1x size
before `transferControlToOffscreen()` detaches the backing buffer that
resizes to the scaled dimensions in the Worker.

### The scale changed live

In the tab the scale changes while the Mac runs (the bar's HiDPI box,
Settings > Display > Sharpness; Settings.md). The page's `'scale'` request
(`web_main.cpp`) calls `VideoDriver::requestScale`; the Display Manager
takes it at the front process's next event call, as a screen size
(`ROMlib_dm_poll`, `displays.cpp` `dm_set_scale`):
`PhoenixVideoDriver::setScale` sets the scale under the frame lock
(`hidpi::set_scale` drops every run, the screen's and the offscreen
buffers'), makes `scaled_` again at the new size (none at 1) and marks the
whole screen; then the whole screen is painted, as after a size change with
nothing moved (`ROMlib_layers_screen_changed(now, now)`: frames, desktop,
update events for the contents) and the menu bar drawn, so text and chrome
are recorded again at the new scale (an application's contents when it
answers its update event). Programs see nothing: the 1x screen, the
GDevice and the Display Manager's mode are unchanged. Executor logs
`[display] scale 1 -> 2`. The frames then go out at the new size and the
Worker's canvas follows them (worker.js `onRects`); the page's fit,
clicks and pointer are in 1x pixels already. Only where the frames go out
through `rectsHook` (the tab): natively the parent sized its shared frames
for the scale Executor started with, so `--executor-scale` stays fixed
there (`setScale` answers false). Test: `ctest -R executor.web.hidpibar`
(executor/tests/web/tab_hidpibar.mjs: 7.6.1 at dpr 2, 2x, 1x and 2x again: the
canvas's size, the menu bar from outlines, the pointer and a menu click
exact, the setting kept over a reload).

Gate: `ctest -R executor.web.hidpi` (needs a Playwright-capable
`EXECUTOR_WEB_NODE_PATH`; `executor/tests/web/tab_hidpi.mjs`). Boots the same
7.6.1 Finder at `?scale=1` and `?scale=2`, and checks the 2x run's menu
bar actually differs from a naive nearest-neighbor doubling of the 1x
run by more than a sliver -- the plain-upscale look that not calling
`hidpi::compose()` would silently produce otherwise. Measured on this
box: canvas backing buffer doubles correctly (640x480 to 1280x960); 2.8-2.9%
of the menu bar's pixels differ from the naive upscale (antialiasing and
hinting at the glyph edges, which a bitmap blow-up has none of); the
Worker's own per-frame draw cost (`Module.stats`, the 'stats' request)
goes from about 0.1-0.14 ms/frame at scale=1 to 2.9-3.8 ms/frame at
scale=2 -- noticeably higher than native's 1.3 ms worst case above,
likely wasm's slower Skia raster path plus `compose()`'s full-surface
republish on every change rather than the dirty rectangle alone;
unoptimized (this was the first run through, not a target to chase
yet). Module size (Release, gzipped over the wire): 59.8 MB/5.39 MB
without Skia, 63.8 MB/6.67 MB with it -- about 4.0 MB/1.27 MB gzipped
for HiDPI in this minimal configuration.

## Phase 2, chrome (started 2026-11-01)

Chrome runs beside the text runs (`hidpi::chrome_draw`): a theme drawing
call that draws on the screen hands over its 1x rectangle, a function that
draws its pixels, and the same piece as vector shapes (`hidpi::Shape`:
circles, polygons, polylines; filled, stroked or a radial gradient; 1x
coordinates). The run keeps the pixels before and after, as a text run
does: while the screen still has what it left, the presentation surface
puts back what was there before and draws the shapes anti-aliased at the
display scale; anything else drawn over it shows the 1x pixels again. A
piece drawn again unchanged replaces its run (keeping the first one's
"before"). Shapes are built only at a scale above 1; the 1x truth is
unchanged (the difftest probes).

So far: radio buttons (a sphere as a radial gradient, its outline, the
dot), the check box's tick (the box itself is lines: crisp at whole
scales already), every triangle arrow `appearance::arrow` draws (scroll
bar arrows, pop-up and little arrows), the slider's thumbs (pointing
down, up or neither), north
tabs (the top corners' curve into a straight slant), push buttons'
rounded corners, the title-bar boxes of document and floating windows
(their lines as they are, the face's diagonal steps a linear gradient:
`gx2`/`gy2` with `gr` 0) and the menu bar's rounded top corners (two runs
around the whole bar's drawing). A piece the port's clip leaves nothing
of records no run, so the runs under it stay (the menu bar's background
redrawn under one title).

`tests/guest/chrome/ChromeGallery` keeps a window of this chrome on the
screen; `hidpi_chrome_executor` (tests/test_hidpi_chrome.sh) runs it on
8.6 at scale 2 and checks that inside each piece the 2x surface differs
from the 1x frame scaled up, and inside a plain face does not.

Not yet: other tab directions, the indeterminate progress bar, the
disclosure triangle, pressed floating boxes,
the grow corners; region twins for clipping (section 2: nothing in 8.6's
chrome clips to a curved region yet).

## Phase 4, GWorld shadows (started 2026-11-01)

Text and chrome drawn into a GWorld, or any port whose pixels aren't the
screen's (an old-style offscreen `BitMap` too), are recorded as runs of
that pixel buffer exactly as on the screen (`hidpi::recording()`,
`port_target()` in hidpi.cpp): the same `Run`, its box, "before" and
"after" in the buffer's own pixels and coordinates. A buffer's runs are
kept by its GWorld (whose pixels move while unlocked) or else by its base
address, at most 1024 runs a buffer and 64 buffers (least recently used
go); `DisposeGWorld` drops them, and a buffer whose size, `rowBytes` or
depth changed (`UpdateGWorld`) starts again empty. They live on the
emulator thread only: the render thread still sees only the screen's.

So the shadow is not a bitmap at the display scale but the runs
themselves, kept where they were drawn, until a copy puts them on the
screen. `hidpi::copy_bits()` wraps `ROMlib_real_copy_bits`, which
`StdBits`, `CopyBits`, `CopyMask` and `CopyDeepMask` all reach with their
pixmaps canonicalised and locked. When the source has runs still valid in
the rectangle copied, each lands in the destination (the screen, or
another offscreen buffer: copied twice they still arrive) moved by the
copy's offset: its box the part copied, its "after" the destination's
pixels after the copy, its "before" the source run's "before" wherever the
destination got the source's pixel (masked out or clipped: the
destination's own, so not ink), its clip the run's own, moved, within the
destination rectangle, the port's vis and clip and the mask region. A run
nothing of whose ink landed isn't carried. Runs already in the destination
that the copy brings again (a window's contents copied again unchanged)
are replaced, as text drawn again is, so they aren't drawn twice.

Staleness is checked at the copy, per run against the buffer's 1x pixels
(`run_valid`, as on the screen), which is stricter than the hash of the
copied rectangle the design proposed: a program that wrote the pixels
behind QuickDraw (`GetPixBaseAddr`), or drew anything else over the text,
leaves those runs behind and the copy shows the 1x pixels, upscaled. Page
protection (section 5) is still to do: what it adds is catching writes
that leave a run's ink pixels unchanged (which nothing visible needs yet).

Copies across depths and colour tables carry runs too: a run's ink is
where the destination has the colour the run left in the source pixel
there (the pixmaps' colours: indexed through their tables, a device
table by its entries' values when those are a pixel value each; 16 and
32 bits direct), and its "before" the source's before in the
destination's pixels. `srcOr` and `transparent` copies carry them where
the source's before is the colour they leave out (white, the background
colour): the "before" there is what the destination had before the copy
(text on a coloured background stays on it). A copy to a rectangle of
another size scales them: each destination pixel's source pixel sampled
as `scale_blt_bitmap` does, the glyphs' positions, advances, baseline
and size mapped (a horizontal scale besides, `Run::xscale`), chrome's
points and strokes. Other transfer modes stay 1x.

Scrolling (`ScrollRect`, and programs' own: AppleWorks copies the window
onto itself and the strip it exposes from its GWorld) is a copy like any
other. What it showed: a line cut in two by separate copies is two runs,
each drawn through its own clip, so (1) a piece replaces an existing run
only if it covers all of it, (2) a run's ink stays its own: drawn over by
another (the next line's ascenders in its box), the pixels that one
changed where this one hadn't go into this one's "before" too
(`recapture`), else this one's restoring them erased the other's glyphs
when the pieces came in the other order, (3) a carried run's ink only
where the copy drew, and the render thread's restoring of "before" only
inside the run's clip.

`--executor-debug hidpi` logs each
copy from a buffer with runs and what became of them.

`tests/guest/gworld/GWorldGallery` draws text in GWorlds and copies it to
its window (`CopyBits`, through a second GWorld, `CopyMask` through a
mask of its left part, a GWorld whose pixels it then inverts itself);
`hidpi_gworld_executor` (tests/test_hidpi_gworld.sh) runs it on 8.6 at
scale 2 and checks that the copied text differs from the 1x frame scaled
up and is anti-aliased, and that the inverted GWorld's text and the
masked-out part don't differ. It also copies text from 8-, 16- and 1-bit
GWorlds, with `srcOr` onto yellow and `transparent` onto blue; its Scroll
Gallery window scrolls text three ways (TextEdit's `TEScroll`,
`ScrollRect` in a GWorld, AppleWorks' copies) and every row with text
must be anti-aliased; its Scale window copies text at 2x, 2x wide, half
and 1.5x.

What it gives (8.6, scale 2): the Finder's window contents (icon labels,
the header) from outlines, selection and window moves clean; AppleWorks
5's word processing text (drawn offscreen and copied) from outlines, and
still while it scrolls; the Finder's windows scrolled; the
menus' saved bits (copied to an offscreen buffer and back) keep their
text. BBEdit Lite 6.1 and SimpleText draw on the screen (phase 1).

Fixed on the way (phase 1): a run replacing an earlier one (text drawn
again) took all of that one's clip, so a desktop label drawn again while
a window covered most of it was drawn over the window at 2x; the old clip
is taken now only round the old text still under the new one.

The same in the wasm tab, nothing tab-specific (the runs reach the
screen's list, which `compose()` draws): `executor.web.hidpi_gworld`
(executor/tests/web/tab_hidpi_gworld.mjs) launches GWorldGallery at `?scale=2` on
8.6 with PowerPC and checks the canvas's pixels: the copied text has
anti-aliasing greys, the inverted GWorld's text and the masked-out part
none.

Not yet: transfer modes other than `srcCopy`, `srcOr` and `transparent`;
page protection.
