# Function Keys on the Official Keyboard

The official magnetic keyboard has no dedicated F1–F12 keys. Hold Right Ctrl and press a key on the top number row instead:

| Shortcut | Function Key |
| --- | --- |
| Right Ctrl + `1`–`9` | F1–F9 |
| Right Ctrl + `0` | F10 |
| Right Ctrl + `-` | F11 |
| Right Ctrl + `=` | F12 |

`-` and `=` are the two keys to the right of `0` on the top row. Left Ctrl can be held alongside these combinations for shortcuts such as Ctrl + F1, and Shift can be combined as well. Right Ctrl paired with any other key behaves as usual.

This mapping is handled in the Linux kernel keyboard driver and emits standard function key events, requiring no extra configuration in GNOME, KDE, or games. For example, pressing Right Ctrl + `3` in Minecraft opens the F3 debug screen. It applies only to the official magnetic keyboard; external USB and Bluetooth keyboards are unaffected.

## Keyboard Backlight

The default backlight brightness is 80% and can be adjusted in GNOME's Keyboard settings. Brightness is controlled through standard LED and UPower interfaces; the last saved level is restored across reboots, and turning it off manually keeps it off.

Detaching the keyboard removes the corresponding backlight device, and reattaching it restores the previously configured brightness, including a manually disabled state.

## Keyboard in UEFI

The official keyboard cannot yet be used in the UEFI menu; use the volume and power buttons to navigate. See [Known Issues](known-issues.md#uefi-menu-and-usb).
