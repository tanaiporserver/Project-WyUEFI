// DSDT สำหรับ nubia NP05J (REDMAGIC Astra, Qualcomm SM8750P / Pakala)
// รองรับทั้ง Windows 11 on ARM และ Linux ACPI พร้อมตัวแปรปรับค่าตอนรันไทม์ผ่าน Np05jAcpiUpdateLib
DefinitionBlock ("", "DSDT", 2, "NUBIA ", "NP05J   ", 0x00008750)
{
    Scope (\_SB)
    {
        // ค่าอัตลักษณ์แพลตฟอร์มและสวิตช์ BIOS ที่อัปเดตตอนรันไทม์
        // หมายเหตุ: ตั้งค่าเริ่มต้นไม่ใช่ 0/1 เพื่อให้ iASL สร้าง AML prefix เต็มความกว้างสำหรับ AslUpdateName()
        Name (SOID, 0x12345678)          // DWORD: SoC ID (639 สำหรับ SM8750P)
        Name (SIDV, 0x12345678)          // DWORD: SoC Revision (0x00020000)
        Name (SVMJ, 0x1234)              // WORD:  SoC Version Major (2)
        Name (SVMI, 0x1234)              // WORD:  SoC Version Minor (0)
        Name (SOSN, 0x1122334455667788)  // QWORD: Board / Serial Number
        Name (STOR, 0x00010001)          // DWORD: เปิด/ปิด UFS0 (_STA)
        Name (UCCA, 0x00010001)          // DWORD: สถานะ Cache Coherency ของ UFS0 (_CCA)
        Name (USBE, 0x00010001)          // DWORD: เปิด/ปิดระบบ USB ทั้งหมด (AcpiUsbEnable)
        Name (URSE, 0x00010001)          // DWORD: 0 = Host-only USB0 (PNP0D10), 1 = Dual-Role URS0 (PNP0CA1)
        Name (TSEN, 0x00010001)          // DWORD: เปิด/ปิด SPI4 / SYNA3930 (AcpiTouchEnable)
        Name (PCIE, 0x00010001)          // DWORD: เปิด/ปิด PCI0 Root Complex (AcpiPcieEnable & Pcie0Enable)
        Name (BTNE, 0x00010001)          // DWORD: เปิด/ปิดปุ่ม PWRB / BTNS (AcpiButtonsEnable)

        Device (CPU0)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU1)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 1)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU2)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU3)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU4)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 4)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU5)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 5)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU6)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU7)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (UFS0)
        {
            Name (_HID, "QCOM24A5")
            Name (_UID, Zero)

            Method (_CCA, 0, NotSerialized)
            {
                If (LEqual (UCCA, Zero))
                {
                    Return (Zero)
                }
                Return (One)
            }

            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (STOR, Zero))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }

            // หน้าต่าง MMIO ต่อเนื่องเดียวครอบ ufs_mem (0x1d84000) + ice + mcq_vs + mcq_sqd ถึง 0x1da7000
            // สอดคล้องกับรูปแบบที่ไดรเวอร์ Qualcomm/Microsoft UFS (ACPI\QCOM24A5) บน Windows ARM64 คาดหวัง
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x01D84000, 0x00023000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 297 }
            })

            Device (DEV0)
            {
                Name (_ADR, 0x00000008)
                Method (_RMV, 0, NotSerialized)
                {
                    Return (Zero)
                }
            }
        }

        Device (GIO0)
        {
            Name (_HID, "QCOM0C0C")
            Name (_UID, Zero)

            // ปิด GIO0 อัตโนมัติเมื่อเปิด TPL0 แบบ Direct TLMM (TSEN != 0) เพื่อป้องกัน Resource Conflict (Code 12)
            Method (_STA, 0, NotSerialized)
            {
                If (LNotEqual (TSEN, Zero))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }

            // ขอบเขต TLMM tile เริ่ม 0x0F100000 สิ้นสุด 0x0F202000 (ตรงตาม pinctrl@f000000 ใน np05j.dtb)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0F100000, 0x00102000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Shared)
                    { 240 }
            })
        }

        Device (UARD)
        {
            Name (_HID, "QCOM0E22")
            Name (_CID, Package (0x02)
            {
                "QCOM0C16",
                "PNP0501"
            })
            Name (_UID, 7)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00A9C000, 0x00004000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 611 }
            })
        }

        // คอนเทนเนอร์ข้อมูลพอร์ต USB Type-C สำหรับใช้ร่วมกันระหว่าง USB0 และ URS0
        Device (UUPC)
        {
            Name (_HID, EisaId ("PNP0A05"))
            Name (_UID, "NP05J USB _UPC Container")
            Name (UPCC, Package (0x04)
            {
                0xFF,   // เชื่อมต่อได้ (Connectable)
                0x09,   // Type-C USB2 + SuperSpeed พร้อมสวิตช์
                Zero,
                Zero
            })
        }

        // โหมดที่ 1 (ค่าเริ่มต้นใน Minimal Bring-up / Host-only):
        // xHCI Host โดยตรงผ่าน Inbox usbxhci.sys (เปิดเมื่อ USBE != 0 และ URSE == 0)
        Device (USB0)
        {
            Name (_HID, "QCOM0E08")
            Name (_CID, "PNP0D10")
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_S0W, 0x03)

            Method (_STA, 0, NotSerialized)
            {
                If (LAnd (LNotEqual (USBE, Zero), LEqual (URSE, Zero)))
                {
                    Return (0x0F)
                }
                Return (Zero)
            }

            Method (_UPC, 0, NotSerialized)
            {
                Return (\_SB.UUPC.UPCC)
            }

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0A600000, 0x00100000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 165 }
            })
        }

        // โหมดที่ 2 (Dual-Role USB Role Switch ผ่าน urssynopsys.sys / Qualcomm qcurs):
        // เปิดเมื่อ USBE != 0 และ URSE != 0 (แยกขาดจาก \_SB.USB0 เพื่อป้องกัน MMIO conflict ที่ 0x0A600000)
        // ค่า MMIO และ GIC INTID ทั้ง 5 ตัวคำนวณจาก ssusb@a600000 + dwc3@a600000 + pdc@b220000 ใน np05j.dtb
        Device (URS0)
        {
            Name (_HID, "QCOM0C8B")
            Name (_CID, Package (0x02)
            {
                "PNP0CA1",
                "QCOMFFE1"
            })
            Name (_UID, Zero)
            Name (_CCA, One)

            Method (_STA, 0, NotSerialized)
            {
                If (LAnd (LNotEqual (USBE, Zero), LNotEqual (URSE, Zero)))
                {
                    Return (0x0F)
                }
                Return (Zero)
            }

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0A600000, 0x00100000)
            })

            // บทบาท Host (xHCI): _ADR = 0
            Device (USB0)
            {
                Name (_ADR, Zero)
                Name (_S0W, 0x03)

                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_UPC, 0, NotSerialized)
                {
                    Return (\_SB.UUPC.UPCC)
                }

                Name (_PLD, Package (0x01)
                {
                    ToPLD (
                        PLD_Revision           = 0x2,
                        PLD_IgnoreColor        = 0x1,
                        PLD_UserVisible        = 0x1,
                        PLD_Panel              = "LEFT",
                        PLD_VerticalPosition   = "CENTER",
                        PLD_HorizontalPosition = "CENTER",
                        PLD_Shape              = "OVAL",
                        PLD_GroupToken         = 0x1,
                        PLD_GroupPosition      = 0x1)
                })

                // ชุด IRQ จาก np05j.dtb:
                // - 0x00A5 (165): dwc3 main IRQ (SPI 0x85 + 32)
                // - 0x00A2 (162): pwr_event_irq (SPI 0x82 + 32)
                // - 0x031A (794): ss_phy_irq    (PDC 0x11 -> SPI 0x2E9+0x11 + 32)
                // - 0x0318 (792): dm_hs_phy_irq (PDC 0x0F -> SPI 0x2E9+0x0F + 32)
                // - 0x0317 (791): dp_hs_phy_irq (PDC 0x0E -> SPI 0x2E9+0x0E + 32)
                Name (_CRS, ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Shared)
                        { 0x000000A5 }
                    Interrupt (ResourceConsumer, Level, ActiveHigh, SharedAndWake)
                        { 0x000000A2 }
                    Interrupt (ResourceConsumer, Level, ActiveHigh, SharedAndWake)
                        { 0x0000031A }
                    Interrupt (ResourceConsumer, Edge, ActiveHigh, SharedAndWake)
                        { 0x00000318 }
                    Interrupt (ResourceConsumer, Edge, ActiveHigh, SharedAndWake)
                        { 0x00000317 }
                })

                Device (RHUB)
                {
                    Name (_ADR, Zero)
                    Device (PRT1)
                    {
                        Name (_ADR, One)
                        Method (_UPC, 0, NotSerialized)
                        {
                            Return (\_SB.UUPC.UPCC)
                        }

                        Name (_PLD, Package (0x01)
                        {
                            ToPLD (
                                PLD_Revision           = 0x2,
                                PLD_IgnoreColor        = 0x1,
                                PLD_UserVisible        = 0x1,
                                PLD_Panel              = "LEFT",
                                PLD_VerticalPosition   = "CENTER",
                                PLD_HorizontalPosition = "CENTER",
                                PLD_Shape              = "OVAL",
                                PLD_GroupToken         = 0x1,
                                PLD_GroupPosition      = 0x1)
                        })
                    }
                }

                // _DSM มาตรฐาน Microsoft USB Controller สำหรับ Synopsys DWC3 xHCI
                Method (_DSM, 4, Serialized)
                {
                    If (LEqual (Arg0, ToUUID ("ce2ee385-00e6-48cb-9f05-2edb927c4899")))
                    {
                        Switch (ToInteger (Arg2))
                        {
                            Case (Zero)
                            {
                                If (LEqual (ToInteger (Arg1), Zero))
                                {
                                    Return (Buffer (One) { 0x1D })
                                }
                                Return (Buffer (One) { 0x01 })
                            }
                            Case (0x02)
                            {
                                Return (Zero)
                            }
                            Case (0x03)
                            {
                                Return (Zero)
                            }
                            Case (0x04)
                            {
                                Return (0x02)
                            }
                        }
                    }
                    Return (Buffer (One) { 0x00 })
                }
            }

            // บทบาท Peripheral / Function (UFX Synopsys): _ADR = 1
            Device (UFN0)
            {
                Name (_ADR, One)
                Name (_S0W, 0x03)

                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_UPC, 0, NotSerialized)
                {
                    Return (\_SB.UUPC.UPCC)
                }

                Name (_PLD, Package (0x01)
                {
                    ToPLD (
                        PLD_Revision           = 0x2,
                        PLD_IgnoreColor        = 0x1,
                        PLD_UserVisible        = 0x1,
                        PLD_Panel              = "LEFT",
                        PLD_VerticalPosition   = "CENTER",
                        PLD_HorizontalPosition = "CENTER",
                        PLD_Shape              = "OVAL",
                        PLD_GroupToken         = 0x1,
                        PLD_GroupPosition      = 0x1)
                })

                Name (_CRS, ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Shared)
                        { 0x000000A5 }
                    Interrupt (ResourceConsumer, Level, ActiveHigh, SharedAndWake)
                        { 0x000000A2 }
                })

                // _DSM สำหรับ ufxsynopsys.sys (จำนวน endpoint และ revision capability ของ DWC3)
                Method (_DSM, 4, Serialized)
                {
                    If (LEqual (Arg0, ToUUID ("fe56cfeb-49d5-4378-a8a2-2978dbe54ad2")))
                    {
                        Switch (ToInteger (Arg2))
                        {
                            Case (Zero)
                            {
                                If (LEqual (ToInteger (Arg1), Zero))
                                {
                                    Return (Buffer (One) { 0x03 })
                                }
                                Return (Buffer (One) { 0x01 })
                            }
                            Case (One)
                            {
                                Return (0x20)
                            }
                        }
                    }
                    ElseIf (LEqual (Arg0, ToUUID ("18de299f-9476-4fc9-b43b-8aeb713ed751")))
                    {
                        Switch (ToInteger (Arg2))
                        {
                            Case (Zero)
                            {
                                If (LEqual (ToInteger (Arg1), Zero))
                                {
                                    Return (Buffer (One) { 0x03 })
                                }
                                Return (Buffer (One) { 0x01 })
                            }
                            Case (One)
                            {
                                Return (0x39)
                            }
                        }
                    }
                    Return (Buffer (One) { 0x00 })
                }
            }
        }

        // PCIe0 Root Complex สำหรับ WCN7861 (FastConnect 7800, PCI\VEN_17CB&DEV_110E)
        // ค่า ECAM (0x40000000), MMIO32 (0x40300000..0x43FFFFFF), และ INTx (181..184) อ้างอิงจาก pcie@1c00000 ใน np05j.dtb
        // หมายเหตุ: ปิดเป็นค่าเริ่มต้น (PCIE=0) จนกว่าจะมี pre-OS PCIe link training DXE หรือไดรเวอร์ Qualcomm PCIe PEP
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_UID, Zero)
            Name (_SEG, Zero)
            Name (_BBN, Zero)
            Name (_CCA, One)
            Name (_S0W, 0x04)

            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (PCIE, Zero))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }

            Method (_CBA, 0, NotSerialized)
            {
                Return (0x0000000040000000)
            }

            Method (_DSW, 3, NotSerialized)
            {
            }

            // ตารางเร้าต์อินเทอร์รัปต์ INT A..D ของ pcie@1c00000 (SPI 0x95..0x98 + 32 = GIC 181..184)
            Name (_PRT, Package (0x04)
            {
                Package (0x04) { 0x0000FFFF, 0x00, Zero, 0x000000B5 },
                Package (0x04) { 0x0000FFFF, 0x01, Zero, 0x000000B6 },
                Package (0x04) { 0x0000FFFF, 0x02, Zero, 0x000000B7 },
                Package (0x04) { 0x0000FFFF, 0x03, Zero, 0x000000B8 }
            })

            // ช่วง Bus 0..1 ตรงตาม MCFG และหน้าต่าง MMIO32 61 MiB แยกออกจากช่วง ECAM (0x40000000..0x401FFFFF)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (
                    ResourceProducer,
                    MinFixed,
                    MaxFixed,
                    PosDecode,
                    0x0000,
                    0x0000,
                    0x0001,
                    0x0000,
                    0x0002
                )
                DWordMemory (
                    ResourceProducer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x00000000,
                    0x40300000,
                    0x43FFFFFF,
                    0x00000000,
                    0x03D00000,
                    ,
                    ,
                    ,
                    AddressRangeMemory,
                    TypeStatic
                )
            })

            Name (SUPP, Zero)
            Name (CTRL, Zero)

            Method (_OSC, 4, NotSerialized)
            {
                CreateDWordField (Arg3, Zero, CDW1)
                If (LEqual (Arg0, ToUUID ("33db4d5b-1ff7-401c-9657-7441c03dd766")))
                {
                    CreateDWordField (Arg3, 0x04, CDW2)
                    CreateDWordField (Arg3, 0x08, CDW3)
                    Store (CDW2, SUPP)
                    Store (CDW3, CTRL)
                    // อนุญาตควบคุม Native PME และ PCIe Capability Structure แบบ conservative (ไม่เปิด AER)
                    And (CTRL, 0x15, CTRL)
                    If (LNotEqual (Arg1, One))
                    {
                        Or (CDW1, 0x08, CDW1)
                    }
                    If (LNotEqual (CDW3, CTRL))
                    {
                        Or (CDW1, 0x10, CDW1)
                    }
                    Store (CTRL, CDW3)
                    Return (Arg3)
                }
                Else
                {
                    Or (CDW1, 0x04, CDW1)
                    Return (Arg3)
                }
            }

            Method (_DSM, 4, Serialized)
            {
                If (LEqual (Arg0, ToUUID ("e5c937d0-3553-4d7a-9117-ea4d19c3434d")))
                {
                    Switch (ToInteger (Arg2))
                    {
                        Case (Zero)
                        {
                            // รองรับ Function 0 และ Function 5 (Ignore PCI Boot Configuration)
                            Return (Buffer (One) { 0x21 })
                        }
                        Case (0x05)
                        {
                            // ให้ OS จัดสรร BAR ภายในช่วง _CRS 0x40300000..0x43FFFFFF เองได้
                            Return (One)
                        }
                    }
                }
                Return (Buffer (One) { 0x00 })
            }

            // Root Port 0 (Bus 0, Dev 0, Fn 0 — SID 0x1400)
            Device (RP00)
            {
                Name (_ADR, Zero)
                Name (_S0W, 0x04)

                Method (_DSW, 3, NotSerialized)
                {
                }

                // เร้าต์อินเทอร์รัปต์สำหรับ Endpoint บน Bus 1 (WCN7861)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0x0000FFFF, 0x00, Zero, 0x000000B5 },
                    Package (0x04) { 0x0000FFFF, 0x01, Zero, 0x000000B6 },
                    Package (0x04) { 0x0000FFFF, 0x02, Zero, 0x000000B7 },
                    Package (0x04) { 0x0000FFFF, 0x03, Zero, 0x000000B8 }
                })

                // Endpoint Wi-Fi 7 Qualcomm WCN7861 (Bus 1, Dev 0, Fn 0 — PCI 17cb:110e, SID 0x1401)
                Device (WCN0)
                {
                    Name (_ADR, Zero)
                    Name (_S0W, 0x04)
                }
            }
        }

        Device (SPI4)
        {
            Name (_HID, "QCOM0E24")
            Name (_CID, "QCOM0C0E")
            Name (_UID, 4)

            Method (_STA, 0, NotSerialized)
            {
                // เปิดเฉพาะเมื่อไม่ได้ใช้ TPL0 แบบ Direct TLMM
                If (LNotEqual (TSEN, Zero))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00A90000, 0x00004000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 389 }
            })
        }

        // จอสัมผัส Synaptics S3930TB0A0 (TCM v2) สำหรับไดรเวอร์ Np05jSynaTouch.sys (KMDF + VHF)
        // ส่งหน้าหน่วยความจำ TLMM 4KB ทั้ง 6 ขาโดยตรง: GPIO 48(MISO), 49(MOSI), 50(CLK), 51(CS), 161(RST), 162(IRQ)
        Device (TPL0)
        {
            Name (_HID, "SYNA3930")
            Name (_UID, Zero)

            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (TSEN, Zero))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0F130000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x0F131000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x0F132000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x0F133000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x0F1A1000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x0F1A2000, 0x00001000)
            })
        }

        Device (PWRB)
        {
            Name (_HID, "PNP0C0C")
            Name (_UID, Zero)

            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (BTNE, Zero))
                {
                    Return (Zero)
                }
                Return (0x0B)
            }
        }

        Device (BTNS)
        {
            Name (_HID, "ACPI0011")
            Name (_UID, Zero)

            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (BTNE, Zero))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }
        }
    }
}
