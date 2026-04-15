# Build the guest-side test suite with Retro68.
#
# Produces two binaries:
#   MacTestSuite.bin       - 68k build (APPL/MxTs)
#   MacTestSuitePPC.bin    - PPC PEF build (APPL/MxTs), links OpenTransport
#
# Toolchain search order (first hit wins):
#   1. $RETRO68_BIN if set
#   2. <project>/toolchain/retro68/bin
#   3. $HOME/Retro68/build/toolchain/bin (legacy)
#
# Usage:
#   make                    # builds both 68k and PPC
#   make m68k               # 68k only
#   make ppc                # PPC only
#   make install EXTFS=...  # copy binaries into a shared folder
#   make clean

PROJECT_ROOT := $(shell cd ../.. && pwd)
ifneq ($(RETRO68_BIN),)
TOOLCHAIN_BIN := $(RETRO68_BIN)
else ifneq ($(wildcard $(PROJECT_ROOT)/toolchain/retro68/bin/m68k-apple-macos-gcc),)
TOOLCHAIN_BIN := $(PROJECT_ROOT)/toolchain/retro68/bin
else
TOOLCHAIN_BIN := $(HOME)/Retro68/build/toolchain/bin
endif

M68K_CC   := $(TOOLCHAIN_BIN)/m68k-apple-macos-gcc
PPC_CC    := $(TOOLCHAIN_BIN)/powerpc-apple-macos-gcc
REZ       := $(TOOLCHAIN_BIN)/Rez
TOOLCHAIN_ROOT := $(TOOLCHAIN_BIN)/..
M68K_RINC := $(TOOLCHAIN_ROOT)/m68k-apple-macos/RIncludes
PPC_RINC  := $(TOOLCHAIN_ROOT)/powerpc-apple-macos/RIncludes

CFLAGS  = -O2 -Isrc
SRCDIR  = src

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/test_report.c \
       $(SRCDIR)/test_extfs.c \
       $(SRCDIR)/test_audio.c \
       $(SRCDIR)/test_serial.c \
       $(SRCDIR)/test_network.c \
       $(SRCDIR)/test_opentransport.c \
       $(SRCDIR)/test_disk.c

TARGET68K    = MacTestSuite
TARGETPPC    = MacTestSuitePPC

EXTFS ?= /tmp/mac-shared

.PHONY: all m68k ppc install clean
all: m68k ppc

# ---- 68k ----
m68k: $(TARGET68K).bin

$(TARGET68K).code.bin: $(SRCS) $(SRCDIR)/test_report.h
	$(M68K_CC) $(CFLAGS) -o $@ $(SRCS)

$(TARGET68K).bin: $(TARGET68K).code.bin
	$(REZ) $(M68K_RINC)/Retro68APPL.r \
		-I$(M68K_RINC) \
		--copy $< \
		-o $@ \
		-t APPL -c MxTs \
		--cc $(TARGET68K).dsk --cc $(TARGET68K).APPL

# ---- PPC ----
# Retro68's PPC path links against CFM shared libraries automatically via
# libOpenTransportLib.a / libOpenTptInternetLib.a stub imports.
ppc: $(TARGETPPC).bin

$(TARGETPPC).xcoff: $(SRCS) $(SRCDIR)/test_report.h
	$(PPC_CC) $(CFLAGS) -o $@ $(SRCS) \
		-lOpenTransportLib -lOpenTransportAppPPC \
		-lOpenTptInternetLib -lOpenTptInetPPC

$(TARGETPPC).pef: $(TARGETPPC).xcoff
	$(TOOLCHAIN_BIN)/MakePEF $< -o $@

$(TARGETPPC).bin: $(TARGETPPC).pef
	$(REZ) $(PPC_RINC)/RetroPPCAPPL.r \
		-I$(PPC_RINC) \
		-DCFRAG_NAME='"$(TARGETPPC)"' \
		-o $@ \
		-t APPL -c MxTs \
		--data $< \
		--cc $(TARGETPPC).dsk --cc $(TARGETPPC).APPL

install: all
	python3 macbin_to_extfs.py $(TARGET68K).bin $(EXTFS) $(TARGET68K)
	python3 macbin_to_extfs.py $(TARGETPPC).bin $(EXTFS) $(TARGETPPC)

clean:
	rm -f $(TARGET68K) $(TARGET68K).gdb $(TARGET68K).code.bin \
	      $(TARGET68K).bin $(TARGET68K).APPL $(TARGET68K).dsk \
	      $(TARGET68K).ad %$(TARGET68K).ad \
	      $(TARGET68K).elf $(TARGET68K).elf.gdb \
	      $(TARGETPPC) $(TARGETPPC).gdb $(TARGETPPC).xcoff \
	      $(TARGETPPC).pef $(TARGETPPC).bin $(TARGETPPC).APPL \
	      $(TARGETPPC).dsk $(TARGETPPC).ad %$(TARGETPPC).ad
