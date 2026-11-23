/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Persistent unlocked BIOS configuration for nubia NP05J (SM8750).

  Combines UEFI NVRAM variable storage with dual-copy CRC32 FAT files
  (\NP05J\bios-a.bin / \NP05J\bios-b.bin), inspired by Project SunUEFI's
  PianoNvFile persistence model (https://github.com/slwyts/Project-SunUEFI).
**/

#include <Uefi.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/Gpt.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/Np05jBiosConfigLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>

STATIC
UINT32
Np05jComputeConfigCrc (
  IN CONST NP05J_BIOS_CONFIG *Config
  )
{
  NP05J_BIOS_CONFIG Copy;

  CopyMem (&Copy, Config, sizeof (Copy));
  Copy.Crc32 = 0;
  return CalculateCrc32 (&Copy, sizeof (Copy));
}

VOID
EFIAPI
Np05jBiosConfigGetDefaults (
  OUT NP05J_BIOS_CONFIG *Config
  )
{
  if (Config == NULL) {
    return;
  }

  ZeroMem (Config, sizeof (*Config));
  Config->Magic                = NP05J_BIOS_CONFIG_MAGIC;
  Config->Version              = NP05J_BIOS_CONFIG_VERSION;
  Config->Size                 = sizeof (NP05J_BIOS_CONFIG);
  Config->Sequence             = 1;

  /* 1. Boot Configuration */
  Config->DefaultOs            = NP05J_OS_BOOT_MENU;
  Config->BootTimeoutSec       = 5;
  Config->BootSelectTimeoutSec = 3;
  Config->SplashStyle          = NP05J_SPLASH_SUNUEFI_VECTOR;
  Config->FastBoot             = 0;
  Config->AutoScanEsp          = 1;
  Config->WatchdogMinutes      = 5;

  /* 2. CPU, Clock & Cooling */
  Config->CpuPerfProfile       = NP05J_CPU_PERF_MAX;
  Config->ActiveCoreMask       = 0xFF;
  Config->FanMode              = NP05J_FAN_AUTO;
  Config->ThermalLimitC        = 95;
  Config->ScmiHandoff          = 1;

  /* 3. Memory & Storage */
  Config->RamLimitGb           = 0; /* Full 24 GiB */
  Config->UfsWriteProtect      = 1; /* Safe stock RO by default */
  Config->UfsCacheCoherency    = 1;
  Config->SmmuPreOsMode        = NP05J_SMMU_DETACH_NON_MDP;
  Config->Pcie0Enable          = 1;

  /* 4. Display, Console & Touch */
  Config->DisplayRotation      = 0;
  Config->ConsoleScale         = 3; /* 2400x1504 -> 800x501 logical (80x25 UiApp) */
  Config->DisplayDriverMode    = 0; /* SimpleFbDxe */
  Config->TouchEnable          = 1;
  Config->TouchSwapXY          = 1; /* Portrait S3930 -> Landscape 2400x1504 */
  Config->TouchInvertX         = 0;
  Config->TouchInvertY         = 1;
  Config->TouchPollRateHz      = 50;
  Config->VirtualKeyboardEnable = 1;

  /* 5. USB & Debug */
  Config->UsbMode              = NP05J_USB_CDC_SERIAL;
  Config->UsbCRolePolicy       = 0; /* Auto */
  Config->UsbPhySpeed          = 0; /* SuperSpeed */
  Config->DebugLogLevel        = 2;
  Config->EarlyFbLogEnable     = 1;

  /* 6. OS & Windows on ARM */
  Config->OsCompatProfile      = NP05J_COMPAT_AUTO;
  Config->AcpiTableProfile     = NP05J_ACPI_PROFILE_WOA_FULL;
  Config->KepEnable            = 1;
  Config->KepPatchActlr        = 1;
  Config->KepPatchAmcnten      = 1;
  /* ปิด IRM เป็นค่าเริ่มต้นบน SM8750P เพราะ MPIDR เป็น 2 คลัสเตอร์ (Aff2=0/1) ไม่ตรงกับสคริปต์ IRM คลัสเตอร์เดียว */
  Config->KepPatchGicIrm       = 0;
  Config->KepPatchPsciMemProt  = 1;
  Config->RuntimeServicesMode  = 0; /* Enabled */
  Config->AcpiSmmuIortMode     = NP05J_IORT_OMIT_FOR_WOA;
  Config->AcpiUsbEnable        = 1;
  Config->AcpiTouchEnable      = 0; /* ปิดไว้จนกว่าจะมีไดรเวอร์ Windows Synaptics TCM v2 SPI4 */
  Config->AcpiPcieEnable       = 0; /* ปิด MCFG/PCIe0 เป็นค่าเริ่มต้นเพื่อเลี่ยงความเสี่ยงช่วง bring-up */
  Config->AcpiButtonsEnable    = 1;
  Config->DtbFixupEnable       = 1;

  /* 7. Security & NVRAM */
  Config->SecureBootMode       = NP05J_SB_DISABLED;
  Config->AllowUnsignedEfi     = 1;
  Config->AllMenusUnlocked     = 1;
  Config->NvramPersistence     = 1;

  Config->Crc32                = Np05jComputeConfigCrc (Config);
}

VOID
EFIAPI
Np05jBiosConfigApplyPreset (
  IN OUT NP05J_BIOS_CONFIG *Config,
  IN     NP05J_BIOS_PRESET  Preset
  )
{
  UINT64 Seq;

  if (Config == NULL) {
    return;
  }

  Seq = Config->Sequence;
  Np05jBiosConfigGetDefaults (Config);
  Config->Sequence = (Seq > 0) ? (Seq + 1) : 1;

  if (Preset == Np05jPresetWindowsOnArm) {
    Config->DefaultOs           = NP05J_OS_WINDOWS_ARM;
    Config->OsCompatProfile     = NP05J_COMPAT_WINDOWS_11_ARM;
    Config->AcpiTableProfile    = NP05J_ACPI_PROFILE_MINIMAL;
    Config->KepEnable           = 1;
    Config->KepPatchActlr       = 1;
    Config->KepPatchAmcnten     = 1;
    Config->KepPatchGicIrm      = 0;
    Config->KepPatchPsciMemProt = 1;
    Config->RuntimeServicesMode = 0;
    Config->AcpiSmmuIortMode    = NP05J_IORT_OMIT_FOR_WOA;
    Config->AcpiUsbEnable       = 1;
    Config->AcpiTouchEnable     = 1;
    Config->AcpiPcieEnable      = 0;
    Config->AcpiButtonsEnable   = 1;
    Config->SecureBootMode      = NP05J_SB_DISABLED;
    Config->AllowUnsignedEfi    = 1;
  } else if (Preset == Np05jPresetMainlineLinux) {
    Config->DefaultOs           = NP05J_OS_LINUX_DT;
    Config->OsCompatProfile     = NP05J_COMPAT_LINUX_DT;
    Config->AcpiTableProfile    = NP05J_ACPI_PROFILE_WOA_FULL;
    Config->AcpiSmmuIortMode    = NP05J_IORT_ENABLED;
    Config->AcpiPcieEnable      = 1;
    Config->AcpiTouchEnable     = 1;
    Config->DtbFixupEnable      = 1;
  }

  Config->Crc32 = Np05jComputeConfigCrc (Config);
}

BOOLEAN
EFIAPI
Np05jBiosConfigValidate (
  IN CONST NP05J_BIOS_CONFIG *Config
  )
{
  if (Config == NULL) {
    return FALSE;
  }

  if ((Config->Magic != NP05J_BIOS_CONFIG_MAGIC) ||
      (Config->Version != NP05J_BIOS_CONFIG_VERSION) ||
      (Config->Size != sizeof (NP05J_BIOS_CONFIG)))
  {
    return FALSE;
  }

  return Config->Crc32 == Np05jComputeConfigCrc (Config);
}

STATIC
EFI_STATUS
ReadBiosFileSlot (
  IN  EFI_FILE_PROTOCOL     *Root,
  IN  CHAR16                *Path,
  OUT NP05J_BIOS_CONFIG     *OutConfig
  )
{
  EFI_STATUS            Status;
  EFI_FILE_PROTOCOL    *File;
  NP05J_BIOS_FILE_IMAGE Img;
  UINTN                 Size;

  Status = Root->Open (Root, &File, Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Size = sizeof (Img);
  ZeroMem (&Img, sizeof (Img));
  Status = File->Read (File, &Size, &Img);
  File->Close (File);

  if (EFI_ERROR (Status) || (Size != sizeof (Img))) {
    return EFI_COMPROMISED_DATA;
  }

  if ((Img.FileMagic != NP05J_BIOS_FILE_MAGIC) ||
      (Img.FileVersion != 1U) ||
      (Img.Sequence != Img.Config.Sequence) ||
      !Np05jBiosConfigValidate (&Img.Config) ||
      (Img.PayloadCrc32 != CalculateCrc32 (&Img.Config, sizeof (Img.Config))))
  {
    return EFI_CRC_ERROR;
  }

  CopyMem (OutConfig, &Img.Config, sizeof (*OutConfig));
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
IsVolumeWritable (
  IN EFI_FILE_PROTOCOL *Root
  )
{
  EFI_STATUS            Status;
  EFI_FILE_SYSTEM_INFO *FsInfo;
  UINTN                 Size = 0;

  Status = Root->GetInfo (Root, &gEfiFileSystemInfoGuid, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL || Size == 0) {
    return FALSE;
  }

  FsInfo = AllocateZeroPool (Size);
  if (FsInfo == NULL) {
    return FALSE;
  }

  Status = Root->GetInfo (Root, &gEfiFileSystemInfoGuid, &Size, FsInfo);
  if (EFI_ERROR (Status)) {
    FreePool (FsInfo);
    return FALSE;
  }

  {
    BOOLEAN Writable = (!FsInfo->ReadOnly) && (FsInfo->FreeSpace >= 65536ULL);
    FreePool (FsInfo);
    return Writable;
  }
}

STATIC
EFI_STATUS
WriteBiosFileSlot (
  IN EFI_FILE_PROTOCOL       *Root,
  IN CHAR16                  *Path,
  IN CONST NP05J_BIOS_CONFIG *Config
  )
{
  EFI_STATUS            Status;
  EFI_FILE_PROTOCOL    *Dir;
  EFI_FILE_PROTOCOL    *File;
  NP05J_BIOS_FILE_IMAGE Img;
  NP05J_BIOS_CONFIG     Verify;
  UINTN                 Size;

  Status = Root->Open (
                   Root,
                   &Dir,
                   L"\\NP05J",
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                   EFI_FILE_DIRECTORY
                   );
  if (!EFI_ERROR (Status)) {
    Dir->Close (Dir);
  }

  /* Recreate file cleanly so exact length is written */
  Status = Root->Open (Root, &File, Path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (!EFI_ERROR (Status)) {
    File->Delete (File);
  }

  Status = Root->Open (
                   Root,
                   &File,
                   Path,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                   0
                   );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (&Img, sizeof (Img));
  Img.FileMagic    = NP05J_BIOS_FILE_MAGIC;
  Img.FileVersion  = 1U;
  Img.Sequence     = Config->Sequence;
  CopyMem (&Img.Config, Config, sizeof (Img.Config));
  Img.PayloadCrc32 = CalculateCrc32 (&Img.Config, sizeof (Img.Config));

  Size   = sizeof (Img);
  Status = File->Write (File, &Size, &Img);
  if (!EFI_ERROR (Status)) {
    Status = File->Flush (File);
  }
  File->Close (File);

  if (EFI_ERROR (Status) || (Size != sizeof (Img))) {
    return EFI_DEVICE_ERROR;
  }

  Status = ReadBiosFileSlot (Root, Path, &Verify);
  if (EFI_ERROR (Status) || (CompareMem (&Verify, Config, sizeof (Verify)) != 0)) {
    return EFI_CRC_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Np05jBiosConfigLoad (
  OUT NP05J_BIOS_CONFIG *Config
  )
{
  EFI_STATUS                       Status;
  NP05J_BIOS_CONFIG                Best;
  NP05J_BIOS_CONFIG                Candidate;
  BOOLEAN                          HaveBest = FALSE;
  UINTN                            VarSize  = sizeof (Candidate);
  EFI_HANDLE                      *Handles  = NULL;
  UINTN                            Count    = 0;

  if (Config == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Np05jBiosConfigGetDefaults (&Best);

  if (gRT != NULL) {
    Status = gRT->GetVariable (
                    NP05J_BIOS_CONFIG_VAR_NAME,
                    &gNp05jBiosConfigGuid,
                    NULL,
                    &VarSize,
                    &Candidate
                    );
    if (!EFI_ERROR (Status) && (VarSize == sizeof (Candidate)) && Np05jBiosConfigValidate (&Candidate)) {
      CopyMem (&Best, &Candidate, sizeof (Best));
      HaveBest = TRUE;
    }
  }

  if (gBS != NULL) {
    Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
    if (!EFI_ERROR (Status) && (Handles != NULL)) {
      for (UINTN Index = 0; Index < Count; Index++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
        EFI_FILE_PROTOCOL               *Root;

        if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
          continue;
        }
        if (EFI_ERROR (Fs->OpenVolume (Fs, &Root))) {
          continue;
        }

        if (!EFI_ERROR (ReadBiosFileSlot (Root, NP05J_BIOS_CONFIG_FILE_A, &Candidate))) {
          if (!HaveBest || (Candidate.Sequence >= Best.Sequence)) {
            CopyMem (&Best, &Candidate, sizeof (Best));
            HaveBest = TRUE;
          }
        }
        if (!EFI_ERROR (ReadBiosFileSlot (Root, NP05J_BIOS_CONFIG_FILE_B, &Candidate))) {
          if (!HaveBest || (Candidate.Sequence >= Best.Sequence)) {
            CopyMem (&Best, &Candidate, sizeof (Best));
            HaveBest = TRUE;
          }
        }

        Root->Close (Root);
      }

      FreePool (Handles);
    }
  }

  /* Enforce unlocked invariant */
  Best.AllMenusUnlocked = 1;
  Best.Crc32            = Np05jComputeConfigCrc (&Best);
  CopyMem (Config, &Best, sizeof (*Config));

  if (HaveBest && (gRT != NULL)) {
    gRT->SetVariable (
           NP05J_BIOS_CONFIG_VAR_NAME,
           &gNp05jBiosConfigGuid,
           EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
           sizeof (Best),
           &Best
           );
  }

  return HaveBest ? EFI_SUCCESS : EFI_NOT_FOUND;
}

EFI_STATUS
EFIAPI
Np05jBiosConfigSave (
  IN CONST NP05J_BIOS_CONFIG *Config
  )
{
  NP05J_BIOS_CONFIG  ToSave;
  EFI_STATUS         VarStatus  = EFI_UNSUPPORTED;
  EFI_STATUS         DiskStatus = EFI_NOT_FOUND;
  EFI_HANDLE        *Handles    = NULL;
  UINTN              Count      = 0;

  if (Config == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (&ToSave, Config, sizeof (ToSave));
  ToSave.Magic            = NP05J_BIOS_CONFIG_MAGIC;
  ToSave.Version          = NP05J_BIOS_CONFIG_VERSION;
  ToSave.Size             = sizeof (NP05J_BIOS_CONFIG);
  ToSave.AllMenusUnlocked = 1;
  ToSave.Sequence         = (ToSave.Sequence == 0) ? 1 : (ToSave.Sequence + 1);
  ToSave.Crc32            = Np05jComputeConfigCrc (&ToSave);

  if (gRT != NULL) {
    VarStatus = gRT->SetVariable (
                       NP05J_BIOS_CONFIG_VAR_NAME,
                       &gNp05jBiosConfigGuid,
                       EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                       sizeof (ToSave),
                       &ToSave
                       );
  }

  // ในโหมด Phase 2 ที่มี PianoNvFile ใช้พื้นที่เก็บ NVRAM บนพาร์ติชัน persist แทนการเขียน FAT บน ESP
  if (gBS != NULL) {
    EFI_GUID NvFileGuid = { 0x8e65d9c0, 0x9943, 0x4130, { 0x9c, 0x24, 0x4e, 0x56, 0x46, 0x49, 0x4c, 0x45 } };
    VOID *NvFile = NULL;
    if (gBS->LocateProtocol (&NvFileGuid, NULL, &NvFile) == EFI_SUCCESS) {
      return VarStatus;
    }
  }

  if ((ToSave.NvramPersistence != 0) && (gBS != NULL)) {
    EFI_STATUS Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
    if (!EFI_ERROR (Status) && (Handles != NULL)) {
      CHAR16 *TargetSlot = ((ToSave.Sequence & 1ULL) != 0ULL) ? NP05J_BIOS_CONFIG_FILE_B : NP05J_BIOS_CONFIG_FILE_A;

      /* ขอบเขตความปลอดภัย: เขียนไฟล์คอนฟิกเฉพาะพาร์ติชัน GPT ESP หรือ volume ที่มี \EFI / \NP05J อยู่แล้วเท่านั้น
       * ห้ามเขียนทับพาร์ติชัน FAT สต็อกของ Android (เช่น logfs หรือ modem) โดยเด็ดขาด
       */
      for (UINTN Index = 0; Index < Count; Index++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
        EFI_PARTITION_INFO_PROTOCOL     *PartInfo = NULL;
        EFI_FILE_PROTOCOL               *Root;
        EFI_FILE_PROTOCOL               *Probe;
        BOOLEAN                          Allowed = FALSE;

        if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPartitionInfoProtocolGuid, (VOID **)&PartInfo)) &&
            (PartInfo != NULL) &&
            (PartInfo->Type == PARTITION_TYPE_GPT) &&
            CompareGuid (&PartInfo->Info.Gpt.PartitionTypeGUID, &gEfiPartTypeSystemPartGuid))
        {
          Allowed = TRUE;
        }

        if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
          continue;
        }
        if (EFI_ERROR (Fs->OpenVolume (Fs, &Root))) {
          continue;
        }
        if (!IsVolumeWritable (Root)) {
          Root->Close (Root);
          continue;
        }

        if (!Allowed) {
          if (!EFI_ERROR (Root->Open (Root, &Probe, L"\\NP05J", EFI_FILE_MODE_READ, 0))) {
            Allowed = TRUE;
            Probe->Close (Probe);
          } else if (!EFI_ERROR (Root->Open (Root, &Probe, L"\\EFI", EFI_FILE_MODE_READ, 0))) {
            Allowed = TRUE;
            Probe->Close (Probe);
          }
        }

        if (!Allowed) {
          Root->Close (Root);
          continue;
        }

        DiskStatus = WriteBiosFileSlot (Root, TargetSlot, &ToSave);
        Root->Close (Root);
        if (!EFI_ERROR (DiskStatus)) {
          break;
        }
      }

      FreePool (Handles);
    }
  }

  return !EFI_ERROR (DiskStatus) ? EFI_SUCCESS : VarStatus;
}

EFI_STATUS
EFIAPI
Np05jBiosConfigReset (
  OUT NP05J_BIOS_CONFIG *Config OPTIONAL
  )
{
  NP05J_BIOS_CONFIG Defaults;

  Np05jBiosConfigGetDefaults (&Defaults);
  if (Config != NULL) {
    CopyMem (Config, &Defaults, sizeof (*Config));
  }
  return Np05jBiosConfigSave (&Defaults);
}
