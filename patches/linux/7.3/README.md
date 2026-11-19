# Linux 7.3 Release Kernel

Follow the [kernel patch maintenance requirements](../AGENTS.md) when organizing and backporting patches.

Starting from the stable `v7.3.3` commit, apply the topic patches and interface fixes in `series.json` in order:

| Patch | Description |
| --- | --- |
| `0001-platform` | Board description, buses, storage, clocks, power, firmware communication, and existing boot adaptation |
| `0002-display` | DPU, dual DSI, panel, backlight, color curves, and seamless VFP updates |
| `0003-input-wireless` | Touch, stylus, keyboard, Bluetooth, and Wi-Fi |
| `0004-media` | Camera sensors, CAMSS, Iris, and flash |
| `0005-audio` | Quad speakers, TDM, and microphones |
| `0006-gunyah` | Android Gunyah driver and mainline interface adaptation |
| `0007-bore` | BORE 6.8.0 scheduler changes from CachyOS |
| `0008-drm-msm-gem-api` | Adapt to the 7.3 GEM allocation API while keeping dedicated buffers for color-correction DMA |
| `0009-gunyah-rmap-api` | Adapt to the reverse-mapping (`rmap`) API while still checking user mappings before lending pages |
| `0010-gunyah-dt-availability` | Respect the disabled status of resource manager nodes, avoid using stale handles from other boots, and fix double-put of Device Tree references |
| `0023-dpu-shared-video-ctl` | Commit only once per frame when dual DSI shares a single CTL, avoiding duplicate commits on the slave channel and growing pending counts; include controller, interface, and commit masks in timeout logs |
| `0024-hid-nanosic-feature-and-release` | Use touchpad capabilities declared in the descriptor instead of querying unimplemented MCU HID Feature reports; send full per-model reports when releasing contacts on lid close |
| `0025-ucsi-clear-released-connectors` | Clear connector pointers after unregistering to avoid double-free on ADSP restart |
| `0026-mca-recover-pd-input` | Fix PD power supply being misreported as Unknown after reconnection |
| `0027-power-piano-mipps-authentication` | Xiaomi fast-charging authentication and fast reconnection |
| `0028-power-piano-otg-configuration` | Sync stock OTG configuration to avoid ADSP crashes when unplugging a dock |
| `0029-hid-xiaomi-p81c-body-gestures` | Report stylus barrel swipe-up and swipe-down as Page Up / Page Down; stop converting double-tap and squeeze into keypad Enter and F19, passing raw reports to the stylus input program |

These files are organized by subsystem from the migration output rather than repeatedly merging backported fixes from older stable branches into a new mainline. The manifest preserves the source, SHA-256, commit parent-child relationships, and resulting tree for every step; the preparation tool applies patches in an isolated build copy without modifying the upstream checkout.

The Piano hardware baseline comes from blu-sharky/linux-piano, and existing local fixes come from the project's 7.2.9 patches. The migration preserves new device support added upstream in 7.3, with interface adaptations for connector state creation, DSI atomic callbacks, FastRPC response waiting, QMP power management, and PCIe MSI initialization order. Source file copyright notices are preserved; see [THIRD_PARTY.md](../../../THIRD_PARTY.md) for detailed third-party notices.

The default configuration enables BORE, MGLRU, 300 Hz tick, and full preemption, while continuing to use `NO_HZ_IDLE`, `schedutil`, and Zstd ZRAM. 300 Hz is the kernel timer tick rate, not the display or touch refresh rate. BORE's own runtime parameter defaults are kept unchanged; `kernel.sched_bore=0` can be used for comparison on the same kernel, though it is not claimed to be equivalent to a completely unpatched EEVDF kernel. Performance, power consumption, and hardware support are determined by runtime results on the corresponding release.
