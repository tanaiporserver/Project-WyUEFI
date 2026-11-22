/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Protocol exposing runtime access to NP05J unlocked BIOS configuration.
**/

#ifndef _NP05J_BIOS_CONFIG_PROTOCOL_H_
#define _NP05J_BIOS_CONFIG_PROTOCOL_H_

#include <Np05jBiosConfig.h>

typedef struct _NP05J_BIOS_CONFIG_PROTOCOL NP05J_BIOS_CONFIG_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *NP05J_BIOS_GET_CONFIG)(
  IN  NP05J_BIOS_CONFIG_PROTOCOL *This,
  OUT NP05J_BIOS_CONFIG          *Config
  );

typedef
EFI_STATUS
(EFIAPI *NP05J_BIOS_SET_CONFIG)(
  IN NP05J_BIOS_CONFIG_PROTOCOL *This,
  IN CONST NP05J_BIOS_CONFIG    *Config,
  IN BOOLEAN                     PersistToDisk
  );

struct _NP05J_BIOS_CONFIG_PROTOCOL {
  UINT32                Revision;
  NP05J_BIOS_GET_CONFIG GetConfig;
  NP05J_BIOS_SET_CONFIG SetConfig;
  BOOLEAN               LoadedFromDisk;
};

extern EFI_GUID gNp05jBiosConfigProtocolGuid;

#endif /* _NP05J_BIOS_CONFIG_PROTOCOL_H_ */
