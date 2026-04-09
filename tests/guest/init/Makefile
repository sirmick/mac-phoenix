# Build BridgeINIT with Retro68
#
# Requires Retro68 toolchain on PATH:
#   export PATH="$HOME/Retro68/build/toolchain/bin:$PATH"
#
# Usage:
#   make                 # build BridgeINIT.bin (MacBinary INIT)
#   make install         # install onto disk image via hfsutils
#   make clean

CC = m68k-apple-macos-gcc
REZ = Rez
TOOLCHAIN = $(shell dirname $(shell which m68k-apple-macos-gcc))/..
RINCLUDES = $(TOOLCHAIN)/m68k-apple-macos/RIncludes

CFLAGS = -O2 -fomit-frame-pointer
LDFLAGS = -Wl,--mac-flat

SRCS = bridge_init.c bridge_launch.c
TARGET = BridgeINIT
FLT = $(TARGET).flt
BIN = $(TARGET).bin

DISK ?= $(HOME)/storage/images/macos-7.5.5.img

all: $(BIN)

$(FLT): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(FLT) $(SRCS)

$(BIN): $(FLT) BridgeINIT.r
	$(REZ) $(RINCLUDES)/Retro68.r BridgeINIT.r \
		-I$(RINCLUDES) \
		-o $(BIN) \
		-t INIT -c 'MxPh'

install: $(BIN)
	@echo "Installing BridgeINIT onto $(DISK)..."
	hmount $(DISK)
	hcopy -m $(BIN) ":System Folder:Extensions:BridgeINIT"
	humount
	@echo "Done. BridgeINIT will load on next boot."

clean:
	rm -f $(FLT) $(BIN) $(TARGET).dsk $(TARGET).INIT $(TARGET).gdb

.PHONY: all install clean
