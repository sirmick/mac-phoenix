# Standalone Makefile for building guest test suite with Retro68
#
# Requires Retro68 toolchain on PATH:
#   export PATH="$HOME/Retro68/build/toolchain/bin:$PATH"
#
# Usage:
#   make                    # build MacTestSuite.bin (MacBinary + .APPL + .dsk)
#   make install EXTFS=...  # copy to ExtFS shared folder
#   make clean

CC = m68k-apple-macos-gcc
REZ = Rez
TOOLCHAIN = $(shell dirname $(shell which m68k-apple-macos-gcc))/..
RINCLUDES = $(TOOLCHAIN)/m68k-apple-macos/RIncludes

CFLAGS = -O2 -Isrc
SRCDIR = src

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/test_report.c \
       $(SRCDIR)/test_extfs.c \
       $(SRCDIR)/test_audio.c \
       $(SRCDIR)/test_serial.c \
       $(SRCDIR)/test_network.c \
       $(SRCDIR)/test_opentransport.c \
       $(SRCDIR)/test_disk.c

TARGET = MacTestSuite
CODE_BIN = $(TARGET).code.bin
BIN = $(TARGET).bin
APPL = $(TARGET).APPL
DSK = $(TARGET).dsk

EXTFS ?= /tmp/mac-shared

all: $(BIN)

# Link step produces the .code.bin (Elf2Mac output from the linker)
$(CODE_BIN): $(SRCS) $(SRCDIR)/test_report.h
	$(CC) $(CFLAGS) -o $(CODE_BIN) $(SRCS)

# Rez wraps .code.bin into a proper Mac application (.bin, .APPL, .dsk)
$(BIN): $(CODE_BIN)
	$(REZ) $(RINCLUDES)/Retro68APPL.r \
		-I$(RINCLUDES) \
		--copy $(CODE_BIN) \
		-o $(BIN) \
		-t APPL -c MxTs \
		--cc $(DSK) --cc $(APPL)

install: $(BIN)
	python3 macbin_to_extfs.py $(BIN) $(EXTFS) $(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).gdb $(CODE_BIN) $(BIN) $(APPL) $(DSK) \
	      $(TARGET).ad %$(TARGET).ad \
	      $(TARGET).elf $(TARGET).elf.gdb

.PHONY: all install clean
