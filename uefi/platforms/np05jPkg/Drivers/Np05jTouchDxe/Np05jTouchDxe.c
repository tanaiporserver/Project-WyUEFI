/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Synaptics S3930TB0A0 (TCM v2) UEFI Touchscreen Driver for nubia NP05J.

  Hardware facts (Device_np05j/sm8750-nubia-np05j-common.dtsi & synaptics_tcm2_spi.c):
    - TLMM base 0x0F100000, stride 0x1000 per pin
    - GPIO 48 = MISO, GPIO 49 = MOSI, GPIO 50 = CLK, GPIO 51 = CS
    - GPIO 161 = Reset (active LOW), GPIO 162 = IRQ (active LOW level)
    - RPMh VRM rails: ldob4 (1800 mV VDD) first, then ldob14 (3200 mV AVDD)
    - SPI Mode 0, 8-bit MSB-first, 11 us CS setup (spi-cs-setup-ns = 0x2af8)

  GPIO-SPI isolation, RPMh rail voting, and AbsolutePointer queueing adapted
  from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#include <Uefi.h>
#include <Guid/ConsoleInDevice.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/Np05jBiosConfigLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>
#include <Protocol/Np05jBiosConfig.h>
#include <Protocol/Np05jTouchInfo.h>

#include <Np05jBiosConfig.h>
#include <Np05jSynapticsTcm2.h>

#define TLMM_BASE           0x0F100000U
#define PIN_CTL(P)          (TLMM_BASE + (P) * 0x1000U)
#define PIN_IO(P)           (PIN_CTL(P) + 4U)
#define PIN_INTR_CFG(P)     (PIN_CTL(P) + 8U)

#define PIN_MISO            48U
#define PIN_MOSI            49U
#define PIN_CLK             50U
#define PIN_CS              51U
#define PIN_RST             161U
#define PIN_IRQ             162U

#define CTL_FIELDS          0x3FFU
#define EGPIO_PRESENT       BIT11
#define EGPIO_ENABLE        BIT12
#define PULL_NONE           0U
#define PULL_DOWN           1U
#define PULL_UP             3U
#define OE_BIT              BIT9

#define QUEUE_CAP           32U
#define LOGICAL_MAX         65535U

/* Native RPMh 1.1 / CmdDb ABI from Project SunUEFI (PianoPogoBoard.c) */
typedef struct {
  UINT64 Revision;
  VOID  *Init;
  EFI_STATUS (EFIAPI *ValidDrv)(UINT32, BOOLEAN *);
  VOID  *TcsCommands, *CreateInternal, *CreateExplicit;
  EFI_STATUS (EFIAPI *Create)(UINT32, CONST CHAR8 *, VOID **);
  VOID  *Invalidate, *RegisterCallback;
  EFI_STATUS (EFIAPI *Delete)(VOID *);
  VOID  *Find;
  EFI_STATUS (EFIAPI *Churn)(VOID *, UINT32);
  VOID  *Unused[7];
  EFI_STATUS (EFIAPI *Issue)(VOID *, UINT32, BOOLEAN, UINT32, UINT32, UINT32 *);
} NP05J_RPMH_PROTOCOL;

typedef struct {
  UINT64 Revision;
  VOID  *Query;
  EFI_STATUS (EFIAPI *Address)(VOID *, CONST CHAR8 *, UINT32 *);
} NP05J_CMD_DB_PROTOCOL;

typedef struct {
  UINT32  Address;
  UINT32  Before;
  UINT32  After;
  BOOLEAN Changed;
} NP05J_RAIL_VOTE;

STATIC CONST EFI_GUID mRpmhGuid  = { 0xe1d0b573, 0xf29f, 0x495d, { 0x93, 0x52, 0xca, 0x0d, 0xd4, 0x2a, 0x0a, 0x74 } };
STATIC CONST EFI_GUID mCmdDbGuid = { 0x1c34f691, 0xd33d, 0x4e14, { 0xb8, 0xf8, 0x32, 0xc6, 0xc0, 0x29, 0xe9, 0x5b } };

STATIC UINT32                       mSavedPinCtl[6];
STATIC UINT32                       mSavedPinIo[6];
STATIC CONST UINT32                 mPins[6] = { PIN_MISO, PIN_MOSI, PIN_CLK, PIN_CS, PIN_RST, PIN_IRQ };
STATIC UINT32                       mSavedIrqCfg;
STATIC BOOLEAN                      mPinsOwned     = FALSE;
STATIC BOOLEAN                      mRunning       = FALSE;
STATIC BOOLEAN                      mExited        = FALSE;
STATIC BOOLEAN                      mBusy          = FALSE;
STATIC BOOLEAN                      mPointerActive = FALSE;
STATIC BOOLEAN                      mKeepRailsForOs = FALSE;

STATIC NP05J_RPMH_PROTOCOL         *mRpmh          = NULL;
STATIC VOID                        *mPowerHandle   = NULL;
STATIC NP05J_RAIL_VOTE              mVotes[6];
STATIC UINT32                       mVoteCount     = 0;

STATIC EFI_HANDLE                   mControllerHandle = NULL;
STATIC EFI_HANDLE                   mInfoHandle       = NULL;
STATIC EFI_EVENT                    mTimerEvent       = NULL;
STATIC EFI_EVENT                    mExitEvent        = NULL;

STATIC EFI_ABSOLUTE_POINTER_MODE    mAbsMode = {
  0, 0, 0, LOGICAL_MAX, LOGICAL_MAX, 0, 0
};
STATIC EFI_ABSOLUTE_POINTER_PROTOCOL mAbsPointer;
STATIC NP05J_TOUCH_INFO_PROTOCOL     mTouchInfo;

STATIC EFI_ABSOLUTE_POINTER_STATE   mQueue[QUEUE_CAP];
STATIC EFI_ABSOLUTE_POINTER_STATE   mLastState;
STATIC UINTN                        mHead = 0;
STATIC UINTN                        mTail = 0;

STATIC TCM2_TOUCH_STATE             mTcmState;
STATIC UINT8                        mSeq = 0;
STATIC UINT16                       mMaxX = TCM2_DEFAULT_MAX_X;
STATIC UINT16                       mMaxY = TCM2_DEFAULT_MAX_Y;
STATIC UINT16                       mMaxObjects = TCM2_MAX_OBJECTS;
STATIC UINT8                        mReportCfg[TCM2_MAX_REPORT_CFG];
STATIC UINT16                       mReportCfgLen = 0;
STATIC UINT8                        mTxBuf[TCM2_MAX_XFER];
STATIC UINT8                        mRxBuf[TCM2_MAX_XFER];
STATIC UINT8                        mReportBuf[TCM2_MAX_XFER];

STATIC CHAR8                        mPartNumber[17] = "UNPROBED";
STATIC UINT32                       mBuildId        = 0;
STATIC UINT32                       mFrameCount     = 0;
STATIC UINT32                       mErrorCount     = 0;
STATIC UINT32                       mIrqCount       = 0;
STATIC UINT32                       mLastDispX      = 0;
STATIC UINT32                       mLastDispY      = 0;

STATIC UINT8                        mSwapXY         = 1;
STATIC UINT8                        mInvertX        = 0;
STATIC UINT8                        mInvertY        = 1;
STATIC UINT8                        mPollRateHz     = 100;

typedef struct {
  VENDOR_DEVICE_PATH       Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} NP05J_TOUCH_DEVICE_PATH;

STATIC NP05J_TOUCH_DEVICE_PATH mTouchPath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { (UINT8)sizeof (VENDOR_DEVICE_PATH), 0 } },
    { 0xA1E45004, 0x9B1C, 0x4F2A, { 0x8C, 0x3D, 0x10, 0x05, 0x4E, 0x50, 0x30, 0x35 } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { (UINT8)END_DEVICE_PATH_LENGTH, 0 } }
};

STATIC
VOID
DrivePin (
  IN UINT32  Pin,
  IN BOOLEAN High
  )
{
  MmioWrite32 (PIN_IO (Pin), High ? BIT1 : 0U);
}

STATIC
UINT32
BuildGpioCtl (
  IN UINT32  OrigCtl,
  IN BOOLEAN Output,
  IN UINT32  Pull,
  IN UINT32  DriveMaBits
  )
{
  UINT32 Value = (OrigCtl & ~(CTL_FIELDS | EGPIO_ENABLE)) | (DriveMaBits << 6) | Pull | (Output ? OE_BIT : 0U);
  if ((Value & EGPIO_PRESENT) != 0U) {
    Value |= EGPIO_ENABLE;
  }
  return Value;
}

STATIC
VOID
SpiBitDelay (
  VOID
  )
{
#ifdef __aarch64__
  /* ใช้ ARM Generic Timer tick spin โดยตรง (~100 ns) แทน NanoSecondDelay ที่ overhead สูง */
  STATIC UINT64 HalfBitTicks = 0ULL;
  if (HalfBitTicks == 0ULL) {
    UINT64 Freq = 0ULL;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(Freq));
    HalfBitTicks = (Freq + 9999999ULL) / 10000000ULL;
    if (HalfBitTicks < 2ULL) {
      HalfBitTicks = 2ULL;
    }
  }
  UINT64 Start = 0ULL;
  UINT64 Now   = 0ULL;
  __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(Start));
  do {
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(Now));
  } while ((Now - Start) < HalfBitTicks);
#else
  NanoSecondDelay (100);
#endif
}

STATIC
UINT8
SpiByte (
  IN UINT8 Tx
  )
{
  UINT8 Rx = 0;

  for (UINTN Bit = 0; Bit < 8; Bit++) {
    DrivePin (PIN_MOSI, (Tx & 0x80U) != 0U);
    Tx = (UINT8)(Tx << 1);
    SpiBitDelay ();
    DrivePin (PIN_CLK, TRUE);
    SpiBitDelay ();
    Rx = (UINT8)((Rx << 1) | (MmioRead32 (PIN_IO (PIN_MISO)) & 1U));
    DrivePin (PIN_CLK, FALSE);
  }

  return Rx;
}

STATIC
VOID
SpiTransfer (
  IN  CONST UINT8 *Tx,
  OUT UINT8       *Rx,
  IN  UINTN        Len
  )
{
  DrivePin (PIN_CS, FALSE);
  MicroSecondDelay (12); /* DTS spi-cs-setup-ns = 0x2af8 (11000 ns) */
  for (UINTN Index = 0; Index < Len; Index++) {
    UINT8 ByteOut = (Tx != NULL) ? Tx[Index] : 0xFFU;
    UINT8 ByteIn  = SpiByte (ByteOut);
    if (Rx != NULL) {
      Rx[Index] = ByteIn;
    }
  }
  DrivePin (PIN_CS, TRUE);
  MicroSecondDelay (4);
}

STATIC
EFI_STATUS
Tcm2ReadReportFast (
  OUT UINT8  *CodeOut,
  OUT UINT8  *PayloadOut,
  IN  UINT16  MaxPayload,
  OUT UINT16 *PayloadLenOut
  )
{
  UINT16 Plen;
  UINTN  TailLen;

  /* อ่านเฉพาะส่วนหัว 4 ไบต์ก่อน แล้วอ่าน payload + CRC16 ตามความยาวจริงใน CS pulse เดียว */
  DrivePin (PIN_CS, FALSE);
  MicroSecondDelay (12); /* DTS spi-cs-setup-ns = 0x2af8 (11000 ns) */
  for (UINTN Index = 0; Index < TCM2_HDR_SIZE; Index++) {
    mRxBuf[Index] = SpiByte (0xFFU);
  }

  if (!Tcm2HdrCrcOk (mRxBuf)) {
    DrivePin (PIN_CS, TRUE);
    MicroSecondDelay (4);
    return EFI_CRC_ERROR;
  }

  Plen = Tcm2GetLe16 (mRxBuf + 1);
  if ((mRxBuf[0] == TCM2_STATUS_WAIT) || (mRxBuf[0] == TCM2_STATUS_IDLE) || (mRxBuf[0] == TCM2_STATUS_INVALID)) {
    DrivePin (PIN_CS, TRUE);
    MicroSecondDelay (4);
    *CodeOut       = mRxBuf[0];
    *PayloadLenOut = 0U;
    return EFI_SUCCESS;
  }

  TailLen = (Plen > 0U) ? ((UINTN)Plen + TCM2_CRC16_SIZE) : 0U;
  if (TailLen > (TCM2_MAX_XFER - TCM2_HDR_SIZE)) {
    TailLen = TCM2_MAX_XFER - TCM2_HDR_SIZE;
  }
  for (UINTN Index = 0; Index < TailLen; Index++) {
    mRxBuf[TCM2_HDR_SIZE + Index] = SpiByte (0xFFU);
  }
  DrivePin (PIN_CS, TRUE);
  MicroSecondDelay (4);

  *CodeOut       = mRxBuf[0];
  *PayloadLenOut = Plen;
  if ((Plen > 0U) && (PayloadOut != NULL) && (MaxPayload > 0U)) {
    UINT16 CopyLen = (Plen < MaxPayload) ? Plen : MaxPayload;
    if (CopyLen > (TCM2_MAX_XFER - TCM2_HDR_SIZE)) {
      CopyLen = (UINT16)(TCM2_MAX_XFER - TCM2_HDR_SIZE);
    }
    CopyMem (PayloadOut, mRxBuf + TCM2_HDR_SIZE, CopyLen);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Tcm2SendPacket (
  IN UINT8        Cmd,
  IN CONST UINT8 *Payload,
  IN UINT16       PayloadLen
  )
{
  UINT32 Total = Tcm2BuildPacket (mTxBuf, sizeof (mTxBuf), Cmd, mSeq, Payload, PayloadLen);
  if (Total == 0U) {
    return EFI_INVALID_PARAMETER;
  }

  SpiTransfer (mTxBuf, mRxBuf, Total);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Tcm2ReadPacket (
  IN  UINTN   ReadLen,
  OUT UINT8  *CodeOut,
  OUT UINT8  *PayloadOut,
  IN  UINT16  MaxPayload,
  OUT UINT16 *PayloadLenOut
  )
{
  UINT16 Plen;

  if (ReadLen < TCM2_HDR_SIZE) {
    ReadLen = TCM2_HDR_SIZE;
  }
  if (ReadLen > TCM2_MAX_XFER) {
    ReadLen = TCM2_MAX_XFER;
  }

  SetMem (mTxBuf, ReadLen, 0xFF);
  SpiTransfer (mTxBuf, mRxBuf, ReadLen);

  if (!Tcm2HdrCrcOk (mRxBuf)) {
    return EFI_CRC_ERROR;
  }

  Plen = Tcm2GetLe16 (mRxBuf + 1);
  if (CodeOut != NULL) {
    *CodeOut = mRxBuf[0];
  }
  if (PayloadLenOut != NULL) {
    *PayloadLenOut = Plen;
  }

  if ((Plen > 0U) && (PayloadOut != NULL) && (MaxPayload > 0U)) {
    UINTN  Avail   = (ReadLen > TCM2_HDR_SIZE) ? (ReadLen - TCM2_HDR_SIZE) : 0U;
    UINT16 CopyLen = (Plen < MaxPayload) ? Plen : MaxPayload;
    if (CopyLen > Avail) {
      CopyLen = (UINT16)Avail;
    }
    CopyMem (PayloadOut, mRxBuf + TCM2_HDR_SIZE, CopyLen);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Tcm2Command (
  IN  UINT8        Cmd,
  IN  CONST UINT8 *TxPayload,
  IN  UINT16       TxLen,
  OUT UINT8       *RespCode,
  OUT UINT8       *RxPayload,
  IN  UINT16       RxMax,
  OUT UINT16      *RxLen,
  IN  UINTN        ReadSize,
  IN  UINT32       WaitUs
  )
{
  UINT8      Code = 0;
  UINT16     Plen = 0;
  EFI_STATUS Status;

  for (UINTN Attempt = 0; Attempt < 3; Attempt++) {
    Status = Tcm2SendPacket (Cmd, TxPayload, TxLen);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    for (UINTN Poll = 0; Poll < 15; Poll++) {
      if (WaitUs > 0U) {
        MicroSecondDelay (WaitUs);
      }

      Status = Tcm2ReadPacket (ReadSize, &Code, RxPayload, RxMax, &Plen);
      if (EFI_ERROR (Status)) {
        MicroSecondDelay (1000);
        continue;
      }

      if (Code == TCM2_STATUS_WAIT) {
        MicroSecondDelay (2500);
        continue;
      }

      if (Code == TCM2_STATUS_INVALID) {
        mSeq ^= 1U;
        break;
      }

      mSeq ^= 1U;
      if (RespCode != NULL) {
        *RespCode = Code;
      }
      if (RxLen != NULL) {
        *RxLen = Plen;
      }
      return EFI_SUCCESS;
    }
  }

  return EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
EnableTouchRails (
  VOID
  )
{
  EFI_STATUS             Status;
  NP05J_CMD_DB_PROTOCOL *Db    = NULL;
  BOOLEAN                Valid = FALSE;
  STATIC CONST struct {
    CONST CHAR8 *Name;
    UINT32       Mv;
  } Rails[2] = {
    { "ldob4",  1800 }, /* VDD 1.8V first */
    { "ldob14", 3200 }  /* AVDD 3.2V second */
  };

  if (mPowerHandle != NULL) {
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] EnableTouchRails locate RPMh/CmdDb\n"));

  if (EFI_ERROR (gBS->LocateProtocol ((EFI_GUID *)&mRpmhGuid, NULL, (VOID **)&mRpmh)) ||
      EFI_ERROR (gBS->LocateProtocol ((EFI_GUID *)&mCmdDbGuid, NULL, (VOID **)&Db)))
  {
    mRpmh = NULL;
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] RPMh/CmdDb protocol not found\n"));
    return EFI_NOT_FOUND;
  }

  if ((mRpmh == NULL) || (Db == NULL) || (mRpmh->Revision != 0x10001ULL) ||
      (mRpmh->ValidDrv == NULL) || (mRpmh->Create == NULL) || (mRpmh->Issue == NULL) ||
      (mRpmh->Churn == NULL) || (mRpmh->Delete == NULL) ||
      (Db->Revision != 0x10000ULL) || (Db->Address == NULL))
  {
    mRpmh = NULL;
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] RPMh/CmdDb revision/table mismatch\n"));
    return EFI_UNSUPPORTED;
  }

  Status = mRpmh->ValidDrv (2, &Valid);
  if (EFI_ERROR (Status) || !Valid) {
    mRpmh = NULL;
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] RPMh ValidDrv(2) failed (%r, valid=%u)\n", Status, (UINT32)Valid));
    return EFI_UNSUPPORTED;
  }

  mVoteCount = 0;
  for (UINTN I = 0; I < ARRAY_SIZE (Rails); I++) {
    UINT32 Base = 0;
    Status = Db->Address (Db, Rails[I].Name, &Base);
    if (EFI_ERROR (Status) || ((Base >> 16) != 4U) || ((Base & 255U) != 0U)) {
      DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] CmdDb '%a' lookup failed (%r, addr=0x%08x)\n", Rails[I].Name, Status, Base));
      return EFI_UNSUPPORTED;
    }

    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] CmdDb '%a' addr=0x%08x target=%u mV\n", Rails[I].Name, Base, Rails[I].Mv));

    for (UINT32 J = 0; J < 3; J++) {
      UINT32 Addr = Base + J * 4U;
      UINT32 Want = (J == 0U) ? Rails[I].Mv : ((J == 1U) ? 1U : 7U);

      mVotes[mVoteCount].Address = Addr;
      mVotes[mVoteCount].Before  = 0U;
      mVotes[mVoteCount].After   = Want;
      mVotes[mVoteCount].Changed = FALSE;
      mVoteCount++;
    }
  }

  Status = mRpmh->Create (2, "NP05J Touch", &mPowerHandle);
  if (EFI_ERROR (Status) || (mPowerHandle == NULL)) {
    mPowerHandle = NULL;
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] RPMh Create failed (%r)\n", Status));
    return EFI_OUT_OF_RESOURCES;
  }

  for (UINT32 I = 0; I < mVoteCount; I++) {
    UINT32 Req = 0;
    mVotes[I].Changed = TRUE;
    Status = mRpmh->Issue (mPowerHandle, 0, TRUE, mVotes[I].Address, mVotes[I].After, &Req);
    if (!EFI_ERROR (Status)) {
      Status = mRpmh->Churn (mPowerHandle, Req);
    }
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] RPMh vote addr=%08x failed: %r\n", mVotes[I].Address, Status));
      return Status;
    }
  }

  MicroSecondDelay (20000);
  DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [1] EnableTouchRails ok (%u votes)\n", mVoteCount));
  return EFI_SUCCESS;
}

STATIC
VOID
ReleaseTouchRails (
  VOID
  )
{
  if ((mRpmh == NULL) || (mPowerHandle == NULL)) {
    return;
  }

  for (UINT32 I = mVoteCount; I > 0; I--) {
    if (mVotes[I - 1].Changed) {
      UINT32 Req = 0;
      if (!EFI_ERROR (mRpmh->Issue (mPowerHandle, 0, TRUE, mVotes[I - 1].Address, mVotes[I - 1].Before, &Req))) {
        mRpmh->Churn (mPowerHandle, Req);
      }
      mVotes[I - 1].Changed = FALSE;
    }
  }

  mRpmh->Delete (mPowerHandle);
  mPowerHandle = NULL;
}

STATIC
EFI_STATUS
AcquirePinsAndClocks (
  VOID
  )
{
  DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [2] AcquirePins TLMM SPI4 (pins 48..51,161,162)\n"));

  if (mPinsOwned) {
    return EFI_SUCCESS;
  }

  for (UINTN I = 0; I < ARRAY_SIZE (mPins); I++) {
    mSavedPinCtl[I] = MmioRead32 (PIN_CTL (mPins[I]));
    mSavedPinIo[I]  = MmioRead32 (PIN_IO (mPins[I]));
    if (mSavedPinCtl[I] == MAX_UINT32) {
      DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [2] TLMM pin %u returned 0xFFFFFFFF\n", mPins[I]));
      return EFI_UNSUPPORTED;
    }
  }
  mSavedIrqCfg = MmioRead32 (PIN_INTR_CFG (PIN_IRQ));
  mPinsOwned   = TRUE;

  /* MISO (input, pull-down) */
  MmioWrite32 (PIN_CTL (PIN_MISO), BuildGpioCtl (mSavedPinCtl[0], FALSE, PULL_DOWN, 2));

  /* Preset idle SPI Mode 0 levels before enabling output */
  DrivePin (PIN_CS, TRUE);
  DrivePin (PIN_CLK, FALSE);
  DrivePin (PIN_MOSI, FALSE);
  DrivePin (PIN_RST, TRUE);
  MemoryFence ();

  MmioWrite32 (PIN_CTL (PIN_MOSI), BuildGpioCtl (mSavedPinCtl[1], TRUE, PULL_NONE, 2));
  MmioWrite32 (PIN_CTL (PIN_CLK),  BuildGpioCtl (mSavedPinCtl[2], TRUE, PULL_NONE, 2));
  MmioWrite32 (PIN_CTL (PIN_CS),   BuildGpioCtl (mSavedPinCtl[3], TRUE, PULL_UP,   2));
  MmioWrite32 (PIN_CTL (PIN_RST),  BuildGpioCtl (mSavedPinCtl[4], TRUE, PULL_UP,   2));

  /* IRQ: input pull-up, disable TLMM summary/PDC CPU interrupt */
  MmioWrite32 (PIN_INTR_CFG (PIN_IRQ), mSavedIrqCfg & ~(BIT0 | BIT7));
  MmioWrite32 (PIN_CTL (PIN_IRQ), BuildGpioCtl (mSavedPinCtl[5], FALSE, PULL_UP, 2));
  MemoryFence ();

  DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [2] AcquirePins ok (irq_io=0x%x)\n", MmioRead32 (PIN_IO (PIN_IRQ))));
  return EFI_SUCCESS;
}

STATIC
VOID
RestorePinsAndClocks (
  VOID
  )
{
  if (mPinsOwned) {
    DrivePin (PIN_CS, TRUE);
    DrivePin (PIN_CLK, FALSE);

    /* Disable OE on SPI/Reset pins before restoring latches and muxes */
    for (UINTN I = 0; I < 5; I++) {
      MmioAnd32 (PIN_CTL (mPins[I]), ~OE_BIT);
    }
    for (UINTN I = 0; I < ARRAY_SIZE (mPins); I++) {
      MmioWrite32 (PIN_IO (mPins[I]), mSavedPinIo[I] & BIT1);
      MmioWrite32 (PIN_CTL (mPins[I]), mSavedPinCtl[I]);
    }
    MmioWrite32 (PIN_INTR_CFG (PIN_IRQ), mSavedIrqCfg);
    MemoryFence ();
    mPinsOwned = FALSE;
  }
}

STATIC
EFI_STATUS
ProbeAndInitS3930 (
  VOID
  )
{
  EFI_STATUS Status;
  UINT8      Buf[TCM2_MAX_REPORT_CFG];
  UINT8      Code        = 0;
  UINT16     Plen        = 0;
  UINT8      EnableTouch = TCM2_REPORT_TOUCH;

  mSeq = 0;
  ZeroMem (&mTcmState, sizeof (mTcmState));

  /* Hardware reset pulse (active LOW: 20 ms low, 200 ms boot delay) */
  DrivePin (PIN_RST, FALSE);
  MicroSecondDelay (20000);
  DrivePin (PIN_RST, TRUE);
  MicroSecondDelay (200000);

  /* Drain startup identify report */
  Tcm2SendPacket (TCM2_CMD_ACK, NULL, 0);
  MicroSecondDelay (8000);
  Tcm2ReadPacket (64, &Code, Buf, sizeof (Buf), &Plen);

  /* 1. CMD_IDENTIFY */
  ZeroMem (Buf, sizeof (Buf));
  Status = Tcm2Command (
             TCM2_CMD_IDENTIFY,
             NULL,
             0,
             &Code,
             Buf,
             sizeof (Buf),
             &Plen,
             64,
             15000
             );
  if (EFI_ERROR (Status) || ((Code != TCM2_STATUS_OK) && (Code != TCM2_REPORT_IDENTIFY)) || (Plen < 24U)) {
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: CMD_IDENTIFY failed Status=%r Code=0x%02x Len=%u\n", Status, Code, Plen));
    return EFI_DEVICE_ERROR;
  }

  CopyMem (mPartNumber, Buf + 2, 16);
  for (UINTN I = 0; I < 16; I++) {
    if ((mPartNumber[I] < 0x20) || (mPartNumber[I] > 0x7E)) {
      mPartNumber[I] = '\0';
      break;
    }
  }
  mPartNumber[16] = '\0';
  mBuildId        = Tcm2GetLe32 (Buf + 18);

  /* 2. CMD_GET_APP_INFO */
  ZeroMem (Buf, sizeof (Buf));
  Status = Tcm2Command (
             TCM2_CMD_GET_APP_INFO,
             NULL,
             0,
             &Code,
             Buf,
             sizeof (Buf),
             &Plen,
             80,
             15000
             );
  if (!EFI_ERROR (Status) && (Code == TCM2_STATUS_OK) && (Plen >= 38U)) {
    mMaxX       = Tcm2GetLe16 (Buf + 32);
    mMaxY       = Tcm2GetLe16 (Buf + 34);
    mMaxObjects = Tcm2GetLe16 (Buf + 36);
  }
  if ((mMaxX == 0U) || (mMaxY == 0U)) {
    mMaxX = TCM2_DEFAULT_MAX_X;
    mMaxY = TCM2_DEFAULT_MAX_Y;
  }
  if ((mMaxObjects == 0U) || (mMaxObjects > TCM2_MAX_OBJECTS)) {
    mMaxObjects = TCM2_MAX_OBJECTS;
  }

  /* 3. CMD_GET_TOUCH_REPORT_CONFIG */
  ZeroMem (Buf, sizeof (Buf));
  Status = Tcm2Command (
             TCM2_CMD_GET_TOUCH_REPORT_CFG,
             NULL,
             0,
             &Code,
             Buf,
             sizeof (Buf),
             &Plen,
             256,
             15000
             );
  if (!EFI_ERROR (Status) && (Code == TCM2_STATUS_OK) && (Plen > 0U)) {
    mReportCfgLen = (Plen < TCM2_MAX_REPORT_CFG) ? Plen : TCM2_MAX_REPORT_CFG;
    CopyMem (mReportCfg, Buf, mReportCfgLen);
    DEBUG ((
      DEBUG_WARN,
      "NP05J_TOUCH: cfg_len=%u head=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
      mReportCfgLen,
      (mReportCfgLen > 0U)  ? mReportCfg[0]  : 0U,
      (mReportCfgLen > 1U)  ? mReportCfg[1]  : 0U,
      (mReportCfgLen > 2U)  ? mReportCfg[2]  : 0U,
      (mReportCfgLen > 3U)  ? mReportCfg[3]  : 0U,
      (mReportCfgLen > 4U)  ? mReportCfg[4]  : 0U,
      (mReportCfgLen > 5U)  ? mReportCfg[5]  : 0U,
      (mReportCfgLen > 6U)  ? mReportCfg[6]  : 0U,
      (mReportCfgLen > 7U)  ? mReportCfg[7]  : 0U,
      (mReportCfgLen > 8U)  ? mReportCfg[8]  : 0U,
      (mReportCfgLen > 9U)  ? mReportCfg[9]  : 0U,
      (mReportCfgLen > 10U) ? mReportCfg[10] : 0U,
      (mReportCfgLen > 11U) ? mReportCfg[11] : 0U,
      (mReportCfgLen > 12U) ? mReportCfg[12] : 0U,
      (mReportCfgLen > 13U) ? mReportCfg[13] : 0U,
      (mReportCfgLen > 14U) ? mReportCfg[14] : 0U,
      (mReportCfgLen > 15U) ? mReportCfg[15] : 0U
      ));
  } else {
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: GET_TOUCH_REPORT_CFG failed Status=%r Code=0x%02x\n", Status, Code));
    return EFI_DEVICE_ERROR;
  }

  /* 4. CMD_ENABLE_REPORT(0x11) */
  Status = Tcm2Command (
             TCM2_CMD_ENABLE_REPORT,
             &EnableTouch,
             1,
             &Code,
             Buf,
             sizeof (Buf),
             &Plen,
             32,
             15000
             );

  DEBUG ((
    DEBUG_WARN,
    "NP05J_TOUCH: S3930 ready part='%a' build=%u res=%ux%u objs=%u cfg=%u en=%r/%02x\n",
    mPartNumber,
    mBuildId,
    mMaxX,
    mMaxY,
    mMaxObjects,
    mReportCfgLen,
    Status,
    Code
    ));

  return EFI_SUCCESS;
}

STATIC
VOID
EnqueuePointerSample (
  IN UINT32  RawX,
  IN UINT32  RawY,
  IN BOOLEAN Pressed
  )
{
  EFI_ABSOLUTE_POINTER_STATE State;
  UINT32                     LogX = 0;
  UINT32                     LogY = 0;
  UINTN                      Next;

  Tcm2MapToDisplay (
    RawX,
    RawY,
    mMaxX,
    mMaxY,
    LOGICAL_MAX + 1U,
    LOGICAL_MAX + 1U,
    mSwapXY,
    mInvertX,
    mInvertY,
    &LogX,
    &LogY
    );

  Tcm2MapToDisplay (
    RawX,
    RawY,
    mMaxX,
    mMaxY,
    2400U,
    1504U,
    mSwapXY,
    mInvertX,
    mInvertY,
    &mLastDispX,
    &mLastDispY
    );

  ZeroMem (&State, sizeof (State));
  State.CurrentX      = (UINT64)LogX;
  State.CurrentY      = (UINT64)LogY;
  State.ActiveButtons = Pressed ? EFI_ABSP_TouchActive : 0U;

  if (CompareMem (&State, &mLastState, sizeof (State)) == 0) {
    return;
  }

  Next = (mTail + 1U) % QUEUE_CAP;
  if (Next == mHead) {
    return;
  }

  mQueue[mTail] = State;
  mTail         = Next;
  mLastState    = State;
  gBS->SignalEvent (mAbsPointer.WaitForInput);
}

STATIC
VOID
EFIAPI
TouchPollTimer (
  IN EFI_EVENT Event,
  IN VOID     *Context
  )
{
  STATIC BOOLEAN mLoggedFirstTouch   = FALSE;
  STATIC BOOLEAN mLoggedFirstRelease = FALSE;
  STATIC BOOLEAN mLoggedFirstErr     = FALSE;
  UINT8          Code                = 0;
  UINT16         Plen                = 0;
  EFI_STATUS     Status;

  if (!mRunning || mExited || mBusy || (mReportCfgLen == 0U)) {
    return;
  }

  mBusy = TRUE;

  for (UINTN Burst = 0; Burst < 8; Burst++) {
    /* Active-LOW IRQ check on GPIO 162 */
    if ((MmioRead32 (PIN_IO (PIN_IRQ)) & 1U) != 0U) {
      break;
    }

    mIrqCount++;
    Status = EFI_DEVICE_ERROR;
    for (UINTN Attempt = 0; Attempt < 2; Attempt++) {
      Status = Tcm2SendPacket (TCM2_CMD_GET_REPORT, NULL, 0);
      if (EFI_ERROR (Status)) {
        break;
      }
      for (UINTN Poll = 0; Poll < 8; Poll++) {
        MicroSecondDelay (Poll == 0U ? 120U : 250U);
        Status = Tcm2ReadReportFast (&Code, mReportBuf, (UINT16)sizeof (mReportBuf), &Plen);
        if (EFI_ERROR (Status)) {
          continue;
        }
        if ((Code == TCM2_STATUS_WAIT) || (Code == TCM2_STATUS_IDLE)) {
          continue;
        }
        if (Code == TCM2_STATUS_INVALID) {
          mSeq  ^= 1U;
          Status = EFI_DEVICE_ERROR;
          break;
        }
        mSeq ^= 1U;
        break;
      }
      if (!EFI_ERROR (Status) && (Code != TCM2_STATUS_WAIT) && (Code != TCM2_STATUS_IDLE)) {
        break;
      }
    }
    if (EFI_ERROR (Status)) {
      mErrorCount++;
      if (!mLoggedFirstErr) {
        mLoggedFirstErr = TRUE;
        DEBUG ((DEBUG_WARN, "NP05J_TOUCH: first report err Status=%r Code=0x%02x Plen=%u\n", Status, Code, Plen));
      }
      break;
    }

    if ((Code == TCM2_REPORT_TOUCH) && (Plen > 0U)) {
      UINT16 Avail = (Plen < sizeof (mReportBuf)) ? Plen : (UINT16)sizeof (mReportBuf);
      if (Tcm2ParseTouchReport (
            mReportCfg,
            mReportCfgLen,
            mReportBuf,
            Avail,
            mMaxObjects,
            mMaxX,
            mMaxY,
            &mTcmState
            ) == 0)
      {
        mFrameCount++;
        EnqueuePointerSample (mTcmState.RawX, mTcmState.RawY, mTcmState.Pressed != 0U);
        if ((mTcmState.Pressed != 0U) && !mLoggedFirstTouch) {
          mLoggedFirstTouch = TRUE;
          DEBUG ((
            DEBUG_WARN,
            "NP05J_TOUCH: first touch raw=%u,%u disp=%u,%u\n",
            mTcmState.RawX,
            mTcmState.RawY,
            mLastDispX,
            mLastDispY
            ));
        } else if ((mTcmState.Pressed == 0U) && !mLoggedFirstRelease) {
          mLoggedFirstRelease = TRUE;
          DEBUG ((DEBUG_WARN, "NP05J_TOUCH: first release disp=%u,%u\n", mLastDispX, mLastDispY));
        }
      } else {
        mErrorCount++;
        if (!mLoggedFirstErr) {
          mLoggedFirstErr = TRUE;
          DEBUG ((DEBUG_WARN, "NP05J_TOUCH: first parse err Avail=%u cfg_len=%u\n", Avail, mReportCfgLen));
        }
      }
    }
  }

  mBusy = FALSE;
}

STATIC
VOID
EFIAPI
TouchWaitForInput (
  IN EFI_EVENT Event,
  IN VOID     *Context
  )
{
  if (!mExited && (mHead != mTail)) {
    gBS->SignalEvent (Event);
  }
}

STATIC
EFI_STATUS
EFIAPI
TouchReset (
  IN EFI_ABSOLUTE_POINTER_PROTOCOL *This,
  IN BOOLEAN                        ExtendedVerification
  )
{
  EFI_TPL OldTpl;

  if ((This != &mAbsPointer) || mExited || !mRunning) {
    return EFI_NOT_READY;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  mHead  = 0;
  mTail  = 0;
  ZeroMem (&mLastState, sizeof (mLastState));
  mTcmState.HasPrimary = 0;
  mTcmState.Pressed    = 0;
  gBS->RestoreTPL (OldTpl);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TouchGetState (
  IN     EFI_ABSOLUTE_POINTER_PROTOCOL *This,
  IN OUT EFI_ABSOLUTE_POINTER_STATE    *State
  )
{
  EFI_TPL    OldTpl;
  EFI_STATUS Status = EFI_NOT_READY;

  if ((This != &mAbsPointer) || (State == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if (mExited || (!mRunning && (mHead == mTail))) {
    return EFI_NOT_READY;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (mHead != mTail) {
    *State = mQueue[mHead];
    mHead  = (mHead + 1U) % QUEUE_CAP;
    Status = EFI_SUCCESS;
  }
  gBS->RestoreTPL (OldTpl);

  return Status;
}

STATIC
VOID
EFIAPI
TouchExitBootServices (
  IN EFI_EVENT Event,
  IN VOID     *Context
  )
{
  mRunning = FALSE;
  mExited  = TRUE;
  if (mTimerEvent != NULL) {
    gBS->SetTimer (mTimerEvent, TimerCancel, 0);
  }
  /* เมื่อเปิด ACPI Touch สำหรับ Windows/WinPE ให้คงไฟเลี้ยง RPMh (ldob4/ldob14) และขา TLMM
   * ไว้ให้ไดรเวอร์ Np05jSynaTouch.sys รับช่วงต่อได้ทันที (ไม่มี DMA ที่ต้อง quiesce)
   */
  if (!mKeepRailsForOs) {
    RestorePinsAndClocks ();
    ReleaseTouchRails ();
  }
}

STATIC
EFI_STATUS
EFIAPI
TouchInfoGetTelemetry (
  IN  NP05J_TOUCH_INFO_PROTOCOL *This,
  OUT NP05J_TOUCH_TELEMETRY     *Telemetry
  )
{
  if ((This == NULL) || (Telemetry == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Telemetry, sizeof (*Telemetry));
  Telemetry->Initialized = mRunning;
  Telemetry->TouchDown   = (mTcmState.Pressed != 0U);
  CopyMem (Telemetry->PartNumber, mPartNumber, sizeof (Telemetry->PartNumber));
  Telemetry->BuildId     = mBuildId;
  Telemetry->MaxX        = mMaxX;
  Telemetry->MaxY        = mMaxY;
  Telemetry->MaxObjects  = mMaxObjects;
  Telemetry->LastRawX    = mTcmState.RawX;
  Telemetry->LastRawY    = mTcmState.RawY;
  Telemetry->LastDispX   = mLastDispX;
  Telemetry->LastDispY   = mLastDispY;
  Telemetry->FrameCount  = mFrameCount;
  Telemetry->ErrorCount  = mErrorCount;
  Telemetry->IrqCount    = mIrqCount;
  Telemetry->SwapXY      = mSwapXY;
  Telemetry->InvertX     = mInvertX;
  Telemetry->InvertY     = mInvertY;
  Telemetry->PollRateHz  = mPollRateHz;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
StartTouchHardware (
  VOID
  );

STATIC
EFI_STATUS
EFIAPI
TouchInfoReinit (
  IN NP05J_TOUCH_INFO_PROTOCOL *This
  )
{
  if (mExited) {
    return EFI_NOT_READY;
  }

  mRunning = FALSE;
  if (mTimerEvent != NULL) {
    gBS->SetTimer (mTimerEvent, TimerCancel, 0);
  }
  RestorePinsAndClocks ();
  ReleaseTouchRails ();

  return StartTouchHardware ();
}

STATIC
EFI_STATUS
EFIAPI
TouchInfoApplyCalibration (
  IN NP05J_TOUCH_INFO_PROTOCOL *This,
  IN UINT8                      SwapXY,
  IN UINT8                      InvertX,
  IN UINT8                      InvertY,
  IN UINT8                      PollRateHz
  )
{
  UINT64 Period100Ns;

  mSwapXY     = (SwapXY != 0U) ? 1U : 0U;
  mInvertX    = (InvertX != 0U) ? 1U : 0U;
  mInvertY    = (InvertY != 0U) ? 1U : 0U;
  mPollRateHz = (PollRateHz >= 100U) ? 100U : ((PollRateHz >= 50U) ? PollRateHz : 50U);

  if (mRunning && (mTimerEvent != NULL)) {
    Period100Ns = (1000ULL / (UINT64)mPollRateHz) * 10000ULL;
    gBS->SetTimer (mTimerEvent, TimerPeriodic, Period100Ns);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
StartTouchHardware (
  VOID
  )
{
  EFI_STATUS Status;
  UINT64     Period100Ns;

  Status = EnableTouchRails ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: RPMh rail vote skipped/failed (%r), probing existing rails\n", Status));
  }

  Status = AcquirePinsAndClocks ();
  if (EFI_ERROR (Status)) {
    RestorePinsAndClocks ();
    ReleaseTouchRails ();
    return Status;
  }

  DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [3] ProbeAndInitS3930 start\n"));
  Status = ProbeAndInitS3930 ();
  if (EFI_ERROR (Status)) {
    RestorePinsAndClocks ();
    ReleaseTouchRails ();
    return Status;
  }

  if (!mPointerActive) {
    mAbsPointer.Reset    = TouchReset;
    mAbsPointer.GetState = TouchGetState;
    mAbsPointer.Mode     = &mAbsMode;

    Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, TouchWaitForInput, NULL, &mAbsPointer.WaitForInput);
    if (EFI_ERROR (Status)) {
      RestorePinsAndClocks ();
      ReleaseTouchRails ();
      return Status;
    }

    Status = gBS->InstallMultipleProtocolInterfaces (
                    &mControllerHandle,
                    &gEfiAbsolutePointerProtocolGuid,
                    &mAbsPointer,
                    &gEfiConsoleInDeviceGuid,
                    NULL,
                    &gEfiDevicePathProtocolGuid,
                    &mTouchPath,
                    NULL
                    );
    if (EFI_ERROR (Status)) {
      gBS->CloseEvent (mAbsPointer.WaitForInput);
      mAbsPointer.WaitForInput = NULL;
      RestorePinsAndClocks ();
      ReleaseTouchRails ();
      return Status;
    }

    mPointerActive = TRUE;
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: [4] AbsPointer installed, connecting ConSplitter\n"));
    gBS->ConnectController (mControllerHandle, NULL, NULL, TRUE);
  }

  if (mTimerEvent == NULL) {
    Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, TouchPollTimer, NULL, &mTimerEvent);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (mExitEvent == NULL) {
    gBS->CreateEventEx (
           EVT_NOTIFY_SIGNAL,
           TPL_CALLBACK,
           TouchExitBootServices,
           NULL,
           &gEfiEventExitBootServicesGuid,
           &mExitEvent
           );
  }

  mRunning    = TRUE;
  Period100Ns = (1000ULL / (UINT64)((mPollRateHz > 0U) ? mPollRateHz : 50U)) * 10000ULL;
  gBS->SetTimer (mTimerEvent, TimerPeriodic, Period100Ns);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Np05jTouchDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  NP05J_BIOS_CONFIG Config;
  EFI_STATUS        Status;

  Np05jBiosConfigLoad (&Config);
  mSwapXY         = Config.TouchSwapXY;
  mInvertX        = Config.TouchInvertX;
  mInvertY        = Config.TouchInvertY;
  mPollRateHz     = (Config.TouchPollRateHz >= 20U) ? Config.TouchPollRateHz : 50U;
  mKeepRailsForOs = (Config.AcpiTouchEnable != 0U);

  mTouchInfo.Revision         = 1U;
  mTouchInfo.GetTelemetry     = TouchInfoGetTelemetry;
  mTouchInfo.Reinit           = TouchInfoReinit;
  mTouchInfo.ApplyCalibration = TouchInfoApplyCalibration;

  gBS->InstallMultipleProtocolInterfaces (
         &mInfoHandle,
         &gNp05jTouchInfoProtocolGuid,
         &mTouchInfo,
         NULL
         );

  if (Config.TouchEnable == 0U) {
    DEBUG ((DEBUG_INFO, "NP05J_TOUCH: disabled by BIOS config\n"));
    return EFI_SUCCESS;
  }

  Status = StartTouchHardware ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "NP05J_TOUCH: hardware probe failed (%r), continuing without touch\n", Status));
  }

  /* Never fail DXE driver load so BDS, Boot Menu, and BIOS Setup stay reachable */
  return EFI_SUCCESS;
}
