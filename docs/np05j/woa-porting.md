# แผนและแนวทางการเตรียมพอร์ต Windows on ARM (WoA) สำหรับ nubia NP05J (REDMAGIC Astra)

> [!IMPORTANT]
> **ขอบเขตและสถานะจริง:**
> - **สิ่งที่พร้อมแล้วบนโฮสต์ (บิลด์และตรวจสอบแล้ว):** โครงสร้าง Hybrid UEFI (`Mu-nubia_np05j-*.img`), ตาราง ACPI ครบ 9 ตาราง (`DSDT`, `FACP`, `APIC`, `GTDT`, `PPTT`, `IORT`, `MCFG`, `SPCR`, `DBG2`) ที่คอมไพล์ผ่าน `iasl`, ไลบรารี `KernelErrataPatcher` แบบปรับค่าตอนรันไทม์ (`Np05jErrataPatchesLib`), การกันช่วงหน่วยความจำ TZ/Hypervisor (`MemoryMapLib`), ตัวจำกัดเพดาน RAM (`Np05jRamManagerLib`), และตัวป้องกันการเขียนทับพาร์ติชัน FAT สต็อก Android (`Np05jBiosConfigLib`)
> - **สิ่งที่ยังต้องทดสอบบนเครื่องจริงและจัดหาภายนอก:** การบูต Windows PE / Windows 11 ARM64 (`bootmgfw.efi` -> `winload.efi` -> `ntoskrnl.exe`) บนฮาร์ดแวร์ NP05J จริง และชุดไดรเวอร์ Windows ARM64 สำหรับ Qualcomm SM8750P (โดยเฉพาะ `ACPI\QCOM24A5` UFS, Adreno 830 GPU, WCN7861 Wi-Fi/BT, AudioReach และ Synaptics S3930TB0A0 TCM v2)

---

## 1. เลือกสาย UEFI สำหรับ WoA

ในคลังมี UEFI ของ NP05J อยู่ 2 สาย:

| สาย UEFI | ไฟล์ผลลัพธ์ | ความพร้อมสำหรับ WoA |
| :--- | :--- | :--- |
| **Hybrid (`uefi/platforms/np05jPkg`)** | `artifacts/np05j/Mu-nubia_np05j-{DEBUG,RELEASE}.img` | **ใช้สายนี้เป็นฐานนำร่อง WoA** — มี `AcpiTableDxe`, `AcpiPlatformDxe`, `BootGraphicsResourceTableDxe`, `SmbiosDxe`, `SmBiosTableDxe`, `KernelErrataPatcherLib`, `Np05jErrataPatchesLib`, `Np05jRamManagerLib`, `Np05jRealTimeClockLib`, และ `UFSDxe`/`UsbConfigDxe` ครบ |
| **ProductCore (`np05jProductPkg`)** | `artifacts/product-np05j/Np05jUEFI-product.img` | เน้น Linux + SimpleInit ปัจจุบันยังไม่มี ACPI/SMBIOS/KEP และใช้ `UnsupportedRealTimeClockLib` (ค่อยผสานกลับหลังพิสูจน์ WoA บนสาย Hybrid สำเร็จ) |

---

## 2. สรุปความพร้อมของฮาร์ดแวร์และไดรเวอร์ Windows 11 ARM64

| อุปกรณ์ย่อย | ฮาร์ดแวร์บน NP05J | ACPI / UEFI ที่เตรียมไว้ | สถานะไดรเวอร์บน Windows 11 ARM64 |
| :--- | :--- | :--- | :--- |
| **CPU / Interrupts / Timers** | SM8750P Oryon 6+2 cores (`0x000..0x500`, `0x10000..0x10100`), GICv3 `0x16000000`, GICR `0x16080000` (stride `0x40000`), ArchTimer + MemTimer `0x16800000` | `APIC`, `GTDT`, `PPTT`, `FACP`, `DSDT` (`CPU0..7`), `Np05jErrataPatchesLib` (`ACTLR`, `AMCNTEN`, `PSCI` เปิด; `GIC IRM` ปิดเพราะ MPIDR 2 คลัสเตอร์) | Inbox Windows HAL + KEP (ต้องทดสอบบนเครื่องจริงว่า Oryon EL1/Gunyah ต้องการแพตช์ตัวไหนบ้าง) |
| **Display** | Himax HX8874 Dual-DSI OLED `2400×1504 @ 32bpp` (`0xFC800000`) | UEFI GOP (`SimpleFbDxe`) + BGRT | **Inbox `BasicDisplay.sys`** ใช้งานแสดงผลผ่าน UEFI GOP ได้ทันที (ไม่มี GPU acceleration จนกว่าจะมีไดรเวอร์ Adreno 830) |
| **USB (Host/OTG)** | DWC3 SuperSpeed `0x0A600000`, IRQ `165`, SID `0x40` | `DSDT` `USB0` (`_HID QCOM0E08`, `_CID PNP0D10`, `_UPC`), `UsbConfigDxe` ข้าม deinit ตอน `ExitBootServices` | **Inbox `usbxhci.sys`** จับคู่กับ `PNP0D10` ได้ทันทีสำหรับคีย์บอร์ด เมาส์ และไดรฟ์ USB ในระยะที่ 1 (ต้องเชื่อมต่อผ่าน USB-C Host/Hub ที่จ่ายไฟหรือโหมด Host) |
| **Internal Storage (UFS 4.0)** | `ufshc@1d84000` (`0x1D84000..0x1DA7000`), IRQ `297`, SID `0x60`, sector 4 KiB | `DSDT` `UFS0` (`_HID QCOM24A5`, `_CRS` หน้าต่างเดียว `0x23000`, `UCCA` แยกจาก `STOR`, `DEV0._RMV=0`) + `IortDtDxe` | **ต้อง inject ไดรเวอร์ Qualcomm UFS (`ACPI\QCOM24A5`)** เข้า `boot.wim`/`install.wim` หากจะบูตจาก UFS ภายใน (ระยะที่ 1 แนะนำบูต WinPE RAMDisk หรือ Windows-to-Go ผ่าน USB xHCI ก่อนเพื่อตัดตัวแปร) |
| **Memory Map & 24 GiB RAM** | 24 GiB LPDDR5X (`0x80000000`, `0x880000000`, `0xA00000000`) | `MemoryMapLib.c` กัน TZ/Hyp gaps (`0xD8600000..0xE3720000`, `0x8AF300000..0x8B0000000`, `0x8B12C0000..0x8C0000000`) + `Np05jRamManagerLib` รองรับ `RamLimitGb` (4/8/12/16 GiB) | ใช้ได้ตาม UEFI Memory Map (หากเจอ BSOD ช่วง high RAM ให้ลองตั้ง `RAM Cap Override = 4 GiB` หรือ `8 GiB` ใน BIOS Setup) |
| **RTC / Reset / NVRAM** | PMK8550 + ARM Generic Timer + PSCI SMC | `Np05jRealTimeClockLib` (runtime-safe ไม่แตะ SPMI), `ArmPsciResetSystemLib`, `Np05jBiosConfigLib` (จำกัดการเขียนเฉพาะ GPT ESP / `\EFI` / `\NP05J`) | Inbox UEFI Runtime Services |
| **Touchscreen** | Synaptics `S3930TB0A0` (TCM v2 ผ่าน SPI4 `0x00A90000`, IRQ GPIO 162) | `DSDT` `SPI4` + `TPL0` (`_HID SYNA3930`, ถอด `_CID PNP0C50` ที่ผิดโปรโตคอลออก) — ปิดเป็นค่าเริ่มต้นใน WoA preset (`TSEN=0`) | ยังไม่มีไดรเวอร์ Windows ARM64 สำหรับ Synaptics TCM v2 over SPI4 (ระยะที่ 1 ใช้เมาส์/คีย์บอร์ด USB แทน) |
| **GPU / Wi-Fi / BT / Audio / PMIC** | Adreno 830, WCN7861 (PCIe0 `0x40000000` / UART14), AudioReach + TFA9874 + WCD9395 | ปิด `MCFG`/`PCIe0` เป็นค่าเริ่มต้นใน WoA preset เพื่อลดความเสี่ยง BSOD | ต้องจัดหาและพอร์ตไดรเวอร์ Qualcomm Windows ARM64 + ACPI `_HID`/PEP/SCM/GLINK/TLMM เพิ่มเติมในระยะที่ 2–3 |

---

## 3. ขั้นตอนการบิลด์และทดสอบระยะที่ 1 (Minimal Bring-up -> WinPE / Windows 11 ARM64)

### 3.1 บิลด์ Hybrid UEFI พร้อม ACPI และ KEP ล่าสุด
```bash
docker run --rm -v "$PWD:/workspace" -w /workspace sunuefi-builder bash -euc '
  git config --global --add safe.directory "*"
  ./build.sh uefi-hybrid ALL
'
```
ผลลัพธ์จะอยู่ที่ `artifacts/np05j/Mu-nubia_np05j-DEBUG.img` (สำหรับดู log บนจอ/USB CDC) และ `artifacts/np05j/Mu-nubia_np05j-RELEASE.img`

### 3.2 เตรียมสื่อทดสอบ WinPE ARM64 บน USB (ปลอดภัย 100% ไม่แตะพาร์ติชัน Android/Linux)
1. ฟอร์แมตแฟลชไดรฟ์ USB เป็น **GPT + FAT32** แล้วคัดลอกไฟล์ WinPE ARM64 (Windows 11 24H2/25H2 ADK ARM64 หรือ ISO Windows 11 ARM64 ที่มี `\EFI\BOOT\BOOTAA64.EFI`, `\boot\bcd`, `\sources\boot.wim`)
2. ปรับ BCD บนสื่อทดสอบให้เหมาะกับการดีบัก ARM64 EL1:
   - `bcdedit /store <BCD> /set {default} testsigning on`
   - `bcdedit /store <BCD> /set {default} nointegritychecks on`
   - `bcdedit /store <BCD> /set {default} hypervisorlaunchtype off`
   - `bcdedit /store <BCD> /set {default} bootlog yes`
   - `bcdedit /store <BCD> /set {default} bootstatuspolicy IgnoreAllFailures`
   - `bcdedit /store <BCD> /set {default} sos on`
3. บูตชั่วคราวด้วย `fastboot boot artifacts/np05j/Mu-nubia_np05j-DEBUG.img` (ไม่แฟลชทับ `boot_a`/`boot_b`)
4. เข้าหน้า **BIOS Setup -> 7. OS & Win11 ARM**:
   - เลือก Preset **Windows 11 ARM** (`ACPI Table Profile = Minimal Debug Set`, `ACPI IORT / SMMU = Omit for WoA`, `KernelErrataPatcher = ENABLED`, `Patch EL1 ACTLR = ENABLED`, `Patch PMU AMCNTEN = ENABLED`, `Patch GICv3 IRM = DISABLED`, `Patch PSCI MemProt = ENABLED`)
   - หากทดสอบครั้งแรกค้างก่อนขึ้นโลโก้ Windows ให้ลองตั้ง **4. Mem & Storage -> RAM Cap Override = 4 GiB Cap** และลองสลับ `Patch EL1 ACTLR` / `Patch PMU AMCNTEN` / `Patch PSCI MemProt` ทีละตัวเพื่อยืนยันว่า SM8750P Gunyah/EL1 ต้องการแพตช์ชุดใดบ้าง
5. เลือก **Windows 11 ARM** หรือ **Boot From File (.EFI)** เพื่อเปิด `\EFI\BOOT\BOOTAA64.EFI` บน USB

### 3.3 ขอบเขตความปลอดภัยเมื่อจะติดตั้งลงดิสก์ภายใน (ระยะที่ 2)
- ห้ามแก้ ลบ หรือเขียนทับพาร์ติชันสต็อกของ Android (`xbl`, `abl`, `modem`, `persist`, `logfs`, `userdata` ฯลฯ)
- ใช้เฉพาะพื้นที่พาร์ติชันเฉพาะ (dedicated storage partition) เช่นแบ่งพื้นที่ NTFS 4Kn (logical sector 4096 ไบต์) แยกออกจาก Android และ Linux root และคงทางหนีกลับ Android (กดปุ่ม Power ค้าง หรือกด Volume Up ใน BootSelect) ให้ใช้งานได้ตลอดเวลา

---

## 4. การแยกและตรวจสอบแพ็กเกจไดรเวอร์ `ACPI\QCOM24A5` (UFS) จาก OEM Snapdragon X (`Purwa`/`Hamoa`)

### 4.1 โครงสร้างสแตกไดรเวอร์ UFS บน Windows 11 ARM64
จากการตรวจสอบ `storufs.inf` ตัวจริงในแพ็กเกจ `Lenovo Yoga Slim 7x (83ED)` (`10.0.26100.4768`) และใน Microsoft ADK `winpe.wim` (Build `10.0.26100.1`):
1. **Microsoft Inbox Storport Miniport (`storufs.sys` / `storufs.inf`):** ติดมากับ Windows 11 ARM64 และ WinPE ARM64 อยู่แล้ว และมีบรรทัด `%ACPI\QCOM24A5.DeviceDesc%=UfsQualcomm8996Install, ACPI\QCOM24A5` -> `AddService=storufs` จับคู่ `ACPI\QCOM24A5` โดยตรง (รองรับ UFS 3.1/4.0, 4Kn, SCSI/UNMAP)
2. **Qualcomm / OEM Platform & Extension INF (จากแพ็กเกจ Snapdragon X `Purwa`/`Hamoa`):** หากอุปกรณ์ต้องการค่าคอนฟิกเพิ่มเติมหรืออัปเดต `storufs.sys` รุ่นใหม่กว่า Build 26100.1 (เช่น Build `26100.4768` จาก Lenovo 83ED หรือแพ็กเกจ Surface Purwa) จะอยู่ในรูป `.inf` + `.cat` + `.sys` และอาจพ่วงความสัมพันธ์กับ Power Engine Plug-in (`qcpep.wd8380.inf`)

> [!IMPORTANT]
> **ข้อควรระวังเรื่อง Dependency (`_DEP` / `qcpep`):**
> - ในแพ็กเกจ OEM ของ `Purwa`/`Hamoa` โหนด `UFS0` ใน DSDT สต็อกมักประกาศ `_DEP` ชี้ไปที่ `\_SB.PEP0` (`qcpep.wd8380.inf`) แต่บน NP05J (`SM8750P`) เรายังไม่มี `PEP0` ที่ตรงกับซิลิคอน SM8750P ดังนั้นใน [DSDT.asl](../../uefi/acpi/np05j/Platforms/ZTE/nubia_np05j/DSDT.asl) จึง **ไม่ใส่ `_DEP { \_SB.PEP0 }`** และอาศัย `UFSDxe` ในเฟิร์มแวร์เปิดคล็อก/PHY ค้างไว้ก่อน `ExitBootServices` เพื่อให้ `storufs.sys` จับคู่ `ACPI\QCOM24A5` ได้ทันทีโดยไม่ติด deadlock
> - ก่อนนำไดรเวอร์ OEM เพิ่มเติมไป inject ต้องเปิดตรวจไฟล์ `.inf` เสมอว่า `StartType = 0` (Boot-Start) และไม่มี hard dependency (`DependOnService`) ไปยังเซอร์วิส `qcpep` ของซิลิคอนอื่น

### 4.2 แหล่งแพ็กเกจ OEM Snapdragon X ที่ใช้ UFS จริง
อุปกรณ์ Snapdragon X รุ่นหลัก (`Hamoa` X1E80100) ส่วนใหญ่ใช้ NVMe PCIe แต่กลุ่ม **`Purwa` (`SC8380PA` / `X1P-42-100` 8-core)** มีรุ่นเชิงพาณิชย์ที่ใช้ **UFS ภายในจริง** จึงเป็นแหล่งหลักสำหรับดึงแพ็กเกจ `ACPI\QCOM24A5`:

| แหล่งที่มา | ซิลิคอน / รหัสแพลตฟอร์ม | รูปแบบไฟล์ | จุดเด่น |
| :--- | :--- | :--- | :--- |
| **Microsoft Surface Pro 12-inch (1st Ed, Snapdragon X Plus)** (`Download ID 108199`) | `Purwa` (`8380_VEA`, ใช้ UFS ภายในจริง) | `.msi` (`SurfacePro_12in_1st_Edition_Win11_26100_*.msi`) | แพ็กเกจ WHQL ทางการจาก Microsoft, โครงสร้างโฟลเดอร์แยกตามไดรเวอร์ชัดเจน |
| **Microsoft Surface Laptop 13-inch (1st Ed, Snapdragon X Plus)** (`Download ID 108710`) | `Purwa` (ใช้ UFS ภายในจริง) | `.msi` (`SurfaceLaptop_13in_1st_Edition_Win11_26100_*.msi`) | ชุดไดรเวอร์ Windows 11 24H2 Build 26100 รุ่นใหม่เช่นเดียวกัน |
| **WOA-Project / `Qualcomm-Reference-Drivers`** | `Purwa` (`8380PA_CRD`) และ `Hamoa` (`8380_CRD` / `8380_RUN`) | คลังไฟล์ `.inf`/`.sys`/`.cat` จาก Windows Update | เหมาะสำหรับเทียบโครงสร้างไดรเวอร์อ้างอิง Qualcomm (`qcpep.wd8380.inf`, UFS, WCN7851, URS) |
| **Microsoft Update Catalog (`catalog.update.microsoft.com`)** | ค้นด้วย `QCOM24A5` หรือ `Qualcomm Universal Flash Storage` | `.cab` (ARM64) | ได้แพ็กเกจย่อยเฉพาะชิ้นโดยไม่ต้องโหลดอิมเมจทั้งเครื่อง |

### 4.3 ขั้นตอนการแตกแพ็กเกจและสแกนหา `ACPI\QCOM24A5`

#### บน Windows (แนะนำสำหรับ `.msi` ของ Surface และ `.cab`)
```powershell
# 1. แตกไฟล์ Surface MSI ด้วย Administrative Install (ไม่ติดตั้งลงเครื่องโฮสต์)
msiexec /a "C:\Downloads\SurfacePro_12in_1st_Edition_Win11_26100_*.msi" /qb TARGETDIR="C:\Extracted\SurfacePurwa"

# หรือแตกไฟล์ .cab จาก Microsoft Update Catalog
mkdir C:\Extracted\UfsCab
expand -F:* "C:\Downloads\qcom_ufs_arm64.cab" "C:\Extracted\UfsCab"

# 2. สแกนหาทุกไฟล์ .inf ที่จับคู่ QCOM24A5 หรืออ้าง storufs / ufs
Get-ChildItem -Path "C:\Extracted\SurfacePurwa" -Recurse -Filter "*.inf" |
  Select-String -Pattern "QCOM24A5|VEN_QCOM&DEV_24A5|storufs|qcufs|qcomufs" -List |
  Select-Object Path, LineNumber, Line
```

#### บน Linux (สำหรับแตกและวิเคราะห์ในเครื่องพัฒนา)
```bash
# 1. แตก Surface .msi ด้วย msiextract (จากแพ็กเกจ msitools) หรือ 7z
mkdir -p /tmp/extracted-purwa
msiextract -C /tmp/extracted-purwa SurfacePro_12in_1st_Edition_Win11_26100_*.msi
# หรือแตก .cab ด้วย cabextract:
# cabextract -d /tmp/extracted-cab qcom_ufs_arm64.cab

# 2. ค้นหาไฟล์ .inf ที่อ้าง QCOM24A5 (รองรับทั้ง UTF-8 และ UTF-16LE ที่ Windows INF นิยมใช้)
python3 - <<'PY'
import pathlib, re
root = pathlib.Path('/tmp/extracted-purwa')
pat = re.compile(r'(QCOM24A5|VEN_QCOM&DEV_24A5|storufs|qcufs|qcomufs)', re.I)
for inf in sorted(root.rglob('*.inf')):
    raw = inf.read_bytes()
    text = raw.decode('utf-16-le', errors='ignore') if b'\x00' in raw[:64] else raw.decode('utf-8', errors='ignore')
    hits = [f"L{i}: {line.strip()}" for i, line in enumerate(text.splitlines(), 1) if pat.search(line)]
    if hits:
        print(f"=== {inf} ===")
        print("\n".join(hits[:25]))
PY
```

### 4.4 รายการตรวจสอบ `.inf` ก่อนนำไป Inject
เมื่อพบโฟลเดอร์ที่มี `.inf` จับคู่ `ACPI\QCOM24A5` ให้ตรวจ 5 จุดต่อไปนี้ก่อนคัดลอกไปใช้:
1. **สถาปัตยกรรม:** ต้องมี section `[Manufacturer]` ชี้ไปยัง `NTARM64` (เช่น `%Qualcomm% = Standard,NTARM64.10.0...`)
2. **Hardware ID:** ต้องระบุ `ACPI\QCOM24A5` หรือ `ACPI\VEN_QCOM&DEV_24A5` (ตรงกับ `_HID "QCOM24A5"` ใน `DSDT.asl`)
3. **โหมดเริ่มทำงานและ Dependency:** ตรวจ `[...AddService]` ว่า `StartType = 0` (`SERVICE_BOOT_START`) หรือเป็น Extension INF ที่อ้าง `Include = storufs.inf` และตรวจว่าไม่มี `DependOnService` ที่ผูกกับ `qcpep` ของซิลิคอน Hamoa/Purwa
4. **ไฟล์ครบชุด:** คัดลอกทั้งโฟลเดอร์ย่อย (`.inf`, `.cat`, `.sys`, และ `.dll` หากมี) ห้ามคัดลอกเฉพาะ `.inf` เพื่อให้ Catalog Signature (WHQL) ยังสมบูรณ์
5. **ความเข้ากันได้เฉพาะ NP05J (`SM8750P`):**
   - คอนโทรลเลอร์ UFS 4.0 บน SM8750P (`ufshc@1d84000`) มีหน้าต่างรีจิสเตอร์ต่อเนื่อง `0x01D84000..0x01DA7000` (`0x23000` ไบต์: `ufs_mem` + `ice` + `mcq_vs` + `mcq_sqd`) และ IRQ เดียว `297` (`0x129`)
   - **MCQ vs Single Doorbell:** หากไดรเวอร์ UFS พยายามเปิดโหมด Multi-Circular Queue (MCQ) โดยอาศัย MSI/GIC ITS ขณะที่เราตัด GIC ITS ออกจาก `APIC` สำหรับ WoA อาจเกิด timeout ตอน probe — ให้ตรวจ log วินโดวส์ว่าไดรเวอร์ fallback กลับ Legacy Single Doorbell หรือไม่ (และหากเปิด `UCCA=1` แล้วเกิด DMA corruption ให้สลับ `UFS Cache Coherency = Disabled` ใน BIOS Setup เพื่อทดสอบ `_CCA=0`)
   - **4Kn Sector:** UFS LUN ของ NP05J ใช้ logical sector ขนาด **4096 ไบต์** (ห้ามโคลนพาร์ติชัน 512e ตรงๆ โดยไม่สร้างไฟล์ระบบ NTFS 4Kn)

### 4.5 การ Inject ไดรเวอร์เข้า `boot.wim` (WinPE) และ `install.wim`
```powershell
# Mount WinPE (boot.wim Index 2 สำหรับ Windows Setup หรือ Index 1 สำหรับ WinPE เปล่า)
mkdir C:\Mount\BootWim
dism /Mount-Wim /WimFile:"E:\sources\boot.wim" /Index:2 /MountDir:"C:\Mount\BootWim"

# เพิ่มเฉพาะโฟลเดอร์ไดรเวอร์ UFS ที่ตรวจแล้ว (เติม /ForceUnsigned เฉพาะกรณีทดสอบแพ็กเกจที่แก้ INF เอง)
dism /Image:"C:\Mount\BootWim" /Add-Driver /Driver:"C:\Extracted\UfsOnly" /Recurse

# ตรวจสอบรายการไดรเวอร์ที่ถูก inject แล้วบันทึก
dism /Image:"C:\Mount\BootWim" /Get-Drivers
dism /Unmount-Wim /MountDir:"C:\Mount\BootWim" /Commit
```
ทำขั้นตอนเดียวกันกับ `E:\sources\install.wim` (หรือไดรฟ์ Windows ที่ติดตั้งแบบ offline ด้วย `dism /Image:W:\ /Add-Driver ...`) เมื่อพร้อมเข้าสู่ระยะที่ 2

---

## 5. โครงสร้าง DSDT สำหรับ `URS0` (USB Role Switch) และ `PCI0` (`PNP0A08` Root Complex)

โครงสร้างทั้งสองถูกวางไว้ใน [DSDT.asl](../../uefi/acpi/np05j/Platforms/ZTE/nubia_np05j/DSDT.asl) และควบคุมการเปิด/ปิดตอนรันไทม์ผ่าน [Np05jAcpiUpdate.c](../../uefi/platforms/np05jPkg/Library/Np05jAcpiUpdateLib/Np05jAcpiUpdate.c) โดยค่า MMIO, IRQ, PDC mapping และ PCIe ranges ทุกตัวอ้างอิงจาก `vendor/np05j/dtb/np05j.dtb` และ `vendor/np05j/dtb/xbl-config.dtb`

### 5.1 โครงสร้าง `URS0` vs `USB0` (USB Type-C DWC3 `0x0A600000`)

เพื่อไม่ให้เกิดความขัดแย้งของทรัพยากร (Windows PnP Code 12) บนหน้าต่าง MMIO `0x0A600000` (ขนาด `0x100000`) ระหว่างโหมด Host-only กับโหมด Dual-Role, `DSDT.asl` แยก 2 โหนดแบบ **mutually exclusive** ผ่านตัวแปร `USBE` และ `URSE`:

| เงื่อนไข BIOS Setup | ค่า `USBE` / `URSE` | โหนดที่เปิด (`_STA = 0x0F`) | ไดรเวอร์ Windows เป้าหมาย |
| :--- | :--- | :--- | :--- |
| `Windows 11 ARM` Preset (`Minimal`) หรือ `USB-C Role Policy = Host (2)` | `USBE=1`, `URSE=0` | `\_SB.USB0` (`_HID QCOM0E08`, `_CID PNP0D10`) | Inbox `usbxhci.sys` (ปลอดภัยสุดสำหรับระยะที่ 1) |
| `ACPI Table Profile = Full` และ `USB-C Role Policy = Auto (0)` / `Device (1)` | `USBE=1`, `URSE=1` | `\_SB.URS0` (`_HID QCOM0C8B`, `_CID PNP0CA1, QCOMFFE1`) -> `USB0` + `UFN0` | Inbox `urssynopsys.sys` (`usbxhci.sys` + `ufxsynopsys.sys`) หรือ Qualcomm `qcurs` |
| ปิด `AcpiUsbEnable = 0` | `USBE=0`, `URSE=0` | ปิดทั้งคู่ (`_STA = 0`) | ไม่เปิด USB ใน ACPI |

ตารางที่มาของ Interrupts 5 เส้นใน `\_SB.URS0.USB0` (พิสูจน์จาก `ssusb@a600000`, `dwc3@a600000` และ `interrupt-controller@b220000` `qcom,pdc-ranges` base `0x2E9` ของ SM8750P):

| ชื่อใน `np05j.dtb` | ต้นทางใน DTB | การคำนวณ GIC INTID (`SPI + 32`) | ค่าใน `DSDT.asl` | Trigger / ใช้ในโหนด |
| :--- | :--- | :--- | :--- | :--- |
| `dwc3` main IRQ | GIC SPI `0x85` (133) | `0x85 + 0x20` | **`0x00A5` (165)** | Level High, Shared (`USB0`, `UFN0`) |
| `pwr_event_irq` | GIC SPI `0x82` (130) | `0x82 + 0x20` | **`0x00A2` (162)** | Level High, SharedAndWake (`USB0`, `UFN0`) |
| `ss_phy_irq` | PDC pin `0x11` (17) | `0x2E9 + 0x11 + 0x20` | **`0x031A` (794)** | Level High, SharedAndWake (`USB0`) |
| `dm_hs_phy_irq` | PDC pin `0x0F` (15) | `0x2E9 + 0x0F + 0x20` | **`0x0318` (792)** | Edge High, SharedAndWake (`USB0`) |
| `dp_hs_phy_irq` | PDC pin `0x0E` (14) | `0x2E9 + 0x0E + 0x20` | **`0x0317` (791)** | Edge High, SharedAndWake (`USB0`) |

> [!NOTE]
> **ข้อจำกัดของ `URS0` เมื่อยังไม่มี PMIC Glink / UCSI (`UCP0`):**
> โหนด `URS0` ประกาศ `_CCA = 1` (ตาม `dma-coherent` ของ `dwc3@a600000`), `RHUB.PRT1` (`_UPC`/`_PLD`), และ `_DSM` ครบทั้งฝั่ง xHCI (`ce2ee385-00e6-48cb-9f05-2edb927c4899`) และฝั่ง Synopsys UFX (`fe56cfeb-49d5-4378-a8a2-2978dbe54ad2`, `18de299f-9476-4fc9-b43b-8aeb713ed751`) แต่หากไม่มีไดรเวอร์ USB-C PD/UCSI ส่ง role-swap event ให้ `urssynopsys.sys` จะต้องกำหนด `DefaultRole` ใน Registry ของ Windows (หรือสลับกลับ `USB-C Role Policy = Host` ใน BIOS Setup เพื่อใช้ `\_SB.USB0` ตรง)

### 5.2 โครงสร้าง `PCI0` (`PNP0A08` Root Complex + `WCN7861`)

ค่าทรัพยากรทั้งหมดของ `\_SB.PCI0` อ้างอิงจาก `pcie@1c00000` และ `qcom,cnss-peach` ใน `vendor/np05j/dtb/np05j.dtb` ร่วมกับ `uefi/acpi/np05j/Platforms/ZTE/nubia_np05j/MCFG.dsl`:

| รายการ ACPI | ค่าบน NP05J (`SM8750P`) | แหล่งอ้างอิงใน DTB / ตารางอื่น |
| :--- | :--- | :--- |
| `_HID` / `_CID` | `PNP0A08` / `PNP0A03` | มาตรฐาน PCIe Root Bridge (`pci.sys`) |
| `_SEG` / `_BBN` / Bus Range | Segment `0`, Bus `0x00..0x01` (Length `2`) | `MCFG.dsl` (`Segment=0`, `Start=00`, `End=01`) |
| `_CBA` (ECAM Base) | `0x0000000040000000` | `dm_core = 0x40000000`, `conf (Bus 1) = 0x40100000` |
| `_CRS` MMIO32 Window | `0x40300000..0x43FFFFFF` (`0x03D00000` = 61 MiB) | `ranges` (`0x2000000`, แยกออกจาก ECAM `0x40000000..0x401FFFFF` เพื่อกัน BAR ชน ECAM) |
| `_PRT` (INT A/B/C/D) | GIC **`0xB5` (181), `0xB6` (182), `0xB7` (183), `0xB8` (184)** | `int_a..int_d` = SPI `0x95..0x98` + 32 (ประกาศทั้งใน `PCI0` และ `RP00`) |
| `_CCA` | `1` (`dma-coherent`) | `pcie@1c00000` มี property `dma-coherent` |
| Topology / SMMU SID | `PCI0` -> `RP00` (`00:00.0`, SID `0x1400`) -> `WCN0` (`01:00.0`, `17cb:110e`, SID `0x1401`) | `iommu-map` ใน `np05j.dtb` และ `NAMEDNODE_PCIE0` (`0x7F1400`) ใน `xbl-config.dtb` |

> [!WARNING]
> **เงื่อนไขก่อนเปิด `PCIE=1` บนเครื่องจริง (เหตุผลที่ปิดไว้เป็นค่าเริ่มต้น):**
> 1. ชุด `vendor/np05j/pakala-dxe/` ของ NP05J **ไม่มี `PcieDxe`** — คอนโทรลเลอร์ DesignWare PCIe Gen4 (`0x01C00000`) และ PHY (`0x01C06000`) รวมถึงไฟ PMU ของ `WCN7861`, GPIO 16 (`wlan-en`), และ GPIO 102 (`PERST#`) ยังไม่ถูกเปิดและทำ Link Training ในเฟิร์มแวร์ UEFI
> 2. หากเปิด `AcpiPcieEnable=1` + `Full ACPI` บน Windows ขณะที่ลิงก์ยังไม่ train และคล็อก/GDSC ยังปิดอยู่ การที่ `pci.sys` อ่านหน้าต่าง ECAM `0x40000000` จะทำให้เกิด **SError / Synchronous External Abort** ทันที
> 3. ดังนั้นในระยะที่ 1–2 จึงล็อก `PCIE=0` (และถอด `MCFG` ออก) ไว้เป็นค่าเริ่มต้นใน `Np05jAcpiUpdate.c` จนกว่าจะมี UEFI pre-OS PCIe init DXE (ที่เปิดคล็อก/PHY/PMU WCN7861 + train link + ตั้ง outbound iATU ให้ `0x40100000` และ `0x40300000`) หรือไดรเวอร์ Qualcomm PCIe PEP/Root Complex ฝั่ง Windows พร้อมใช้งาน

---

## 6. การสร้างและแฟลชอิมเมจ WinPE ARM64 พร้อมไดรเวอร์จอสัมผัส (`winpe.img` <= 512 MiB)

### 6.1 สถาปัตยกรรมจอสัมผัสบน WinPE สำหรับ NP05J
- แพ็กเกจต้นแบบ `Lenovo Yoga Slim 7x (83ED)` ใช้จอสัมผัส HID มาตรฐาน (`hidi2c`/`hidspi_km`) จึง **ไม่มี** ไดรเวอร์ Synaptics TCM v2 สำหรับ `S3930TB0A0` ของ NP05J
- เราจึงสร้างไดรเวอร์ Windows ARM64 KMDF 1.31 + Virtual HID Framework (`vhf.sys`) โดยเฉพาะใน [Np05jSynaTouch.c](../../windows/drivers/np05j-syna-touch/Np05jSynaTouch.c) และ [Np05jSynaTouch.inf](../../windows/drivers/np05j-syna-touch/Np05jSynaTouch.inf):
  - ใช้แกนโปรโตคอล `Np05jSynapticsTcm2.h` ตัวเดียวกับ UEFI ที่พิสูจน์บนเครื่องจริงแล้ว
  - รับหน้าต่าง TLMM 4KB ทั้ง 6 ขาจาก `\_SB.TPL0` (`ACPI\SYNA3930`, GPIO 48/49/50/51/161/162) และให้ `GIO0` ปิดอัตโนมัติเมื่อ `TSEN != 0` เพื่อเลี่ยง Code 12 Resource Conflict
  - ใน [Np05jTouchDxe.c](../../uefi/platforms/np05jPkg/Drivers/Np05jTouchDxe/Np05jTouchDxe.c) เมื่อ `AcpiTouchEnable != 0` จะคงไฟ RPMh `ldob4` (1.8V) และ `ldob14` (3.2V) ไว้ตอน `ExitBootServices` เพื่อให้ `Np05jSynaTouch.sys` คุยกับพาเนลต่อได้ทันที
  - พร้อมแนบไดรเวอร์จาก `lenovo-83ed-x1e-drivers` (`storufs`, `hidvhf`, `hidspi_km`, `hidi2c`, `hidinterrupt`, `mshidkmdf`, `msgpiowin32`, `qcursext`, `QcXhciFilter8380` และกลุ่ม `Optional_8380`: `qcgpio8380`, `qcgpi8380`, `qcspi8380`, `qcuart8380`) ใน `X:\Drivers\`

### 6.2 คำสั่งบิลด์และแฟลชลง `sunuefi_esp`
```bash
# บิลด์ winpe.img (Android Sparse 4096-byte, แตกเป็น FAT32 4Kn 512 MiB พอดี)
docker run --rm -v "$PWD:/workspace" -v /tmp/winpe-work:/tmp/winpe-work -w /workspace sunuefi-builder bash -euc '
  apt-get update -qq && apt-get install -y -qq cabextract wimtools osslsigncode libhivex-bin python3-hivex >/dev/null
  ./build.sh winpe-np05j --work-dir /tmp/winpe-work --output-dir /workspace/artifacts/np05j
'

# แฟลชลงพาร์ติชัน sunuefi_esp (รองรับทั้ง Android Factory Fastboot และ SunUEFI Fastboot)
fastboot flash sunuefi_esp artifacts/np05j/winpe.img

# บูต Hybrid UEFI แล้วเลือก Preset "Windows 11 ARM" -> กด "Windows 11 ARM"
fastboot boot artifacts/np05j/Mu-nubia_np05j-DEBUG.img
```

> [!NOTE]
> - `winpe.img` ใช้รูปแบบ Android Sparse (block size 4096 ไบต์, chunk สูงสุด 32 MiB) เพื่อให้แฟลชผ่าน `fastboot flash sunuefi_esp winpe.img` ได้โดยไม่ติดข้อจำกัด `NP05J_FASTBOOT_MAX_DOWNLOAD` (64 MiB) และไม่แตะพาร์ติชันสต็อก Android ใดๆ
> - `startnet.cmd` ใน `boot.wim` จะรัน `drvload` โหลด `Np05jSynaTouch.inf` และแสดงค่า `Stage`/`Status`/`BuildId`/`FrameCount` ใต้ `HKLM\SYSTEM\CurrentControlSet\Services\Np05jSynaTouch\Parameters` บนหน้าจอทันทีเพื่อช่วยตรวจสอบบนเครื่องจริง


