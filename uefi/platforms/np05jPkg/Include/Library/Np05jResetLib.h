/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Hardware reboot-mode (Recovery / Bootloader / EDL 9008) library for nubia NP05J.
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#ifndef _NP05J_RESET_LIB_H_
#define _NP05J_RESET_LIB_H_

#include <Uefi.h>

typedef enum {
  Np05jResetNormal = 0,
  Np05jResetRecovery,
  Np05jResetBootloader,
  Np05jResetEdl9008,
  Np05jResetShutdown
} NP05J_RESET_TARGET;

EFI_STATUS
EFIAPI
Np05jSetRebootReason (
  IN NP05J_RESET_TARGET Target
  );

EFI_STATUS
EFIAPI
Np05jExecuteReset (
  IN NP05J_RESET_TARGET Target
  );

#endif /* _NP05J_RESET_LIB_H_ */
