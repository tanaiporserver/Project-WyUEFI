/*
 * Serial Port Console Redirection Table (SPCR Rev 4) สำหรับ nubia NP05J (SM8750P)
 * ชี้ไปยัง QUPv3 SE7 GENI Debug UART @ 0x00A9C000, GIC GSIV 611 (0x243 + 32)
 */
[0004]                          Signature : "SPCR"
[0004]                       Table Length : 00000062
[0001]                           Revision : 04
[0001]                           Checksum : 00
[0006]                             Oem ID : "NUBIA "
[0008]                       Oem Table ID : "NP05J   "
[0004]                       Oem Revision : 00008750
[0004]                    Asl Compiler ID : "INTL"
[0004]              Asl Compiler Revision : 20250404

[0001]                     Interface Type : 13
[0003]                           Reserved : 000000

[0012]               Serial Port Register : [Generic Address Structure]
[0001]                           Space ID : 00 [SystemMemory]
[0001]                          Bit Width : 20
[0001]                         Bit Offset : 00
[0001]               Encoded Access Width : 03 [DWord Access:32]
[0008]                            Address : 0000000000A9C000

[0001]                     Interrupt Type : 08
[0001]                PCAT-compatible IRQ : 00
[0004]                          Interrupt : 00000263
[0001]                          Baud Rate : 07
[0001]                             Parity : 00
[0001]                          Stop Bits : 01
[0001]                       Flow Control : 00
[0001]                      Terminal Type : 03
[0001]                           Language : 00
[0002]                      PCI Device ID : FFFF
[0002]                      PCI Vendor ID : FFFF
[0001]                            PCI Bus : 00
[0001]                         PCI Device : 00
[0001]                       PCI Function : 00
[0004]                          PCI Flags : 00000000
[0001]                        PCI Segment : 00
[0004]                    Uart Clock Freq : 00000000
[0004]                  Precise Baud rate : 00000000
[0002]              NameSpaceStringLength : 000A
[0002]              NameSpaceStringOffset : 0058
[0010]                    NamespaceString : "\_SB.UARD"
