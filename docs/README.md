# Documentation

The [project homepage](../README.md) introduces the project and quick start, and [Project Status](status.md) lists what each feature can currently do.

## User Guides

| Page | Contents |
| --- | --- |
| [Getting Started](user/getting-started.md) | Full path for first-time installation |
| [Installation Guide](user/install-from-artifact.md) | Partitioning, installing images, and choosing an OS in the Boot Helper |
| [Install from Computer](user/install-from-computer.md) | Updating existing partitions with the desktop installer |
| [Temporary Boot](user/temporary-boot.md) | Trying UEFI via `fastboot boot` without modifying the tablet |
| [Recovery & Troubleshooting](user/recovery.md) | Returning to Android and handling boot failures |
| [UEFI Fastboot](user/fastboot.md) | Reading logs, taking screenshots, and reading partitions read-only |
| [Known Issues](user/known-issues.md) | Current limitations and workarounds |
| [Official Keyboard](user/keyboard.md) | Key combinations when function (F) keys are absent |
| [HMCL Graphics Acceleration](user/hmcl.md) | Running the Minecraft launcher on Linux |

## Building & Contributing

| Page | Contents |
| --- | --- |
| [Architecture](devel/architecture.md) | Boot chain from power-on to desktop, partition layout, and OS handoff |
| [UEFI Core](devel/uefi-core.md) | UEFI firmware internals, safe exit, and storage protection |
| [Documentation Writing Style](devel/writing-style.md) | Writing rules, terminology glossary, and rewrite examples |
| [Upstream Projects](devel/upstreams.md) | Upstream repositories the project depends on and their responsibilities |
| [Building Guide](devel/building.md) | Environment setup, commands, artifact locations, and CI downloads |
| [Repository Map](devel/repository-map.md) | Directory purposes and which files can be edited directly |
| [Testing](devel/testing.md) | Host-side checks and on-device verification |
| [Release Requirements & Remaining Work](devel/release.md) | Native packages, standard boot, hardware enablement, and pre-release checklist |
| [Local Inputs](devel/local-inputs.md) | Files required for a full build that are not tracked in the repository |
| [Patch Registry](devel/patches.md) | Patch directories and selection rules |
| [Contributing Guide](../CONTRIBUTING.md) | Contribution conventions and workflow |

Implementation notes by topic:

| Directory | Contents |
| --- | --- |
| [`devel/boot/`](devel/boot/combined-boot.md) | Combined BOOT, Android modules, boot requests, UEFI console, and display |
| [`devel/hardware/`](devel/hardware/pen-protocol.md) | Hardware implementation for display, audio, cameras, sensors, stylus, power, and more |
| [`devel/linux/`](devel/linux/kernel.md) | Kernel configuration, system services, and SteamOS |

Distribution recipes live in [BSP Packages](../linux/bsp/README.md), [Rootfs Assembly](../linux/rootfs/README.md), and [Desktop Configurations](../linux/desktops/README.md). For third-party origins, see [THIRD_PARTY.md](../THIRD_PARTY.md).
