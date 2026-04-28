# Build BrowserSpike — a Retro68 m68k app that validates the M1
# host→guest pixel pipe by CopyBits-ing the host gradient out of
# BrowserShm at 0x02910000 into a window.
#
# Requires Retro68 toolchain on PATH:
#   export PATH="$HOME/Retro68/build/toolchain/bin:$PATH"

CC = m68k-apple-macos-gcc
REZ = Rez
TOOLCHAIN = $(shell dirname $(shell which m68k-apple-macos-gcc))/..
RINCLUDES = $(TOOLCHAIN)/m68k-apple-macos/RIncludes

CFLAGS = -O2 -I$(CURDIR)/../../../src/common/include

TARGET   = BrowserSpike
CODE_BIN = $(TARGET).code.bin
BIN      = $(TARGET).bin
APPL     = $(TARGET).APPL
DSK      = $(TARGET).dsk

all: $(BIN)

$(CODE_BIN): browser_spike.c browser_shm.c browser_shm.h
	$(CC) $(CFLAGS) -o $(CODE_BIN) browser_spike.c browser_shm.c

$(BIN): $(CODE_BIN) browser_spike.r
	$(REZ) $(RINCLUDES)/Retro68APPL.r browser_spike.r \
		-I$(RINCLUDES) \
		--copy $(CODE_BIN) \
		-o $(BIN) \
		-t APPL -c MxBs \
		--cc $(DSK) --cc $(APPL)

clean:
	rm -f $(TARGET) $(TARGET).gdb $(CODE_BIN) $(BIN) $(APPL) $(DSK) \
	      $(TARGET).elf $(TARGET).elf.gdb

.PHONY: all clean
