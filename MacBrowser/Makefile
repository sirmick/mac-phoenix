# Build MacBrowser — a Retro68 m68k app that hosts the guest side of
# the MacBrowser pipeline: opens a window with URL bar + viewport,
# CopyBits's the host pixels out of BrowserShm into the viewport,
# pushes BR_CMD_* commands into the g2h ring, drains BR_EV_* events
# from the h2g ring.
#
# The committed MacBrowser.bin gets installed in the guest's
# :System Folder:Startup Items: so it auto-launches when the disk
# boots with --browser.
#
# Requires Retro68 toolchain on PATH:
#   export PATH="$HOME/Retro68/build/toolchain/bin:$PATH"
# or use the in-tree toolchain at ../toolchain/retro68/bin/.

CC = m68k-apple-macos-gcc
REZ = Rez
TOOLCHAIN = $(shell dirname $(shell which m68k-apple-macos-gcc))/..
RINCLUDES = $(TOOLCHAIN)/m68k-apple-macos/RIncludes

CFLAGS = -O2 -I$(CURDIR)/../src/common/include

TARGET   = MacBrowser
CODE_BIN = $(TARGET).code.bin
BIN      = $(TARGET).bin
APPL     = $(TARGET).APPL
DSK      = $(TARGET).dsk

all: $(BIN)

$(CODE_BIN): MacBrowser.c browser_shm.c browser_shm.h
	$(CC) $(CFLAGS) -o $(CODE_BIN) MacBrowser.c browser_shm.c

$(BIN): $(CODE_BIN) MacBrowser.r
	$(REZ) $(RINCLUDES)/Retro68APPL.r MacBrowser.r \
		-I$(RINCLUDES) \
		--copy $(CODE_BIN) \
		-o $(BIN) \
		-t APPL -c MxBr \
		--cc $(DSK) --cc $(APPL)

clean:
	rm -f $(TARGET) $(TARGET).gdb $(CODE_BIN) $(BIN) $(APPL) $(DSK) \
	      $(TARGET).elf $(TARGET).elf.gdb

.PHONY: all clean
