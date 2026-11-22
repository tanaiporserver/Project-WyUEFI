// UFS-only OS DMA topology. The Android FDT supplies the SMMU aperture and coherent UFS SID; XBL supplies the MMU model and IRQ metadata.

[000h 0000 004h]                   Signature : "IORT"    [IO Remapping Table]
[004h 0004 004h]                Table Length : 0000043C
[008h 0008 001h]                    Revision : 00
[009h 0009 001h]                    Checksum : 17
[00Ah 0010 006h]                      Oem ID : "NUBIA "
[010h 0016 008h]                Oem Table ID : "PAKALA  "
[018h 0024 004h]                Oem Revision : 00008750
[01Ch 0028 004h]             Asl Compiler ID : "INTL"
[020h 0032 004h]       Asl Compiler Revision : 00000000

[024h 0036 004h]                  Node Count : 00000002
[028h 0040 004h]                 Node Offset : 00000030
[02Ch 0044 004h]                    Reserved : 00000000

[030h 0048 001h]                        Type : 03
[031h 0049 002h]                      Length : 03CC
[033h 0051 001h]                    Revision : 01
[034h 0052 004h]                    Reserved : 00000000
[038h 0056 004h]               Mapping Count : 00000000
[03Ch 0060 004h]              Mapping Offset : 00000000

[040h 0064 008h]                Base Address : 0000000015000000
[048h 0072 008h]                        Span : 0000000000100000
[050h 0080 004h]                       Model : 00000003
[054h 0084 004h]       Flags (decoded below) : 00000003
                               DVM Supported : 1
                               Coherent Walk : 1
[058h 0088 004h]     Global Interrupt Offset : 0000003C
[05Ch 0092 004h]     Context Interrupt Count : 00000070
[060h 0096 004h]    Context Interrupt Offset : 0000004C
[064h 0100 004h]         PMU Interrupt Count : 00000000
[068h 0104 004h]        PMU Interrupt Offset : 000003CC

[06Ch 0108 004h]                     NSgIrpt : 00000061
[070h 0112 004h] NSgIrpt Flags (decoded below) : 00000000
                              Edge Triggered : 0
[074h 0116 004h]                  NSgCfgIrpt : 00000000
[078h 0120 004h] NSgCfgIrpt Flags (decoded below) : 00000000
                              Edge Triggered : 0
[07Ch 0124 008h]           Context Interrupt : 0000000000000081
[084h 0132 008h]           Context Interrupt : 0000000000000082
[08Ch 0140 008h]           Context Interrupt : 0000000000000083
[094h 0148 008h]           Context Interrupt : 0000000000000084
[09Ch 0156 008h]           Context Interrupt : 0000000000000085
[0A4h 0164 008h]           Context Interrupt : 0000000000000086
[0ACh 0172 008h]           Context Interrupt : 0000000000000087
[0B4h 0180 008h]           Context Interrupt : 0000000000000088
[0BCh 0188 008h]           Context Interrupt : 0000000000000089
[0C4h 0196 008h]           Context Interrupt : 000000000000008A
[0CCh 0204 008h]           Context Interrupt : 000000000000008B
[0D4h 0212 008h]           Context Interrupt : 000000000000008C
[0DCh 0220 008h]           Context Interrupt : 000000000000008D
[0E4h 0228 008h]           Context Interrupt : 000000000000008E
[0ECh 0236 008h]           Context Interrupt : 000000000000008F
[0F4h 0244 008h]           Context Interrupt : 0000000000000090
[0FCh 0252 008h]           Context Interrupt : 0000000000000091
[104h 0260 008h]           Context Interrupt : 0000000000000092
[10Ch 0268 008h]           Context Interrupt : 0000000000000093
[114h 0276 008h]           Context Interrupt : 0000000000000094
[11Ch 0284 008h]           Context Interrupt : 0000000000000095
[124h 0292 008h]           Context Interrupt : 0000000000000096
[12Ch 0300 008h]           Context Interrupt : 00000000000000D5
[134h 0308 008h]           Context Interrupt : 00000000000000D6
[13Ch 0316 008h]           Context Interrupt : 00000000000000D7
[144h 0324 008h]           Context Interrupt : 00000000000000D8
[14Ch 0332 008h]           Context Interrupt : 00000000000000D9
[154h 0340 008h]           Context Interrupt : 00000000000000DA
[15Ch 0348 008h]           Context Interrupt : 00000000000000DB
[164h 0356 008h]           Context Interrupt : 00000000000000DC
[16Ch 0364 008h]           Context Interrupt : 00000000000000DD
[174h 0372 008h]           Context Interrupt : 00000000000000DE
[17Ch 0380 008h]           Context Interrupt : 00000000000000DF
[184h 0388 008h]           Context Interrupt : 00000000000000E0
[18Ch 0396 008h]           Context Interrupt : 000000000000015B
[194h 0404 008h]           Context Interrupt : 000000000000015C
[19Ch 0412 008h]           Context Interrupt : 000000000000015D
[1A4h 0420 008h]           Context Interrupt : 000000000000015E
[1ACh 0428 008h]           Context Interrupt : 000000000000015F
[1B4h 0436 008h]           Context Interrupt : 0000000000000160
[1BCh 0444 008h]           Context Interrupt : 0000000000000161
[1C4h 0452 008h]           Context Interrupt : 0000000000000162
[1CCh 0460 008h]           Context Interrupt : 0000000000000163
[1D4h 0468 008h]           Context Interrupt : 0000000000000164
[1DCh 0476 008h]           Context Interrupt : 0000000000000165
[1E4h 0484 008h]           Context Interrupt : 0000000000000166
[1ECh 0492 008h]           Context Interrupt : 0000000000000167
[1F4h 0500 008h]           Context Interrupt : 0000000000000168
[1FCh 0508 008h]           Context Interrupt : 0000000000000169
[204h 0516 008h]           Context Interrupt : 000000000000016A
[20Ch 0524 008h]           Context Interrupt : 000000000000016B
[214h 0532 008h]           Context Interrupt : 000000000000016C
[21Ch 0540 008h]           Context Interrupt : 000000000000016D
[224h 0548 008h]           Context Interrupt : 000000000000016E
[22Ch 0556 008h]           Context Interrupt : 000000000000016F
[234h 0564 008h]           Context Interrupt : 0000000000000170
[23Ch 0572 008h]           Context Interrupt : 0000000000000171
[244h 0580 008h]           Context Interrupt : 0000000000000172
[24Ch 0588 008h]           Context Interrupt : 0000000000000173
[254h 0596 008h]           Context Interrupt : 0000000000000174
[25Ch 0604 008h]           Context Interrupt : 0000000000000175
[264h 0612 008h]           Context Interrupt : 0000000000000176
[26Ch 0620 008h]           Context Interrupt : 0000000000000177
[274h 0628 008h]           Context Interrupt : 0000000000000178
[27Ch 0636 008h]           Context Interrupt : 0000000000000179
[284h 0644 008h]           Context Interrupt : 00000000000001AB
[28Ch 0652 008h]           Context Interrupt : 00000000000001AC
[294h 0660 008h]           Context Interrupt : 00000000000001AD
[29Ch 0668 008h]           Context Interrupt : 00000000000001AE
[2A4h 0676 008h]           Context Interrupt : 00000000000001AF
[2ACh 0684 008h]           Context Interrupt : 00000000000001B0
[2B4h 0692 008h]           Context Interrupt : 00000000000001B1
[2BCh 0700 008h]           Context Interrupt : 00000000000001B2
[2C4h 0708 008h]           Context Interrupt : 00000000000001B3
[2CCh 0716 008h]           Context Interrupt : 00000000000001B4
[2D4h 0724 008h]           Context Interrupt : 00000000000001B5
[2DCh 0732 008h]           Context Interrupt : 00000000000001B6
[2E4h 0740 008h]           Context Interrupt : 00000000000001B7
[2ECh 0748 008h]           Context Interrupt : 00000000000001B8
[2F4h 0756 008h]           Context Interrupt : 00000000000001B9
[2FCh 0764 008h]           Context Interrupt : 00000000000001C2
[304h 0772 008h]           Context Interrupt : 00000000000001C3
[30Ch 0780 008h]           Context Interrupt : 00000000000001BC
[314h 0788 008h]           Context Interrupt : 00000000000001C5
[31Ch 0796 008h]           Context Interrupt : 00000000000002E3
[324h 0804 008h]           Context Interrupt : 00000000000001C7
[32Ch 0812 008h]           Context Interrupt : 00000000000001C8
[334h 0820 008h]           Context Interrupt : 00000000000001C9
[33Ch 0828 008h]           Context Interrupt : 00000000000002D2
[344h 0836 008h]           Context Interrupt : 00000000000002D3
[34Ch 0844 008h]           Context Interrupt : 00000000000002D4
[354h 0852 008h]           Context Interrupt : 00000000000002D5
[35Ch 0860 008h]           Context Interrupt : 00000000000002D6
[364h 0868 008h]           Context Interrupt : 00000000000002D7
[36Ch 0876 008h]           Context Interrupt : 00000000000002D8
[374h 0884 008h]           Context Interrupt : 00000000000002D9
[37Ch 0892 008h]           Context Interrupt : 00000000000001BA
[384h 0900 008h]           Context Interrupt : 0000000000000208
[38Ch 0908 008h]           Context Interrupt : 0000000000000209
[394h 0916 008h]           Context Interrupt : 000000000000020A
[39Ch 0924 008h]           Context Interrupt : 000000000000020B
[3A4h 0932 008h]           Context Interrupt : 000000000000020C
[3ACh 0940 008h]           Context Interrupt : 000000000000020D
[3B4h 0948 008h]           Context Interrupt : 000000000000020E
[3BCh 0956 008h]           Context Interrupt : 000000000000020F
[3C4h 0964 008h]           Context Interrupt : 0000000000000210
[3CCh 0972 008h]           Context Interrupt : 0000000000000211
[3D4h 0980 008h]           Context Interrupt : 0000000000000212
[3DCh 0988 008h]           Context Interrupt : 0000000000000213
[3E4h 0996 008h]           Context Interrupt : 0000000000000214
[3ECh 1004 008h]           Context Interrupt : 0000000000000215
[3F4h 1012 008h]           Context Interrupt : 0000000000000216

[3FCh 1020 001h]                        Type : 01
[3FDh 1021 002h]                      Length : 0040
[3FFh 1023 001h]                    Revision : 00
[400h 1024 004h]                    Reserved : 00000000
[404h 1028 004h]               Mapping Count : 00000001
[408h 1032 004h]              Mapping Offset : 0000002C

[40Ch 1036 004h]                  Node Flags : 00000000
[410h 1040 008h]           Memory Properties : [IORT Memory Access Properties]
[410h 1040 004h]             Cache Coherency : 00000001
[414h 1044 001h]       Hints (decoded below) : 00
                                   Transient : 0
                              Write Allocate : 0
                               Read Allocate : 0
                                    Override : 0
[415h 1045 002h]                    Reserved : 0000
[417h 1047 001h] Memory Flags (decoded below) : 00
                                   Coherency : 0
                            Device Attribute : 0
[418h 1048 001h]           Memory Size Limit : 24
[419h 1049 00Bh]                 Device Name : "\_SB_.UFS0"
[424h 1060 004h]                     Padding : 00 00 00 00

[428h 1064 004h]                  Input base : 00000000
[42Ch 1068 004h]                    ID Count : 00000000
[430h 1072 004h]                 Output Base : 00000060
[434h 1076 004h]            Output Reference : 00000030
[438h 1080 004h]       Flags (decoded below) : 00000001
                              Single Mapping : 1
