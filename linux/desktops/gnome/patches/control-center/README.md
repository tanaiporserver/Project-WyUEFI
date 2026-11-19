# GNOME System Info

`0001-about-gunyah-primary-domain.patch` distinguishes Qualcomm's primary system domain from standard guest virtual machines.
It hides the generic `vm-other` row on the GNOME "About" page only when the Device Tree declares both Gunyah and Qualcomm HLOS VMID 3. Unknown domains and other virtualization types retain their original display.

This is a desktop display adjustment only. The kernel Device Tree, systemd's `Virtualization` property, `systemd-detect-virt`, and the Gunyah driver interfaces remain unchanged. Do not delete firmware nodes to alter detection, as virtualization drivers also rely on these nodes to discover the Resource Manager.

Nyarch uses the full GNOME 51.1 package recipe in the adjacent `arch/control-center` directory, staged into the build by `tools/build_nyarch_rootfs.py`. Sources come from official GNOME release archives, preserving the distribution's original features, translations, and split packages. Packaging for Debian's GNOME 48 is still pending integration.
