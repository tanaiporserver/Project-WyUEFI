// SPDX-License-Identifier: BSD-2-Clause-Patent
// บริการควบคุมจอสัมผัส Synaptics S3930TB0A0 (TCM v2) สำหรับ NP05J ProductCore
// ใช้ GPIO bit-bang SPI4 (TLMM GPIO 48..51, RST 161, IRQ 162) และรางไฟ RPMh
// โดยไม่จอง DMA และทำงานเฉพาะระดับ TPL_APPLICATION พร้อมกู้คืนและอ่านตรวจกลับทั้งหมด
#include "Np05jTouchService.h"
#include "Np05jSynapticsTcm2.h"
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/ConsoleInDevice.h>
#include <Guid/EventGroup.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/FdtLib.h>

#define TLMM_BASE       0x0F100000U
#define PIN_CTL(P)      (TLMM_BASE + (UINT32)(P) * 0x1000U)
#define PIN_IO(P)       (PIN_CTL (P) + 4U)
#define PIN_ICFG(P)     (PIN_CTL (P) + 8U)

#define PIN_MISO        48U
#define PIN_MOSI        49U
#define PIN_CLK         50U
#define PIN_CS          51U
#define PIN_RST         161U
#define PIN_IRQ         162U
#define TOUCH_PIN_COUNT 6U

#define CTL_FIELDS      0x3FFU
#define EGPIO_PRESENT   BIT11
#define EGPIO_ENABLE    BIT12
#define OE_BIT          BIT9
#define PULL_NONE       0U
#define PULL_DOWN       1U
#define PULL_UP         3U

#define TOUCH_QUEUE_LEN 32U
#define MAX_POLL_FRAMES 8U
#define MAX_ERROR_STREAK 3U

#define TOUCH_DRV_ID    2U
#define RAIL_COUNT      2U
#define VOTES_PER_RAIL  3U
#define TOTAL_VOTES     (RAIL_COUNT * VOTES_PER_RAIL)

// โครงสร้าง ABI ของ RpmhDxe และ CmdDbDxe (อ้างอิง PianoPogoBoard.c และ PianoUsbPhy.c)
typedef struct {
  UINT64 Revision;
  VOID *Init;
  EFI_STATUS (EFIAPI *ValidDrv)(UINT32, BOOLEAN *);
  VOID *TcsCommands, *CreateInternal, *CreateExplicit;
  EFI_STATUS (EFIAPI *Create)(UINT32, CONST CHAR8 *, VOID **);
  VOID *Invalidate, *RegisterCallback;
  EFI_STATUS (EFIAPI *Delete)(VOID *);
  EFI_STATUS (EFIAPI *Find)(UINT32, VOID **);
  EFI_STATUS (EFIAPI *Churn)(VOID *, UINT32);
  VOID *Unused[7];
  EFI_STATUS (EFIAPI *Issue)(VOID *, UINT32, BOOLEAN, UINT32, UINT32, UINT32 *);
} NP05J_RPMH;

typedef struct {
  UINT64 Revision;
  VOID *Query;
  EFI_STATUS (EFIAPI *Address)(VOID *, CONST CHAR8 *, UINT32 *);
} NP05J_CMD_DB;

typedef struct {
  UINT32 Data;
  BOOLEAN Valid;
  UINT8 Reserved[3];
  UINT32 Dirty;
} NP05J_RPMH_VOTE;

typedef struct NP05J_RPMH_DRV {
  UINT32 Drv, State, Priority;
  NP05J_RPMH_VOTE AtRpmh, Local[3];
  struct NP05J_RPMH_DRV *Next;
} NP05J_RPMH_DRV;

typedef struct {
  UINT32 Address;
  NP05J_RPMH_DRV *Votes;
  VOID *PdcVotes;
} NP05J_RPMH_RESOURCE;

STATIC_ASSERT (OFFSET_OF (NP05J_RPMH, Issue) == 0x98, "NP05J RPMh issue ABI");
STATIC_ASSERT (OFFSET_OF (NP05J_RPMH_DRV, Local) == 0x18 && OFFSET_OF (NP05J_RPMH_DRV, Next) == 0x40, "NP05J RPMh vote ABI");

typedef enum {
  TOff,
  TRails,
  TPins,
  TResetRelease,
  TInit,
  TRuntime,
  TUnavailable,
  TStopped
} NP05J_TOUCH_STEP;

STATIC CONST CHAR8 *mStepNames[] = {
  "off", "rails", "pins", "reset-release", "init", "runtime", "unavailable", "stopped"
};

STATIC CONST UINT32 mPins[TOUCH_PIN_COUNT] = {
  PIN_MISO, PIN_MOSI, PIN_CLK, PIN_CS, PIN_RST, PIN_IRQ
};

STATIC struct {
  BOOLEAN Started;
  BOOLEAN Busy;
  BOOLEAN InTransfer;
  BOOLEAN PinsOwned;
  BOOLEAN RailsOn;
  BOOLEAN Published;
  BOOLEAN Stopping;
  BOOLEAN Exited;
  BOOLEAN Retained;
  BOOLEAN ReleaseClean;
  BOOLEAN PinsRestored;
  BOOLEAN RailsRestored;

  NP05J_TOUCH_STEP Step;
  EFI_STATUS LastStatus;
  EFI_STATUS PinsStatus;
  EFI_STATUS RailsStatus;
  EFI_STATUS ProtocolsStatus;

  UINT64 Frequency;
  UINT64 CounterStart;
  UINT64 CounterEnd;
  UINT64 StartUs;
  UINT64 NextUs;
  UINT64 SpiHalfBitTicks;

  NP05J_RPMH *Rpmh;
  VOID *PowerHandle;
  UINT32 RailAddr[TOTAL_VOTES];
  UINT32 RailTarget[TOTAL_VOTES];
  BOOLEAN RailVoted[TOTAL_VOTES];

  UINT32 SavedCtl[TOUCH_PIN_COUNT];
  UINT32 SavedIo[TOUCH_PIN_COUNT];
  UINT32 SavedIrqCfg;

  UINT8 Seq;
  UINT16 MaxX;
  UINT16 MaxY;
  UINT16 MaxObjects;
  UINT16 ReportCfgLen;
  UINT8 ReportCfg[TCM2_MAX_REPORT_CFG];
  UINT8 TxBuf[TCM2_MAX_XFER];
  UINT8 RxBuf[TCM2_MAX_XFER];
  UINT8 PayBuf[TCM2_MAX_XFER];
  TCM2_TOUCH_STATE TcmState;

  CHAR8 PartNumber[17];
  UINT32 BuildId;
  UINT32 Frames;
  UINT32 Irqs;
  UINT32 Errors;
  UINT32 Events;
  UINT32 ConsecutiveErrors;

  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_HANDLE Handle;
  EFI_EVENT ExitEvent;
  EFI_ABSOLUTE_POINTER_PROTOCOL Pointer;
  EFI_ABSOLUTE_POINTER_MODE Mode;
  EFI_ABSOLUTE_POINTER_STATE Queue[TOUCH_QUEUE_LEN];
  EFI_ABSOLUTE_POINTER_STATE Last;
  UINTN Head;
  UINTN Tail;
} mT;

STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} mDevicePath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
    { 0xA1E45004, 0x9B1C, 0x4F2A, { 0x8C, 0x3D, 0x10, 0x05, 0x4E, 0x50, 0x30, 0x35 } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { 4, 0 } }
};

STATIC EFI_STATUS
Exact (
  IN EFI_STATUS S
  )
{
  return S == EFI_SUCCESS ? S : EFI_ERROR (S) ? S : EFI_DEVICE_ERROR;
}

STATIC BOOLEAN
Live (
  VOID
  )
{
  return !mT.Exited && gST && gBS && gST->BootServices == gBS &&
         gBS->Hdr.Signature == EFI_BOOT_SERVICES_SIGNATURE;
}

STATIC EFI_STATUS
AtApp (
  VOID
  )
{
  EFI_TPL T;

  if (!Live ()) {
    return EFI_ABORTED;
  }

  T = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  gBS->RestoreTPL (T);
  if (!Live ()) {
    return EFI_ABORTED;
  }

  return T == TPL_APPLICATION ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC UINT64
NowUs (
  VOID
  )
{
  UINT64 Counter = GetPerformanceCounter ();
  UINT64 Ticks   = (mT.CounterEnd >= mT.CounterStart)
                   ? (Counter - mT.CounterStart)
                   : (mT.CounterStart - Counter);

  return (Ticks / mT.Frequency) * 1000000ULL +
         ((Ticks % mT.Frequency) * 1000000ULL) / mT.Frequency;
}

STATIC VOID
DrivePin (
  IN UINT32  Pin,
  IN BOOLEAN High
  )
{
  MmioWrite32 (PIN_IO (Pin), High ? BIT1 : 0U);
  MemoryFence ();
}

// กำหนดค่า pinmux โดยรับสิทธิ์ eGPIO บน SM8750 หากบิต EGPIO_PRESENT ตั้งอยู่
STATIC UINT32
BuildCtl (
  IN UINT32  Orig,
  IN BOOLEAN Output,
  IN UINT32  Pull,
  IN UINT32  Drive
  )
{
  UINT32 V = (Orig & ~(CTL_FIELDS | EGPIO_ENABLE)) |
             (Drive << 6) | Pull | (Output ? OE_BIT : 0U);

  if ((V & EGPIO_PRESENT) != 0U) {
    V |= EGPIO_ENABLE;
  }

  return V;
}

// หน่วงเวลาครึ่งคาบสัญญาณ SPI (~100 ns) ด้วย PerformanceCounter ลด overhead ของ NanoSecondDelay
STATIC VOID
SpiBitDelay (
  VOID
  )
{
  UINT64 Start = GetPerformanceCounter ();
  UINT64 Target = mT.SpiHalfBitTicks;

  if (mT.CounterEnd >= mT.CounterStart) {
    while ((GetPerformanceCounter () - Start) < Target) {
    }
  } else {
    while ((Start - GetPerformanceCounter ()) < Target) {
    }
  }
}

STATIC UINT8
SpiByte (
  IN UINT8 Tx
  )
{
  UINT8 Rx = 0U;

  for (UINTN B = 0U; B < 8U; ++B) {
    MmioWrite32 (PIN_IO (PIN_MOSI), (Tx & 0x80U) != 0U ? BIT1 : 0U);
    Tx = (UINT8)(Tx << 1);
    SpiBitDelay ();
    MmioWrite32 (PIN_IO (PIN_CLK), BIT1);
    SpiBitDelay ();
    Rx = (UINT8)((Rx << 1) | (MmioRead32 (PIN_IO (PIN_MISO)) & 1U));
    MmioWrite32 (PIN_IO (PIN_CLK), 0U);
  }

  return Rx;
}

STATIC VOID
SpiReadBytes (
  OUT UINT8  *Rx,
  IN  UINT32 Len
  )
{
  // ค้าง MOSI เป็น 1 ตลอดช่วงอ่าน dummy 0xFF และสลับเฉพาะ CLK ลด MMIO write ลงครึ่งหนึ่ง
  DrivePin (PIN_MOSI, TRUE);
  for (UINT32 I = 0U; I < Len; ++I) {
    UINT8 V = 0U;
    for (UINTN B = 0U; B < 8U; ++B) {
      SpiBitDelay ();
      MmioWrite32 (PIN_IO (PIN_CLK), BIT1);
      SpiBitDelay ();
      V = (UINT8)((V << 1) | (MmioRead32 (PIN_IO (PIN_MISO)) & 1U));
      MmioWrite32 (PIN_IO (PIN_CLK), 0U);
    }
    if (Rx) {
      Rx[I] = V;
    }
  }
}

STATIC VOID
SpiXfer (
  IN  CONST UINT8 *Tx,
  OUT UINT8       *Rx,
  IN  UINT32       Len
  )
{
  DrivePin (PIN_CS, FALSE);
  // คงเวลา CS setup 12us ตามค่า DTS (spi-cs-setup-ns = 11000 ns) ของ S3930TB0A0
  MicroSecondDelay (12);
  if (Tx == NULL) {
    SpiReadBytes (Rx, Len);
  } else {
    for (UINT32 I = 0U; I < Len; ++I) {
      UINT8 B = SpiByte (Tx[I]);
      if (Rx) {
        Rx[I] = B;
      }
    }
  }
  DrivePin (PIN_CS, TRUE);
  MicroSecondDelay (4);
}

STATIC BOOLEAN
TcmCmd (
  IN  UINT8       Cmd,
  IN  CONST UINT8 *Pay,
  IN  UINT16      PayLen,
  OUT UINT8       *OutPay,
  IN  UINT16      MaxOut,
  OUT UINT16      *OutLen
  )
{
  for (UINT32 Retry = 0U; Retry < 3U; ++Retry) {
    UINT32 Total = Tcm2BuildPacket (mT.TxBuf, sizeof (mT.TxBuf), Cmd, mT.Seq, Pay, PayLen);
    if (Total == 0U) {
      return FALSE;
    }

    SpiXfer (mT.TxBuf, mT.RxBuf, Total);

    for (UINT32 Poll = 0U; Poll < 60U; ++Poll) {
      MicroSecondDelay (400);
      SpiXfer (NULL, mT.RxBuf, 16U);

      if (!Tcm2HdrCrcOk (mT.RxBuf)) {
        continue;
      }
      if ((mT.RxBuf[0] == TCM2_STATUS_WAIT) || (mT.RxBuf[0] == TCM2_STATUS_IDLE)) {
        continue;
      }
      if (mT.RxBuf[0] == TCM2_STATUS_INVALID) {
        // จบการรอคำสั่งรอบนี้แล้วให้ท้ายลูป Retry สลับ Seq เพื่อซิงก์กับชิปใหม่
        break;
      }
      if (mT.RxBuf[0] == TCM2_STATUS_OK) {
        UINT16 Plen = Tcm2GetLe16 (mT.RxBuf + 1);
        if (OutLen) {
          *OutLen = Plen;
        }
        if ((Plen > 0U) && OutPay) {
          UINT32 Rd = (UINT32)TCM2_HDR_SIZE + Plen + TCM2_CRC16_SIZE;
          if (Rd > TCM2_MAX_XFER) {
            Rd = TCM2_MAX_XFER;
          }
          SpiXfer (NULL, mT.RxBuf, Rd);
          if (!Tcm2HdrCrcOk (mT.RxBuf)) {
            break;
          }
          UINT16 Copy = (Plen < MaxOut) ? Plen : MaxOut;
          CopyMem (OutPay, mT.RxBuf + TCM2_HDR_SIZE, Copy);
        }
        mT.Seq ^= 1U;
        return TRUE;
      }
    }
    mT.Seq ^= 1U;
  }

  return FALSE;
}

STATIC EFI_STATUS
VoteRailWord (
  IN UINT32 Address,
  IN UINT32 Value
  )
{
  UINT32 Request = 0U;
  EFI_STATUS S   = Exact (mT.Rpmh->Issue (mT.PowerHandle, 0U, TRUE, Address, Value, &Request));

  if (S == EFI_SUCCESS) {
    S = Exact (mT.Rpmh->Churn (mT.PowerHandle, Request));
  }
  return S;
}

STATIC EFI_STATUS
RestorePins (
  VOID
  )
{
  EFI_STATUS S = EFI_SUCCESS;

  if (!mT.PinsOwned) {
    mT.PinsRestored = TRUE;
    return EFI_SUCCESS;
  }

  // ตั้งระดับ idle และปิด output enable ก่อนคืนค่า CTL ทั้งหมดเพื่อป้องกัน glitch
  DrivePin (PIN_CS, TRUE);
  DrivePin (PIN_CLK, FALSE);
  DrivePin (PIN_MOSI, FALSE);
  for (UINTN I = 1U; I <= 4U; ++I) {
    MmioAnd32 (PIN_CTL (mPins[I]), ~OE_BIT);
  }
  MemoryFence ();

  MmioWrite32 (PIN_ICFG (PIN_IRQ), mT.SavedIrqCfg);
  MemoryFence ();

  for (UINTN I = 0U; I < TOUCH_PIN_COUNT; ++I) {
    MmioWrite32 (PIN_IO (mPins[I]), mT.SavedIo[I] & BIT1);
    MmioWrite32 (PIN_CTL (mPins[I]), mT.SavedCtl[I]);
  }
  MemoryFence ();

  // อ่านตรวจสอบค่า TLMM กลับมาทั้ง 6 ขา + IRQ_CFG ให้ตรงกับที่บันทึกไว้
  if (MmioRead32 (PIN_ICFG (PIN_IRQ)) != mT.SavedIrqCfg) {
    S = EFI_DEVICE_ERROR;
  }
  for (UINTN I = 0U; I < TOUCH_PIN_COUNT; ++I) {
    if ((MmioRead32 (PIN_CTL (mPins[I])) != mT.SavedCtl[I]) ||
        ((MmioRead32 (PIN_IO (mPins[I])) & BIT1) != (mT.SavedIo[I] & BIT1)))
    {
      S = EFI_DEVICE_ERROR;
    }
  }

  if (S == EFI_SUCCESS) {
    mT.PinsOwned    = FALSE;
    mT.PinsRestored = TRUE;
  }
  mT.PinsStatus = S;
  return S;
}

STATIC EFI_STATUS
ReleaseRails (
  VOID
  )
{
  EFI_STATUS S = EFI_SUCCESS;

  if (!mT.RailsOn || !mT.PowerHandle) {
    mT.RailsRestored = TRUE;
    return EFI_SUCCESS;
  }

  // ปิดโหวตย้อนลำดับ: ldob14 (AVDD 3.2V) ก่อน แล้วจึง ldob4 (VDD 1.8V) เป็น 0 แล้ว Delete
  for (UINTN I = TOTAL_VOTES; I > 0U; --I) {
    if (!mT.RailVoted[I - 1U]) {
      continue;
    }
    EFI_STATUS V = VoteRailWord (mT.RailAddr[I - 1U], 0U);
    if (V == EFI_SUCCESS) {
      mT.RailVoted[I - 1U] = FALSE;
    } else if (S == EFI_SUCCESS) {
      S = V;
    }
  }

  if (S == EFI_SUCCESS) {
    EFI_STATUS D = Exact (mT.Rpmh->Delete (mT.PowerHandle));
    if (D == EFI_SUCCESS) {
      mT.PowerHandle   = NULL;
      mT.RailsOn       = FALSE;
      mT.RailsRestored = TRUE;
    } else {
      S = D;
    }
  }

  mT.RailsStatus = S;
  return S;
}

STATIC EFI_STATUS
ReleaseHardware (
  VOID
  )
{
  EFI_STATUS PinS  = RestorePins ();
  EFI_STATUS RailS = ReleaseRails ();

  mT.ReleaseClean = (PinS == EFI_SUCCESS) && (RailS == EFI_SUCCESS);
  return mT.ReleaseClean ? EFI_SUCCESS : (PinS != EFI_SUCCESS ? PinS : RailS);
}

STATIC EFI_STATUS
ReleaseProtocols (
  VOID
  )
{
  EFI_STATUS S = EFI_SUCCESS;

  if (mT.Published && mT.Handle) {
    EFI_STATUS D = gBS->DisconnectController (mT.Handle, NULL, NULL);
    if ((D != EFI_SUCCESS) && (D != EFI_NOT_FOUND) && (D != EFI_NOT_STARTED)) {
      S = Exact (D);
    }
    if (S == EFI_SUCCESS) {
      EFI_STATUS U = gBS->UninstallMultipleProtocolInterfaces (
                            mT.Handle,
                            &gEfiAbsolutePointerProtocolGuid, &mT.Pointer,
                            &gEfiConsoleInDeviceGuid, NULL,
                            &gEfiDevicePathProtocolGuid, &mDevicePath,
                            NULL
                            );
      if (U == EFI_SUCCESS) {
        mT.Handle    = NULL;
        mT.Published = FALSE;
      } else {
        S = Exact (U);
      }
    }
  }

  if (mT.Pointer.WaitForInput) {
    EFI_STATUS W = gBS->CloseEvent (mT.Pointer.WaitForInput);
    if (W == EFI_SUCCESS) {
      mT.Pointer.WaitForInput = NULL;
    } else if (S == EFI_SUCCESS) {
      S = Exact (W);
    }
  }

  if (mT.ExitEvent) {
    EFI_STATUS E = gBS->CloseEvent (mT.ExitEvent);
    if (E == EFI_SUCCESS) {
      mT.ExitEvent = NULL;
    } else if (S == EFI_SUCCESS) {
      S = Exact (E);
    }
  }

  mT.ProtocolsStatus = S;
  return S;
}

STATIC VOID
PushPointer (
  IN UINT32  DispX,
  IN UINT32  DispY,
  IN BOOLEAN Pressed
  )
{
  EFI_ABSOLUTE_POINTER_STATE P;
  EFI_TPL Old;
  UINTN Next;

  ZeroMem (&P, sizeof (P));
  P.CurrentX      = DispX;
  P.CurrentY      = DispY;
  P.ActiveButtons = Pressed ? EFI_ABSP_TouchActive : 0U;

  Old = gBS->RaiseTPL (TPL_NOTIFY);
  if (CompareMem (&P, &mT.Last, sizeof (P)) != 0) {
    Next = (mT.Tail + 1U) % TOUCH_QUEUE_LEN;
    if (Next == mT.Head) {
      // ทิ้ง sample เก่าสุดเพื่อให้พิกัดล่าสุดและสถานะปล่อยนิ้วส่งถึง UI เสมอ
      mT.Head = (mT.Head + 1U) % TOUCH_QUEUE_LEN;
    }
    mT.Queue[mT.Tail] = P;
    mT.Tail           = Next;
    mT.Last           = P;
    ++mT.Events;
    if (mT.Pointer.WaitForInput) {
      gBS->SignalEvent (mT.Pointer.WaitForInput);
    }
  }
  gBS->RestoreTPL (Old);
}

STATIC VOID
ReleasePointer (
  VOID
  )
{
  EFI_TPL Old = gBS->RaiseTPL (TPL_NOTIFY);
  BOOLEAN HadPress = (mT.Last.ActiveButtons != 0U);

  if (HadPress) {
    UINTN Next = (mT.Tail + 1U) % TOUCH_QUEUE_LEN;
    if (Next == mT.Head) {
      mT.Head = (mT.Head + 1U) % TOUCH_QUEUE_LEN;
    }
    EFI_ABSOLUTE_POINTER_STATE P = mT.Last;
    P.ActiveButtons    = 0U;
    mT.Queue[mT.Tail]  = P;
    mT.Tail            = Next;
    mT.Last            = P;
    ++mT.Events;
    if (mT.Pointer.WaitForInput) {
      gBS->SignalEvent (mT.Pointer.WaitForInput);
    }
  }
  gBS->RestoreTPL (Old);
}

STATIC VOID EFIAPI
WaitNotify (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  (VOID)Context;
  if (Live () && !mT.Stopping && !mT.Retained && (mT.Head != mT.Tail)) {
    gBS->SignalEvent (Event);
  }
}

STATIC VOID EFIAPI
ExitFence (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  (VOID)Event;
  (VOID)Context;
  mT.Exited = TRUE;
  if (mT.PinsOwned || mT.RailsOn) {
    mT.Retained = TRUE;
  }
}

STATIC EFI_STATUS EFIAPI
PointerReset (
  IN EFI_ABSOLUTE_POINTER_PROTOCOL *This,
  IN BOOLEAN                       ExtendedVerification
  )
{
  EFI_TPL Old;

  (VOID)ExtendedVerification;
  if (This != &mT.Pointer) {
    return EFI_INVALID_PARAMETER;
  }
  if (!Live () || mT.Stopping || !mT.Published) {
    return EFI_NOT_READY;
  }

  Old = gBS->RaiseTPL (TPL_NOTIFY);
  mT.Head = 0U;
  mT.Tail = 0U;
  ZeroMem (&mT.Last, sizeof (mT.Last));
  mT.TcmState.HasPrimary = 0U;
  mT.TcmState.Pressed    = 0U;
  gBS->RestoreTPL (Old);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
PointerGetState (
  IN  EFI_ABSOLUTE_POINTER_PROTOCOL *This,
  OUT EFI_ABSOLUTE_POINTER_STATE    *State
  )
{
  EFI_TPL Old;
  EFI_STATUS S = EFI_NOT_READY;

  if ((This != &mT.Pointer) || (State == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if (!Live () || mT.Stopping) {
    return EFI_NOT_READY;
  }

  Old = gBS->RaiseTPL (TPL_NOTIFY);
  if (mT.Head != mT.Tail) {
    *State  = mT.Queue[mT.Head];
    mT.Head = (mT.Head + 1U) % TOUCH_QUEUE_LEN;
    S       = EFI_SUCCESS;
  } else if (!mT.Published) {
    S = EFI_NOT_READY;
  }
  gBS->RestoreTPL (Old);
  return S;
}

STATIC VOID
Unavailable (
  IN CONST CHAR8 *Reason,
  IN EFI_STATUS  Status
  )
{
  NP05J_TOUCH_STEP Failed = mT.Step;

  if (Failed == TStopped) {
    return;
  }
  (VOID)ReleaseProtocols ();
  (VOID)ReleaseHardware ();
  if (!mT.ReleaseClean) {
    mT.Retained = TRUE;
  }
  mT.Step       = TUnavailable;
  mT.LastStatus = Status;
  DEBUG ((
    DEBUG_WARN,
    "NP05J_TOUCH_UNAVAILABLE reason=%a step=%a status=%r clean=%u\n",
    Reason,
    mStepNames[Failed],
    Status,
    mT.ReleaseClean
    ));
}

STATIC VOID
StepBringUp (
  VOID
  )
{
  EFI_STATUS S        = EFI_SUCCESS;
  UINT32 WaitUs       = 0U;
  NP05J_TOUCH_STEP Next = (NP05J_TOUCH_STEP)(mT.Step + 1);
  CONST CHAR8 *Reason = mStepNames[mT.Step];

  switch (mT.Step) {
    case TRails: {
      EFI_GUID RpmhGuid = { 0xe1d0b573, 0xf29f, 0x495d, { 0x93, 0x52, 0xca, 0x0d, 0xd4, 0x2a, 0x0a, 0x74 } };
      EFI_GUID DbGuid   = { 0x1c34f691, 0xd33d, 0x4e14, { 0xb8, 0xf8, 0x32, 0xc6, 0xc0, 0x29, 0xe9, 0x5b } };
      NP05J_RPMH *Rpmh  = NULL;
      NP05J_CMD_DB *Db  = NULL;
      BOOLEAN Valid     = FALSE;

      S = gBS->LocateProtocol (&RpmhGuid, NULL, (VOID **)&Rpmh);
      if (S == EFI_SUCCESS) {
        S = gBS->LocateProtocol (&DbGuid, NULL, (VOID **)&Db);
      }
      if ((S != EFI_SUCCESS) || !Rpmh || !Db ||
          (Rpmh->Revision != 0x10001ULL) || !Rpmh->ValidDrv || !Rpmh->Create ||
          !Rpmh->Delete || !Rpmh->Issue || !Rpmh->Churn ||
          (Db->Revision != 0x10000ULL) || !Db->Address)
      {
        if (S == EFI_SUCCESS) {
          S = EFI_UNSUPPORTED;
        }
        break;
      }

      S = Exact (Rpmh->ValidDrv (TOUCH_DRV_ID, &Valid));
      if ((S != EFI_SUCCESS) || !Valid) {
        S = EFI_UNSUPPORTED;
        break;
      }

      STATIC CONST struct {
        CONST CHAR8 *Name;
        UINT32 Mv;
      } Rails[RAIL_COUNT] = {
        { "ldob4", 1800U },
        { "ldob14", 3200U }
      };

      for (UINTN R = 0U; R < RAIL_COUNT; ++R) {
        UINT32 Base = 0U;
        S = Exact (Db->Address (Db, Rails[R].Name, &Base));
        if ((S != EFI_SUCCESS) || ((Base >> 16) != 4U) || ((Base & 255U) != 0U)) {
          S = EFI_UNSUPPORTED;
          break;
        }
        mT.RailAddr[R * 3U + 0U]   = Base + 0U;
        mT.RailTarget[R * 3U + 0U] = Rails[R].Mv;
        mT.RailAddr[R * 3U + 1U]   = Base + 4U;
        mT.RailTarget[R * 3U + 1U] = 1U;
        mT.RailAddr[R * 3U + 2U]   = Base + 8U;
        mT.RailTarget[R * 3U + 2U] = 7U;
      }
      if (S != EFI_SUCCESS) {
        break;
      }

      S = Exact (Rpmh->Create (TOUCH_DRV_ID, "SunUEFI touch", &mT.PowerHandle));
      if ((S != EFI_SUCCESS) || !mT.PowerHandle) {
        mT.PowerHandle = NULL;
        if (S == EFI_SUCCESS) {
          S = EFI_OUT_OF_RESOURCES;
        }
        break;
      }

      mT.Rpmh    = Rpmh;
      mT.RailsOn = TRUE;

      for (UINTN I = 0U; I < TOTAL_VOTES; ++I) {
        S = VoteRailWord (mT.RailAddr[I], mT.RailTarget[I]);
        if (S != EFI_SUCCESS) {
          break;
        }
        mT.RailVoted[I] = TRUE;
      }

      WaitUs = 20000U;
      break;
    }

    case TPins: {
      for (UINTN I = 0U; I < TOUCH_PIN_COUNT; ++I) {
        mT.SavedCtl[I] = MmioRead32 (PIN_CTL (mPins[I]));
        mT.SavedIo[I]  = MmioRead32 (PIN_IO (mPins[I]));
        if (mT.SavedCtl[I] == MAX_UINT32) {
          S = EFI_UNSUPPORTED;
          break;
        }
      }
      if (S != EFI_SUCCESS) {
        break;
      }
      mT.SavedIrqCfg = MmioRead32 (PIN_ICFG (PIN_IRQ));
      if (mT.SavedIrqCfg == MAX_UINT32) {
        S = EFI_UNSUPPORTED;
        break;
      }
      mT.PinsOwned = TRUE;

      MmioWrite32 (PIN_CTL (PIN_MISO), BuildCtl (mT.SavedCtl[0], FALSE, PULL_DOWN, 2U));
      DrivePin (PIN_CS, TRUE);
      DrivePin (PIN_CLK, FALSE);
      DrivePin (PIN_MOSI, FALSE);
      DrivePin (PIN_RST, FALSE);
      MmioWrite32 (PIN_CTL (PIN_MOSI), BuildCtl (mT.SavedCtl[1], TRUE, PULL_NONE, 2U));
      MmioWrite32 (PIN_CTL (PIN_CLK), BuildCtl (mT.SavedCtl[2], TRUE, PULL_NONE, 2U));
      MmioWrite32 (PIN_CTL (PIN_CS), BuildCtl (mT.SavedCtl[3], TRUE, PULL_NONE, 2U));
      MmioWrite32 (PIN_CTL (PIN_RST), BuildCtl (mT.SavedCtl[4], TRUE, PULL_NONE, 1U));
      // ปิด TLMM summary และ PDC wake ก่อนตั้งค่า IRQ เป็นอินพุต PULL_UP
      MmioWrite32 (PIN_ICFG (PIN_IRQ), mT.SavedIrqCfg & ~(BIT0 | BIT7));
      MmioWrite32 (PIN_CTL (PIN_IRQ), BuildCtl (mT.SavedCtl[5], FALSE, PULL_UP, 0U));
      MemoryFence ();

      for (UINTN I = 0U; I < TOUCH_PIN_COUNT; ++I) {
        if ((MmioRead32 (PIN_CTL (mPins[I])) & 0x3CU) != 0U) {
          S = EFI_DEVICE_ERROR;
          break;
        }
      }
      WaitUs = 12000U;
      break;
    }

    case TResetRelease:
      DrivePin (PIN_RST, TRUE);
      WaitUs = 200000U;
      break;

    case TInit: {
      UINT16 Len = 0U;
      UINT8 RepId = TCM2_REPORT_TOUCH;

      mT.InTransfer = TRUE;
      // ระบาย identify report หลังรีเซ็ตชิปตามลำดับของ TCM v2 SPI
      SetMem (mT.TxBuf, TCM2_HDR_SIZE, 0U);
      Tcm2BuildPacket (mT.TxBuf, sizeof (mT.TxBuf), TCM2_CMD_ACK, mT.Seq, NULL, 0U);
      SpiXfer (mT.TxBuf, mT.RxBuf, TCM2_HDR_SIZE);
      MicroSecondDelay (4000);
      SpiXfer (NULL, mT.RxBuf, 64U);

      if (!TcmCmd (TCM2_CMD_IDENTIFY, NULL, 0U, mT.PayBuf, (UINT16)sizeof (mT.PayBuf), &Len)) {
        mT.InTransfer = FALSE;
        S             = EFI_DEVICE_ERROR;
        Reason        = "identify";
        break;
      }

      // payload ของ TCM2_CMD_IDENTIFY ถูกตัด header 4 ไบต์แล้ว: part_number อยู่ที่ offset 2..17 และ build_id ที่ 18..21
      if (Len >= 22U) {
        for (UINTN K = 0U; K < 16U; ++K) {
          UINT8 C = mT.PayBuf[2U + K];
          if ((C < 0x20U) || (C >= 0x7FU)) {
            mT.PartNumber[K] = '\0';
            break;
          }
          mT.PartNumber[K] = (CHAR8)C;
        }
        mT.PartNumber[16] = '\0';
        mT.BuildId = Tcm2GetLe32 (mT.PayBuf + 18U);
      }

      if (TcmCmd (TCM2_CMD_GET_APP_INFO, NULL, 0U, mT.PayBuf, (UINT16)sizeof (mT.PayBuf), &Len) &&
          (Len >= 38U))
      {
        UINT16 Mx = Tcm2GetLe16 (mT.PayBuf + 32U);
        UINT16 My = Tcm2GetLe16 (mT.PayBuf + 34U);
        UINT16 Mo = Tcm2GetLe16 (mT.PayBuf + 36U);
        if ((Mx > 100U) && (My > 100U)) {
          mT.MaxX = Mx;
          mT.MaxY = My;
        }
        if ((Mo > 0U) && (Mo <= TCM2_MAX_OBJECTS)) {
          mT.MaxObjects = Mo;
        }
      }

      if (!TcmCmd (TCM2_CMD_GET_TOUCH_REPORT_CFG, NULL, 0U, mT.ReportCfg, (UINT16)sizeof (mT.ReportCfg), &mT.ReportCfgLen) ||
          (mT.ReportCfgLen == 0U))
      {
        mT.InTransfer = FALSE;
        S             = EFI_DEVICE_ERROR;
        Reason        = "report-cfg";
        break;
      }

      if (!TcmCmd (TCM2_CMD_ENABLE_REPORT, &RepId, 1U, mT.PayBuf, (UINT16)sizeof (mT.PayBuf), &Len)) {
        mT.InTransfer = FALSE;
        S             = EFI_DEVICE_ERROR;
        Reason        = "enable-report";
        break;
      }
      mT.InTransfer = FALSE;

      (VOID)gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mT.Gop);
      ZeroMem (&mT.TcmState, sizeof (mT.TcmState));
      ZeroMem (&mT.Last, sizeof (mT.Last));
      mT.Head = 0U;
      mT.Tail = 0U;

      mT.Mode = (EFI_ABSOLUTE_POINTER_MODE){ 0, 0, 0, 65535, 65535, 0, 0 };
      mT.Pointer.Reset    = PointerReset;
      mT.Pointer.GetState = PointerGetState;
      mT.Pointer.Mode     = &mT.Mode;

      S = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitNotify, NULL, &mT.Pointer.WaitForInput);
      if (S == EFI_SUCCESS) {
        S = gBS->CreateEventEx (
                   EVT_NOTIFY_SIGNAL,
                   TPL_NOTIFY,
                   ExitFence,
                   NULL,
                   &gEfiEventExitBootServicesGuid,
                   &mT.ExitEvent
                   );
      }
      if (S == EFI_SUCCESS) {
        S = gBS->InstallMultipleProtocolInterfaces (
                   &mT.Handle,
                   &gEfiAbsolutePointerProtocolGuid, &mT.Pointer,
                   &gEfiConsoleInDeviceGuid, NULL,
                   &gEfiDevicePathProtocolGuid, &mDevicePath,
                   NULL
                   );
      }
      if (S == EFI_SUCCESS) {
        mT.Published = TRUE;
        (VOID)gBS->ConnectController (mT.Handle, NULL, NULL, TRUE);
        DEBUG ((
          DEBUG_WARN,
          "NP05J_TOUCH_READY part=%a build=%u max=%u,%u obj=%u bring_up_ms=%lu\n",
          mT.PartNumber[0] ? mT.PartNumber : "S3930",
          mT.BuildId,
          mT.MaxX,
          mT.MaxY,
          mT.MaxObjects,
          (NowUs () - mT.StartUs) / 1000ULL
          ));
      } else {
        Reason = "publish";
      }
      break;
    }

    default:
      return;
  }

  if (mT.Step == TStopped) {
    return;
  }
  if (S != EFI_SUCCESS) {
    Unavailable (Reason, S);
    return;
  }

  mT.LastStatus = EFI_SUCCESS;
  mT.Step       = Next;
  mT.NextUs     = NowUs () + WaitUs;
}

// อ่าน report แบบมี turnaround delay และ poll TCM2_STATUS_WAIT/INVALID เพื่อรักษา Seq ให้ตรงกับชิปเสมอ
STATIC BOOLEAN
Tcm2GetReportRuntime (
  OUT UINT8  *CodeOut,
  OUT UINT16 *PlenOut
  )
{
  for (UINTN Attempt = 0U; Attempt < 2U; ++Attempt) {
    UINT32 Total = Tcm2BuildPacket (mT.TxBuf, sizeof (mT.TxBuf), TCM2_CMD_GET_REPORT, mT.Seq, NULL, 0U);
    if (Total == 0U) {
      return FALSE;
    }

    SpiXfer (mT.TxBuf, mT.RxBuf, Total);

    for (UINTN Poll = 0U; Poll < 6U; ++Poll) {
      MicroSecondDelay (Poll == 0U ? 400U : 600U);
      // อ่าน header 4 ไบต์ก่อนแล้วค่อยอ่าน payload+CRC ตามความยาวจริงภายใน CS เดิม (คง CS setup 12us ตาม DTS)
      DrivePin (PIN_CS, FALSE);
      MicroSecondDelay (12);
      SpiReadBytes (mT.RxBuf, TCM2_HDR_SIZE);

      if (!Tcm2HdrCrcOk (mT.RxBuf)) {
        DrivePin (PIN_CS, TRUE);
        MicroSecondDelay (4);
        continue;
      }

      UINT8 Code = mT.RxBuf[0];
      if ((Code == TCM2_STATUS_WAIT) || (Code == TCM2_STATUS_IDLE)) {
        DrivePin (PIN_CS, TRUE);
        MicroSecondDelay (4);
        continue;
      }
      if (Code == TCM2_STATUS_INVALID) {
        DrivePin (PIN_CS, TRUE);
        MicroSecondDelay (4);
        mT.Seq ^= 1U;
        break;
      }

      UINT16 Plen = Tcm2GetLe16 (mT.RxBuf + 1);
      UINT32 Rem  = (UINT32)Plen + TCM2_CRC16_SIZE;
      if (Rem > (TCM2_REPORT_RD_SIZE - TCM2_HDR_SIZE)) {
        Rem = TCM2_REPORT_RD_SIZE - TCM2_HDR_SIZE;
      }
      if (Rem > 0U) {
        SpiReadBytes (mT.RxBuf + TCM2_HDR_SIZE, Rem);
      }
      DrivePin (PIN_CS, TRUE);
      MicroSecondDelay (4);

      mT.Seq  ^= 1U;
      *CodeOut = Code;
      *PlenOut = Plen;
      return TRUE;
    }
  }

  return FALSE;
}

STATIC VOID
StepRuntime (
  VOID
  )
{
  BOOLEAN Landscape = mT.Gop && mT.Gop->Mode && mT.Gop->Mode->Info &&
                      (mT.Gop->Mode->Info->HorizontalResolution >
                       mT.Gop->Mode->Info->VerticalResolution);

  for (UINTN Count = 0U; Count < MAX_POLL_FRAMES; ++Count) {
    if (!Live () || mT.Stopping || mT.Retained) {
      break;
    }
    if ((MmioRead32 (PIN_IO (PIN_IRQ)) & 1U) != 0U) {
      break;
    }

    UINT8   Code = 0U;
    UINT16  Plen = 0U;
    BOOLEAN Ok;

    ++mT.Irqs;
    mT.InTransfer = TRUE;
    Ok            = Tcm2GetReportRuntime (&Code, &Plen);
    mT.InTransfer = FALSE;

    if (!Ok) {
      ++mT.Errors;
      ++mT.ConsecutiveErrors;
      if (mT.ConsecutiveErrors >= MAX_ERROR_STREAK) {
        ReleasePointer ();
        mT.TcmState.HasPrimary = 0U;
      }
      if (mT.ConsecutiveErrors >= 6U) {
        // กู้ลิงก์ SPI อัตโนมัติเมื่อ Seq คลาดเคลื่อนสะสมโดยไม่หยุดบริการทัช
        UINT32 Total = Tcm2BuildPacket (mT.TxBuf, sizeof (mT.TxBuf), TCM2_CMD_ACK, mT.Seq, NULL, 0U);
        mT.InTransfer = TRUE;
        if (Total > 0U) {
          SpiXfer (mT.TxBuf, mT.RxBuf, Total);
        }
        MicroSecondDelay (800);
        SetMem (mT.TxBuf, 64U, 0xFFU);
        SpiXfer (mT.TxBuf, mT.RxBuf, 64U);
        mT.InTransfer        = FALSE;
        mT.Seq              ^= 1U;
        mT.ConsecutiveErrors = 0U;
      }
      break;
    }

    mT.ConsecutiveErrors = 0U;
    if (Code != TCM2_REPORT_TOUCH) {
      // report สถานะ/identify ถูกระบายออกแล้วและ Seq ตรงเฟส ไม่นับเป็น error
      continue;
    }

    if ((Plen == 0U) || ((UINT32)TCM2_HDR_SIZE + Plen > TCM2_REPORT_RD_SIZE) ||
        (Tcm2ParseTouchReport (
           mT.ReportCfg,
           mT.ReportCfgLen,
           mT.RxBuf + TCM2_HDR_SIZE,
           Plen,
           mT.MaxObjects,
           mT.MaxX,
           mT.MaxY,
           &mT.TcmState
           ) != 0))
    {
      ++mT.Errors;
      if (++mT.ConsecutiveErrors >= MAX_ERROR_STREAK) {
        ReleasePointer ();
        mT.TcmState.HasPrimary = 0U;
      }
      break;
    }

    mT.ConsecutiveErrors = 0U;
    ++mT.Frames;

    if (mT.TcmState.Pressed == 0U) {
      ReleasePointer ();
    } else {
      tcm2_u32_t DispX = 0U;
      tcm2_u32_t DispY = 0U;
      Tcm2MapToDisplay (
        mT.TcmState.RawX,
        mT.TcmState.RawY,
        mT.MaxX,
        mT.MaxY,
        65536U,
        65536U,
        Landscape ? 1 : 0,
        0,
        Landscape ? 1 : 0,
        &DispX,
        &DispY
        );
      PushPointer (DispX, DispY, TRUE);
    }
  }
}

EFI_STATUS
Np05jTouchStart (
  IN CONST VOID *Fdt
  )
{
  INT32 Bus, Dev, Len = 0, RegLen = 0, CompLen = 0;
  CONST UINT8 *Reg, *DevReg;
  CONST CHAR8 *DevCmp;
  BOOLEAN BusOk, DevOk;
  STATIC CONST UINT8 ExpectedBusReg[8] = { 0x00, 0xA9, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00 };

  if (mT.Started) {
    return EFI_ALREADY_STARTED;
  }
  if ((Fdt == NULL) || (FdtCheckHeader (Fdt) != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Bus = FdtPathOffset (Fdt, "/soc/qcom,qupv3_1_geni_se@ac0000/spi@a90000");
  Dev = FdtPathOffset (Fdt, "/soc/qcom,qupv3_1_geni_se@ac0000/spi@a90000/synaptics_tcm@0");

  Reg   = (Bus < 0) ? NULL : FdtGetProp (Fdt, Bus, "reg", &Len);
  BusOk = (Reg != NULL) && (Len == (INT32)sizeof (ExpectedBusReg)) &&
          (CompareMem (Reg, ExpectedBusReg, sizeof (ExpectedBusReg)) == 0);

  DevReg = (Dev < 0) ? NULL : FdtGetProp (Fdt, Dev, "reg", &RegLen);
  DevCmp = (Dev < 0) ? NULL : FdtGetProp (Fdt, Dev, "compatible", &CompLen);
  DevOk  = (DevReg != NULL) && (RegLen == 4) &&
           (DevReg[0] == 0 && DevReg[1] == 0 && DevReg[2] == 0 && DevReg[3] == 0) &&
           (DevCmp != NULL) && (CompLen >= 18) &&
           (AsciiStrCmp (DevCmp, "synaptics,tcm-spi") == 0);

  if (!BusOk || !DevOk) {
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH_START status=%r bus=%u dev=%u\n", EFI_NOT_FOUND, BusOk, DevOk));
    return EFI_NOT_FOUND;
  }

  ZeroMem (&mT, sizeof (mT));
  mT.Frequency = GetPerformanceCounterProperties (&mT.CounterStart, &mT.CounterEnd);
  if ((mT.Frequency < 1000000ULL) || (mT.CounterStart == mT.CounterEnd)) {
    return EFI_UNSUPPORTED;
  }
  mT.SpiHalfBitTicks = (mT.Frequency + 9999999ULL) / 10000000ULL;
  if (mT.SpiHalfBitTicks < 2ULL) {
    mT.SpiHalfBitTicks = 2ULL;
  }

  mT.Started    = TRUE;
  mT.Step       = TRails;
  mT.MaxX       = TCM2_DEFAULT_MAX_X;
  mT.MaxY       = TCM2_DEFAULT_MAX_Y;
  mT.MaxObjects = TCM2_MAX_OBJECTS;
  mT.StartUs    = NowUs ();
  mT.NextUs     = mT.StartUs;
  mT.LastStatus = EFI_NOT_STARTED;

  DEBUG ((DEBUG_WARN, "NP05J_TOUCH_START status=%r gpio_spi=1 dma=0\n", EFI_SUCCESS));
  return EFI_SUCCESS;
}

VOID
Np05jTouchPump (
  VOID
  )
{
  if (!mT.Started || mT.Busy || mT.Stopping || mT.Retained ||
      (mT.Step == TUnavailable) || (mT.Step == TStopped) || (AtApp () != EFI_SUCCESS))
  {
    return;
  }

  mT.Busy = TRUE;
  if (mT.Step == TRuntime) {
    StepRuntime ();
  } else if (NowUs () >= mT.NextUs) {
    StepBringUp ();
  }
  mT.Busy = FALSE;
}

VOID
Np05jTouchReplay (
  VOID
  )
{
  DEBUG ((
    DEBUG_WARN,
    "NP05J_TOUCH_STATE step=%a status=%r published=%u retained=%u pins_owned=%u rails_on=%u frames=%u irqs=%u errors=%u events=%u\n",
    mStepNames[mT.Step],
    mT.LastStatus,
    mT.Published,
    mT.Retained,
    mT.PinsOwned,
    mT.RailsOn,
    mT.Frames,
    mT.Irqs,
    mT.Errors,
    mT.Events
    ));
}

EFI_STATUS
Np05jTouchStop (
  IN  VOID                              *Context,
  OUT PIANO_PRODUCT_INPUT_RETIRE_REPORT *Report
  )
{
  EFI_STATUS ProtoS, HwS, AppS;

  (VOID)Context;
  if (Report == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Report, sizeof (*Report));
  Report->Revision  = 1U;
  Report->Started   = mT.Started;
  Report->Timer     = EFI_SUCCESS;
  Report->Protocols = EFI_SUCCESS;

  if (!Live ()) {
    Report->ServicesLost = TRUE;
    Report->Retained     = mT.Started;
    return Report->Status = EFI_ABORTED;
  }

  AppS = AtApp ();
  if (AppS != EFI_SUCCESS) {
    Report->Retained = mT.Started;
    return Report->Status = AppS;
  }

  if (!mT.Started || mT.Retained || mT.InTransfer) {
    Report->Retained = mT.Started;
    DEBUG ((
      DEBUG_WARN,
      "NP05J_TOUCH_RELEASE clean=0 not_restored=%a handoff=blocked\n",
      mT.Started ? (mT.InTransfer ? "transfer-in-flight" : "retained") : "none,not-started"
      ));
    return Report->Status = EFI_NOT_READY;
  }

  mT.Stopping       = TRUE;
  ProtoS            = ReleaseProtocols ();
  HwS               = ReleaseHardware ();
  Report->Returned  = TRUE;
  Report->Protocols = ProtoS;
  Report->ServicesLost = mT.Exited;
  Report->Clean     = (ProtoS == EFI_SUCCESS) && (HwS == EFI_SUCCESS) &&
                      mT.ReleaseClean && !Report->ServicesLost;
  Report->Retained  = !Report->Clean;
  mT.Retained       = Report->Retained;
  Report->Status    = Report->Clean ? EFI_SUCCESS : (HwS != EFI_SUCCESS ? HwS : ProtoS);
  mT.Step           = TStopped;

  DEBUG ((
    DEBUG_WARN,
    "NP05J_TOUCH_RELEASE clean=%u handoff=%a protocols=%r pins=%r rails=%r frames=%u irqs=%u errors=%u\n",
    Report->Clean,
    Report->Clean ? "ready" : "blocked",
    mT.ProtocolsStatus,
    mT.PinsStatus,
    mT.RailsStatus,
    mT.Frames,
    mT.Irqs,
    mT.Errors
    ));

  return Report->Status;
}
