/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Protocol exposing Project SunUEFI resident UEFI Fastboot service status
  and application-TPL step/action pump for nubia NP05J.

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#ifndef _NP05J_FASTBOOT_PROTOCOL_H_
#define _NP05J_FASTBOOT_PROTOCOL_H_

typedef struct _NP05J_FASTBOOT_PROTOCOL NP05J_FASTBOOT_PROTOCOL;

typedef enum {
  Np05jFbActionNone = 0,
  Np05jFbActionOpenSetup,
  Np05jFbActionOpenShell,
  Np05jFbActionOpenBootMenu,
  Np05jFbActionBootStable,
  Np05jFbActionReboot,
  Np05jFbActionContinue
} NP05J_FASTBOOT_ACTION;

typedef struct {
  BOOLEAN  Attached;
  BOOLEAN  Configured;
  BOOLEAN  CompositeMode;
  BOOLEAN  DiagnosticsOnly;
  BOOLEAN  FlashActive;
  UINT8    BusSpeed;
  UINT32   PartitionsFound;
  UINT32   WritablePartitions;
  UINT64   DownloadBytes;
  UINT64   CommandsHandled;
  CHAR8    SerialNo[32];
  CHAR8    LastCommand[64];
} NP05J_FASTBOOT_STATUS;

typedef
EFI_STATUS
(EFIAPI *NP05J_FASTBOOT_GET_STATUS)(
  IN  NP05J_FASTBOOT_PROTOCOL  *This,
  OUT NP05J_FASTBOOT_STATUS    *Status
  );

typedef
EFI_STATUS
(EFIAPI *NP05J_FASTBOOT_SERVICE_STEP)(
  IN  NP05J_FASTBOOT_PROTOCOL  *This,
  OUT NP05J_FASTBOOT_ACTION    *PendingAction
  );

typedef
EFI_STATUS
(EFIAPI *NP05J_FASTBOOT_SET_DIAGNOSTICS_ONLY)(
  IN NP05J_FASTBOOT_PROTOCOL  *This,
  IN BOOLEAN                  AllowReboot
  );

struct _NP05J_FASTBOOT_PROTOCOL {
  UINT32                               Revision;
  NP05J_FASTBOOT_GET_STATUS            GetStatus;
  NP05J_FASTBOOT_SERVICE_STEP          ServiceStep;
  NP05J_FASTBOOT_SET_DIAGNOSTICS_ONLY  SetDiagnosticsOnly;
};

extern EFI_GUID gNp05jFastbootProtocolGuid;

#endif /* _NP05J_FASTBOOT_PROTOCOL_H_ */
