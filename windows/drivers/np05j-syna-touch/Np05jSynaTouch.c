/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  ไดรเวอร์จอสัมผัส Synaptics S3930TB0A0 (TCM v2) สำหรับ Windows 11 / WinPE ARM64 บน nubia NP05J

  ใช้โปรโตคอล TCM v2 เดียวกับ UEFI (Np05jSynapticsTcm2.h) และสื่อสารผ่าน TLMM GPIO bit-bang SPI Mode 0
  ร่วมกับ Microsoft Virtual HID Framework (vhf.sys) เพื่อทำงานได้ทันทีใน WinPE โดยไม่ต้องพึ่ง GENI SPI/PEP
**/

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

#define MAX_UINT8 0xFFU
#include "../../../uefi/platforms/np05jPkg/Include/Np05jSynapticsTcm2.h"

#define NP05J_TOUCH_POOL_TAG    'hTpN'
#define NP05J_TLMM_PIN_COUNT    6U

/* ลำดับหน้าหน่วยความจำ 4KB ที่ได้รับจาก ACPI \_SB.TPL0._CRS */
#define PIN_IDX_MISO            0U  /* GPIO 48 (0x0F130000) */
#define PIN_IDX_MOSI            1U  /* GPIO 49 (0x0F131000) */
#define PIN_IDX_CLK             2U  /* GPIO 50 (0x0F132000) */
#define PIN_IDX_CS              3U  /* GPIO 51 (0x0F133000) */
#define PIN_IDX_RST             4U  /* GPIO 161 (0x0F1A1000) */
#define PIN_IDX_IRQ             5U  /* GPIO 162 (0x0F1A2000) */

#define TLMM_OFF_CTL            0x0000U
#define TLMM_OFF_IO             0x0004U
#define TLMM_OFF_INTR_CFG       0x0008U

#define TLMM_CTL_FIELDS         0x000003FFU
#define TLMM_OE_BIT             (1U << 9)
#define TLMM_EGPIO_PRESENT      (1U << 11)
#define TLMM_EGPIO_ENABLE       (1U << 12)
#define TLMM_IO_OUT_BIT         (1U << 1)
#define TLMM_PULL_NONE          0U
#define TLMM_PULL_DOWN          1U
#define TLMM_PULL_UP            3U

#define HID_REPORT_ID_TOUCH     0x01U
#define HID_REPORT_ID_MAX_CNT   0x02U
#define HID_REPORT_ID_THQA      0x03U
#define HID_MAX_SLOTS           5U

#define NP05J_DISP_WIDTH        2400U
#define NP05J_DISP_HEIGHT       1504U

#pragma pack(push, 1)
typedef struct _HID_TOUCH_CONTACT {
    UCHAR  Flags;       /* bit0: Tip Switch, bit1: In Range */
    UCHAR  ContactId;
    USHORT X;           /* 0..2399 */
    USHORT Y;           /* 0..1503 */
} HID_TOUCH_CONTACT;

typedef struct _HID_TOUCH_INPUT_REPORT {
    UCHAR             ReportId;
    HID_TOUCH_CONTACT Contacts[HID_MAX_SLOTS];
    USHORT            ScanTime;
    UCHAR             ContactCount;
} HID_TOUCH_INPUT_REPORT;
#pragma pack(pop)

/* รายละเอียด HID Descriptor มาตรฐาน Windows Multi-Touch Touchscreen (5 จุดต่อรายงาน) */
#define HID_CONTACT_SLOT_DESC \
    0x09, 0x22,                   /* Usage (Finger) */ \
    0xA1, 0x02,                   /* Collection (Logical) */ \
    0x09, 0x42,                   /*   Usage (Tip Switch) */ \
    0x09, 0x32,                   /*   Usage (In Range) */ \
    0x15, 0x00,                   /*   Logical Minimum (0) */ \
    0x25, 0x01,                   /*   Logical Maximum (1) */ \
    0x75, 0x01,                   /*   Report Size (1) */ \
    0x95, 0x02,                   /*   Report Count (2) */ \
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */ \
    0x75, 0x06,                   /*   Report Size (6) padding */ \
    0x95, 0x01,                   /*   Report Count (1) */ \
    0x81, 0x03,                   /*   Input (Const,Var,Abs) */ \
    0x09, 0x51,                   /*   Usage (Contact Identifier) */ \
    0x75, 0x08,                   /*   Report Size (8) */ \
    0x95, 0x01,                   /*   Report Count (1) */ \
    0x25, 0x09,                   /*   Logical Maximum (9) */ \
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */ \
    0x05, 0x01,                   /*   Usage Page (Generic Desktop) */ \
    0x09, 0x30,                   /*   Usage (X) */ \
    0x26, 0x5F, 0x09,             /*   Logical Maximum (2399) */ \
    0x46, 0x9E, 0x07,             /*   Physical Maximum (1950 = 195.0 mm) */ \
    0x55, 0x0E,                   /*   Unit Exponent (-2 -> 0.1 mm) */ \
    0x65, 0x11,                   /*   Unit (SI Linear: cm) */ \
    0x75, 0x10,                   /*   Report Size (16) */ \
    0x95, 0x01,                   /*   Report Count (1) */ \
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */ \
    0x09, 0x31,                   /*   Usage (Y) */ \
    0x26, 0xDF, 0x05,             /*   Logical Maximum (1503) */ \
    0x46, 0xC4, 0x04,             /*   Physical Maximum (1220 = 122.0 mm) */ \
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */ \
    0x05, 0x0D,                   /*   Usage Page (Digitizers) */ \
    0xC0                          /* End Collection */

static UCHAR gHidReportDescriptor[] = {
    0x05, 0x0D,                   /* Usage Page (Digitizers) */
    0x09, 0x04,                   /* Usage (Touch Screen) */
    0xA1, 0x01,                   /* Collection (Application) */
    0x85, HID_REPORT_ID_TOUCH,    /*   Report ID (1) */
    HID_CONTACT_SLOT_DESC,
    HID_CONTACT_SLOT_DESC,
    HID_CONTACT_SLOT_DESC,
    HID_CONTACT_SLOT_DESC,
    HID_CONTACT_SLOT_DESC,
    0x05, 0x0D,                   /*   Usage Page (Digitizers) */
    0x09, 0x56,                   /*   Usage (Scan Time) */
    0x55, 0x0C,                   /*   Unit Exponent (-4 -> 100 us) */
    0x66, 0x01, 0x10,             /*   Unit (Seconds) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x27, 0xFF, 0xFF, 0x00, 0x00, /*   Logical Maximum (65535) */
    0x75, 0x10,                   /*   Report Size (16) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */
    0x09, 0x54,                   /*   Usage (Contact Count) */
    0x25, 0x0A,                   /*   Logical Maximum (10) */
    0x75, 0x08,                   /*   Report Size (8) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */
    0x85, HID_REPORT_ID_MAX_CNT,  /*   Report ID (2) Feature: Contact Count Maximum */
    0x09, 0x55,                   /*   Usage (Contact Count Maximum) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x0A,                   /*   Logical Maximum (10) */
    0x75, 0x08,                   /*   Report Size (8) */
    0x95, 0x01,                   /*   Report Count (1) */
    0xB1, 0x02,                   /*   Feature (Data,Var,Abs) */
    0x85, HID_REPORT_ID_THQA,     /*   Report ID (3) Feature: Windows THQA Cert */
    0x06, 0x00, 0xFF,             /*   Usage Page (Vendor Defined 0xFF00) */
    0x09, 0xC5,                   /*   Usage (0xC5) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,             /*   Logical Maximum (255) */
    0x75, 0x08,                   /*   Report Size (8) */
    0x96, 0x00, 0x01,             /*   Report Count (256) */
    0xB1, 0x02,                   /*   Feature (Data,Var,Abs) */
    0xC0                          /* End Collection */
};

typedef struct _NP05J_TOUCH_CTX {
    WDFDEVICE        Device;
    VHFHANDLE        VhfHandle;
    WDFTIMER         PollTimer;

    PUCHAR           PinBase[NP05J_TLMM_PIN_COUNT];
    SIZE_T           PinLen[NP05J_TLMM_PIN_COUNT];
    ULONG            SavedCtl[NP05J_TLMM_PIN_COUNT];
    ULONG            SavedIo[NP05J_TLMM_PIN_COUNT];
    ULONG            SavedIrqCfg;
    BOOLEAN          PinsMapped;
    BOOLEAN          PinsAcquired;
    BOOLEAN          Running;

    /* ค่าตั้งต้นจาก Registry */
    ULONG            SwapXY;
    ULONG            InvertX;
    ULONG            InvertY;
    ULONG            PollIntervalMs;
    ULONG            SkipReset;

    /* สถานะ Synaptics TCM v2 */
    UCHAR            Seq;
    USHORT           MaxX;
    USHORT           MaxY;
    USHORT           MaxObjects;
    UCHAR            ReportCfg[TCM2_MAX_REPORT_CFG];
    USHORT           ReportCfgLen;
    UCHAR            TxBuf[TCM2_MAX_XFER];
    UCHAR            RxBuf[TCM2_MAX_XFER];
    UCHAR            ReportBuf[TCM2_MAX_XFER];
    TCM2_TOUCH_STATE TcmState;

    /* ตัวนับและข้อมูลวินิจฉัย */
    ULONG            BuildId;
    ULONG            IrqCount;
    ULONG            FrameCount;
    ULONG            ErrorCount;
    ULONG            LastX;
    ULONG            LastY;
    ULONG            LastPressed;
    BOOLEAN          HadActiveContacts;
} NP05J_TOUCH_CTX, *PNP05J_TOUCH_CTX;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NP05J_TOUCH_CTX, Np05jTouchGetContext)

static VOID
WriteDiagDword(
    _In_ PCWSTR Name,
    _In_ ULONG  Value
    )
{
    UNICODE_STRING keyPath;
    UNICODE_STRING valName;
    OBJECT_ATTRIBUTES objAttr;
    HANDLE hKey = NULL;

    RtlInitUnicodeString(
        &keyPath,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\Np05jSynaTouch\\Parameters"
    );
    InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(ZwCreateKey(&hKey, KEY_WRITE, &objAttr, 0, NULL, REG_OPTION_NON_VOLATILE, NULL))) {
        RtlInitUnicodeString(&valName, Name);
        ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &Value, sizeof(Value));
        ZwClose(hKey);
    }
}

static ULONG
ReadParamDword(
    _In_ PCWSTR Name,
    _In_ ULONG  DefaultValue
    )
{
    UNICODE_STRING keyPath;
    UNICODE_STRING valName;
    OBJECT_ATTRIBUTES objAttr;
    HANDLE hKey = NULL;
    UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    ULONG resultLen = 0;
    ULONG out = DefaultValue;

    RtlInitUnicodeString(
        &keyPath,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\Np05jSynaTouch\\Parameters"
    );
    InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &objAttr))) {
        RtlInitUnicodeString(&valName, Name);
        if (NT_SUCCESS(ZwQueryValueKey(hKey, &valName, KeyValuePartialInformation, buf, sizeof(buf), &resultLen))) {
            PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
            if ((info->Type == REG_DWORD) && (info->DataLength == sizeof(ULONG))) {
                RtlCopyMemory(&out, info->Data, sizeof(ULONG));
            }
        }
        ZwClose(hKey);
    }
    return out;
}

static VOID
SleepMs(
    _In_ ULONG Millis
    )
{
    LARGE_INTEGER interval;
    interval.QuadPart = -((LONGLONG)Millis * 10000LL);
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

static __inline ULONG
PinRead(
    _In_ PNP05J_TOUCH_CTX Ctx,
    _In_ ULONG            Idx,
    _In_ ULONG            Offset
    )
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Ctx->PinBase[Idx] + Offset));
}

static __inline VOID
PinWrite(
    _In_ PNP05J_TOUCH_CTX Ctx,
    _In_ ULONG            Idx,
    _In_ ULONG            Offset,
    _In_ ULONG            Val
    )
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Ctx->PinBase[Idx] + Offset), Val);
}

static __inline VOID
DrivePin(
    _In_ PNP05J_TOUCH_CTX Ctx,
    _In_ ULONG            Idx,
    _In_ BOOLEAN          High
    )
{
    PinWrite(Ctx, Idx, TLMM_OFF_IO, High ? TLMM_IO_OUT_BIT : 0U);
}

static ULONG
BuildGpioCtl(
    _In_ ULONG   OrigCtl,
    _In_ BOOLEAN Output,
    _In_ ULONG   Pull,
    _In_ ULONG   DriveMaBits
    )
{
    ULONG val = (OrigCtl & ~(TLMM_CTL_FIELDS | TLMM_EGPIO_ENABLE)) |
                (DriveMaBits << 6) | Pull | (Output ? TLMM_OE_BIT : 0U);
    if ((val & TLMM_EGPIO_PRESENT) != 0U) {
        val |= TLMM_EGPIO_ENABLE;
    }
    return val;
}

static __inline VOID
SpiBitDelay(VOID)
{
    KeStallExecutionProcessor(1);
}

static UCHAR
SpiByte(
    _In_ PNP05J_TOUCH_CTX Ctx,
    _In_ UCHAR            Tx
    )
{
    UCHAR rx = 0;
    for (ULONG bit = 0; bit < 8U; bit++) {
        DrivePin(Ctx, PIN_IDX_MOSI, (Tx & 0x80U) != 0U);
        Tx = (UCHAR)(Tx << 1);
        SpiBitDelay();
        DrivePin(Ctx, PIN_IDX_CLK, TRUE);
        SpiBitDelay();
        rx = (UCHAR)((rx << 1) | (PinRead(Ctx, PIN_IDX_MISO, TLMM_OFF_IO) & 1U));
        DrivePin(Ctx, PIN_IDX_CLK, FALSE);
    }
    return rx;
}

static VOID
SpiTransfer(
    _In_     PNP05J_TOUCH_CTX Ctx,
    _In_opt_ const UCHAR     *Tx,
    _Out_opt_ UCHAR          *Rx,
    _In_     ULONG            Len
    )
{
    DrivePin(Ctx, PIN_IDX_CS, FALSE);
    KeStallExecutionProcessor(12); /* DTS spi-cs-setup-ns = 11000 ns */
    for (ULONG i = 0; i < Len; i++) {
        UCHAR bOut = (Tx != NULL) ? Tx[i] : 0xFFU;
        UCHAR bIn  = SpiByte(Ctx, bOut);
        if (Rx != NULL) {
            Rx[i] = bIn;
        }
    }
    DrivePin(Ctx, PIN_IDX_CS, TRUE);
    KeStallExecutionProcessor(4);
}

static NTSTATUS
Tcm2SendPacket(
    _In_     PNP05J_TOUCH_CTX Ctx,
    _In_     UCHAR            Cmd,
    _In_opt_ const UCHAR     *Payload,
    _In_     USHORT           PayloadLen
    )
{
    tcm2_u32_t total = Tcm2BuildPacket(Ctx->TxBuf, sizeof(Ctx->TxBuf), Cmd, Ctx->Seq, Payload, PayloadLen);
    if (total == 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    SpiTransfer(Ctx, Ctx->TxBuf, Ctx->RxBuf, total);
    return STATUS_SUCCESS;
}

static NTSTATUS
Tcm2ReadPacket(
    _In_  PNP05J_TOUCH_CTX Ctx,
    _In_  ULONG            ReadLen,
    _Out_ UCHAR           *CodeOut,
    _Out_opt_ UCHAR       *PayloadOut,
    _In_  USHORT           MaxPayload,
    _Out_ USHORT          *PayloadLenOut
    )
{
    USHORT plen;

    if (ReadLen < TCM2_HDR_SIZE) {
        ReadLen = TCM2_HDR_SIZE;
    }
    if (ReadLen > TCM2_MAX_XFER) {
        ReadLen = TCM2_MAX_XFER;
    }

    RtlFillMemory(Ctx->TxBuf, ReadLen, 0xFFU);
    SpiTransfer(Ctx, Ctx->TxBuf, Ctx->RxBuf, ReadLen);

    if (!Tcm2HdrCrcOk(Ctx->RxBuf)) {
        return STATUS_CRC_ERROR;
    }

    plen = Tcm2GetLe16(Ctx->RxBuf + 1);
    *CodeOut       = Ctx->RxBuf[0];
    *PayloadLenOut = plen;

    if ((plen > 0U) && (PayloadOut != NULL) && (MaxPayload > 0U)) {
        ULONG avail = (ReadLen > TCM2_HDR_SIZE) ? (ReadLen - TCM2_HDR_SIZE) : 0U;
        USHORT copyLen = (plen < MaxPayload) ? plen : MaxPayload;
        if (copyLen > avail) {
            copyLen = (USHORT)avail;
        }
        RtlCopyMemory(PayloadOut, Ctx->RxBuf + TCM2_HDR_SIZE, copyLen);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Tcm2ReadReportFast(
    _In_  PNP05J_TOUCH_CTX Ctx,
    _Out_ UCHAR           *CodeOut,
    _Out_opt_ UCHAR       *PayloadOut,
    _In_  USHORT           MaxPayload,
    _Out_ USHORT          *PayloadLenOut
    )
{
    USHORT plen;
    ULONG tailLen;

    DrivePin(Ctx, PIN_IDX_CS, FALSE);
    KeStallExecutionProcessor(12);
    for (ULONG i = 0; i < TCM2_HDR_SIZE; i++) {
        Ctx->RxBuf[i] = SpiByte(Ctx, 0xFFU);
    }

    if (!Tcm2HdrCrcOk(Ctx->RxBuf)) {
        DrivePin(Ctx, PIN_IDX_CS, TRUE);
        KeStallExecutionProcessor(4);
        return STATUS_CRC_ERROR;
    }

    plen = Tcm2GetLe16(Ctx->RxBuf + 1);
    if ((Ctx->RxBuf[0] == TCM2_STATUS_WAIT) ||
        (Ctx->RxBuf[0] == TCM2_STATUS_IDLE) ||
        (Ctx->RxBuf[0] == TCM2_STATUS_INVALID))
    {
        DrivePin(Ctx, PIN_IDX_CS, TRUE);
        KeStallExecutionProcessor(4);
        *CodeOut       = Ctx->RxBuf[0];
        *PayloadLenOut = 0U;
        return STATUS_SUCCESS;
    }

    tailLen = (plen > 0U) ? ((ULONG)plen + TCM2_CRC16_SIZE) : 0U;
    if (tailLen > (TCM2_MAX_XFER - TCM2_HDR_SIZE)) {
        tailLen = TCM2_MAX_XFER - TCM2_HDR_SIZE;
    }
    for (ULONG i = 0; i < tailLen; i++) {
        Ctx->RxBuf[TCM2_HDR_SIZE + i] = SpiByte(Ctx, 0xFFU);
    }
    DrivePin(Ctx, PIN_IDX_CS, TRUE);
    KeStallExecutionProcessor(4);

    *CodeOut       = Ctx->RxBuf[0];
    *PayloadLenOut = plen;
    if ((plen > 0U) && (PayloadOut != NULL) && (MaxPayload > 0U)) {
        USHORT copyLen = (plen < MaxPayload) ? plen : MaxPayload;
        if (copyLen > (TCM2_MAX_XFER - TCM2_HDR_SIZE)) {
            copyLen = (USHORT)(TCM2_MAX_XFER - TCM2_HDR_SIZE);
        }
        RtlCopyMemory(PayloadOut, Ctx->RxBuf + TCM2_HDR_SIZE, copyLen);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Tcm2Command(
    _In_     PNP05J_TOUCH_CTX Ctx,
    _In_     UCHAR            Cmd,
    _In_opt_ const UCHAR     *TxPayload,
    _In_     USHORT           TxLen,
    _Out_    UCHAR           *RespCode,
    _Out_opt_ UCHAR          *RxPayload,
    _In_     USHORT           RxMax,
    _Out_    USHORT          *RxLen,
    _In_     ULONG            ReadSize,
    _In_     ULONG            WaitMs
    )
{
    UCHAR code = 0;
    USHORT plen = 0;
    NTSTATUS status;

    for (ULONG attempt = 0; attempt < 3U; attempt++) {
        status = Tcm2SendPacket(Ctx, Cmd, TxPayload, TxLen);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        for (ULONG poll = 0; poll < 15U; poll++) {
            if (WaitMs > 0U) {
                SleepMs(WaitMs);
            }
            status = Tcm2ReadPacket(Ctx, ReadSize, &code, RxPayload, RxMax, &plen);
            if (!NT_SUCCESS(status)) {
                SleepMs(2);
                continue;
            }
            if (code == TCM2_STATUS_WAIT) {
                SleepMs(3);
                continue;
            }
            if (code == TCM2_STATUS_INVALID) {
                Ctx->Seq ^= 1U;
                break;
            }
            Ctx->Seq ^= 1U;
            *RespCode = code;
            *RxLen    = plen;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_IO_DEVICE_ERROR;
}

static NTSTATUS
AcquireTlmmPins(
    _In_ PNP05J_TOUCH_CTX Ctx
    )
{
    for (ULONG i = 0; i < NP05J_TLMM_PIN_COUNT; i++) {
        Ctx->SavedCtl[i] = PinRead(Ctx, i, TLMM_OFF_CTL);
        Ctx->SavedIo[i]  = PinRead(Ctx, i, TLMM_OFF_IO);
        if (Ctx->SavedCtl[i] == 0xFFFFFFFFU) {
            return STATUS_DEVICE_DOES_NOT_EXIST;
        }
    }
    Ctx->SavedIrqCfg  = PinRead(Ctx, PIN_IDX_IRQ, TLMM_OFF_INTR_CFG);
    Ctx->PinsAcquired = TRUE;

    /* ตั้งขา MISO เป็น input pull-down */
    PinWrite(Ctx, PIN_IDX_MISO, TLMM_OFF_CTL, BuildGpioCtl(Ctx->SavedCtl[PIN_IDX_MISO], FALSE, TLMM_PULL_DOWN, 2U));

    /* กำหนดระดับเริ่มต้น SPI Mode 0 ก่อนเปิด output */
    DrivePin(Ctx, PIN_IDX_CS, TRUE);
    DrivePin(Ctx, PIN_IDX_CLK, FALSE);
    DrivePin(Ctx, PIN_IDX_MOSI, FALSE);
    DrivePin(Ctx, PIN_IDX_RST, TRUE);
    KeMemoryBarrier();

    PinWrite(Ctx, PIN_IDX_MOSI, TLMM_OFF_CTL, BuildGpioCtl(Ctx->SavedCtl[PIN_IDX_MOSI], TRUE, TLMM_PULL_NONE, 2U));
    PinWrite(Ctx, PIN_IDX_CLK,  TLMM_OFF_CTL, BuildGpioCtl(Ctx->SavedCtl[PIN_IDX_CLK],  TRUE, TLMM_PULL_NONE, 2U));
    PinWrite(Ctx, PIN_IDX_CS,   TLMM_OFF_CTL, BuildGpioCtl(Ctx->SavedCtl[PIN_IDX_CS],   TRUE, TLMM_PULL_UP,   2U));
    PinWrite(Ctx, PIN_IDX_RST,  TLMM_OFF_CTL, BuildGpioCtl(Ctx->SavedCtl[PIN_IDX_RST],  TRUE, TLMM_PULL_UP,   2U));

    /* ปิด summary/PDC interrupt บนขา IRQ เพื่ออ่านแบบ polling ที่ปลอดภัยใน WinPE */
    PinWrite(Ctx, PIN_IDX_IRQ, TLMM_OFF_INTR_CFG, Ctx->SavedIrqCfg & ~((1U << 0) | (1U << 7)));
    PinWrite(Ctx, PIN_IDX_IRQ, TLMM_OFF_CTL, BuildGpioCtl(Ctx->SavedCtl[PIN_IDX_IRQ], FALSE, TLMM_PULL_UP, 2U));
    KeMemoryBarrier();

    return STATUS_SUCCESS;
}

static NTSTATUS
InitSynapticsS3930(
    _In_ PNP05J_TOUCH_CTX Ctx
    )
{
    NTSTATUS status;
    UCHAR buf[TCM2_MAX_REPORT_CFG];
    UCHAR code = 0;
    USHORT plen = 0;
    UCHAR enableTouch = TCM2_REPORT_TOUCH;

    Ctx->Seq = 0;
    RtlZeroMemory(&Ctx->TcmState, sizeof(Ctx->TcmState));

    WriteDiagDword(L"Stage", 0x14U);
    if (Ctx->SkipReset == 0U) {
        DrivePin(Ctx, PIN_IDX_RST, FALSE);
        SleepMs(20);
        DrivePin(Ctx, PIN_IDX_RST, TRUE);
        SleepMs(200);
    }

    /* ล้าง identify report ที่ค้างอยู่หลังรีเซ็ต */
    Tcm2SendPacket(Ctx, TCM2_CMD_ACK, NULL, 0);
    SleepMs(8);
    Tcm2ReadPacket(Ctx, 64U, &code, buf, sizeof(buf), &plen);

    /* 1. CMD_IDENTIFY */
    WriteDiagDword(L"Stage", 0x1EU);
    RtlZeroMemory(buf, sizeof(buf));
    status = Tcm2Command(Ctx, TCM2_CMD_IDENTIFY, NULL, 0, &code, buf, sizeof(buf), &plen, 64U, 15U);
    if (!NT_SUCCESS(status) || ((code != TCM2_STATUS_OK) && (code != TCM2_REPORT_IDENTIFY)) || (plen < 24U)) {
        WriteDiagDword(L"Status", (ULONG)STATUS_DEVICE_NOT_READY);
        return STATUS_DEVICE_NOT_READY;
    }

    Ctx->BuildId = Tcm2GetLe32(buf + 18);
    WriteDiagDword(L"BuildId", Ctx->BuildId);

    /* 2. CMD_GET_APP_INFO */
    WriteDiagDword(L"Stage", 0x20U);
    RtlZeroMemory(buf, sizeof(buf));
    status = Tcm2Command(Ctx, TCM2_CMD_GET_APP_INFO, NULL, 0, &code, buf, sizeof(buf), &plen, 80U, 15U);
    if (NT_SUCCESS(status) && (code == TCM2_STATUS_OK) && (plen >= 38U)) {
        Ctx->MaxX       = Tcm2GetLe16(buf + 32);
        Ctx->MaxY       = Tcm2GetLe16(buf + 34);
        Ctx->MaxObjects = Tcm2GetLe16(buf + 36);
    }
    if ((Ctx->MaxX == 0U) || (Ctx->MaxY == 0U)) {
        Ctx->MaxX = TCM2_DEFAULT_MAX_X;
        Ctx->MaxY = TCM2_DEFAULT_MAX_Y;
    }
    if ((Ctx->MaxObjects == 0U) || (Ctx->MaxObjects > TCM2_MAX_OBJECTS)) {
        Ctx->MaxObjects = TCM2_MAX_OBJECTS;
    }
    WriteDiagDword(L"MaxX", Ctx->MaxX);
    WriteDiagDword(L"MaxY", Ctx->MaxY);

    /* 3. CMD_GET_TOUCH_REPORT_CFG */
    WriteDiagDword(L"Stage", 0x28U);
    RtlZeroMemory(buf, sizeof(buf));
    status = Tcm2Command(Ctx, TCM2_CMD_GET_TOUCH_REPORT_CFG, NULL, 0, &code, buf, sizeof(buf), &plen, 256U, 15U);
    if (!NT_SUCCESS(status) || (code != TCM2_STATUS_OK) || (plen == 0U)) {
        WriteDiagDword(L"Status", (ULONG)STATUS_DEVICE_DATA_ERROR);
        return STATUS_DEVICE_DATA_ERROR;
    }
    Ctx->ReportCfgLen = (plen < TCM2_MAX_REPORT_CFG) ? plen : TCM2_MAX_REPORT_CFG;
    RtlCopyMemory(Ctx->ReportCfg, buf, Ctx->ReportCfgLen);
    WriteDiagDword(L"CfgLen", Ctx->ReportCfgLen);

    /* 4. CMD_ENABLE_REPORT (0x11) */
    WriteDiagDword(L"Stage", 0x32U);
    status = Tcm2Command(Ctx, TCM2_CMD_ENABLE_REPORT, &enableTouch, 1U, &code, buf, sizeof(buf), &plen, 32U, 15U);
    if (!NT_SUCCESS(status)) {
        WriteDiagDword(L"Status", (ULONG)status);
        return status;
    }

    WriteDiagDword(L"Stage", 0x64U);
    WriteDiagDword(L"Status", 0U);
    return STATUS_SUCCESS;
}

static VOID
SubmitTouchHidReport(
    _In_ PNP05J_TOUCH_CTX Ctx
    )
{
    HID_TOUCH_INPUT_REPORT report;
    HID_XFER_PACKET packet;
    UCHAR slot = 0;
    ULONGLONG ticks100us;

    if (Ctx->VhfHandle == NULL) {
        return;
    }

    RtlZeroMemory(&report, sizeof(report));
    report.ReportId = HID_REPORT_ID_TOUCH;

    for (ULONG i = 0; (i < Ctx->MaxObjects) && (slot < HID_MAX_SLOTS); i++) {
        if (Ctx->TcmState.Objects[i].Active != 0U) {
            tcm2_u32_t outX = 0;
            tcm2_u32_t outY = 0;

            Tcm2MapToDisplay(
                Ctx->TcmState.Objects[i].X,
                Ctx->TcmState.Objects[i].Y,
                Ctx->MaxX,
                Ctx->MaxY,
                NP05J_DISP_WIDTH,
                NP05J_DISP_HEIGHT,
                (int)(Ctx->SwapXY != 0U),
                (int)(Ctx->InvertX != 0U),
                (int)(Ctx->InvertY != 0U),
                &outX,
                &outY
            );

            report.Contacts[slot].Flags     = 0x03U; /* Tip Switch + In Range */
            report.Contacts[slot].ContactId = (UCHAR)i;
            report.Contacts[slot].X         = (USHORT)outX;
            report.Contacts[slot].Y         = (USHORT)outY;
            if (slot == 0U) {
                Ctx->LastX       = outX;
                Ctx->LastY       = outY;
                Ctx->LastPressed = 1U;
            }
            slot++;
        }
    }

    if ((slot == 0U) && !Ctx->HadActiveContacts) {
        return;
    }

    if (slot == 0U) {
        Ctx->LastPressed       = 0U;
        Ctx->HadActiveContacts = FALSE;
    } else {
        Ctx->HadActiveContacts = TRUE;
    }

    ticks100us = (ULONGLONG)KeQueryInterruptTime() / 1000ULL;
    report.ScanTime     = (USHORT)(ticks100us & 0xFFFFU);
    report.ContactCount = slot;

    packet.reportBuffer    = (PUCHAR)&report;
    packet.reportBufferLen = sizeof(report);
    packet.reportId        = HID_REPORT_ID_TOUCH;

    (VOID)VhfReadReportSubmit(Ctx->VhfHandle, &packet);
}

static VOID
EvtVhfGetFeature(
    _In_     PVOID              VhfClientContext,
    _In_     VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID              VhfOperationContext,
    _In_     PHID_XFER_PACKET   HidTransferPacket
    )
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    UNREFERENCED_PARAMETER(VhfClientContext);
    UNREFERENCED_PARAMETER(VhfOperationContext);

    if ((HidTransferPacket != NULL) && (HidTransferPacket->reportBuffer != NULL)) {
        if ((HidTransferPacket->reportId == HID_REPORT_ID_MAX_CNT) && (HidTransferPacket->reportBufferLen >= 2U)) {
            HidTransferPacket->reportBuffer[0] = HID_REPORT_ID_MAX_CNT;
            HidTransferPacket->reportBuffer[1] = HID_MAX_SLOTS;
            status = STATUS_SUCCESS;
        } else if ((HidTransferPacket->reportId == HID_REPORT_ID_THQA) && (HidTransferPacket->reportBufferLen >= 257U)) {
            RtlZeroMemory(HidTransferPacket->reportBuffer, 257U);
            HidTransferPacket->reportBuffer[0] = HID_REPORT_ID_THQA;
            HidTransferPacket->reportBuffer[1] = 0xFCU;
            status = STATUS_SUCCESS;
        }
    }

    VhfAsyncOperationComplete(VhfOperationHandle, status);
}

static VOID
EvtTouchPollTimer(
    _In_ WDFTIMER Timer
    )
{
    WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    PNP05J_TOUCH_CTX ctx = Np05jTouchGetContext(device);
    UCHAR code = 0;
    USHORT plen = 0;
    NTSTATUS status;

    if (!ctx->Running || (ctx->ReportCfgLen == 0U)) {
        return;
    }

    for (ULONG burst = 0; burst < 4U; burst++) {
        /* ตรวจสอบขา IRQ (GPIO 162, Active LOW) */
        if ((PinRead(ctx, PIN_IDX_IRQ, TLMM_OFF_IO) & 1U) != 0U) {
            break;
        }

        ctx->IrqCount++;
        status = STATUS_IO_DEVICE_ERROR;
        for (ULONG attempt = 0; attempt < 2U; attempt++) {
            status = Tcm2SendPacket(ctx, TCM2_CMD_GET_REPORT, NULL, 0);
            if (!NT_SUCCESS(status)) {
                break;
            }
            for (ULONG poll = 0; poll < 8U; poll++) {
                KeStallExecutionProcessor((poll == 0U) ? 120U : 250U);
                status = Tcm2ReadReportFast(ctx, &code, ctx->ReportBuf, (USHORT)sizeof(ctx->ReportBuf), &plen);
                if (!NT_SUCCESS(status)) {
                    continue;
                }
                if ((code == TCM2_STATUS_WAIT) || (code == TCM2_STATUS_IDLE)) {
                    continue;
                }
                if (code == TCM2_STATUS_INVALID) {
                    ctx->Seq ^= 1U;
                    status = STATUS_IO_DEVICE_ERROR;
                    break;
                }
                ctx->Seq ^= 1U;
                break;
            }
            if (NT_SUCCESS(status) && (code != TCM2_STATUS_WAIT) && (code != TCM2_STATUS_IDLE)) {
                break;
            }
        }

        if (!NT_SUCCESS(status)) {
            ctx->ErrorCount++;
            break;
        }

        if ((code == TCM2_REPORT_TOUCH) && (plen > 0U)) {
            USHORT avail = (plen < sizeof(ctx->ReportBuf)) ? plen : (USHORT)sizeof(ctx->ReportBuf);
            if (Tcm2ParseTouchReport(
                    ctx->ReportCfg,
                    ctx->ReportCfgLen,
                    ctx->ReportBuf,
                    avail,
                    ctx->MaxObjects,
                    ctx->MaxX,
                    ctx->MaxY,
                    &ctx->TcmState) == 0)
            {
                ctx->FrameCount++;
                SubmitTouchHidReport(ctx);
                if ((ctx->FrameCount == 1U) || ((ctx->FrameCount % 64U) == 0U)) {
                    WriteDiagDword(L"IrqCount", ctx->IrqCount);
                    WriteDiagDword(L"FrameCount", ctx->FrameCount);
                    WriteDiagDword(L"LastX", ctx->LastX);
                    WriteDiagDword(L"LastY", ctx->LastY);
                    WriteDiagDword(L"Pressed", ctx->LastPressed);
                }
            } else {
                ctx->ErrorCount++;
            }
        }
    }
}

static NTSTATUS
EvtDevicePrepareHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PNP05J_TOUCH_CTX ctx = Np05jTouchGetContext(Device);
    ULONG count = WdfCmResourceListGetCount(ResourcesTranslated);
    ULONG memIdx = 0;
    NTSTATUS status;
    VHF_CONFIG vhfCfg;
    UNREFERENCED_PARAMETER(ResourcesRaw);

    WriteDiagDword(L"DiagVersion", 1U);
    WriteDiagDword(L"Stage", 0x03U);

    for (ULONG i = 0; (i < count) && (memIdx < NP05J_TLMM_PIN_COUNT); i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if ((desc != NULL) && (desc->Type == CmResourceTypeMemory)) {
            ctx->PinLen[memIdx]  = desc->u.Memory.Length;
            ctx->PinBase[memIdx] = (PUCHAR)MmMapIoSpaceEx(
                desc->u.Memory.Start,
                desc->u.Memory.Length,
                PAGE_READWRITE | PAGE_NOCACHE
            );
            if (ctx->PinBase[memIdx] == NULL) {
                WriteDiagDword(L"Status", (ULONG)STATUS_INSUFFICIENT_RESOURCES);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            memIdx++;
        }
    }

    if (memIdx < NP05J_TLMM_PIN_COUNT) {
        WriteDiagDword(L"Status", (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    ctx->PinsMapped = TRUE;
    WriteDiagDword(L"Stage", 0x04U);

    status = AcquireTlmmPins(ctx);
    if (!NT_SUCCESS(status)) {
        WriteDiagDword(L"Status", (ULONG)status);
        return status;
    }

    WriteDiagDword(L"Stage", 0x05U);
    VHF_CONFIG_INIT(
        &vhfCfg,
        WdfDeviceWdmGetDeviceObject(Device),
        (USHORT)sizeof(gHidReportDescriptor),
        gHidReportDescriptor
    );
    vhfCfg.VendorID                      = 0x06CBU; /* Synaptics */
    vhfCfg.ProductID                     = 0x3930U; /* S3930TB0A0 */
    vhfCfg.VersionNumber                 = 0x0100U;
    vhfCfg.VhfClientContext              = ctx;
    vhfCfg.EvtVhfAsyncOperationGetFeature = EvtVhfGetFeature;

    status = VhfCreate(&vhfCfg, &ctx->VhfHandle);
    if (!NT_SUCCESS(status)) {
        WriteDiagDword(L"Status", (ULONG)status);
        return status;
    }

    status = InitSynapticsS3930(ctx);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = VhfStart(ctx->VhfHandle);
    if (!NT_SUCCESS(status)) {
        WriteDiagDword(L"Status", (ULONG)status);
        return status;
    }

    ctx->Running = TRUE;
    WdfTimerStart(ctx->PollTimer, WDF_REL_TIMEOUT_IN_MS(ctx->PollIntervalMs));
    return STATUS_SUCCESS;
}

static NTSTATUS
EvtDeviceReleaseHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PNP05J_TOUCH_CTX ctx = Np05jTouchGetContext(Device);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    ctx->Running = FALSE;
    if (ctx->PollTimer != NULL) {
        WdfTimerStop(ctx->PollTimer, TRUE);
    }
    if (ctx->VhfHandle != NULL) {
        VhfDelete(ctx->VhfHandle, TRUE);
        ctx->VhfHandle = NULL;
    }
    if (ctx->PinsMapped) {
        for (ULONG i = 0; i < NP05J_TLMM_PIN_COUNT; i++) {
            if (ctx->PinBase[i] != NULL) {
                MmUnmapIoSpace(ctx->PinBase[i], ctx->PinLen[i]);
                ctx->PinBase[i] = NULL;
                ctx->PinLen[i]  = 0;
            }
        }
        ctx->PinsMapped = FALSE;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
EvtDriverDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_OBJECT_ATTRIBUTES devAttr;
    WDF_TIMER_CONFIG timerCfg;
    WDF_OBJECT_ATTRIBUTES timerAttr;
    WDFDEVICE device;
    PNP05J_TOUCH_CTX ctx;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(Driver);

    WriteDiagDword(L"Stage", 0x01U);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = EvtDeviceReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&devAttr, NP05J_TOUCH_CTX);
    devAttr.ExecutionLevel       = WdfExecutionLevelPassive;
    devAttr.SynchronizationScope = WdfSynchronizationScopeDevice;

    status = WdfDeviceCreate(&DeviceInit, &devAttr, &device);
    if (!NT_SUCCESS(status)) {
        WriteDiagDword(L"Status", (ULONG)status);
        return status;
    }

    ctx = Np05jTouchGetContext(device);
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->Device         = device;
    ctx->SwapXY         = ReadParamDword(L"SwapXY", 1U);
    ctx->InvertX        = ReadParamDword(L"InvertX", 0U);
    ctx->InvertY        = ReadParamDword(L"InvertY", 1U);
    ctx->PollIntervalMs = ReadParamDword(L"PollIntervalMs", 10U);
    ctx->SkipReset      = ReadParamDword(L"SkipReset", 0U);
    if (ctx->PollIntervalMs < 5U) {
        ctx->PollIntervalMs = 5U;
    }

    WDF_TIMER_CONFIG_INIT_PERIODIC(&timerCfg, EvtTouchPollTimer, ctx->PollIntervalMs);
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttr);
    timerAttr.ParentObject   = device;
    timerAttr.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfTimerCreate(&timerCfg, &timerAttr, &ctx->PollTimer);
    if (!NT_SUCCESS(status)) {
        WriteDiagDword(L"Status", (ULONG)status);
        return status;
    }

    WriteDiagDword(L"Stage", 0x02U);
    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, EvtDriverDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}
