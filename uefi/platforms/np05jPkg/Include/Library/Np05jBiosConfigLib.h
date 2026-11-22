/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Library API for NP05J persistent unlocked BIOS configuration.
**/

#ifndef _NP05J_BIOS_CONFIG_LIB_H_
#define _NP05J_BIOS_CONFIG_LIB_H_

#include <Np05jBiosConfig.h>

VOID
EFIAPI
Np05jBiosConfigGetDefaults (
  OUT NP05J_BIOS_CONFIG *Config
  );

VOID
EFIAPI
Np05jBiosConfigApplyPreset (
  IN OUT NP05J_BIOS_CONFIG *Config,
  IN     NP05J_BIOS_PRESET  Preset
  );

EFI_STATUS
EFIAPI
Np05jBiosConfigLoad (
  OUT NP05J_BIOS_CONFIG *Config
  );

EFI_STATUS
EFIAPI
Np05jBiosConfigSave (
  IN CONST NP05J_BIOS_CONFIG *Config
  );

EFI_STATUS
EFIAPI
Np05jBiosConfigReset (
  OUT NP05J_BIOS_CONFIG *Config OPTIONAL
  );

BOOLEAN
EFIAPI
Np05jBiosConfigValidate (
  IN CONST NP05J_BIOS_CONFIG *Config
  );

#endif /* _NP05J_BIOS_CONFIG_LIB_H_ */
