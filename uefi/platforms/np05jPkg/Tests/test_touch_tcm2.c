/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Host unit tests for Synaptics S3930TB0A0 (TCM v2) protocol decoder & display mapping.
**/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "Np05jSynapticsTcm2.h"

static void PushBits(uint8_t *Buf, uint32_t *BitPos, uint32_t Val, uint8_t NBits) {
  for (uint8_t I = 0U; I < NBits; I++) {
    uint32_t P = *BitPos + I;
    if ((Val & (1U << I)) != 0U) {
      Buf[P >> 3] |= (uint8_t)(1U << (P & 7U));
    }
  }
  *BitPos += NBits;
}

static void TestCrcAndPacket(void) {
  uint8_t Buf[64];
  uint8_t Payload[3] = { 0x11U, 0x22U, 0x33U };

  /* Header-only packet */
  assert(TCM2_CMD_GET_TOUCH_REPORT_CFG == 0x25U);
  uint32_t Total = Tcm2BuildPacket(Buf, sizeof(Buf), TCM2_CMD_IDENTIFY, 0U, NULL, 0U);
  assert(Total == TCM2_HDR_SIZE);
  assert(Buf[0] == TCM2_CMD_IDENTIFY);
  assert(Tcm2HdrCrcOk(Buf) == 1);

  /* Corrupting 1 bit must fail header CRC6 */
  Buf[2] ^= 0x04U;
  assert(Tcm2HdrCrcOk(Buf) == 0);

  /* Payload + CRC16 packet (seq=1) */
  Total = Tcm2BuildPacket(Buf, sizeof(Buf), TCM2_CMD_ENABLE_REPORT, 1U, Payload, 3U);
  assert(Total == TCM2_HDR_SIZE + 3U + TCM2_CRC16_SIZE);
  assert(Tcm2HdrCrcOk(Buf) == 1);
  assert(((Buf[3] >> 6) & 1U) == 1U);

  tcm2_u16_t CrcExpected = Tcm2Crc16(Buf, TCM2_HDR_SIZE + 3U);
  tcm2_u16_t CrcActual   = ((tcm2_u16_t)Buf[TCM2_HDR_SIZE + 3U] << 8) |
                           (tcm2_u16_t)Buf[TCM2_HDR_SIZE + 4U];
  assert(CrcExpected == CrcActual);
}

static void TestTouchDecodeAndHold(void) {
  const uint8_t ReportCfg[] = {
    TCM2_ENTITY_FOREACH_ACTIVE_OBJ,
    TCM2_ENTITY_OBJ_INDEX, 4U,
    TCM2_ENTITY_OBJ_CLASS, 4U,
    TCM2_ENTITY_OBJ_X_POS, 16U,
    TCM2_ENTITY_OBJ_Y_POS, 16U,
    TCM2_ENTITY_OBJ_Z, 8U,
    TCM2_ENTITY_FOREACH_END,
    TCM2_ENTITY_END
  };

  TCM2_TOUCH_STATE State;
  memset(&State, 0, sizeof(State));

  /* Frame 1: finger 0 down at (3000, 18000) */
  uint8_t Rep1[16];
  uint32_t Bp = 0U;
  memset(Rep1, 0, sizeof(Rep1));
  PushBits(Rep1, &Bp, 0U, 4U);
  PushBits(Rep1, &Bp, TCM2_CLASS_FINGER, 4U);
  PushBits(Rep1, &Bp, 3000U, 16U);
  PushBits(Rep1, &Bp, 18000U, 16U);
  PushBits(Rep1, &Bp, 45U, 8U);

  assert(Tcm2ParseTouchReport(ReportCfg, sizeof(ReportCfg), Rep1, 6U, 10U, TCM2_DEFAULT_MAX_X, TCM2_DEFAULT_MAX_Y, &State) == 0);
  assert(State.Pressed == 1U);
  assert(State.HasPrimary == 1U);
  assert(State.PrimaryIndex == 0U);
  assert(State.RawX == 3000U);
  assert(State.RawY == 18000U);

  /* Frame 2: finger 0 stays at (3100, 18050), finger 1 touches at (12000, 4000).
     Primary finger must NOT jump to finger 1! */
  uint8_t Rep2[16];
  Bp = 0U;
  memset(Rep2, 0, sizeof(Rep2));
  PushBits(Rep2, &Bp, 0U, 4U); PushBits(Rep2, &Bp, TCM2_CLASS_FINGER, 4U);
  PushBits(Rep2, &Bp, 3100U, 16U); PushBits(Rep2, &Bp, 18050U, 16U); PushBits(Rep2, &Bp, 45U, 8U);
  PushBits(Rep2, &Bp, 1U, 4U); PushBits(Rep2, &Bp, TCM2_CLASS_FINGER, 4U);
  PushBits(Rep2, &Bp, 12000U, 16U); PushBits(Rep2, &Bp, 4000U, 16U); PushBits(Rep2, &Bp, 40U, 8U);

  assert(Tcm2ParseTouchReport(ReportCfg, sizeof(ReportCfg), Rep2, 12U, 10U, TCM2_DEFAULT_MAX_X, TCM2_DEFAULT_MAX_Y, &State) == 0);
  assert(State.Pressed == 1U);
  assert(State.PrimaryIndex == 0U);
  assert(State.RawX == 3100U);
  assert(State.RawY == 18050U);
  assert(State.ActiveCount == 2U);

  /* Frame 3: finger 0 lifts while finger 1 remains. Must release before switching! */
  uint8_t Rep3[16];
  Bp = 0U;
  memset(Rep3, 0, sizeof(Rep3));
  PushBits(Rep3, &Bp, 1U, 4U); PushBits(Rep3, &Bp, TCM2_CLASS_FINGER, 4U);
  PushBits(Rep3, &Bp, 12000U, 16U); PushBits(Rep3, &Bp, 4000U, 16U); PushBits(Rep3, &Bp, 40U, 8U);

  assert(Tcm2ParseTouchReport(ReportCfg, sizeof(ReportCfg), Rep3, 6U, 10U, TCM2_DEFAULT_MAX_X, TCM2_DEFAULT_MAX_Y, &State) == 0);
  assert(State.Pressed == 0U);
  assert(State.HasPrimary == 0U);

  /* Frame 4: finger 1 still down -> now cleanly promotes to primary */
  assert(Tcm2ParseTouchReport(ReportCfg, sizeof(ReportCfg), Rep3, 6U, 10U, TCM2_DEFAULT_MAX_X, TCM2_DEFAULT_MAX_Y, &State) == 0);
  assert(State.Pressed == 1U);
  assert(State.HasPrimary == 1U);
  assert(State.PrimaryIndex == 1U);
  assert(State.RawX == 12000U);
  assert(State.RawY == 4000U);
}

static void TestCoordinateMapping(void) {
  uint32_t Dx = 0, Dy = 0;

  /* Portrait S3930 (15039x23999) -> Landscape OLED (2400x1504): SwapXY=1, InvertX=0, InvertY=1 */
  Tcm2MapToDisplay(TCM2_DEFAULT_MAX_X, 0U, TCM2_DEFAULT_MAX_X, TCM2_DEFAULT_MAX_Y, 2400U, 1504U, 1, 0, 1, &Dx, &Dy);
  assert((Dx == 0U) && (Dy == 0U));

  Tcm2MapToDisplay(0U, TCM2_DEFAULT_MAX_Y, TCM2_DEFAULT_MAX_X, TCM2_DEFAULT_MAX_Y, 2400U, 1504U, 1, 0, 1, &Dx, &Dy);
  assert((Dx == 2399U) && (Dy == 1503U));

  Tcm2MapToDisplay(TCM2_DEFAULT_MAX_X / 2U, TCM2_DEFAULT_MAX_Y / 2U, TCM2_DEFAULT_MAX_X, TCM2_DEFAULT_MAX_Y, 2400U, 1504U, 1, 0, 1, &Dx, &Dy);
  assert((Dx >= 1190U) && (Dx <= 1210U));
  assert((Dy >= 745U) && (Dy <= 760U));
}

int main(void) {
  TestCrcAndPacket();
  TestTouchDecodeAndHold();
  TestCoordinateMapping();
  printf("PASS: test_touch_tcm2 (CRC6, CRC16, TCM2 dynamic bitfield decode, primary hold, S3930->2400x1504 mapping)\n");
  return 0;
}
