# SteamOS

The SteamOS image provides both Steam Gaming Mode and KDE Desktop Mode on the tablet, booting into Gaming Mode by default. Display and input in Gaming Mode, as well as switching between Gaming Mode and KDE Desktop, work properly, with solid Proton and FEX support. Game-specific launch configurations (such as lock handling) are specific to individual games and unrelated to the OS itself.

## On-Device Screenshots

| Steam Gaming Mode | Genshin Impact In-Game |
| --- | --- |
| ![Chinese Steam Gaming Mode and game library](../assets/gallery/steamos-gaming-mode.webp) | ![Genshin Impact character inside the game world](../assets/gallery/steamos-genshin-world.webp) |
| **Official Genshin Impact Launcher** | **Magical Girl Witch Trials** |
| ![Official Genshin Impact Launcher](../assets/gallery/steamos-genshin-launcher.webp) | ![Magical Girl Witch Trials story scene](../assets/gallery/steamos-witch-trial.webp) |

## Installation

Download `SunUEFI-Module`, `SunUEFI-ESP`, and `SunUEFI-Root-steamos` from [Build products](https://github.com/slwyts/Project-SunUEFI/actions/workflows/build-products.yml). The boot image (ESP) is shared with other distributions, and the installation steps are the same as for Debian; see the [Installation Guide](install-from-artifact.md).

The system image (root) is 16 GiB, so the dedicated root partition must be at least that large. The login username is `piano` and the password is `piano123456`. Launching Steam for the first time requires an internet connection to check for client updates and sign in.

## Daily Use

* Input method: Ships with IBus Pinyin by default; press `Ctrl+Space` to switch between Chinese and English.
* Touchpad: Natural scrolling is enabled by default in Gaming Mode, and Steam has no settings page to change it. In KDE Desktop Mode, you can adjust it on the Touchpad page in System Settings; the two modes save input preferences independently.
* Display: Gaming Mode runs at 3200×2136 with menus locked at 120 Hz. KDE Desktop defaults to 2x scaling; HDR and wide color gamut can be enabled in Display Settings.
* Power: In Gaming Mode, short-press the power button to suspend. KDE Desktop turns off the screen after 2 minutes by default, and suspends after 5 minutes on battery or 10 minutes on AC power.
* Auto-brightness: Auto-brightness in Steam is not wired up; adjust backlight brightness manually.
* Reinstalling the OS: SteamOS here does not include Valve's factory reset feature. To reinstall, flash the system image (root) again from the Boot Helper.

## System Updates

Gaming Mode system updates, KDE Discover, and `sudo pacman -Syu` all use the same package repositories instead of Valve's A/B image updates. `sunuefi-steamos` takes priority and provides override packages built against Valve's dependencies, while `sunuefi` provides the shared kernel and device packages; both repositories require signature verification. The Steam client still updates via its own updater.

Run `pacman-conf --repo-list` to check for these two repositories. Earlier images that do not list them can only pull updates from the original repositories and will not see the project's device packages. For updating and selecting an older kernel, see the [Installation Guide](install-from-artifact.md#updating-the-system).

## Running Genshin Impact

Genshin Impact (CN server) 7.1.0 can enter the game world through the official launcher. Due to a locking mechanism in the game engine itself, if it hangs on the user agreement screen, add the following game-specific launch option (unrelated to the OS itself):

```text
STEAM_FEX_MULTIBLOCK=0 %command%
```

The image comes preinstalled with ARM64 builds of GE-Proton11-11 and Steam Linux Runtime 4.0, with the default compatibility tool already configured. Proton bundles FEX, DXVK, and vkd3d-proton, so no runtime downloads are needed on first launch. Steam login, game content, and subsequent updates still require network access. For technical details, see [Proton/FEX Cooperative Suspend](../devel/linux/steamos-proton-fex-suspend.md).

For build instructions, patches, and debugging notes, see [SteamOS ARM64 (Developer Docs)](../devel/linux/steamos.md).
