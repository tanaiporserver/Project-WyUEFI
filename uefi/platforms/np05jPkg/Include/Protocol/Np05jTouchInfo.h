/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Protocol exposing live telemetry and calibration control for Np05jTouchDxe.
**/

#ifndef _NP05J_TOUCH_INFO_PROTOCOL_H_
#define _NP05J_TOUCH_INFO_PROTOCOL_H_

typedef struct _NP05J_TOUCH_INFO_PROTOCOL NP05J_TOUCH_INFO_PROTOCOL;

typedef struct {
  BOOLEAN  Initialized;
  BOOLEAN  TouchDown;
  CHAR8    PartNumber[17];
  UINT32   BuildId;
  UINT16   MaxX;
  UINT16   MaxY;
  UINT16   MaxObjects;
  UINT32   LastRawX;
  UINT32   LastRawY;
  UINT32   LastDispX;
  UINT32   LastDispY;
  UINT32   FrameCount;
  UINT32   ErrorCount;
  UINT32   IrqCount;
  UINT8    SwapXY;
  UINT8    InvertX;
  UINT8    InvertY;
  UINT8    PollRateHz;
} NP05J_TOUCH_TELEMETRY;

typedef
EFI_STATUS
(EFIAPI *NP05J_TOUCH_GET_TELEMETRY)(
  IN  NP05J_TOUCH_INFO_PROTOCOL *This,
  OUT NP05J_TOUCH_TELEMETRY     *Telemetry
  );

typedef
EFI_STATUS
(EFIAPI *NP05J_TOUCH_REINIT)(
  IN NP05J_TOUCH_INFO_PROTOCOL *This
  );

typedef
EFI_STATUS
(EFIAPI *NP05J_TOUCH_APPLY_CALIBRATION)(
  IN NP05J_TOUCH_INFO_PROTOCOL *This,
  IN UINT8                      SwapXY,
  IN UINT8                      InvertX,
  IN UINT8                      InvertY,
  IN UINT8                      PollRateHz
  );

struct _NP05J_TOUCH_INFO_PROTOCOL {
  UINT32                       Revision;
  NP05J_TOUCH_GET_TELEMETRY    GetTelemetry;
  NP05J_TOUCH_REINIT           Reinit;
  NP05J_TOUCH_APPLY_CALIBRATION ApplyCalibration;
};

extern EFI_GUID gNp05jTouchInfoProtocolGuid;

#endif /* _NP05J_TOUCH_INFO_PROTOCOL_H_ */
