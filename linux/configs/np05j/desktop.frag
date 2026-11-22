# คอนฟิกเพิ่มเติมสำหรับเดสก์ท็อป Ubuntu 24.04 GNOME (Wayland/simpledrm + ระบบเครือข่าย + Bluetooth HID)
# ใช้ simpledrm ครอบ UEFI GOP (0xfc800000 2400x1504) เพื่อให้ Mutter/GNOME มี /dev/dri/card0 โดยไม่แตะ MDSS/DSI ของจอ OLED
CONFIG_SYSFB=y
CONFIG_SYSFB_SIMPLEFB=y
CONFIG_FB_EFI=n
CONFIG_FB_SIMPLE=n
CONFIG_DRM_SIMPLEDRM=y
CONFIG_DRM_FBDEV_EMULATION=y

# แสดง kmsg ลงจอ OLED ทันทีเมื่อเกิด panic (ชดเชยที่ drm_fb_helper ข้ามการ flush ตอน oops_in_progress)
# และบันทึก Linux console ลง ramoops@e4a00000 เพื่อให้ SunUEFI fastboot oem ramlog ดึง log ย้อนหลังได้
CONFIG_DRM_PANIC=y
CONFIG_DRM_PANIC_SCREEN="kmsg"
CONFIG_PSTORE_RAM=y
CONFIG_PSTORE_CONSOLE=y

# ใช้ SHM Bridge สำหรับ Qualcomm TrustZone บน SM8750P (ตรงกับ Piano และ fallback อัตโนมัติหากไม่รองรับ)
CONFIG_QCOM_TZMEM_MODE_GENERIC=n
CONFIG_QCOM_TZMEM_MODE_SHMBRIDGE=y

# ระบบเครือข่ายมาตรฐานสำหรับ Ubuntu systemd / NetworkManager / apt / snap / flatpak
CONFIG_IPV6=y
CONFIG_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y
CONFIG_NF_TABLES=y
CONFIG_NF_TABLES_INET=y
CONFIG_NF_CONNTRACK=y
CONFIG_IP_NF_IPTABLES=y
CONFIG_IP6_NF_IPTABLES=y

# อินพุต / Bluetooth HID / คอนโซล USB-C / ระบบไฟล์มาตรฐานเดสก์ท็อป
CONFIG_INPUT_UINPUT=y
CONFIG_UHID=y
CONFIG_HID_MULTITOUCH=y
CONFIG_BT_RFCOMM=y
CONFIG_BT_RFCOMM_TTY=y
CONFIG_BT_BNEP=y
CONFIG_BT_HIDP=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_CONFIGFS_ACM=y
CONFIG_USB_CONFIGFS_F_FS=y
CONFIG_FUSE_FS=y
CONFIG_OVERLAY_FS=y
CONFIG_SQUASHFS=y
CONFIG_SQUASHFS_ZSTD=y
CONFIG_SQUASHFS_XZ=y
