# SPDX-License-Identifier: BSD-2-Clause-Patent
# Pre-UEFI BootSelect & RelocShim Makefile for nubia NP05J

CROSS   ?= aarch64-linux-gnu-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf

ROOT    := $(abspath ../../../../../)
OUTDIR  := $(ROOT)/Build/nubia_np05jPkg/BootSelect

CFLAGS  := -Os -ffreestanding -fno-builtin -nostdlib -nostartfiles \
           -fPIE -fvisibility=hidden -mgeneral-regs-only -mstrict-align \
           -Wall -Wextra -Werror -I../../Include

SRCS_C  := BootRequest.c EarlyKeys.c EarlyTrace.c EarlyTouch.c EarlySplash.c BootSelect.c FreestandingMem.c
SRCS_S  := Entry.S EarlyMmio.S
OBJS    := $(patsubst %.c,$(OUTDIR)/%.o,$(SRCS_C)) $(patsubst %.S,$(OUTDIR)/%.o,$(SRCS_S))

.PHONY: all clean

all: $(OUTDIR)/np05j-bootselect.bin $(OUTDIR)/np05j-reloc-shim.bin $(OUTDIR)/selector.json

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: %.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/%.o: %.S | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/np05j-bootselect.elf: $(OBJS) BootSelect.ld
	$(LD) -T BootSelect.ld --no-dynamic-linker -z max-page-size=4096 --no-warn-rwx-segments $(OBJS) -o $@

$(OUTDIR)/np05j-bootselect.bin: $(OUTDIR)/np05j-bootselect.elf
	$(OBJCOPY) -O binary $< $@
	@python3 -c 'import os; d=open("$@","rb").read(); assert d[0x38:0x3c]==b"ARM\x64"; print("OK", "$@", len(d), "bytes magic:", d[0x38:0x3c])'

$(OUTDIR)/np05j-reloc-shim.elf: RelocShim.S | $(OUTDIR)
	$(CC) $(CFLAGS) -c RelocShim.S -o $(OUTDIR)/RelocShim.o
	$(LD) -Ttext=0x0 -e _reloc_start --no-warn-rwx-segments $(OUTDIR)/RelocShim.o -o $@

$(OUTDIR)/np05j-reloc-shim.bin: $(OUTDIR)/np05j-reloc-shim.elf
	$(OBJCOPY) -O binary $< $@
	@ls -la $@

$(OUTDIR)/selector.json: $(OUTDIR)/np05j-bootselect.bin $(OUTDIR)/np05j-reloc-shim.bin
	@python3 -c 'import json, os; j={"device":"nubia_np05j","soc":"SM8750P","display":{"width":2400,"height":1504,"fb":"0xFC800000"},"touch":{"ic":"Synaptics S3930TB0A0","bus":"SPI4_GPIO","miso":48,"mosi":49,"clk":50,"cs":51,"rst":161,"irq":162,"max_x":15039,"max_y":23999},"selector_bytes":os.path.getsize("$(OUTDIR)/np05j-bootselect.bin"),"reloc_shim_bytes":os.path.getsize("$(OUTDIR)/np05j-reloc-shim.bin"),"embed_addr":"0xA7FFF100","cards":[{"id":0,"target":0,"label":"ANDROID"},{"id":1,"target":1,"label":"UEFI BOOT MENU"},{"id":2,"target":3,"label":"BIOS SETUP"}]}; open("$@","w").write(json.dumps(j, indent=2)+"\n"); print("OK $@")'

clean:
	rm -rf $(OUTDIR)
