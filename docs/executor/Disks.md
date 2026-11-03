# The tab's Disks panel

The page's disks (`web/executor/disks.js`, `disks.css`; main.js owns the
disks, the panel draws them). The bar has a **Disks** button (with the
count of disks in the Mac) and no other disk controls: the panel docks on
the right and the Mac's screen moves aside (`fitCanvas` leaves out the
body's right padding). Esc or ✕ closes it; files dropped anywhere on the
page are added and open it.

## Sections

- **System disk**: the System choice (`#system`: Executor's own, an image
  in the Mac, or a library disk tagged `system`, which is put in and
  started from) and what the Mac starts from. An image there shows its
  own state and its Revert and Download (reverting the System's disk
  restarts Executor). **Reset…** forgets Executor's own disk for this
  System (preferences, installed files). Changing the System restarts.
- **In the Mac**: the data disks and uploaded applications. Each has a
  badge and, when there is something to say, what it means:
  - `ORIGINAL`: no changes.
  - `CHANGED`: how much (the overlay's blocks), where it is kept and what
    Revert and Download do. The image itself is never written (overlay.js).
  - `CD · READ-ONLY`: `.iso`/`.toast`/`.cdr`, or a library disk tagged
    `cd`. Mounted locked, as `--cdrom`'s: `ROMlib_locked_images` (at the
    start through the host config's `cdroms`, put in live by `mount-cd`).
  - `THIS VISIT`: a disk the server didn't take: its changes last until
    Executor restarts.
  - `APPLICATION`: copied into `executor:Uploads`.
  - `INSERTING…`: on its way in (downloading, opening, mounting).
  Buttons: ↺ Revert, ⤓ Download, ⏏ Eject. Below them: **Add disk image or
  application…** (the page's `#add`) or drop files: a disk image is
  uploaded into the server's library (main.js `uploadImage`, tools/standalone
  `POST /api/uploads/NAME`, tagged `uploaded`) and put in; one the server
  refuses is held for this visit.
- **Library**: `library.json`'s disks, one's uploads among them, as a
  table: a row per disk (cover, or an icon for a hard disk, CD or System;
  title with its tags beneath; size; the Systems it is for, the record's
  `systems` from make_library.py; Insert, or "In the Mac"), sorted by the
  column header clicked (again: reversed); a filter on title, note, tags
  and what is on the disk; tag chips (all must match). A click on a row
  inserts. A disk whose record lists its contents (`items`, WebSite.md
  "What is on a disk") has "▸ N items" under its title: a row opens under
  it, without inserting, with the note and a table of the items (name with
  publisher, year, description; version, arch, size; groups such as
  "Tools ƒ" as headings). An upload
  not in the Mac has a ✕: deleted from the server (`DELETE
  api/uploads/NAME`: image, record, overlay).

## In and out while Executor runs

Inserting needs no restart (main.js `putIn` -> worker.js `insert` ->
web_main.cpp `mount`):

1. The page has the file (a library disk, uploads included, read by Range
   as needed; one held for the visit in memory).
2. The Worker opens its overlay (asking another tab for it first, as at
   the start), fetches its changes (Sync/Store `overlayDown`) and adds it
   to the mounted OVERLAYFS (`/uploads` is mounted even when empty).
3. At the running application's next event call the image's drive goes
   into the drive queue (`ROMlib_image_drive`: locked for a CD, no volume
   yet) and `_MountVol` is called on it through the trap table, as
   GetNextEvent does for a disk put in; `PBMountVol` mounts an image's
   drive. Through the trap because System 7 patches `_MountVol` and
   `_UnmountVol`: that is how the Finder hears of volumes coming and
   going (a mount made from C++ alone never shows on the desktop). A
   `diskEvt` follows for the program in front. The reply is the OSErr
   (-1: the image can't be opened; `nsvErr`: no Mac volume on it).

The tab's images are removable disks (`ROMlib_ejectable_images`), as a
floppy or a CD: the Finder puts them away without asking (`_Eject`); the
System's own image stays a fixed disk. A disk put back on its drive gets
the vRefNum it had (`ROMlib_mount_vrefnum`): the Finder doesn't always
hear an unmount before the disk is back (Revert), and kept with the old
number, its windows and pending flushes would fail with `nsvErr`.

An application added while Executor runs restarts it, as before: it goes
into `executor:Uploads`, and Executor's own volume doesn't see a file put
there under it.

Ejecting (the panel's ⏏, or `eject` by request) unmounts the image's
volumes through `_UnmountVol` (`fBsyErr` when files are open: the page
says so). As on a Mac, an HFS volume with a program's file open on it
(an application running from it, a document open) is busy: `PBUnmountVol`
answers `fBsyErr` and leaves it mounted (hfs/hfsVolume.cpp; HFS+ did
already); the volume's own files (its B-trees, the desktop database's
FCB) don't count, and go with it. The Finder's Put Away gets the same answer.
Executor's end (`ROMlib_OurClose`) closes what is still open first.
Whatever unmounts or ejects a disk image, the Finder's Put Away
included, web_main.cpp notices at the next event call (`watch_images`: a
drive of `/uploads/` that had a volume and has none; an ejected volume
still known offline is unmounted, so no grey icon stays) and calls
`Module.onEjected`; the Worker sends the overlay's changes to the server
at once, takes it out of `/uploads` and tells the page, which takes it out
of the panel and of `disks.mounted` (it stays in the library).

The page follows `disks.mounted` (`x-apply: live`): a URL added there
(the panel's library, the Settings dialog's list, another tab) is put
in, one taken off is ejected. Executor not running, everything waits for
the next start, as before.

Revert takes the disk out, the server forgets its changes
(`DELETE api/sync/overlays/KEY`) and puts it back as it came. A disk in use (or
the System's) reverts with a restart, as before.

## Requests (web_main.cpp)

`mount PATH`, `mount-cd PATH`, `eject PATH` and `volumes` (the mounted volumes' names)
join `app`, `windows`, `menus`, `launch`; the page's `insert`
(`{ file, cd, ids, legacyKey }`) and `eject NAME` go through the Worker.

## Tests

`executor.web.disks` (`web/tests/tab_disks.mjs`, standalone, the System
from `EXECUTOR_WEB_SYSTEM` picked in the panel from the library): a blank
disk put in while the Finder runs (no restart), a folder made on it,
Revert (the folder gone, the disk back in), Put Away in the Finder (out
of the panel and the setting), a CD read-only and out by the panel's
eject, a disk SimpleText runs from in use (the eject refused, the disk
in) until SimpleText quits. `executor_tests` `HFSUnmountTest.*`
(`src/executor/tests/hfs_volume.cpp`): a data or resource fork open
makes a blank HFS floppy busy, the desktop database doesn't, the forced
unmount closes. The other tab cells reach the panel through `web/tests/harness.mjs`
(`openDisks`, `pickSystem`, `diskButton`, `diskState`).
