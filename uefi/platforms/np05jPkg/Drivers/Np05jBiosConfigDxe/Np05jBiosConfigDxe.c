/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  DXE Driver publishing gNp05jBiosConfigProtocolGuid and synchronizing
  persisted BIOS settings once FAT filesystems mount.
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/Np05jBiosConfigLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Np05jBiosConfig.h>
#include <Protocol/SimpleFileSystem.h>

STATIC NP05J_BIOS_CONFIG  mCachedConfig;
STATIC EFI_EVENT          mFsNotifyEvent = NULL;
STATIC VOID              *mFsNotifyReg   = NULL;

STATIC
EFI_STATUS
EFIAPI
ProtocolGetConfig (
  IN  NP05J_BIOS_CONFIG_PROTOCOL *This,
  OUT NP05J_BIOS_CONFIG          *Config
  );

STATIC
EFI_STATUS
EFIAPI
ProtocolSetConfig (
  IN NP05J_BIOS_CONFIG_PROTOCOL *This,
  IN CONST NP05J_BIOS_CONFIG    *Config,
  IN BOOLEAN                     PersistToDisk
  );

STATIC NP05J_BIOS_CONFIG_PROTOCOL mProtocol = {
  1U,
  ProtocolGetConfig,
  ProtocolSetConfig,
  FALSE
};

STATIC
EFI_STATUS
EFIAPI
ProtocolGetConfig (
  IN  NP05J_BIOS_CONFIG_PROTOCOL *This,
  OUT NP05J_BIOS_CONFIG          *Config
  )
{
  if ((This == NULL) || (Config == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Config, &mCachedConfig, sizeof (*Config));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ProtocolSetConfig (
  IN NP05J_BIOS_CONFIG_PROTOCOL *This,
  IN CONST NP05J_BIOS_CONFIG    *Config,
  IN BOOLEAN                     PersistToDisk
  )
{
  EFI_STATUS Status;

  if ((This == NULL) || (Config == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (&mCachedConfig, Config, sizeof (mCachedConfig));
  if (PersistToDisk) {
    Status = Np05jBiosConfigSave (&mCachedConfig);
    if (!EFI_ERROR (Status)) {
      mProtocol.LoadedFromDisk = TRUE;
    }
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
OnFileSystemInstalled (
  IN EFI_EVENT Event,
  IN VOID     *Context
  )
{
  NP05J_BIOS_CONFIG Loaded;

  if (!EFI_ERROR (Np05jBiosConfigLoad (&Loaded))) {
    if (Loaded.Sequence >= mCachedConfig.Sequence) {
      CopyMem (&mCachedConfig, &Loaded, sizeof (mCachedConfig));
      mProtocol.LoadedFromDisk = TRUE;
    }
  }
}

EFI_STATUS
EFIAPI
Np05jBiosConfigDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_HANDLE Handle = NULL;

  Np05jBiosConfigGetDefaults (&mCachedConfig);
  if (!EFI_ERROR (Np05jBiosConfigLoad (&mCachedConfig))) {
    mProtocol.LoadedFromDisk = TRUE;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gNp05jBiosConfigProtocolGuid,
                  &mProtocol,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnFileSystemInstalled,
                  NULL,
                  &mFsNotifyEvent
                  );
  if (!EFI_ERROR (Status)) {
    gBS->RegisterProtocolNotify (
           &gEfiSimpleFileSystemProtocolGuid,
           mFsNotifyEvent,
           &mFsNotifyReg
           );
  }

  return EFI_SUCCESS;
}
