/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Synaptics TouchComm v2 (TCM v2) protocol core for S3930TB0A0 (nubia NP05J).

  Protocol and report-config decoding verified on NP05J hardware via
  synaptics_tcm2_spi.c (Linux 7.3.5 QUPv3 SPI4). Primary-finger tracking and
  lift-before-switch logic adapted from Project SunUEFI (Y700TouchDecode.h).
**/

#ifndef _NP05J_SYNAPTICS_TCM2_H_
#define _NP05J_SYNAPTICS_TCM2_H_

#if defined(MAX_UINT8)
typedef UINT8  tcm2_u8_t;
typedef UINT16 tcm2_u16_t;
typedef UINT32 tcm2_u32_t;
typedef UINT64 tcm2_u64_t;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  tcm2_u8_t;
typedef uint16_t tcm2_u16_t;
typedef uint32_t tcm2_u32_t;
typedef uint64_t tcm2_u64_t;
#endif

#define TCM2_HDR_SIZE                   4U
#define TCM2_CRC16_SIZE                 2U
#define TCM2_MAX_XFER                   512U
#define TCM2_REPORT_RD_SIZE             384U
#define TCM2_MAX_REPORT_CFG             256U
#define TCM2_MAX_OBJECTS                10U

#define TCM2_DEFAULT_MAX_X              15039U
#define TCM2_DEFAULT_MAX_Y              23999U

/* TCM v2 command codes */
#define TCM2_CMD_ACK                    0x00U
#define TCM2_CMD_IDENTIFY               0x02U
#define TCM2_CMD_RESET                  0x04U
#define TCM2_CMD_ENABLE_REPORT          0x05U
#define TCM2_CMD_DISABLE_REPORT         0x06U
#define TCM2_CMD_GET_REPORT             0x0AU
#define TCM2_CMD_GET_APP_INFO           0x20U
#define TCM2_CMD_GET_TOUCH_REPORT_CFG   0x25U

/* TCM v2 status / report codes */
#define TCM2_STATUS_IDLE                0x00U
#define TCM2_STATUS_OK                  0x01U
#define TCM2_STATUS_CONTINUED_READ      0x03U
#define TCM2_STATUS_WAIT                0x04U
#define TCM2_STATUS_INVALID             0x08U
#define TCM2_REPORT_IDENTIFY            0x10U
#define TCM2_REPORT_TOUCH               0x11U
#define TCM2_REPORT_STATUS              0x12U

/* TCM touch report config entity codes */
#define TCM2_ENTITY_END                 0x00U
#define TCM2_ENTITY_FOREACH_ACTIVE_OBJ  0x01U
#define TCM2_ENTITY_FOREACH_OBJ         0x02U
#define TCM2_ENTITY_FOREACH_END         0x03U
#define TCM2_ENTITY_PAD_TO_NEXT_BYTE    0x04U
#define TCM2_ENTITY_TIMESTAMP           0x05U
#define TCM2_ENTITY_OBJ_INDEX           0x06U
#define TCM2_ENTITY_OBJ_CLASS           0x07U
#define TCM2_ENTITY_OBJ_X_POS           0x08U
#define TCM2_ENTITY_OBJ_Y_POS           0x09U
#define TCM2_ENTITY_OBJ_Z               0x0AU
#define TCM2_ENTITY_OBJ_X_WIDTH         0x0BU
#define TCM2_ENTITY_OBJ_Y_WIDTH         0x0CU
#define TCM2_ENTITY_NUM_ACTIVE_OBJS     0x18U
#define TCM2_ENTITY_GESTURE_DATA        0x1BU

/* Object classification values */
#define TCM2_CLASS_LIFT                 0U
#define TCM2_CLASS_FINGER               1U
#define TCM2_CLASS_GLOVED               2U
#define TCM2_CLASS_STYLUS               3U
#define TCM2_CLASS_ERASER               4U
#define TCM2_CLASS_SMALL_OBJ            5U
#define TCM2_CLASS_PALM                 6U

typedef struct {
  tcm2_u8_t  Active;
  tcm2_u8_t  Classification;
  tcm2_u16_t X;
  tcm2_u16_t Y;
  tcm2_u16_t Z;
  tcm2_u16_t XWidth;
  tcm2_u16_t YWidth;
} TCM2_TOUCH_OBJECT;

typedef struct {
  tcm2_u8_t  HasPrimary;
  tcm2_u8_t  PrimaryIndex;
  tcm2_u8_t  Pressed;
  tcm2_u32_t RawX;
  tcm2_u32_t RawY;
  tcm2_u32_t ActiveCount;
  TCM2_TOUCH_OBJECT Objects[TCM2_MAX_OBJECTS];
} TCM2_TOUCH_STATE;

static inline tcm2_u16_t
Tcm2GetLe16 (
  const tcm2_u8_t *Buf
  )
{
  return (tcm2_u16_t)Buf[0] | ((tcm2_u16_t)Buf[1] << 8);
}

static inline tcm2_u32_t
Tcm2GetLe32 (
  const tcm2_u8_t *Buf
  )
{
  return (tcm2_u32_t)Buf[0] |
         ((tcm2_u32_t)Buf[1] << 8) |
         ((tcm2_u32_t)Buf[2] << 16) |
         ((tcm2_u32_t)Buf[3] << 24);
}

static inline tcm2_u8_t
Tcm2Crc6 (
  tcm2_u8_t B0,
  tcm2_u8_t B1,
  tcm2_u8_t B2,
  tcm2_u8_t Seq
  )
{
  tcm2_u8_t       Crc = 0x3FU;
  const tcm2_u8_t Bytes[3] = { B0, B1, B2 };
  const tcm2_u8_t Tail[2]  = { 0U, (tcm2_u8_t)(Seq & 1U) };
  int             I;
  int             J;

  for (J = 0; J < 3; J++) {
    for (I = 7; I >= 0; I--) {
      tcm2_u8_t Bit = (tcm2_u8_t)((Bytes[J] >> I) & 1U);
      tcm2_u8_t Fb  = (tcm2_u8_t)(((Crc >> 5) & 1U) ^ Bit);

      Crc = (tcm2_u8_t)((Crc << 1) & 0x3FU);
      if (Fb != 0U) {
        Crc ^= 0x03U;
      }
    }
  }

  for (J = 0; J < 2; J++) {
    tcm2_u8_t Fb = (tcm2_u8_t)(((Crc >> 5) & 1U) ^ Tail[J]);

    Crc = (tcm2_u8_t)((Crc << 1) & 0x3FU);
    if (Fb != 0U) {
      Crc ^= 0x03U;
    }
  }

  return (tcm2_u8_t)(((Seq & 1U) << 6) | Crc);
}

static inline int
Tcm2HdrCrcOk (
  const tcm2_u8_t *Hdr
  )
{
  tcm2_u8_t Seq;

  if (Hdr == 0) {
    return 0;
  }

  Seq = (tcm2_u8_t)((Hdr[3] >> 6) & 1U);
  return Hdr[3] == Tcm2Crc6 (Hdr[0], Hdr[1], Hdr[2], Seq);
}

static inline tcm2_u16_t
Tcm2Crc16 (
  const tcm2_u8_t *Data,
  tcm2_u32_t       Len
  )
{
  tcm2_u16_t Crc = 0xFFFFU;
  tcm2_u32_t I;
  int        Bit;

  for (I = 0; I < Len; I++) {
    Crc ^= (tcm2_u16_t)Data[I] << 8;
    for (Bit = 0; Bit < 8; Bit++) {
      if ((Crc & 0x8000U) != 0U) {
        Crc = (tcm2_u16_t)((Crc << 1) ^ 0x1021U);
      } else {
        Crc = (tcm2_u16_t)(Crc << 1);
      }
    }
  }

  return Crc;
}

static inline tcm2_u32_t
Tcm2BuildPacket (
  tcm2_u8_t        *TxBuf,
  tcm2_u32_t        TxCap,
  tcm2_u8_t         Cmd,
  tcm2_u8_t         Seq,
  const tcm2_u8_t  *Payload,
  tcm2_u16_t        PayloadLen
  )
{
  tcm2_u32_t Total = TCM2_HDR_SIZE;
  tcm2_u32_t I;

  if ((TxBuf == 0) || (PayloadLen > 0U && Payload == 0)) {
    return 0;
  }

  if (PayloadLen > 0U) {
    Total += (tcm2_u32_t)PayloadLen + TCM2_CRC16_SIZE;
  }

  if (Total > TxCap) {
    return 0;
  }

  TxBuf[0] = Cmd;
  TxBuf[1] = (tcm2_u8_t)(PayloadLen & 0xFFU);
  TxBuf[2] = (tcm2_u8_t)((PayloadLen >> 8) & 0xFFU);
  TxBuf[3] = Tcm2Crc6 (TxBuf[0], TxBuf[1], TxBuf[2], Seq);

  if (PayloadLen > 0U) {
    tcm2_u16_t Crc;

    for (I = 0; I < PayloadLen; I++) {
      TxBuf[TCM2_HDR_SIZE + I] = Payload[I];
    }

    Crc = Tcm2Crc16 (TxBuf, TCM2_HDR_SIZE + (tcm2_u32_t)PayloadLen);
    TxBuf[TCM2_HDR_SIZE + PayloadLen]     = (tcm2_u8_t)((Crc >> 8) & 0xFFU);
    TxBuf[TCM2_HDR_SIZE + PayloadLen + 1] = (tcm2_u8_t)(Crc & 0xFFU);
  }

  return Total;
}

static inline tcm2_u32_t
Tcm2GetBits (
  const tcm2_u8_t *Buf,
  tcm2_u32_t       BufLen,
  tcm2_u32_t       BitPos,
  tcm2_u32_t       NBits
  )
{
  tcm2_u32_t Val = 0;
  tcm2_u32_t I;

  if ((NBits == 0U) || (NBits > 32U)) {
    return 0;
  }

  if (((BitPos + NBits + 7U) / 8U) > BufLen) {
    return 0;
  }

  for (I = 0; I < NBits; I++) {
    tcm2_u32_t Pos = BitPos + I;

    if ((Buf[Pos >> 3] & (1U << (Pos & 7U))) != 0U) {
      Val |= (1U << I);
    }
  }

  return Val;
}

static inline int
Tcm2ParseTouchReport (
  const tcm2_u8_t  *ReportCfg,
  tcm2_u16_t        ReportCfgLen,
  const tcm2_u8_t  *Data,
  tcm2_u16_t        DataLen,
  tcm2_u16_t        MaxObjects,
  tcm2_u16_t        MaxX,
  tcm2_u16_t        MaxY,
  TCM2_TOUCH_STATE *State
  )
{
  tcm2_u32_t BitPos       = 0;
  tcm2_u32_t TotalBits    = (tcm2_u32_t)DataLen * 8U;
  tcm2_u32_t Idx          = 0;
  tcm2_u32_t LoopStart    = 0;
  tcm2_u32_t ObjIdx       = 0;
  tcm2_u32_t Iter         = 0;
  tcm2_u32_t ActiveObjs   = 0;
  int        HasNumActive = 0;
  int        InLoop       = 0;
  int        ActiveOnly   = 0;
  int        ObjLoopSeen  = 0;
  int        Loops        = 0;
  tcm2_u32_t I;
  tcm2_u32_t FirstActive  = TCM2_MAX_OBJECTS;

  if ((ReportCfg == 0) || (Data == 0) || (State == 0) ||
      (ReportCfgLen == 0U) || (DataLen == 0U) ||
      (MaxX == 0U) || (MaxY == 0U))
  {
    return -1;
  }

  if ((MaxObjects == 0U) || (MaxObjects > TCM2_MAX_OBJECTS)) {
    MaxObjects = TCM2_MAX_OBJECTS;
  }

  for (I = 0; I < TCM2_MAX_OBJECTS; I++) {
    State->Objects[I].Active         = 0;
    State->Objects[I].Classification = 0;
    State->Objects[I].X              = 0;
    State->Objects[I].Y              = 0;
    State->Objects[I].Z              = 0;
    State->Objects[I].XWidth         = 0;
    State->Objects[I].YWidth         = 0;
  }

  while ((Idx < ReportCfgLen) && (Loops++ < 1024)) {
    tcm2_u8_t  Code = ReportCfg[Idx++];
    tcm2_u8_t  Bits;
    tcm2_u32_t Val;

    switch (Code) {
      case TCM2_ENTITY_END:
        goto Done;

      case TCM2_ENTITY_FOREACH_ACTIVE_OBJ:
        Iter        = 0;
        ObjIdx      = 0;
        ObjLoopSeen = 1;
        if (HasNumActive && (ActiveObjs == 0U)) {
          while (Idx < ReportCfgLen) {
            tcm2_u8_t C = ReportCfg[Idx++];

            if (C == TCM2_ENTITY_FOREACH_END) {
              break;
            }
            if (C == TCM2_ENTITY_END) {
              goto Done;
            }
            if ((C != TCM2_ENTITY_PAD_TO_NEXT_BYTE) && (Idx < ReportCfgLen)) {
              Idx++;
            }
          }
          break;
        }
        InLoop     = 1;
        ActiveOnly = 1;
        LoopStart  = Idx;
        break;

      case TCM2_ENTITY_FOREACH_OBJ:
        Iter        = 0;
        ObjIdx      = 0;
        InLoop      = 1;
        ActiveOnly  = 0;
        LoopStart   = Idx;
        ObjLoopSeen = 1;
        break;

      case TCM2_ENTITY_FOREACH_END:
        if (!InLoop) {
          break;
        }
        Iter++;
        ObjIdx = Iter;
        if (ActiveOnly) {
          if ((BitPos + 8U <= TotalBits) &&
              (Iter < MaxObjects) &&
              (!HasNumActive || (Iter < ActiveObjs)))
          {
            Idx = LoopStart;
          } else {
            InLoop = 0;
          }
        } else {
          if ((Iter < MaxObjects) && (BitPos < TotalBits)) {
            Idx = LoopStart;
          } else {
            InLoop = 0;
          }
        }
        break;

      case TCM2_ENTITY_PAD_TO_NEXT_BYTE:
        BitPos = (BitPos + 7U) & ~7U;
        break;

      case TCM2_ENTITY_OBJ_INDEX:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits    = ReportCfg[Idx++];
        Val     = Tcm2GetBits (Data, DataLen, BitPos, Bits);
        BitPos += Bits;
        if (Val < TCM2_MAX_OBJECTS) {
          ObjIdx = Val;
        }
        break;

      case TCM2_ENTITY_OBJ_CLASS:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits    = ReportCfg[Idx++];
        Val     = Tcm2GetBits (Data, DataLen, BitPos, Bits);
        BitPos += Bits;
        if (ObjIdx < TCM2_MAX_OBJECTS) {
          State->Objects[ObjIdx].Classification = (tcm2_u8_t)Val;
          State->Objects[ObjIdx].Active =
            ((Val != TCM2_CLASS_LIFT) && (Val != TCM2_CLASS_PALM)) ? 1U : 0U;
        }
        break;

      case TCM2_ENTITY_OBJ_X_POS:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits    = ReportCfg[Idx++];
        Val     = Tcm2GetBits (Data, DataLen, BitPos, Bits);
        BitPos += Bits;
        if (ObjIdx < TCM2_MAX_OBJECTS) {
          State->Objects[ObjIdx].X = (tcm2_u16_t)(Val > MaxX ? MaxX : Val);
        }
        break;

      case TCM2_ENTITY_OBJ_Y_POS:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits    = ReportCfg[Idx++];
        Val     = Tcm2GetBits (Data, DataLen, BitPos, Bits);
        BitPos += Bits;
        if (ObjIdx < TCM2_MAX_OBJECTS) {
          State->Objects[ObjIdx].Y = (tcm2_u16_t)(Val > MaxY ? MaxY : Val);
        }
        break;

      case TCM2_ENTITY_OBJ_Z:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits    = ReportCfg[Idx++];
        Val     = Tcm2GetBits (Data, DataLen, BitPos, Bits);
        BitPos += Bits;
        if (ObjIdx < TCM2_MAX_OBJECTS) {
          State->Objects[ObjIdx].Z = (tcm2_u16_t)Val;
        }
        break;

      case TCM2_ENTITY_OBJ_X_WIDTH:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits    = ReportCfg[Idx++];
        Val     = Tcm2GetBits (Data, DataLen, BitPos, Bits);
        BitPos += Bits;
        if (ObjIdx < TCM2_MAX_OBJECTS) {
          State->Objects[ObjIdx].XWidth = (tcm2_u16_t)Val;
        }
        break;

      case TCM2_ENTITY_OBJ_Y_WIDTH:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits    = ReportCfg[Idx++];
        Val     = Tcm2GetBits (Data, DataLen, BitPos, Bits);
        BitPos += Bits;
        if (ObjIdx < TCM2_MAX_OBJECTS) {
          State->Objects[ObjIdx].YWidth = (tcm2_u16_t)Val;
        }
        break;

      case TCM2_ENTITY_NUM_ACTIVE_OBJS:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits         = ReportCfg[Idx++];
        ActiveObjs   = Tcm2GetBits (Data, DataLen, BitPos, Bits);
        HasNumActive = 1;
        BitPos      += Bits;
        break;

      default:
        if (Idx >= ReportCfgLen) {
          goto Done;
        }
        Bits    = ReportCfg[Idx++];
        BitPos += Bits;
        break;
    }
  }

Done:
  if (!ObjLoopSeen) {
    return -1;
  }

  State->ActiveCount = 0;
  for (I = 0; I < MaxObjects; I++) {
    if (State->Objects[I].Active != 0U) {
      State->ActiveCount++;
      if (FirstActive == TCM2_MAX_OBJECTS) {
        FirstActive = I;
      }
    }
  }

  /* Hold primary finger until lift so a second finger never creates a drag jump. */
  if (State->HasPrimary != 0U) {
    if ((State->PrimaryIndex < MaxObjects) && (State->Objects[State->PrimaryIndex].Active != 0U)) {
      State->RawX    = State->Objects[State->PrimaryIndex].X;
      State->RawY    = State->Objects[State->PrimaryIndex].Y;
      State->Pressed = 1U;
      return 0;
    }

    /* Primary lifted: emit release once at previous coordinates before switching. */
    State->HasPrimary = 0U;
    State->Pressed    = 0U;
    return 0;
  }

  if (FirstActive < MaxObjects) {
    State->HasPrimary   = 1U;
    State->PrimaryIndex = (tcm2_u8_t)FirstActive;
    State->RawX         = State->Objects[FirstActive].X;
    State->RawY         = State->Objects[FirstActive].Y;
    State->Pressed      = 1U;
  } else {
    State->Pressed = 0U;
  }

  return 0;
}

static inline void
Tcm2MapToDisplay (
  tcm2_u32_t  RawX,
  tcm2_u32_t  RawY,
  tcm2_u32_t  MaxX,
  tcm2_u32_t  MaxY,
  tcm2_u32_t  TargetWidth,
  tcm2_u32_t  TargetHeight,
  int         SwapXY,
  int         InvertX,
  int         InvertY,
  tcm2_u32_t *OutX,
  tcm2_u32_t *OutY
  )
{
  tcm2_u32_t X    = (RawX > MaxX) ? MaxX : RawX;
  tcm2_u32_t Y    = (RawY > MaxY) ? MaxY : RawY;
  tcm2_u32_t SrcX = X;
  tcm2_u32_t SrcY = Y;
  tcm2_u32_t DenX = MaxX;
  tcm2_u32_t DenY = MaxY;

  if ((MaxX == 0U) || (MaxY == 0U) || (TargetWidth == 0U) || (TargetHeight == 0U)) {
    if (OutX != 0) {
      *OutX = 0;
    }
    if (OutY != 0) {
      *OutY = 0;
    }
    return;
  }

  if (InvertX) {
    X = MaxX - X;
  }

  if (InvertY) {
    Y = MaxY - Y;
  }

  if (SwapXY) {
    /*
     * NP05J panel is native portrait (15039 x 23999) scanned onto a 2400 x 1504
     * landscape framebuffer: SwapXY=1, InvertY=1 (on raw X before/after swap)
     * maps: DisplayX = rawY * W / MaxY, DisplayY = (MaxX - rawX) * H / MaxX.
     * Applying InvertX/InvertY on raw axes then swapping makes both intuitive!
     */
    SrcX = RawY;
    SrcY = RawX;
    if (InvertX) {
      SrcX = MaxY - SrcX;
    }
    if (InvertY) {
      SrcY = MaxX - SrcY;
    }
    DenX = MaxY;
    DenY = MaxX;
  } else {
    SrcX = X;
    SrcY = Y;
  }

  if (OutX != 0) {
    *OutX = (tcm2_u32_t)(((tcm2_u64_t)SrcX * (tcm2_u64_t)TargetWidth) / (tcm2_u64_t)DenX);
    if (*OutX >= TargetWidth) {
      *OutX = TargetWidth - 1U;
    }
  }

  if (OutY != 0) {
    *OutY = (tcm2_u32_t)(((tcm2_u64_t)SrcY * (tcm2_u64_t)TargetHeight) / (tcm2_u64_t)DenY);
    if (*OutY >= TargetHeight) {
      *OutY = TargetHeight - 1U;
    }
  }
}

#endif /* _NP05J_SYNAPTICS_TCM2_H_ */
