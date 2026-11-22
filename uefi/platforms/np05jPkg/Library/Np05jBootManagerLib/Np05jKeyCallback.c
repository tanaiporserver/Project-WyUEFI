/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Volume Up (Boot Menu) / Volume Down (BIOS Setup) key notify callbacks for NP05J.
**/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/SimpleTextInEx.h>

#include <Np05jBiosConfig.h>
#include "Np05jBootManager.h"

extern EFI_GUID gNp05jBiosConfigGuid;
extern EFI_GUID gKeypadDeviceProtocolGuid;

STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *mSimpleInExProtocol = NULL;
STATIC VOID                              *mNotifyUp           = NULL;
STATIC VOID                              *mNotifyDown         = NULL;
STATIC BOOLEAN                            mEnterBootManager   = FALSE;

VOID
Np05jSetEnterBootMenu (
  IN BOOLEAN EnterSetupDirect
  )
{
  mEnterBootManager = TRUE;
  if (EnterSetupDirect && (gRT != NULL)) {
    UINT8 Flag = 1U;
    gRT->SetVariable (
           L"Np05jOpenSetup",
           &gNp05jBiosConfigGuid,
           EFI_VARIABLE_BOOTSERVICE_ACCESS,
           sizeof (Flag),
           &Flag
           );
  }
}

STATIC
EFI_STATUS
EFIAPI
OnVolUpKey (
  IN EFI_KEY_DATA *KeyData
  )
{
  Np05jSetEnterBootMenu (FALSE);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
OnVolDownKey (
  IN EFI_KEY_DATA *KeyData
  )
{
  Np05jSetEnterBootMenu (TRUE);
  return EFI_SUCCESS;
}

EFI_STATUS
Np05jRegisterKeyCallback (
  IN EFI_DEVICE_PATH_PROTOCOL *DevicePath
  )
{
  EFI_STATUS   Status;
  EFI_HANDLE   StiHandle;
  EFI_HANDLE  *KeypadBuffer = NULL;
  UINTN        KeypadCount  = 0;
  EFI_KEY_DATA KeyData;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gKeypadDeviceProtocolGuid, NULL, &KeypadCount, &KeypadBuffer);
  if (!EFI_ERROR (Status) && (KeypadBuffer != NULL)) {
    for (UINTN I = 0; I < KeypadCount; I++) {
      gBS->ConnectController (KeypadBuffer[I], NULL, NULL, TRUE);
    }
    FreePool (KeypadBuffer);
  }

  Status = gBS->LocateDevicePath (&gEfiSimpleTextInputExProtocolGuid, &DevicePath, &StiHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (StiHandle, &gEfiSimpleTextInputExProtocolGuid, (VOID **)&mSimpleInExProtocol);
  if (EFI_ERROR (Status) || (mSimpleInExProtocol == NULL)) {
    return Status;
  }

  mSimpleInExProtocol->Reset (mSimpleInExProtocol, TRUE);

  ZeroMem (&KeyData, sizeof (KeyData));
  KeyData.Key.ScanCode = SCAN_UP;
  mSimpleInExProtocol->RegisterKeyNotify (mSimpleInExProtocol, &KeyData, OnVolUpKey, &mNotifyUp);

  KeyData.Key.ScanCode = SCAN_DOWN;
  mSimpleInExProtocol->RegisterKeyNotify (mSimpleInExProtocol, &KeyData, OnVolDownKey, &mNotifyDown);

  return EFI_SUCCESS;
}

VOID
Np05jUnregisterKeyCallback (
  VOID
  )
{
  if (mSimpleInExProtocol == NULL) {
    return;
  }

  if (mNotifyUp != NULL) {
    mSimpleInExProtocol->UnregisterKeyNotify (mSimpleInExProtocol, mNotifyUp);
    mNotifyUp = NULL;
  }
  if (mNotifyDown != NULL) {
    mSimpleInExProtocol->UnregisterKeyNotify (mSimpleInExProtocol, mNotifyDown);
    mNotifyDown = NULL;
  }
}

BOOLEAN
Np05jShouldEnterBootMenu (
  VOID
  )
{
  return mEnterBootManager;
}
