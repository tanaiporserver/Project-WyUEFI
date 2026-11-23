/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Runtime ACPI Table updater for nubia NP05J (SM8750P).

  Patches DSDT platform/BIOS feature flags using AslUpdateLib and applies
  configurable WoA / Linux ACPI policies (optional IORT & MCFG removal).
**/

#include <PiDxe.h>
#include <IndustryStandard/Acpi.h>
#include <Library/AslUpdateLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/Np05jBiosConfigLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AcpiTable.h>

#include <Np05jBiosConfig.h>

STATIC
VOID
UninstallAcpiBySignature (
  IN EFI_ACPI_TABLE_PROTOCOL *AcpiTableProtocol,
  IN UINT32                   Signature,
  IN CONST CHAR8             *Label
  )
{
  EFI_ACPI_DESCRIPTION_HEADER *Table  = NULL;
  UINTN                        Handle = 0;
  EFI_STATUS                   Status;

  Status = LocateTableBySignature (Signature, &Table, &Handle);
  if (EFI_ERROR (Status)) {
    return;
  }

  if (Table != NULL) {
    FreePool (Table);
  }

  Status = AcpiTableProtocol->UninstallAcpiTable (AcpiTableProtocol, Handle);
  DEBUG ((DEBUG_INFO, "NP05J_ACPI: Uninstall %a -> %r\n", Label, Status));
}

VOID
UpdateAcpiTables (
  VOID
  )
{
  NP05J_BIOS_CONFIG            Config;
  EFI_ACPI_DESCRIPTION_HEADER *DsdtTable         = NULL;
  UINTN                        DsdtHandle        = 0;
  EFI_ACPI_TABLE_PROTOCOL     *AcpiTableProtocol = NULL;
  EFI_STATUS                   Status;

  UINT32 Soid = 639U;           /* SM8750P / NP05JHW1.0 */
  UINT32 Sidv = 0x00020000U;    /* Rev 2.0 */
  UINT16 Svmj = 2U;
  UINT16 Svmi = 0U;
  UINT64 Sosn = 0x4E5030354A008750ULL;
  UINT32 Stor = 1U;
  UINT32 Ucca;
  UINT32 Usbe;
  UINT32 Urse;
  UINT32 Tsen;
  UINT32 Pcie;
  UINT32 Btne;
  BOOLEAN MinimalAcpi;

  Np05jBiosConfigLoad (&Config);

  MinimalAcpi = (Config.AcpiTableProfile == NP05J_ACPI_PROFILE_MINIMAL);
  Ucca = (Config.UfsCacheCoherency != 0U) ? 1U : 0U;
  Usbe = (Config.AcpiUsbEnable != 0U) ? 1U : 0U;
  /* เปิด URS0 (Dual-Role) เฉพาะในโหมด Full ACPI ที่ไม่ได้บังคับ Host-only (UsbCRolePolicy == 2) */
  Urse = (!MinimalAcpi && (Usbe != 0U) && (Config.UsbCRolePolicy != 2U)) ? 1U : 0U;
  Tsen = (Config.AcpiTouchEnable != 0U) ? 1U : 0U;
  Pcie = (!MinimalAcpi && (Config.AcpiPcieEnable != 0U) && (Config.Pcie0Enable != 0U)) ? 1U : 0U;
  Btne = (Config.AcpiButtonsEnable != 0U) ? 1U : 0U;

  Status = LocateTableBySignature (
             EFI_ACPI_6_3_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
             &DsdtTable,
             &DsdtHandle
             );
  if (!EFI_ERROR (Status) && (DsdtTable != NULL)) {
    AslUpdateName (DsdtTable, SIGNATURE_32 ('S', 'O', 'I', 'D'), &Soid, sizeof (Soid));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('S', 'I', 'D', 'V'), &Sidv, sizeof (Sidv));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('S', 'V', 'M', 'J'), &Svmj, sizeof (Svmj));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('S', 'V', 'M', 'I'), &Svmi, sizeof (Svmi));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('S', 'O', 'S', 'N'), &Sosn, sizeof (Sosn));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('S', 'T', 'O', 'R'), &Stor, sizeof (Stor));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('U', 'C', 'C', 'A'), &Ucca, sizeof (Ucca));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('U', 'S', 'B', 'E'), &Usbe, sizeof (Usbe));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('U', 'R', 'S', 'E'), &Urse, sizeof (Urse));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('T', 'S', 'E', 'N'), &Tsen, sizeof (Tsen));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('P', 'C', 'I', 'E'), &Pcie, sizeof (Pcie));
    AslUpdateName (DsdtTable, SIGNATURE_32 ('B', 'T', 'N', 'E'), &Btne, sizeof (Btne));

    Status = ReinstallTable (DsdtTable, &DsdtHandle);
    DEBUG ((DEBUG_INFO, "NP05J_ACPI: Reinstall DSDT (min=%u ucca=%u usb=%u urs=%u touch=%u pcie=%u) -> %r\n",
            (UINT32)MinimalAcpi, Ucca, Usbe, Urse, Tsen, Pcie, Status));
  } else {
    DEBUG ((DEBUG_WARN, "NP05J_ACPI: DSDT not found (%r)\n", Status));
  }

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTableProtocol);
  if (EFI_ERROR (Status) || (AcpiTableProtocol == NULL)) {
    return;
  }

  if (MinimalAcpi || (Config.AcpiSmmuIortMode == NP05J_IORT_OMIT_FOR_WOA)) {
    UninstallAcpiBySignature (AcpiTableProtocol, SIGNATURE_32 ('I', 'O', 'R', 'T'), "IORT (WoA safe)");
  }

  if (Pcie == 0U) {
    UninstallAcpiBySignature (AcpiTableProtocol, SIGNATURE_32 ('M', 'C', 'F', 'G'), "MCFG (PCIe disabled)");
  }

  if (MinimalAcpi) {
    UninstallAcpiBySignature (AcpiTableProtocol, SIGNATURE_32 ('D', 'B', 'G', '2'), "DBG2 (minimal bring-up)");
    UninstallAcpiBySignature (AcpiTableProtocol, SIGNATURE_32 ('S', 'P', 'C', 'R'), "SPCR (minimal bring-up)");
  }
}
