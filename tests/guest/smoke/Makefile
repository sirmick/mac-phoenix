# Build SmokeTest with Retro68
#
# Usage:
#   make                 # build SmokeTest.bin (MacBinary APPL)
#   make clean

CC = m68k-apple-macos-gcc
REZ = Rez
TOOLCHAIN = $(shell dirname $(shell which m68k-apple-macos-gcc))/..
RINCLUDES = $(TOOLCHAIN)/m68k-apple-macos/RIncludes

CFLAGS = -O2
TARGET = SmokeTest
CODE_BIN = $(TARGET).code.bin
BIN = $(TARGET).bin
APPL = $(TARGET).APPL
DSK = $(TARGET).dsk

all: $(BIN)

$(CODE_BIN): main.c
	$(CC) $(CFLAGS) -o $(CODE_BIN) main.c

$(BIN): $(CODE_BIN)
	$(REZ) $(RINCLUDES)/Retro68APPL.r \
		-I$(RINCLUDES) \
		--copy $(CODE_BIN) \
		-o $(BIN) \
		-t APPL -c 'SmkT' \
		--cc $(DSK) --cc $(APPL)

clean:
	rm -f $(CODE_BIN) $(BIN) $(APPL) $(DSK) *.gdb

.PHONY: all clean
