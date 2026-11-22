# SPDX-License-Identifier: BSD-2-Clause-Patent
# Build host & static AArch64 np05j-boot-repack utilities

ROOT      := $(abspath ../../../../..)
OUTDIR    := $(ROOT)/Build/nubia_np05jPkg/AndroidHelper

HOST_CC   ?= gcc
ARM64_CC  ?= aarch64-linux-gnu-gcc
CFLAGS    := -O2 -Wall -Wextra -Werror -I. -I../../Include -I../../Tools/BootSelect

SRCS      := np05j-boot-repack.c sha256.c ../../Tools/BootSelect/BootRequest.c

.PHONY: all clean

all: $(OUTDIR)/np05j-boot-repack-host $(OUTDIR)/np05j-boot-repack-arm64

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/np05j-boot-repack-host: $(SRCS) sha256.h | $(OUTDIR)
	$(HOST_CC) $(CFLAGS) $(SRCS) -o $@

$(OUTDIR)/np05j-boot-repack-arm64: $(SRCS) sha256.h | $(OUTDIR)
	$(ARM64_CC) $(CFLAGS) -static $(SRCS) -o $@
	$(ARM64_CC:gcc=strip) --strip-unneeded $@

clean:
	rm -rf $(OUTDIR)
