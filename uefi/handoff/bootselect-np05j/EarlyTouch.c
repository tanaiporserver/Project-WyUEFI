/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Pre-UEFI Synaptics S3930TB0A0 (TCM v2) GPIO-SPI4 touchscreen driver for
  NP05J BootSelect.

  Hardware verified from Device_np05j/sm8750-nubia-np05j-common.dtsi:
    - TLMM 0x0F100000, stride 0x1000
    - GPIO 48=MISO, 49=MOSI, 50=CLK, 51=CS, 161=RST(active LOW), 162=IRQ(active LOW)
    - SPI Mode 0, 11us CS setup (spi-cs-setup-ns = 0x2af8)
**/

#include "EarlyTouch.h"
#include "EarlyTrace.h"
#include "../../Include/Np05jSynapticsTcm2.h"

extern uint32_t EarlyMmioRead32(uint64_t Addr);
extern void     EarlyMmioWrite32(uint64_t Addr, uint32_t Val);
extern void     EarlyDelayUs(uint64_t Us);

#define TLMM_BASE     0x0F100000ULL
#define PIN_CTL(P)    (TLMM_BASE + (uint64_t)(P) * 0x1000ULL)
#define PIN_IO(P)     (PIN_CTL(P) + 4ULL)
#define PIN_ICFG(P)   (PIN_CTL(P) + 8ULL)

#define PIN_MISO      48U
#define PIN_MOSI      49U
#define PIN_CLK       50U
#define PIN_CS        51U
#define PIN_RST       161U
#define PIN_IRQ       162U

#define CTL_FIELDS    0x3FFU
#define EGPIO_PRESENT (1U << 11)
#define EGPIO_ENABLE  (1U << 12)
#define OE_BIT        (1U << 9)
#define PULL_NONE     0U
#define PULL_DOWN     1U
#define PULL_UP       3U

static const uint32_t mPins[6] = { PIN_MISO, PIN_MOSI, PIN_CLK, PIN_CS, PIN_RST, PIN_IRQ };
static uint32_t       mSavedCtl[6];
static uint32_t       mSavedIo[6];
static uint32_t       mSavedIrqCfg;
static int            mOwned       = 0;
static int            mReady       = 0;
static uint8_t        mSeq         = 0U;
static uint16_t       mMaxX        = TCM2_DEFAULT_MAX_X;
static uint16_t       mMaxY        = TCM2_DEFAULT_MAX_Y;
static uint16_t       mMaxObjects  = TCM2_MAX_OBJECTS;
static uint8_t        mReportCfg[TCM2_MAX_REPORT_CFG];
static uint16_t       mReportCfgLen = 0U;
static uint8_t        mTxBuf[TCM2_MAX_XFER];
static uint8_t        mRxBuf[TCM2_MAX_XFER];
static uint8_t        mPayBuf[TCM2_MAX_XFER];
static TCM2_TOUCH_STATE mState;

static void DrivePin(uint32_t Pin, int High) {
  EarlyMmioWrite32(PIN_IO(Pin), High ? (1U << 1) : 0U);
}

static uint32_t BuildCtl(uint32_t Orig, int Output, uint32_t Pull, uint32_t Drive) {
  uint32_t V = (Orig & ~(CTL_FIELDS | EGPIO_ENABLE)) | (Drive << 6) | Pull | (Output ? OE_BIT : 0U);
  if ((V & EGPIO_PRESENT) != 0U) {
    V |= EGPIO_ENABLE;
  }
  return V;
}

static uint8_t SpiByte(uint8_t Tx) {
  uint8_t Rx = 0U;
  for (uint32_t B = 0U; B < 8U; B++) {
    DrivePin(PIN_MOSI, (Tx & 0x80U) != 0U);
    Tx = (uint8_t)(Tx << 1);
    EarlyDelayUs(1U);
    DrivePin(PIN_CLK, 1);
    EarlyDelayUs(1U);
    Rx = (uint8_t)((Rx << 1) | (EarlyMmioRead32(PIN_IO(PIN_MISO)) & 1U));
    DrivePin(PIN_CLK, 0);
  }
  return Rx;
}

static void SpiXfer(const uint8_t *Tx, uint8_t *Rx, uint32_t Len) {
  DrivePin(PIN_CS, 0);
  EarlyDelayUs(12U);
  for (uint32_t I = 0U; I < Len; I++) {
    uint8_t B = SpiByte(Tx ? Tx[I] : 0xFFU);
    if (Rx) {
      Rx[I] = B;
    }
  }
  DrivePin(PIN_CS, 1);
  EarlyDelayUs(4U);
}

static int TcmCmd(uint8_t Cmd, const uint8_t *Pay, uint16_t PayLen, uint8_t *OutPay, uint16_t MaxOut, uint16_t *OutLen) {
  for (uint32_t Retry = 0U; Retry < 3U; Retry++) {
    uint32_t Total = Tcm2BuildPacket(mTxBuf, sizeof(mTxBuf), Cmd, mSeq, Pay, PayLen);
    if (Total == 0U) {
      return 0;
    }
    SpiXfer(mTxBuf, mRxBuf, Total);

    for (uint32_t Poll = 0U; Poll < 25U; Poll++) {
      EarlyDelayUs(1200U);
      for (uint32_t K = 0U; K < 16U; K++) {
        mTxBuf[K] = 0xFFU;
      }
      SpiXfer(mTxBuf, mRxBuf, 16U);
      if (!Tcm2HdrCrcOk(mRxBuf)) {
        continue;
      }
      if ((mRxBuf[0] == TCM2_STATUS_WAIT) || (mRxBuf[0] == TCM2_STATUS_IDLE)) {
        continue;
      }
      if (mRxBuf[0] == TCM2_STATUS_OK) {
        uint16_t Plen = Tcm2GetLe16(mRxBuf + 1);
        if (OutLen) {
          *OutLen = Plen;
        }
        if ((Plen > 0U) && OutPay) {
          uint32_t Rd = (uint32_t)TCM2_HDR_SIZE + Plen + TCM2_CRC16_SIZE;
          if (Rd > TCM2_MAX_XFER) {
            Rd = TCM2_MAX_XFER;
          }
          for (uint32_t K = 0U; K < Rd; K++) {
            mTxBuf[K] = 0xFFU;
          }
          SpiXfer(mTxBuf, mRxBuf, Rd);
          if (!Tcm2HdrCrcOk(mRxBuf)) {
            break;
          }
          uint16_t Copy = (Plen < MaxOut) ? Plen : MaxOut;
          for (uint16_t K = 0U; K < Copy; K++) {
            OutPay[K] = mRxBuf[TCM2_HDR_SIZE + K];
          }
        }
        mSeq ^= 1U;
        return 1;
      }
    }
    mSeq ^= 1U;
  }
  return 0;
}

int EarlyTouchInit(void) {
  uint16_t Len = 0U;

  mReady = 0;
  for (uint32_t I = 0U; I < 6U; I++) {
    mSavedCtl[I] = EarlyMmioRead32(PIN_CTL(mPins[I]));
    mSavedIo[I]  = EarlyMmioRead32(PIN_IO(mPins[I]));
    if (mSavedCtl[I] == 0xFFFFFFFFU) {
      EarlyTraceLog("TOUCH: TLMM unreadable");
      return 0;
    }
  }
  mSavedIrqCfg = EarlyMmioRead32(PIN_ICFG(PIN_IRQ));
  mOwned       = 1;

  EarlyMmioWrite32(PIN_CTL(PIN_MISO), BuildCtl(mSavedCtl[0], 0, PULL_DOWN, 2U));
  DrivePin(PIN_CS, 1);
  DrivePin(PIN_CLK, 0);
  DrivePin(PIN_MOSI, 0);
  DrivePin(PIN_RST, 1);
  EarlyMmioWrite32(PIN_CTL(PIN_MOSI), BuildCtl(mSavedCtl[1], 1, PULL_NONE, 2U));
  EarlyMmioWrite32(PIN_CTL(PIN_CLK),  BuildCtl(mSavedCtl[2], 1, PULL_NONE, 2U));
  EarlyMmioWrite32(PIN_CTL(PIN_CS),   BuildCtl(mSavedCtl[3], 1, PULL_NONE, 2U));
  EarlyMmioWrite32(PIN_CTL(PIN_RST),  BuildCtl(mSavedCtl[4], 1, PULL_NONE, 1U));
  EarlyMmioWrite32(PIN_ICFG(PIN_IRQ), 0U);
  EarlyMmioWrite32(PIN_CTL(PIN_IRQ),  BuildCtl(mSavedCtl[5], 0, PULL_UP, 0U));

  /* Hardware reset pulse */
  DrivePin(PIN_RST, 0);
  EarlyDelayUs(20000U);
  DrivePin(PIN_RST, 1);
  EarlyDelayUs(85000U);

  /* Drain boot IDENTIFY */
  for (uint32_t K = 0U; K < 64U; K++) {
    mTxBuf[K] = 0xFFU;
  }
  SpiXfer(mTxBuf, mRxBuf, 64U);

  if (!TcmCmd(TCM2_CMD_IDENTIFY, 0, 0U, mPayBuf, (uint16_t)sizeof(mPayBuf), &Len)) {
    EarlyTraceLog("TOUCH: IDENTIFY failed");
    EarlyTouchShutdown();
    return 0;
  }

  if (TcmCmd(TCM2_CMD_GET_APP_INFO, 0, 0U, mPayBuf, (uint16_t)sizeof(mPayBuf), &Len) && (Len >= 38U)) {
    uint16_t Mx = Tcm2GetLe16(mPayBuf + 32);
    uint16_t My = Tcm2GetLe16(mPayBuf + 34);
    uint16_t Mo = Tcm2GetLe16(mPayBuf + 36);
    if ((Mx > 100U) && (My > 100U)) {
      mMaxX = Mx;
      mMaxY = My;
    }
    if ((Mo > 0U) && (Mo <= TCM2_MAX_OBJECTS)) {
      mMaxObjects = Mo;
    }
  }

  if (!TcmCmd(TCM2_CMD_GET_TOUCH_REPORT_CFG, 0, 0U, mReportCfg, (uint16_t)sizeof(mReportCfg), &mReportCfgLen) ||
      (mReportCfgLen == 0U))
  {
    EarlyTraceLog("TOUCH: REPORT_CFG failed");
    EarlyTouchShutdown();
    return 0;
  }

  {
    uint8_t RepId = TCM2_REPORT_TOUCH;
    TcmCmd(TCM2_CMD_ENABLE_REPORT, &RepId, 1U, mPayBuf, (uint16_t)sizeof(mPayBuf), &Len);
  }

  for (uint32_t K = 0U; K < sizeof(mState); K++) {
    ((uint8_t *)&mState)[K] = 0U;
  }
  mReady = 1;
  EarlyTraceLog("TOUCH: S3930 TCMv2 ready");
  return 1;
}

int EarlyTouchPoll(NP05J_EARLY_TOUCH_EVENT *Out) {
  if (!Out) {
    return 0;
  }
  Out->Initialized = (uint8_t)mReady;
  Out->MaxX        = mMaxX;
  Out->MaxY        = mMaxY;

  if (!mReady) {
    return 0;
  }

  if ((EarlyMmioRead32(PIN_IO(PIN_IRQ)) & 1U) != 0U) {
    return 0;
  }

  uint32_t Total = Tcm2BuildPacket(mTxBuf, sizeof(mTxBuf), TCM2_CMD_GET_REPORT, mSeq, 0, 0U);
  SpiXfer(mTxBuf, mRxBuf, Total);
  mSeq ^= 1U;

  /* อ่านเฉพาะส่วนหัว 4 ไบต์ก่อนแล้วค่อยอ่าน payload + CRC16 ตามความยาวจริงใน CS pulse เดียว */
  DrivePin(PIN_CS, 0);
  EarlyDelayUs(12U);
  for (uint32_t K = 0U; K < TCM2_HDR_SIZE; K++) {
    mRxBuf[K] = SpiByte(0xFFU);
  }
  if (!Tcm2HdrCrcOk(mRxBuf) || (mRxBuf[0] != TCM2_REPORT_TOUCH)) {
    DrivePin(PIN_CS, 1);
    EarlyDelayUs(4U);
    return 0;
  }

  uint16_t Plen = Tcm2GetLe16(mRxBuf + 1);
  if ((Plen == 0U) || ((uint32_t)TCM2_HDR_SIZE + Plen > TCM2_REPORT_RD_SIZE)) {
    DrivePin(PIN_CS, 1);
    EarlyDelayUs(4U);
    return 0;
  }

  uint32_t Tail = (uint32_t)Plen + TCM2_CRC16_SIZE;
  if ((uint32_t)TCM2_HDR_SIZE + Tail > TCM2_MAX_XFER) {
    Tail = TCM2_MAX_XFER - TCM2_HDR_SIZE;
  }
  for (uint32_t K = 0U; K < Tail; K++) {
    mRxBuf[TCM2_HDR_SIZE + K] = SpiByte(0xFFU);
  }
  DrivePin(PIN_CS, 1);
  EarlyDelayUs(4U);

  if (Tcm2ParseTouchReport(
        mReportCfg,
        mReportCfgLen,
        mRxBuf + TCM2_HDR_SIZE,
        Plen,
        mMaxObjects,
        mMaxX,
        mMaxY,
        &mState
        ) != 0)
  {
    return 0;
  }

  Tcm2MapToDisplay(
    mState.RawX,
    mState.RawY,
    mMaxX,
    mMaxY,
    2400U,
    1504U,
    1,
    0,
    1,
    &Out->DispX,
    &Out->DispY
    );
  Out->TouchDown = mState.Pressed;
  return 1;
}

void EarlyTouchShutdown(void) {
  if (!mOwned) {
    return;
  }
  for (uint32_t I = 0U; I < 6U; I++) {
    EarlyMmioWrite32(PIN_IO(mPins[I]), mSavedIo[I]);
    EarlyMmioWrite32(PIN_CTL(mPins[I]), mSavedCtl[I]);
  }
  EarlyMmioWrite32(PIN_ICFG(PIN_IRQ), mSavedIrqCfg);
  mOwned = 0;
  mReady = 0;
}
