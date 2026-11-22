# SPDX-License-Identifier: BSD-2-Clause-Patent
# Host unit-test suite for nubia NP05J SunUEFI adaptation

ROOT    := $(abspath ../../../..)
OUTDIR  := $(ROOT)/Build/nubia_np05jPkg/Tests
CC      ?= gcc
CFLAGS  := -O2 -Wall -Wextra -Werror -I../Include -I../Tools/BootSelect -I../Drivers/Np05jUsbSerialDxe

.PHONY: all test clean

all: $(OUTDIR)/test_touch_tcm2 $(OUTDIR)/test_bootselect $(OUTDIR)/test_fastboot

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/test_touch_tcm2: test_touch_tcm2.c ../Include/Np05jSynapticsTcm2.h | $(OUTDIR)
	$(CC) $(CFLAGS) test_touch_tcm2.c -o $@

$(OUTDIR)/test_bootselect: test_bootselect.c ../Tools/BootSelect/BootRequest.c ../Tools/BootSelect/EarlySplash.c | $(OUTDIR)
	$(CC) $(CFLAGS) test_bootselect.c ../Tools/BootSelect/BootRequest.c ../Tools/BootSelect/EarlySplash.c -o $@

$(OUTDIR)/test_fastboot: test_fastboot.c ../Drivers/Np05jUsbSerialDxe/Np05jFastbootCore.c ../Drivers/Np05jUsbSerialDxe/Np05jFastbootCore.h | $(OUTDIR)
	$(CC) $(CFLAGS) -DNP05J_HOST_TEST test_fastboot.c ../Drivers/Np05jUsbSerialDxe/Np05jFastbootCore.c -o $@

test: all
	$(OUTDIR)/test_touch_tcm2
	$(OUTDIR)/test_bootselect $(ROOT)/Build/nubia_np05jPkg/boot-selection-preview.bmp
	$(OUTDIR)/test_fastboot
	python3 test_boot_repack.py

clean:
	rm -rf $(OUTDIR)
