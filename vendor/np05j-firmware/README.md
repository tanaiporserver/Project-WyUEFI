# เฟิร์มแวร์สต็อกสำหรับ nubia NP05J (REDMAGIC Astra)

โฟลเดอร์นี้เก็บเฟิร์มแวร์ฮาร์ดแวร์สำหรับฝั่ง Linux ของ REDMAGIC Astra (`nubia_np05j`, SM8750P) แยกจาก `vendor/firmware/` ของ Xiaomi Piano เพื่อไม่ให้ชนกับสคริปต์สเตจเฟิร์มแวร์ของ Piano (`tools/stage_piano_firmware.py`) และแยกจาก `vendor/np05j/` ซึ่งเป็นอินพุต UEFI

## แหล่งที่มา
- **รอมสต็อก:** `REDMAGICOS10.5.14_NP05J_EU` (`nubia/PQ84P01-EEA/PQ84P01:15/AQ3A.240812.002/20251205.092946:user/release-keys`)
- **URL:** https://rom.download.nubia.com/Europe/PQ84P01%28NP05J%29/REDMAGICOS10.5.14_NP05J_EU/update.zip
- **พาร์ติชันที่ตรวจ SHA-256 ตรงกับ OTA `payload.bin` manifest:**
  - `vendor.img`: `9a8b82fc38eb3d9438f31d14e4d34f4bf78938d6f666b8af8a6be19f5a8dbbb0`
  - `modem.img`: `ec4dddd1c046b95c1bcfce3e12ed33fa11b4471791f8021ee2d6d5fe21382d92`
  - `bluetooth.img`: `f0233b0f8a27e7cb8d8e1ae3330d901fe9d03394d0ccaf619bf1c1487fda2cf5`

## องค์ประกอบที่ดึงมา (7 ชุด)
- **`np05j-adsp/`** (ปลายทาง `/usr/lib/firmware/qcom/sm8750/`):
  - `adsp.mdt`, `adsp.b00..b49`, `adsp_dtb.mdt`, `adsp_dtb.b00..b02`, `adspr.jsn`, `adsps.jsn`, `adspua.jsn`
- **`np05j-cdsp/`** (ปลายทาง `/usr/lib/firmware/qcom/sm8750/`):
  - `cdsp.mdt`, `cdsp.b00..b15`, `cdsp_dtb.mdt`, `cdsp_dtb.b00..b02`, `cdspr.jsn`
- **`np05j-ath12k/`** (ปลายทาง `/usr/lib/firmware/ath12k/PEACH/hw2.0/`):
  - `amss.bin`, `aux_ucode.bin`, `board.bin`, `m3.bin`, `qdss_trace_config.bin`, `regdb.bin`, `tmel.bin` และ `bdwlan.e01..e40` สำหรับ `ath12k_wifi7_pci` (`17cb:110e`)
- **`np05j-bluetooth/`** (ปลายทาง `/usr/lib/firmware/qca/`):
  - `brhbtfw20.tlv`, `brhperifw20.tlv`, `brhperinv20.bin`, `brhbtnv20.{bin,b2e,b34..b41}` พร้อม alias `hmtbtfw20.tlv` และ `hmtnv20.*` สำหรับ `qcom,wcn7850-bt`
- **`np05j-gpu/`** (ปลายทาง `/usr/lib/firmware/qcom/`):
  - `gen80000_aqe.fw`, `gen80000_gmu.bin`, `gen80000_sqe.fw`, `gen80000_zap.mbn` (เซ็นด้วยคีย์ nubia OEM)
- **`np05j-audio/`** (ปลายทาง `/usr/lib/firmware/`):
  - `tfa98xx.cnt` (โปรไฟล์แอมป์ลำโพงสเตอริโอคู่ NXP TFA9874 `@0x34`, `@0x35`) และ `QRD_acdb_cal.acdb` (ฐานข้อมูลปรับเทียบ AudioReach ACDB ของ `sun_qrd`)
- **`np05j-touch/`** (ปลายทาง `/usr/lib/firmware/nubia/np05j/`):
  - `firmware.img` (อิมเมจแฟลช Synaptics S3930TB0A0 TCM v2, magic `0x4818472b`) และ `Himax_firmware.bin` (พาเนลสำรองตาม `np05j.dtb`)
