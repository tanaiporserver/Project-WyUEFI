/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Internal header for Np05jBootManagerLib.
**/

#ifndef _NP05J_BOOT_MANAGER_H_
#define _NP05J_BOOT_MANAGER_H_

#include <Uefi.h>
#include <Protocol/DevicePath.h>

EFI_STATUS
Np05jRegisterKeyCallback (
  IN EFI_DEVICE_PATH_PROTOCOL *DevicePath
  );

VOID
Np05jUnregisterKeyCallback (
  VOID
  );

BOOLEAN
Np05jShouldEnterBootMenu (
  VOID
  );

VOID
Np05jSetEnterBootMenu (
  IN BOOLEAN EnterSetupDirect
  );

#endif /* _NP05J_BOOT_MANAGER_H_ */
