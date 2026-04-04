# Standalone Makefile for building guest test suite with Retro68
#
# Usage:
#   make                    # build MacTestSuite
#   make install EXTFS=...  # copy to ExtFS shared folder
#   make clean

CC = m68k-apple-macos-gcc
CFLAGS = -O2 -Isrc
SRCDIR = src

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/test_extfs.c \
       $(SRCDIR)/test_audio.c \
       $(SRCDIR)/test_serial.c \
       $(SRCDIR)/test_network.c \
       $(SRCDIR)/test_disk.c

TARGET = MacTestSuite

EXTFS ?= /tmp/mac-shared

$(TARGET): $(SRCS) $(SRCDIR)/test_report.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

install: $(TARGET)
	mkdir -p $(EXTFS)
	cp $(TARGET) $(EXTFS)/

clean:
	rm -f $(TARGET)

.PHONY: install clean
