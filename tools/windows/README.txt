Executor for Windows, version @VERSION@
=======================================

Executor reimplements the classic Mac OS Toolbox: it runs Mac applications
(68k and PowerPC) without a Mac ROM, on its own small System, or on Apple's
System 7.x and Mac OS 8.6 System Folders from disk images. This zip has two
ways to run it; unzip it anywhere (a folder of its own) first.

1. executor-desktop.exe: the desktop application
------------------------------------------------
Double-click it. It starts on Executor's own System (the Browser); for
Apple's System, Settings... > System: a System disk image (.img). Disks:
add disk images (Add Image..., or drop them on the window) and tick them to
put them in the Mac. An image is never written: what the Mac changes goes
to an "overlay" in the data folder (Disks > Revert throws it away).

Keys: Ctrl is the Mac's Command key, Alt is Option (Settings > Input).
F11: full screen.

Its data folder: %AppData%\MacPhoenix\Executor
  (paste that into Explorer's address bar), with
  logs\executor-DATE.log    the log: every run appends to the day's file;
                            its first line names the version
  logs\crash-*.txt, *.dmp   a crash report (see below)
  settings.json             the settings
  overlays\                 your disks' changes
  volumes\                  each System's System Folder and preferences
  screenshots\              Screenshot's pictures
  --data FOLDER on the command line uses another folder.

Watching the log as it is written: start it from a Command Prompt or
PowerShell with --console:
    executor-desktop.exe --console
(The Log button in the window shows it too.)

If it crashes, a dialog says so and where the report is: two files,
crash-DATE-TIME.txt and crash-DATE-TIME.dmp, in the logs folder. Send
both, with that day's executor-DATE.log.

Command line (all optional):
    executor-desktop.exe [--data FOLDER] [--system IMAGE] [--disk IMAGE]...
                         [--ppc] [--size 800x600] [--console] [IMAGE...]

2. executor-standalone.exe: Executor in your browser
----------------------------------------------------
A small web server with the browser version of Executor inside. Run it
from a Command Prompt:
    executor-standalone.exe --open
and it opens http://127.0.0.1:8090/ in your browser (Chrome, Edge or
Firefox). The first browser to open it has it until the server stops.
Its data: %AppData%\executor-web (disk images go in its "images" folder,
or --images FOLDER). executor-standalone.exe --help lists the rest.
Close the Command Prompt window (or Ctrl+C) to stop it.

SmartScreen
-----------
Neither program is signed, so the first time Windows may say "Windows
protected your PC": More info > Run anyway. (Or right-click the file >
Properties > Unblock, before running it.)

What to expect
--------------
Windows 10 (1903 or later) or 11, 64-bit x86. 68k applications and
PowerPC ones (Settings > PowerPC) run as on Linux. Not yet on Windows:
networking from inside the Mac (MacTCP/Open Transport have no network in
this build). Sound uses Windows' default output device.

Licences: licenses\ (Qt is LGPL-3.0; MacPhoenix GPL-2.0-or-later, Executor
MIT). SHA256SUMS lists every file's checksum.
