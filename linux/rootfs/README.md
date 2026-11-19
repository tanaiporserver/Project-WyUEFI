# Distribution Base Systems

The entry point is `python3 tools/assemble_rootfs.py --distro debian --desktop gnome --plan`. Only `--execute` invokes the debootstrap/APT or Arch tarball/pacman backend, which requires root privileges, a private mount namespace, `chroot`, and `qemu`/binfmt when cross-building. Output must go to a fresh `build/distros/` project directory.

| Base System | GNOME | KDE | DDE |
| --- | --- | --- | --- |
| Debian trixie | Build draft | Build draft | Unavailable: no reviewed recipe |
| Ubuntu noble / 24.04 LTS | Build draft | Build draft | Unavailable: DDE PPA not added |
| Arch Linux ARM aarch64 | Build draft | Build draft (Maliit subject to package availability) | Unavailable: no reviewed recipe |
| Deepin ARM64 | Not open | Not open | Requires explicitly signed base and target repository package verification |

"Build draft" does not confirm bootability on `piano`. Required packages are checked before APT/pacman execution; missing packages cause a failure rather than using ignore-missing or disabling signature verification. Default repositories remain mutable; package manifests and repository metadata are recorded, with no claim of bit-for-bit reproducibility.

The official Arch `latest` tarball is a mutable source, so execution requires passing `--base-sha256` explicitly; an unverified hash is never filled in automatically. Default `root`/`alarm` passwords are locked, SSH host keys and `machine-id` are cleared, and `pacman-key` is initialized and populated with the Arch Linux ARM signing keys for the target repositories.

Deepin has official ARM64 releases, but this repository does not ship a verified, directly importable base tarball. You must supply `--suite --base-url --base-sha256 --base-signature-url --base-keyring --base-keyring-sha256 --keyring --sources-list`. Digests and GPG signatures are verified before import, and distribution identity is verified after import; sources files accept only official Deepin HTTPS repositories for the same suite, without mixing Ubuntu or Debian sources. When these inputs are omitted, the tool only reports `INPUTS_REQUIRED` and neither downloads nor assumes their existence.

Base systems are decoupled from [Desktops](../desktops/README.md), and the BSP is supplied as separate packages. [storage.json](storage.json) defines the root partition name `sunuefi_root` while keeping the filesystem label `PIANOROOT`; the assembler does not modify GPT tables, UUIDs, or device data. `--bsp DIR --layer MESA --layer RUNTIME --layer MODULES --layer FIRMWARE` requires all manifests to match `target={distro,suite,arch:aarch64}`; Mesa and runtime layers must also declare their glibc version or static libc, and package managers keep normal dependency checks enabled. A Debian `.deb` cannot serve as a generic compiled layer for other distributions. BSP `payload.tar`/`PKGBUILD` files are recipes only; installation cannot be claimed complete until target packages are actually built.

When no BSP is selected, a base system + desktop image can still be generated with status `HOST_ROOTFS_DESKTOP_BUILT_BSP_PENDING`. When a full BSP is requested, missing compiled layers are rejected. Distribution generic kernel packages are removed during installation, and the dispatcher does not register them as ESP boot entries; the final kernel/DTB/label bootstrap is provided by the shared release pipeline.

Desktop sessions default to auto-login as the password-locked `piano` user; set your own password on first setup through the installer or a trusted root console. There is no shared default password. See the Desktops page for each desktop's 200% default scaling and OSK boundaries; screen rotation still depends on sensor verification.

Official references: [Debian trixie ARM64](https://www.debian.org/releases/trixie/arm64/), [Ubuntu noble ARM64](https://cdimage.ubuntu.com/ubuntu/releases/noble/release/), [Ubuntu debootstrap/keyring](https://packages.ubuntu.com/noble/debootstrap), [Arch Linux ARM Generic AArch64](https://archlinuxarm.org/platforms/armv8/generic), [Deepin Download Page](https://www.deepin.org/en/download/).
