# Boot Architecture & System Handoff

This page explains the complete boot chain from pressing the power button to entering the Linux desktop, covering Combined BOOT, EDK2 UEFI firmware, storage partitions, and system handoff.

## Boot Chain

```
Power button pressed
  │
  ▼
Stock bootloader (XBL / ABL)
  │  Loads kernel and SunUEFI as a single block per BOOT partition kernel_size
  ▼
Combined BOOT selector (3-second countdown)
  ├── Select Android ──► Jump to stock kernel entry ──► HyperOS
  └── Select UEFI ─────► Enter EDK2 UEFI core (PianoUEFI-product.img)
                           │
                           ├─► Resident USB Fastboot service (SunUEFI-piano)
                           ├─► Display SimpleInit graphical menu
                           │
                           ▼ 5 seconds idle, or manual Linux boot
                         systemd-boot auto-discovers versioned UKIs
                           │ systemd-stub → Linux EFI stub
                           │
                           ▼ ExitBootServices safe exit
                         Release USB controller, UFS, clocks, and SMMU
                           │
                           ▼
                         Linux kernel (Image) + Device Tree (board.dtb) + initramfs
                           │
                           ▼
                         Mount root filesystem (sunuefi_root, ext4)
                           │
                           ▼
                         Enter Linux desktop (GNOME / KDE / SteamOS)
```

## Core Mechanisms

### Combined BOOT

Stock ABL does not load `pvmfw` when Recovery carries its own kernel, so replacing the kernel directly prevents Recovery from booting. SunUEFI leaves the stock Android kernel in place and only redirects the entry jump to a small selector; the selector and UEFI core are appended after the kernel and loaded into memory together by ABL. Selecting Android jumps back to the stock kernel, while selecting UEFI enters EDK2. See [Combined BOOT Details](boot/combined-boot.md) for how it works.

### Boot Route Persistence

The OS selected in the Android Boot Helper is saved as the default boot route; temporarily changing the selection with volume keys at startup does not alter the default. The Android slot (A/B) is read directly from Qualcomm's GPT active slot attribute. From Linux, run `piano-next-boot android --reboot` to switch back to Android.

### Two Fastboot Services

SunUEFI provides a separate Fastboot service distinct from the stock bootloader:

| Service | Device Serial | Stage | Capabilities |
| --- | --- | --- | --- |
| Stock bootloader Fastboot | Tablet's real serial number | ABL stage | Allows flashing and erasing partitions |
| UEFI Fastboot | `SunUEFI-piano` | During UEFI execution | Read logs, take screenshots, read-only partition access; refuses flashing and erasing |

See [UEFI Fastboot](../user/fastboot.md) for detailed commands.

## Storage Layout

Internal storage is partitioned with two dedicated partitions without touching stock Android partitions:

* `sunuefi_esp` (512 MiB, FAT32, 4 KiB logical sectors): stores Linux boot files;
* `sunuefi_root` (customizable size, ext4): stores the Linux root filesystem.

```
sunuefi_esp/
├── EFI/
│   ├── BOOT/BOOTAA64.EFI
│   ├── systemd/systemd-bootaa64.efi
│   └── Linux/linux-<release>.efi   # Kernel, initramfs, DTB, boot arguments
└── loader/loader.conf
```

All four production distributions share the initial ESP layout above. `systemd-boot` reads the OS metadata and kernel version from UKIs under `EFI/Linux/`; distribution updates generate their own UKIs through standard hooks without requiring fixed filenames or a specific production rootfs.

The legacy Linux `boot.img` and split-file BLS entries are no longer production entry points. When updating firmware, update the boot image (ESP) alongside it without overwriting an existing root partition. UEFI automatically adapts to the memory layout of each boot, so UKIs are not built separately for 12 GB vs. 16 GB models. For memory and exit-identity handling, see [UEFI Core](uefi-core.md#esp-boot-files-and-os-handoff).
