// SPDX-License-Identifier: BSD-2-Clause-Patent
// บริการควบคุมจอสัมผัส Synaptics S3930TB0A0 (TCM v2) แบบไม่บล็อกสำหรับ NP05J ProductCore
#pragma once
#include <Uefi.h>
#include "PianoProductOwners.h"

// ตรวจสอบโหนด SPI4 และ Synaptics TCM2 บน Device Tree เท่านั้น;
// คืนค่า EFI_NOT_FOUND หากไม่พบฮาร์ดแวร์ใน DTB
EFI_STATUS Np05jTouchStart(IN CONST VOID *Fdt);

// เรียกเฉพาะที่ TPL_APPLICATION เท่านั้น: ดำเนินขั้นตอน bring-up ทีละขั้น
// หรืออ่านรายงานสัมผัสจาก GPIO bit-bang SPI4 เมื่อสาย IRQ อยู่ในสถานะ LOW
VOID Np05jTouchPump(VOID);

// พิมพ์สถานะและสถิติการสัมผัสย้อนหลังลง boot log
VOID Np05jTouchReplay(VOID);

// ถอดโปรโตคอล, กู้คืนค่า TLMM ทั้ง 6 ขา + IRQ_CFG พร้อมอ่านตรวจกลับ,
// และคืนค่าโหวตรางไฟ RPMh (ldob14 แล้ว ldob4 -> 0 และ Delete)
EFI_STATUS Np05jTouchStop(IN VOID *Context,OUT PIANO_PRODUCT_INPUT_RETIRE_REPORT *Report);
