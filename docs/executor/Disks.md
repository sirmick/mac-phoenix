# The tab's Disks panel

The page's disks (`executor/web/disks.js`, `disks.css`; main.js owns the
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
  uploaded into the server's library (main.js `uploadImage`, executor/server
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
executor/host `mount`, requests.cpp):

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
included, executor/host notices at the next event call (images.cpp `watch_images`: a
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

## System Folder additions

What an application's installer put in the System Folder (its
preferences and registration, Adobe's Application Support and shared
libraries, fonts, ColorSync profiles) is on its library disk too, and the
tab copies it into the System it runs on. Photoshop 4.0 wants its Pipeline
Preferences ("Could not personalize" without), Photoshop 5.5 its
Application Support ("a disk error"), and the Systems in the library are
Apple's plain ones.

On the disk: an invisible top-level folder, `System Folder Additions`,
laid out as the System Folder is (`Preferences:`, `Extensions:`,
`Application Support:Adobe:` ...). `tools/web/make_library.py` fills it
from the provisioning snapshots of the products on the disk (each
snapshot's `:System Folder:` part: what its bake's install added or
changed there, `provisioning/bake.py` capture), found through the source
image's definition (`provisioning/images/NAME.json` "place": the folders
each snapshot put at the top); `make_disk.py --system-from` lays them
out, later snapshots' files winning over earlier ones' as in a boot
image's `system_from` (the Creative disks' Adobe Registration Database is
the last Adobe product's). Not added: the System's own files (System,
Finder, Clipboard ...) and what the boot disks' cuts leave out
(`make_boot_disk.py`: Help, ColorSync Profiles, DROP), and startup code:
the System Folder's folders of startup items (Control Panels, Startup
Items, Launcher Items, ...) and files of the types that run at startup or
register themselves anywhere (INIT, cdev, appe, thng, libr, RDEV, fext,
AINI, ...), and CarbonLib. Kept by name: startup code an application
can't do without that runs clean on Executor (allowed in
`executor/core/res/extension-policy.txt`): the Microsoft OLE Extension,
which Works 4.0b won't start without (make_disk.py `STARTUP_KEPT`,
hfs_sideload.cpp `startupCode`). What applications need to start is their
preferences, support files, fonts and shared libraries; installers'
extensions and startup items run at every boot on whatever System the tab
runs. They once brought the tab down, with an illegal instruction a few
seconds after the Finder started, an Executor bug: the Shared Library
Manager's code ('root' 2, read with the System heap current) was in the
startup application heap, as Executor read a resource into its file's
map's zone rather than the current one (`ResourceTest.LoadsIntoCurrentZone`,
res/resOpen.cpp `ROMlib_load_zone`); that heap is reused once the Finder runs
(here as it launched Kodak's application in Startup Items), and the
`.SLM` driver's accRun jumped into what was put there. The disk's record
says what is there: `system_additions` `{folder, files, bytes, items,
restart}`, `items` one level into the System Folder's folders, `restart`
what is in use only after a restart. A disk entry with `"additions":
false` carries none. Aliases to nothing are left off the disk (make_disk.py
`--with`, the boot disks of its Systems: Photoshop 5.5's Preview In aliases
to browsers no disk has, its Adobe Gamma alias into Control Panels), and
`tools/diskaudit/disk_audit.py` knows the folder (not a stray invisible
one), resolves aliases into a System Folder against it, and checks a disk's
aliases only against the boot disks of the Systems its record lists.

In the tab (web_main.cpp `add_to_system`, hfs_sideload.cpp
`system_additions`, the same HFS/HFS+ reader the System is copied from):

- Which disks: those whose record lists the System running (the
  library System disk's version, its record's `systems`: main.js
  `additionsFor`); an 8.6-only disk's libraries stay out of 7.6.1, and a
  disk or System not from the library gets none. The page passes them as
  `--additions IMAGE` at the start and inserts them with
  `mount-additions` (plain `mount` for the rest).
- When: for the disks there at the start, before Executor starts (fonts
  in use at once; on Executor's own System, which it
  makes as it starts, at the first event call); for a disk put in later,
  at `mount`, before `_MountVol`. Executor's volume lists a folder again
  after a second (localvolume's directory cache), CFM looks in Extensions
  when a library is asked for, and applications open their preferences
  when they start: the rest is in use at once.
- What: each file the System hasn't, or has older (by modification date)
  than the disk's; startup code skipped here too (disks made before the
  cut left it out carry it). A newer one (preferences the user changed since) stays.
  Folder names match without case, as HFS's do.
- Restart: fonts copied while Executor runs are in use after one: the
  page says so in its status line ("NAME: N files added to the System
  Folder; restart to use Fonts").
  `Module.onAdditions` -> the Worker's `additions` message ->
  `executor.additions[NAME]` `{copied, kept, restart, live}`.
- Eject: the additions stay, as an installer's would on a Mac: other
  disks' applications may use the same files (Adobe's), and they are kept
  with Executor's volume on the server like the rest of its System. Revert
  doesn't take them out either. A fresh start of the System (the saved
  volume forgotten) is the way back to a plain one.
- CDs (`--cdrom`, `mount-cd`) are left as they are.

## Requests (executor/host/requests.cpp, web_main.cpp)

`mount PATH`, `mount-cd PATH`, `eject PATH` and `volumes` (the mounted volumes' names)
join `app`, `windows`, `menus`, `launch`; the page's `insert`
(`{ file, cd, ids, legacyKey }`) and `eject NAME` go through the Worker.

## Tests

`executor.web.disks` (`executor/tests/web/tab_disks.mjs`, standalone, the System
from `EXECUTOR_WEB_SYSTEM` picked in the panel from the library): a blank
disk put in while the Finder runs (no restart), a folder made on it,
Revert (the folder gone, the disk back in), Put Away in the Finder (out
of the panel and the setting), a CD read-only and out by the panel's
eject, a disk SimpleText runs from in use (the eject refused, the disk
in) until SimpleText quits; that disk's System Folder additions (a
preferences file, a font) copied in as it goes in, the font's restart
said, kept after it is out, nothing copied twice when it is in again.
`system_additions` (`tests/test_system_additions.py`, hfsutils): the
disk's additions folder made from two snapshots. `executor_tests` `HFSUnmountTest.*`
(`executor/tests/hfs_volume.cpp`): a data or resource fork open
makes a blank HFS floppy busy, the desktop database doesn't, the forced
unmount closes. The other tab cells reach the panel through `executor/tests/web/harness.mjs`
(`openDisks`, `pickSystem`, `diskButton`, `diskState`).
