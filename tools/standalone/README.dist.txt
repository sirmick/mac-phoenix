Executor standalone, version VERSION

Executor (a reimplementation of the classic Mac OS Toolbox) running in
your browser, served by one program on your own machine. Nothing else
to install: the page and the emulator are inside the program.

Pick the file for your system:

  executor-standalone-VERSION-linux-amd64        Linux, x86-64
  executor-standalone-VERSION-linux-arm64        Linux, ARM (Raspberry Pi 4/5 with a 64-bit OS, ...)
  executor-standalone-VERSION-darwin-arm64       macOS, Apple silicon
  executor-standalone-VERSION-darwin-amd64       macOS, Intel
  executor-standalone-VERSION-windows-amd64.exe  Windows, x86-64
  executor-standalone-VERSION-windows-arm64.exe  Windows on ARM

Check a download against SHA256SUMS:
  sha256sum -c SHA256SUMS --ignore-missing         (Linux)
  shasum -a 256 -c SHA256SUMS --ignore-missing     (macOS)
  certutil -hashfile FILE SHA256                   (Windows: compare by eye)

Run it:
  ./executor-standalone-VERSION-linux-amd64 --open
It serves http://127.0.0.1:8090/ (--open opens it in your browser). The
first browser to open it has it until the server stops: any other browser
is told it is in use, and gets nothing from it. Restart the server to
use it from another browser; a tab left open then says to reload.
The binaries are not signed: on macOS, chmod +x the file and, the first
time, right-click > Open (or: xattr -d com.apple.quarantine FILE); on
Windows, SmartScreen asks once (More info > Run anyway).

Disk images go in the images folder: by default the "images" folder of
the data directory (~/.config/executor-web on Linux, ~/Library/Application
Support/executor-web on macOS, %AppData%\executor-web on Windows), or
--images DIR. An image is offered when NAME.img has a NAME.img.json
beside it ({"title": "...", "note": "..."}). Changes you make to a disk
are kept beside it (NAME.img-ID.overlay); the image itself is not
written unless you merge them.

  --port N, --bind ADDR   another port; listen beyond this machine
                          (--bind 0.0.0.0: it prints the LAN addresses too)
  --tls auto              HTTPS, with a certificate it makes once (self-signed:
                          the browser asks once; compare the SHA-256 it prints)
  --data DIR              the data directory
  --help                  every flag

More: docs/executor/Standalone.md in the MacPhoenix sources.
