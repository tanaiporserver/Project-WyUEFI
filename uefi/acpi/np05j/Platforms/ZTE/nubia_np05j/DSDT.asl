// DSDT for nubia NP05J (REDMAGIC Astra, Qualcomm SM8750P / Pakala)
// Supports Linux ACPI and Windows 11 on ARM with runtime-patchable flags via Np05jAcpiUpdateLib.
DefinitionBlock ("", "DSDT", 2, "NUBIA ", "NP05J   ", 0x00008750)
{
    Scope (\_SB)
    {
        // Runtime-patched platform identity & BIOS feature gates.
        // IMPORTANT: Initialized to non-0/1 values so iASL emits full-width AML prefixes
        // required by AslUpdateLib::AslUpdateName().
        Name (SOID, 0x12345678)          // DWORD: SoC ID (639 for SM8750P)
        Name (SIDV, 0x12345678)          // DWORD: SoC Revision (0x00020000)
        Name (SVMJ, 0x1234)              // WORD:  SoC Version Major (2)
        Name (SVMI, 0x1234)              // WORD:  SoC Version Minor (0)
        Name (SOSN, 0x1122334455667788)  // QWORD: Board / Serial Number
        Name (STOR, 0x00010001)          // DWORD: UFS0 enable/coherency
        Name (USBE, 0x00010001)          // DWORD: USB0 enable (AcpiUsbEnable)
        Name (TSEN, 0x00010001)          // DWORD: SPI4 / SYNA3930 touch enable (AcpiTouchEnable)
        Name (PCIE, 0x00010001)          // DWORD: PCIe0 gate (AcpiPcieEnable)
        Name (BTNE, 0x00010001)          // DWORD: Buttons / PWRB gate (AcpiButtonsEnable)

        Device (CPU0)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)
        }

        Device (CPU1)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 1)
        }

        Device (CPU2)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)
        }

        Device (CPU3)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
        }

        Device (CPU4)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 4)
        }

        Device (CPU5)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 5)
        }

        Device (CPU6)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)
        }

        Device (CPU7)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)
        }

        Device (UFS0)
        {
            Name (_HID, "QCOM24A5")
            Name (_UID, Zero)
            Name (_CCA, One)

            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (STOR, Zero))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x01D84000, 0x00003000)  // ufs_mem
                Memory32Fixed (ReadWrite, 0x01D88000, 0x00018000)  // ice
                Memory32Fixed (ReadWrite, 0x01DA5000, 0x00002000)  // mcq_sqd
                Memory32Fixed (ReadWrite, 0x01DA4000, 0x00000010)  // mcq_vs
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 297 }
            })

            Device (DEV0)
            {
                Name (_ADR, 0x00000008)
            }
        }

        Device (GIO0)
        {
            Name (_HID, "QCOM0C0C")
            Name (_UID, Zero)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0F100000, 0x00C00000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Shared)
                    { 240 }
            })
        }

        Device (UARD)
        {
            Name (_HID, "QCOM0E22")
            Name (_CID, "PNP0501")
            Name (_UID, 7)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00A9C000, 0x00004000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 611 }
            })
        }

        Device (USB0)
        {
            Name (_HID, "QCOM0E08")
            Name (_CID, "PNP0D10")
            Name (_UID, Zero)
            Name (_CCA, One)

            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (USBE, Zero))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0A600000, 0x00100000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 165 }
            })
        }

        Device (SPI4)
        {
            Name (_HID, "QCOM0E24")
            Name (_UID, 4)

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
                Memory32Fixed (ReadWrite, 0x00A90000, 0x00004000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 389 }
            })

            Device (TPL0)
            {
                Name (_HID, "SYNA3930")
                Name (_CID, "PNP0C50")
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
                    SpiSerialBusV2 (
                        0,                      // DeviceSelection (CS0 / GPIO 51)
                        PolarityLow,
                        FourWireMode,
                        8,                      // DataBitLength
                        ControllerInitiated,
                        16000000,               // 16 MHz max SPI clock
                        ClockPolarityLow,
                        ClockPhaseFirst,
                        "\\_SB.SPI4",
                        0,
                        ResourceConsumer
                    )
                    GpioInt (Level, ActiveLow, ExclusiveAndWake, PullUp, 0, "\\_SB.GIO0", 0, ResourceConsumer)
                        { 162 }
                })
            }
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
