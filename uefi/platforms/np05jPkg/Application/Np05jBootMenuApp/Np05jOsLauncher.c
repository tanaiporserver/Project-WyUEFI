/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  OS, arbitrary EFI image, and built-in UEFI Shell launcher for nubia NP05J.
**/

#include "Np05jBootMenuApp.h"

UINTN
Np05jCountFileSystems (
  VOID
  )
{
  EFI_HANDLE *Handles = NULL;
  UINTN       Count   = 0;

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles)) &&
      (Handles != NULL))
  {
    FreePool (Handles);
    return Count;
  }
  return 0;
}

STATIC
BOOLEAN
FileExistsOnVolume (
  IN EFI_FILE_PROTOCOL *Root,
  IN CONST CHAR16      *Path
  )
{
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_STATUS         Status;

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) || (File == NULL)) {
    return FALSE;
  }
  File->Close (File);
  return TRUE;
}

EFI_STATUS
Np05jLaunchEfiPathOnVolume (
  IN EFI_HANDLE    FsHandle,
  IN CONST CHAR16 *FilePath,
  IN BOOLEAN       ConnectDrivers
  )
{
  EFI_DEVICE_PATH_PROTOCOL *DevPath     = NULL;
  EFI_HANDLE                ImageHandle = NULL;
  EFI_STATUS                Status;

  if (ConnectDrivers) {
    EfiBootManagerConnectAll ();
  }

  DevPath = FileDevicePath (FsHandle, (CHAR16 *)FilePath);
  if (DevPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
  }

  Status = gBS->LoadImage (TRUE, gImageHandle, DevPath, NULL, 0, &ImageHandle);
  FreePool (DevPath);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->StartImage (ImageHandle, NULL, NULL);
  return Status;
}

STATIC
EFI_STATUS
LaunchFirstMatchingPath (
  IN OUT NP05J_UI_CTX  *Ctx,
  IN     CONST CHAR16 **Paths,
  IN     UINTN          PathCount,
  IN     CONST CHAR8   *Label
  )
{
  EFI_HANDLE *Handles = NULL;
  UINTN       Count   = 0;
  EFI_STATUS  Status;

  EfiBootManagerConnectAll ();
  Np05jUiRefreshHandles (Ctx);

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || (Count == 0U) || (Handles == NULL)) {
    Np05jUiSetBanner (Ctx, COL_RED, "%a: No FAT/ESP volumes found", Label);
    return EFI_NOT_FOUND;
  }

  for (UINTN P = 0; P < PathCount; P++) {
    for (UINTN V = 0; V < Count; V++) {
      EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs   = NULL;
      EFI_FILE_PROTOCOL               *Root = NULL;

      if (EFI_ERROR (gBS->HandleProtocol (Handles[V], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) ||
          (Fs == NULL) ||
          EFI_ERROR (Fs->OpenVolume (Fs, &Root)) ||
          (Root == NULL))
      {
        continue;
      }

      if (FileExistsOnVolume (Root, Paths[P])) {
        Root->Close (Root);
        Status = Np05jLaunchEfiPathOnVolume (Handles[V], Paths[P], FALSE);
        FreePool (Handles);
        Np05jUiSetBanner (Ctx, COL_RED, "%a returned: %r", Label, Status);
        return Status;
      }
      Root->Close (Root);
    }
  }

  FreePool (Handles);
  Np05jUiSetBanner (Ctx, COL_GOLD, "%a: Bootloader not found on %u volume(s)", Label, (UINT32)Count);
  return EFI_NOT_FOUND;
}

EFI_STATUS
Np05jLaunchWindowsArm (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  STATIC CONST CHAR16 *mWinPaths[] = {
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
    L"\\EFI\\BOOT\\BOOTAA64.EFI"
  };

  return LaunchFirstMatchingPath (Ctx, mWinPaths, ARRAY_SIZE (mWinPaths), "Windows 11 ARM");
}

EFI_STATUS
Np05jLaunchLinux (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     BOOLEAN       UseAcpi
  )
{
  STATIC CONST CHAR16 *mLinuxPaths[] = {
    L"\\EFI\\systemd\\systemd-bootaa64.efi",
    L"\\EFI\\ubuntu\\grubaa64.efi",
    L"\\EFI\\debian\\grubaa64.efi",
    L"\\EFI\\arch\\grubaa64.efi",
    L"\\EFI\\fedora\\grubaa64.efi",
    L"\\EFI\\opensuse\\grubaa64.efi",
    L"\\EFI\\Linux\\linux.efi",
    L"\\EFI\\Linux\\vmlinuz.efi",
    L"\\EFI\\BOOT\\BOOTAA64.EFI"
  };

  return LaunchFirstMatchingPath (
           Ctx,
           mLinuxPaths,
           ARRAY_SIZE (mLinuxPaths),
           UseAcpi ? "Linux (ACPI)" : "Linux (DeviceTree)"
           );
}

EFI_STATUS
Np05jLaunchEfiShell (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  STATIC EFI_GUID                    mShellGuid  = NP05J_UEFI_SHELL_GUID;
  EFI_LOADED_IMAGE_PROTOCOL         *LoadedImage = NULL;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_DEVICE_PATH_PROTOCOL          *DevPath     = NULL;
  EFI_HANDLE                         ShellHandle = NULL;
  EFI_STATUS                         Status;

  Status = gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (EFI_ERROR (Status) || (LoadedImage == NULL)) {
    Np05jUiSetBanner (Ctx, COL_RED, "UEFI Shell: LoadedImage unavailable (%r)", Status);
    return Status;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode, &mShellGuid);
  DevPath = AppendDevicePathNode (DevicePathFromHandle (LoadedImage->DeviceHandle), (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
  if (DevPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
    gST->ConOut->EnableCursor (gST->ConOut, TRUE);
  }

  Status = gBS->LoadImage (TRUE, gImageHandle, DevPath, NULL, 0, &ShellHandle);
  FreePool (DevPath);
  if (EFI_ERROR (Status)) {
    Np05jUiSetBanner (Ctx, COL_RED, "UEFI Shell LoadImage failed: %r", Status);
    return Status;
  }

  Status = gBS->StartImage (ShellHandle, NULL, NULL);
  if (gST->ConOut != NULL) {
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }
  Np05jUiSetBanner (Ctx, COL_ACCENT, "UEFI Shell exited (%r)", Status);
  return Status;
}

EFI_STATUS
Np05jLaunchTianoSetup (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  STATIC EFI_GUID                    mUiAppGuid  = NP05J_UIAPP_GUID;
  EFI_LOADED_IMAGE_PROTOCOL         *LoadedImage = NULL;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_DEVICE_PATH_PROTOCOL          *DevPath     = NULL;
  EFI_HANDLE                         UiHandle    = NULL;
  EFI_STATUS                         Status      = EFI_NOT_FOUND;

  EfiInitializeFwVolDevicepathNode (&FileNode, &mUiAppGuid);

  if (!EFI_ERROR (gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage)) &&
      (LoadedImage != NULL))
  {
    DevPath = AppendDevicePathNode (DevicePathFromHandle (LoadedImage->DeviceHandle), (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
    if (DevPath != NULL) {
      Status = gBS->LoadImage (TRUE, gImageHandle, DevPath, NULL, 0, &UiHandle);
      FreePool (DevPath);
    }
  }

  if (EFI_ERROR (Status)) {
    EFI_HANDLE *Volumes = NULL;
    UINTN       Count   = 0;

    if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiFirmwareVolume2ProtocolGuid, NULL, &Count, &Volumes)) &&
        (Volumes != NULL))
    {
      for (UINTN I = 0; I < Count; I++) {
        DevPath = AppendDevicePathNode (DevicePathFromHandle (Volumes[I]), (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
        if (DevPath == NULL) {
          continue;
        }
        Status = gBS->LoadImage (TRUE, gImageHandle, DevPath, NULL, 0, &UiHandle);
        FreePool (DevPath);
        if (!EFI_ERROR (Status)) {
          break;
        }
      }
      FreePool (Volumes);
    }
  }

  if (EFI_ERROR (Status) || (UiHandle == NULL)) {
    Np05jUiSetBanner (Ctx, COL_RED, "TianoCore Setup (UiApp) unavailable (%r)", Status);
    return Status;
  }

  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
    gST->ConOut->EnableCursor (gST->ConOut, TRUE);
  }

  Status = gBS->StartImage (UiHandle, NULL, NULL);

  if (gST->ConOut != NULL) {
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }

  Np05jUiDrainInput (Ctx);
  Np05jUiSetBanner (Ctx, COL_ACCENT, "TianoCore Setup exited (%r)", Status);
  return Status;
}
