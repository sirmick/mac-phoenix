# Build SmokeTest with Retro68
#
# Usage:
#   make                 # build SmokeTest.bin (MacBinary APPL)
#   make clean

CC = m68k-apple-macos-gcc
REZ = Rez
TOOLCHAIN = $(shell dirname $(shell which m68k-apple-macos-gcc))/..
RINCLUDES = $(TOOLCHAIN)/m68k-apple-macos/RIncludes

CFLAGS = -O2 -I../src
SRCDIR = ../src
TARGET = SmokeTest
CODE_BIN = $(TARGET).code.bin
BIN = $(TARGET).bin
APPL = $(TARGET).APPL
DSK = $(TARGET).dsk

SRCS = main.c \
       $(SRCDIR)/test_report.c \
       $(SRCDIR)/test_disk.c \
       $(SRCDIR)/test_extfs.c \
       $(SRCDIR)/test_audio.c \
       $(SRCDIR)/test_serial.c \
       $(SRCDIR)/test_network.c \
       $(SRCDIR)/test_opentransport.c

all: $(BIN)

$(CODE_BIN): $(SRCS) $(SRCDIR)/test_report.h
	$(CC) $(CFLAGS) -o $(CODE_BIN) $(SRCS)

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
