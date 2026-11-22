# เฟิร์มแวร์สต็อกสำหรับ nubia NP05J (REDMAGIC Astra)

โฟลเดอร์นี้เก็บเฟิร์มแวร์ฮาร์ดแวร์สำหรับฝั่ง Linux ของ REDMAGIC Astra (`nubia_np05j`, SM8750P) แยกจาก `vendor/firmware/` ของ Xiaomi Piano เพื่อไม่ให้ชนกับสคริปต์สเตจเฟิร์มแวร์ของ Piano (`tools/stage_piano_firmware.py`) และแยกจาก `vendor/np05j/` ซึ่งเป็นอินพุต UEFI

## แหล่งที่มา
- **รอมสต็อก:** `REDMAGICOS10.5.14_NP05J_EU` (`nubia/PQ84P01-EEA/PQ84P01:15/AQ3A.240812.002/20251205.092946:user/release-keys`)
- **URL:** https://rom.download.nubia.com/Europe/PQ84P01%28NP05J%29/REDMAGICOS10.5.14_NP05J_EU/update.zip
- **พาร์ติชัน `vendor.img` SHA-256:** `9a8b82fc38eb3d9438f31d14e4d34f4bf78938d6f666b8af8a6be19f5a8dbbb0` (ตรวจตรงกับแฮชใน OTA `payload.bin` manifest)

## องค์ประกอบที่ดึงมา
- **`np05j-gpu/`** (ปลายทาง `/usr/lib/firmware/qcom/`):
  - `gen80000_aqe.fw`, `gen80000_gmu.bin`, `gen80000_sqe.fw`, `gen80000_zap.mbn` (เซ็นด้วยคีย์ nubia OEM)
- **`np05j-audio/`** (ปลายทาง `/usr/lib/firmware/`):
  - `tfa98xx.cnt` (โปรไฟล์แอมป์ลำโพงสเตอริโอคู่ NXP TFA9874 `@0x34`, `@0x35`)
  - `QRD_acdb_cal.acdb` (ฐานข้อมูลปรับเทียบ AudioReach ACDB ของ `sun_qrd` พร้อมพารามิเตอร์ `tfa` และ `DAP`)
- **`np05j-touch/`** (ปลายทาง `/usr/lib/firmware/nubia/np05j/`):
  - `firmware.img` (อิมเมจแฟลช Synaptics S3930TB0A0 TCM v2, magic `0x4818472b`, มีส่วน `APP_CODE` และ `APP_CONFIG`)
  - `Himax_firmware.bin` (เฟิร์มแวร์พาเนลทัช Himax รุ่นสำรองที่ระบุใน `np05j.dtb`)
