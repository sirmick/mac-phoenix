# Build the BridgeAgent — a Retro68 application installed in
# System Folder:Startup Items on the guest disk image.
#
# Requires Retro68 toolchain on PATH:
#   export PATH="$HOME/Retro68/build/toolchain/bin:$PATH"

CC = m68k-apple-macos-gcc
REZ = Rez
TOOLCHAIN = $(shell dirname $(shell which m68k-apple-macos-gcc))/..
RINCLUDES = $(TOOLCHAIN)/m68k-apple-macos/RIncludes

CFLAGS = -O2

TARGET   = BridgeAgent
CODE_BIN = $(TARGET).code.bin
BIN      = $(TARGET).bin
APPL     = $(TARGET).APPL
DSK      = $(TARGET).dsk

all: $(BIN)

$(CODE_BIN): BridgeAgent.c
	$(CC) $(CFLAGS) -o $(CODE_BIN) BridgeAgent.c

$(BIN): $(CODE_BIN) BridgeAgent.r
	$(REZ) $(RINCLUDES)/Retro68APPL.r BridgeAgent.r \
		-I$(RINCLUDES) \
		--copy $(CODE_BIN) \
		-o $(BIN) \
		-t APPL -c MxBr \
		--cc $(DSK) --cc $(APPL)

clean:
	rm -f $(TARGET) $(TARGET).gdb $(CODE_BIN) $(BIN) $(APPL) $(DSK) \
	      $(TARGET).elf $(TARGET).elf.gdb

.PHONY: all clean
