# คู่มือการพอร์ต การแพตช์ และการบิลด์สำหรับ nubia NP05J (REDMAGIC Astra)

> [!IMPORTANT]
> **สถานะการตรวจสอบบนเครื่องจริงและโฮสต์**:
> - **Phase 1 Hybrid (`Mu-nubia_np05j-*.img`)**: บูตทดสอบบนเครื่องจริง `nubia_np05j` **ปกติ**
> - **Phase 2 Unified `ProductCore` (`Np05jUEFI-product.img`)**: บูตผ่าน `DISPLAY OK` และ `PAYLOAD OK 215 MS` แล้ว; รุ่นล่าสุดปิดสวิตช์ DCD startup dependencies (`0x3A4E0: 1 -> 0`) ใน `ClockDxe.efi` เพิ่มเติมจาก CESTA (`0xC270`) เพื่อแก้ปัญหาหน้าจอหยุดนิ่งหลัง `ClockDxe` เตรียมทดสอบ `fastboot boot` รอบถัดไปบนเครื่องจริง
> - **Linux 7.3 Kernel/DTB และ Userspace utility**: ผ่านการคอมไพล์และตรวจรับโครงสร้างไฟล์บนโฮสต์ใน `sunuefi-builder` แล้ว (ยังไม่ได้ทดสอบบูต Linux บนเครื่องจริง)

เอกสารนี้สรุปสถาปัตยกรรม แผนผังไฟล์ และคำสั่งบิลด์/ตรวจสอบสำหรับ **nubia NP05J (REDMAGIC Astra, Qualcomm SM8750P)** ที่ผสานเข้ากับ `Project-WyUEFI` (`./build.sh`) โดยรักษาความเข้ากันได้ของ `piano` และคงไฟล์ใน `upstream/` แบบ read-only

---

## 1. ภาพรวมฮาร์ดแวร์และความแตกต่างจาก `piano`

| หัวข้อ | `nubia NP05J` (REDMAGIC Astra) | `Xiaomi Pad 8 Pro` (`piano`) |
| :--- | :--- | :--- |
| **SoC / Platform** | Qualcomm SM8750P (`Pakala`) | Qualcomm SM8750P (`Pakala`) |
| **ความละเอียดจอภาพ (GOP)** | `2400×1504` (`0xFC800000`, `0xDC5000` ไบต์) | `3200×2136` (`0xFC800000`, `0x1A13000` ไบต์) |
| **Console Scaling** | `3×` -> `800×501` (`PianoConsoleScale.h`) | `4×` -> `800×534` (`PianoConsoleScale.h`) |
| **UEFI Touch Controller** | Synaptics `S3930TB0A0` (TCM v2) ผ่าน TLMM GPIO bit-bang SPI4 (`48..51`, RST `161`, IRQ `162`, `ldob4` 1.8V / `ldob14` 3.2V) | Novatek `NT36532E` ผ่าน QUP1 SE2 + GPI1 DMA |
| **คีย์บอร์ด Pogo** | ไม่มี (`pogo_keyboard_touchpad: false`) | Nanosic Pogo Keyboard |
| **RAM Console (`ramoops`)** | `0xE4A00000` (`0x200000` ไบต์) | `0xA3500000` (`0x400000` ไบต์) |
| **Android Boot Image Header** | Phase 1 Hybrid (ไม่ใช้ ramdisk): Header v4<br>Phase 2 ProductCore (แนบ `app-payload.bin` ใน ramdisk): Header v3 | Header v3 (ค่าเริ่มต้นของ `piano`) |
| **UEFI FD Target** | Phase 1 Hybrid: `0xA8010000` / `0x200000`<br>Phase 2 ProductCore: `0xA7100000` / `0x300000` | `0xA7100000` / `0x300000` |

---

## 2. สถาปัตยกรรม 2 ระยะ

### ระยะที่ 1: Hybrid Pakala UEFI + BootSelect + Android Helper (`./build.sh uefi-hybrid ALL`)
- โคลน `upstream/Mu-Silicium` และ nested submodules ทั้ง 12 ตัวแบบชั่วคราวลง `build/np05j-workspace/Mu-Silicium`
- Apply แพตช์เฉพาะ Phase 1 จาก [patches/firmware/np05j/](../../patches/firmware/np05j/series.json) (`0001..0003`) และ `MuPatches` ของ Mu-Silicium พร้อม stage แพ็กเกจ `uefi/platforms/np05jPkg/` และแพตช์ Pakala DXE ทั้ง 8 ตัว (`vendor/np05j/pakala-dxe/build_pakala_patches.py`)
- คอมไพล์ `BootShim` แบบ `IN_PLACE=1`, บิลด์ `nubia_np05jPkg/nubia_np05j.dsc` (`DEBUG` และ `RELEASE`) ด้วย `Mu_Basecore/BaseTools/Source/Python/build/build.py` (เพื่อไม่ต้องเรียก `stuart`/`nuget` ที่ไม่มีใน `sunuefi-builder`)
- คอมไพล์ Pre-UEFI `BootSelect` / `RelocShim` (`boot-selection` แบบสัมผัส 3 การ์ด: Android / UEFI Boot Menu / BIOS Setup) และแพ็ค `NP05J-SunUEFI-Helper.zip` (KSU/SukiSU/Magisk module + WebUI EN/TH/ZH + `np05j-boot-repack` AArch64 static)

### ระยะที่ 2: Unified SunUEFI `ProductCore` + Linux 7.3 (`./build.sh uefi --device np05j`, `./build.sh linux --device np05j`)
- **UEFI Touch (`uefi/components/touch-np05j/`)**: `Np05jTouchService.c` ทำงานเฉพาะระดับ `TPL_APPLICATION` ตรวจโหนด DTB `/soc/qcom,qupv3_1_geni_se@ac0000/spi@a90000/synaptics_tcm@0`, โหวต RPMh `ldob4`/`ldob14` สำหรับ DRV 2, ใช้ TLMM GPIO-SPI4 โดยไม่ใช้ DMA และถอดโปรโตคอล + คืนค่า TLMM ทั้ง 6 ขา / `PIN_ICFG(162)` (อ่านตรวจกลับทุกขา) + ปิดโหวต RPMh ใน `Np05jTouchStop()` ก่อน handoff
- **Core Adaptation (`#ifdef SUNUEFI_NP05J`)**: ผูก `2400×1504` (`0xDC5000`), `PianoConsoleScale` (3× -> `800×501`), `SimpleInit` (`artifacts/simpleinit/product-np05j/SimpleInit.efi`), `ramoops` `0xE4A00000`, `root compatible = "nubia,np05j"`, ตารางหน่วยความจำ 48->52 แถว (`tools/np05j_display_mapping.py` + `fix_np05j_low_heap`), และ derived `ClockDxe.efi` (`tools/np05j_inherited_clock.py`)
- **Linux 7.3 Kernel & DTBs (`tools/build/np05j_kernel.py`)**: สร้าง worktree แยก `build/kernel-worktrees/np05j` จาก `build/release/source-manifest.json` (ผ่านแพตช์ `0001..0140`), apply แพตช์ [patches/linux/7.3/np05j/](../../patches/linux/7.3/np05j/series.json) (`0141..0143`: Synaptics TCM2 SPI, `sm8750-nubia-np05j{,-otg,-gpu}`, ath12k `bdwlan.eXX`), รวม `defconfig` กับ 6 fragments ใต้ `linux/configs/np05j/` (พร้อม `CONFIG_QCOM_PBS=y` และ `CONFIG_QCOM_OCMEM=y`), และคอมไพล์ด้วย LLVM 23.1.5

---

## 3. แผนผังไฟล์สำคัญในคลัง

- **Board Inputs & Contract**: `config/np05j-product.json`, `vendor/np05j/manifest.json`, `vendor/np05j/dtb/{np05j,xbl-config}.dtb`, `vendor/np05j/early-dxe/EnvDxeEnhanced/`, `vendor/np05j/pakala-dxe/`
- **Phase 1 Hybrid**: `uefi/platforms/np05jPkg/`, `uefi/handoff/bootselect-np05j/`, `uefi/handoff/bootshim-np05j/`, `uefi/acpi/np05j/`, `android-np05j/`, `tools/build_np05j_hybrid.sh`
- **Phase 2 ProductCore & Tools**: `uefi/components/touch-np05j/{Np05jTouchService.c,Np05jTouchService.h,Np05jSynapticsTcm2.h}`, `tools/np05j_vendor_inputs.py`, `tools/np05j_inherited_clock.py`, `tools/np05j_display_mapping.py`, `tools/prepare_product.py`, `tools/prepare_simpleinit.py`, `tools/package_product.py`, `build.sh`
- **Linux 7.3 Kernel & Userspace**: `patches/firmware/np05j/`, `patches/linux/7.3/np05j/`, `linux/dts/np05j/`, `linux/configs/np05j/`, `linux/userspace/np05j/`, `tools/build/np05j_kernel.py`
- **Tests**: `tests/unit/test_np05j_port.py`, `tests/native/test_np05j_port.sh`, `uefi/platforms/np05jPkg/Tests/test_boot_repack.py`

---

## 4. ขั้นตอนการเตรียม Environment และคำสั่งบิลด์ / ตรวจสอบ

### 4.1 เตรียมแหล่งซอร์ส, Container และ Virtual Environment
```bash
# 1. ทดสอบชุด host unit + native C checks แบบออฟไลน์
./build.sh check
python3 tools/build/check_patch_series.py

# 2. ดึง upstream submodules ของเฟิร์มแวร์ (upstream/Mu-Silicium, simple-init, dtc)
./build.sh sources --group firmware

# 3. สร้าง Docker image sunuefi-builder และ .venv
python3 tools/build_container.py
docker run --rm -v "$PWD:/workspace" -w /workspace sunuefi-builder bash -euc '
  git config --global --add safe.directory "*"
  python3 -m venv .venv
  .venv/bin/pip install --upgrade pip
  .venv/bin/pip install -r requirements-build.txt
'
```

### 4.2 คำสั่งบิลด์จริงทั้งหมดภายใน `sunuefi-builder`
```bash
# Phase 1: Hybrid UEFI (DEBUG + RELEASE), BootSelect และ Android Helper ZIP
docker run --rm -v "$PWD:/workspace" -w /workspace sunuefi-builder bash -euc '
  git config --global --add safe.directory "*"
  ./build.sh uefi-hybrid ALL
'
python3 uefi/platforms/np05jPkg/Tests/test_boot_repack.py \
  --build-dir build/np05j-workspace/Mu-Silicium/Build/nubia_np05jPkg

# Phase 2: Unified ProductCore (Np05jUEFI-product.img, Np05jUEFI-product.fd, SimpleInit.efi)
docker run --rm -v "$PWD:/workspace" -w /workspace sunuefi-builder bash -euc '
  git config --global --add safe.directory "*"
  . .venv/bin/activate
  ./build.sh uefi --device np05j
'

# Cross-compile ยูทิลิตี userspace (np05j-tool.c)
docker run --rm -v "$PWD:/workspace" -w /workspace sunuefi-builder bash -euc '
  aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror linux/userspace/np05j/np05j-tool.c -o /tmp/np05j-tool
  file /tmp/np05j-tool
'

# Linux 7.3 Kernel + DTBs (ตรวจคอนฟิกก่อนแล้วค่อยคอมไพล์)
docker run --rm -v "$PWD:/workspace" -w /workspace sunuefi-builder bash -euc '
  git config --global --add safe.directory "*"
  . .venv/bin/activate
  ./build.sh linux --device np05j --configure-only
  ./build.sh linux --device np05j --rebuild
'
```

---

## 5. สรุปผลการบิลด์และตรวจรับบนโฮสต์

| เป้าหมาย | ไฟล์ผลลัพธ์ (Artifact) | ขนาด (ไบต์) | การตรวจรับบนโฮสต์ (`sunuefi-builder`) |
| :--- | :--- | ---: | :--- |
| **Phase 1 Image (DEBUG)** | `artifacts/np05j/Mu-nubia_np05j-DEBUG.img` | 3,076,096 | Header `ANDROID!` v4, sha256 `667e74ccb3c1bda5...` |
| **Phase 1 Image (RELEASE)** | `artifacts/np05j/Mu-nubia_np05j-RELEASE.img` | 3,076,096 | Header `ANDROID!` v4, sha256 `5c59c05d6de57de3...` |
| **Phase 1 FD (DEBUG / RELEASE)** | `artifacts/np05j/SILICIUM_UEFI-{DEBUG,RELEASE}.fd` | 2,097,152 | ตรงตาม `0x200000` ไบต์ (`InPlace.S` base `0xA8010000`) |
| **Pre-UEFI BootSelect** | `artifacts/np05j/BootSelect/np05j-bootselect.bin` | 13,470 | ARM64 magic `ARM\x64` (`0x38`), `test_bootselect` ผ่าน |
| **Android Helper ZIP** | `artifacts/np05j/NP05J-SunUEFI-Helper.zip` | 1,818,717 | `unzip -tq` ผ่าน, `bin/np05j-boot-repack` เป็น static AArch64 ELF, `test_boot_repack.py` ผ่าน |
| **Phase 2 Product Image** | `artifacts/product-np05j/Np05jUEFI-product.img` | 28,745,728 | Header `ANDROID!` v3, ramdisk `SUNUEFI-APPv1` + `SimpleInit.efi`, sha256 `5c4ac9e36856d49f...` |
| **Phase 2 Product FD** | `artifacts/product-np05j/Np05jUEFI-product.fd` | 3,145,728 | ตรงตาม `0x300000` ไบต์; `GNUmakefile` ลิงก์ `Np05jTouchService.obj` และ `PianoProductPumpLib.lib` |
| **Phase 2 SimpleInit** | `artifacts/simpleinit/product-np05j/SimpleInit.efi` | 26,824,704 | ผ่าน `simpleinit_build_identity` seal พร้อม `app-payload.bin` |
| **Linux 7.3 Kernel Image** | `artifacts/kernels/np05j/Image` | 42,965,504 | `7.3.3-np05j`, header `MZ` + `ARM\x64` (`efi_stub: true`), ผ่าน `REQUIRED` 10 สัญลักษณ์ |
| **Linux 7.3 DTBs** | `artifacts/kernels/np05j/sm8750-nubia-np05j.dtb`<br>`artifacts/kernels/np05j/sm8750-nubia-np05j-otg.dtb`<br>`artifacts/kernels/np05j/sm8750-nubia-np05j-gpu.dtb` | 119,269<br>119,677<br>119,421 | `compatible = ["nubia,np05j", "qcom,sm8750"]`, parse ด้วย `dtc -I dtb -O dts` สะอาด |

---

## 6. ข้อควรระวังก่อนทดสอบบนเครื่องจริง `nubia_np05j`

1. **ทดสอบแบบชั่วคราวก่อนเสมอ**: เมื่อพร้อมทดสอบบนอุปกรณ์ ควรใช้ `fastboot boot` กับ `Np05jUEFI-product.img` หรือ `Mu-nubia_np05j-RELEASE.img` ก่อน เพื่อไม่เขียนทับพาร์ติชัน `boot_a`/`boot_b` โดยไม่จำเป็น
2. **รักษาทางหนีกลับ Android (Escape Hatch)**: ตรวจสอบให้แน่ใจว่าหากการบูตล้มเหลว ผู้ใช้ยังคงกดปุ่ม **Volume Up** เพื่อเข้า Android จากหน้าเลือกเริ่มต้น หรือกดปุ่ม **Power** ค้างเพื่อรีบูตกลับ Android ได้
3. **ความปลอดภัยของพาร์ติชันสต็อก**: ห้ามเขียนทับพาร์ติชันสต็อกของ Qualcomm/Android (`modem`, `persist` metadata ของโรงงาน, `xbl`, `abl`, `userdata` หรือพาร์ติชันที่ไม่ใช่ `boot` target ที่ผ่าน `np05j-boot-repack` แบบ lossless และสำรองข้อมูลไว้แล้ว)
