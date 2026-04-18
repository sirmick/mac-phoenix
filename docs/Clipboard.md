# Clipboard Bridge

Bidirectional clipboard sync between the host (and ultimately the browser UI) and the emulated Mac OS scrap. v1 scope: 'TEXT' only, MacRoman ↔ UTF-8. Both m68k and PPC.

## Status

- [x] Scrap visibility in `/api/status` (`mac.scrap`: `count`/`state`/`size`/`handle`/`preview`/`chunks[]`/`text`) — `src/core/boot_progress.cpp`
- [x] MacRoman ↔ UTF-8 codec — `src/core/mac_roman.{h,cpp}`
- [x] Codec unit test (`ctest -R mac_roman`) — `tests/test_mac_roman.cpp`
- [ ] Host clipboard cache + 200 ms poller thread — `src/core/command_bridge.cpp`
- [ ] `GET`/`POST /api/clipboard` — `src/webserver/api_handlers.cpp`
- [ ] BridgeAgent scrap watch + apply — `tests/guest/bridge/bridge_agent.c`, rebuild `BridgeAgent.bin`
- [ ] Browser clipboard module — `client/client.js`
- [ ] End-to-end test (m68k + PPC) — `tests/test_clipboard.sh`

## Transport

Two plain disk files in `cfg.bridge_dir` — the existing ExtFS shared folder. Guest sees them via the "Host" volume. No ExtFS interception, no callbacks. Same pattern as the existing `_bridge_cmd`/`_bridge_result` files used by `/api/launch` (see `src/webserver/api_handlers.cpp:292-360` for `bridge_read_file`/`write_file`/`has_file`/`remove_file`).

| File | Direction | Notes |
|------|-----------|-------|
| `_bridge_clip_in`  | host → guest | host writes; agent reads + unlinks |
| `_bridge_clip_out` | guest → host | agent writes; host reads + unlinks |

Both files contain raw MacRoman bytes (no headers, no chunk wrapper). The 'TEXT' framing of the scrap blob is stripped/added by the agent.

## Round-trip flow

```
HOST → GUEST  (browser pastes "hello")
  POST /api/clipboard {"text":"hello"}
    host writes _bridge_clip_in (MacRoman, '\n'→'\r')
    host stashes sha1(bytes) as last_sent_hash
    HTTP 200
  next agent tick (~100 ms)
    stat _bridge_clip_in → present
    read, ZeroScrap(), PutScrap(len,'TEXT',buf), unlink
    last_count = InfoScrap()->scrapCount    (echo guard)
  next agent tick
    ScrapCount unchanged → no _bridge_clip_out write

GUEST → HOST  (user copies in MacWrite)
  toolbox bumps ScrapCount
  next agent tick
    ScrapCount != last_count → GetScrap('TEXT')
    write _bridge_clip_out, last_count = ScrapCount
  host poller (200 ms)
    stat _bridge_clip_out changed → read → unlink
    if sha1(bytes) == last_sent_hash → drop (echo)
    else transcode MacRoman→UTF-8, update cache
  GET /api/clipboard → cached UTF-8
```

## Components

### Mac side — BridgeAgent

`tests/guest/bridge/bridge_agent.c`. Add one new function `poll_clipboard()` called from the main loop alongside the existing `poll_bridge()`. Add `<Scrap.h>` include.

```c
static void poll_clipboard(void) {
    static short last_count = -1;

    /* Apply incoming */
    FSSpec in_spec;
    if (FSMakeFSSpec(0, 0, "\pHost:_bridge_clip_in", &in_spec) == noErr) {
        /* read, ZeroScrap, PutScrap, FSpDelete, stash count */
    }

    /* Detect outgoing */
    PScrapStuff info = InfoScrap();
    if (last_count == -1) { last_count = info->scrapCount; return; }
    if (info->scrapCount == last_count) return;
    last_count = info->scrapCount;
    if (info->scrapState <= 0) return;

    Handle h = NewHandle(0);
    long off;
    long size = GetScrap(h, 'TEXT', &off);
    if (size > 0) {
        /* write *h to Host:_bridge_clip_out */
    }
    DisposeHandle(h);
}
```

`InfoScrap()` returns a `PScrapStuff` pointer to the same low-mem fields you already see in `/api/status` (0x0960..0x096A) — works identically on m68k native and PPC's 68k thunk.

Cap each direction at 1 MiB. After PutScrap from a host POST, re-stash `scrapCount` so the export branch on the same tick won't bounce the text back.

Rebuild via `make -C tests/guest/bridge` (Retro68 toolchain expected on `PATH`). Reinstall via `provisioning/install_bridge_agent.sh`.

### Host side — poller + endpoints

**Background poller** in `src/core/command_bridge.cpp`. Started by `command_bridge_init()` when `cfg.bridge_enabled` and we're in the parent (webserver) process — guarded so it doesn't also fire in the CPU subprocess. Polls `_bridge_clip_out` every 200 ms; on read:
1. Drop if `sha1(bytes) == last_sent_hash` (echo).
2. Else `mac_roman_to_utf8()` → store in cached UTF-8 string under a `std::mutex`.
3. `unlink(_bridge_clip_out)`.

`std::atomic<bool>` shutdown flag; thread joined on exit.

**Endpoints** in `src/webserver/api_handlers.cpp`:
- `GET  /api/clipboard` → `{"text":"<cached utf8>"}` (O(1), reads cache under lock).
- `POST /api/clipboard {"text":"…"}` → `utf8_to_mac_roman()`, write `_bridge_clip_in`, stash `last_sent_hash`. Fire-and-forget (HTTP 200 immediately; if the agent is down, the file just sits there until next launch).

Reject POSTs > 1 MiB UTF-8 with 413; cap GET at the same.

### Browser

`client/client.js`. Pure HTTP, no DataChannel changes:
- On `visibilitychange`/`focus` → fetch GET, attempt `navigator.clipboard.writeText()` inside the user-gesture window.
- On canvas `paste` event or "Paste to Mac" toolbar button → POST.
- Permission-denied fallback: a "Paste from Mac" button that triggers the writeText on click.

## Echo suppression

Belt-and-braces — both sides participate so neither side alone has to be perfect:

1. **Guest** stashes `ScrapCount` after every `PutScrap` (whether guest- or host-initiated). Primary defense.
2. **Host** keeps `last_sent_hash = sha1(macroman_bytes)`. Drops any incoming `_bridge_clip_out` that matches. Covers the rare case where the agent restarts between a POST and re-export.

## MacRoman codec

`src/core/mac_roman.{h,cpp}`. Single 256-entry lookup table for MacRoman→Unicode (System 7.5+ Apple mapping; 0xDB = €, 0xF0 = Apple logo at PUA U+F8FF). Reverse map is sorted by codepoint, binary searched. Unmappable UTF-8 → `?`. Newline swap (`\r` ↔ `\n`) built into both directions.

Unit test (`tests/test_mac_roman.cpp`) covers ASCII, NBSP, curly quotes, en/em dash, Apple logo round-trip, €, emoji rejection, and a full 254-byte MacRoman round trip. Standalone — no core link.

## Limits / non-goals

- 1 MiB cap each direction.
- 'TEXT' only — `'PICT'` (QuickDraw) and `'styl'` (styled runs) deferred. The host can still see them via `mac.scrap.chunks[]` for debugging.
- BridgeAgent stays foreground (no app-type change to background-only).
- No clipboard history, no monitoring of which app put the scrap there.

## Risks worth verifying during implementation

- **Browser clipboard permissions.** `navigator.clipboard.readText` requires a user gesture; auto-sync on focus may need the explicit-button fallback.
- **Disk-backed scrap (`Clipboard` file in System Folder).** `LoadScrap` flushes when scrap exceeds in-memory threshold. Round trip should still work since we go through `GetScrap`/`PutScrap`, but worth a smoke test with large text.
- **Subprocess fork mode.** Confirm the poller thread starts in the parent (webserver) process, not the CPU subprocess. `bridge_dir` is shared across both, but only the parent serves HTTP.
- **`ScrapCount` semantics.** Empirically observed on both m68k and PPC during the debug-field validation phase. `PutScrap` increments; `ZeroScrap` does not. Validation note: PPC's classic toolbox emulation appears to use the same low-mem fields — `InfoScrap()` should remain the right call.

## Test plan

- `ctest -R mac_roman` — unit, codec round-trips. (Done.)
- `tests/test_clipboard.sh` (new):
  1. Boot, wait `boot=Finder`.
  2. POST `{"text":"hello\nworld"}` → 200 ms → MacPerl one-liner reads scrap → assert `"hello\rworld"`.
  3. MacPerl `PutScrap('TEXT', "from-mac\r")` → poll GET → assert `"from-mac\n"`.
  4. UTF-8 round trip: `"café — \u201Csmart\u201D"` exact byte-for-byte.
  5. Echo guard: POST X; verify GET stays unchanged after the agent applies it.
- Run under both `--arch m68k` and `--arch ppc` (label `clipboard`).

## Files

| File | Role |
|------|------|
| `src/core/mac_roman.{h,cpp}` | MacRoman ↔ UTF-8 codec |
| `src/core/boot_progress.cpp` | Scrap debug surfaced via `/api/status` |
| `src/core/command_bridge.cpp` | (todo) clipboard cache + poller thread |
| `src/webserver/api_handlers.cpp` | (todo) `/api/clipboard` GET/POST |
| `tests/guest/bridge/bridge_agent.c` | (todo) `poll_clipboard()` |
| `client/client.js` | (todo) browser clipboard module |
| `tests/test_mac_roman.cpp` | Codec unit test |
| `tests/test_clipboard.sh` | (todo) end-to-end round-trip |
