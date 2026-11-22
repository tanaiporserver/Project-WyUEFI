// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/LoadedImage.h>

// The slot B driver publishes its interface after building its channel cache.
// Its bus 0 context, capabilities, PID heads and six-byte records are all
// inside its own loaded PE. Inspect only the verified layout of this image.
STATIC EFI_GUID mSpmiProtocol = {
  0xfa5f306b, 0xf47d, 0x4ac4, {0xa4, 0x7d, 0x88, 0x2f, 0x82, 0x04, 0xec, 0x30}
};

#define CHANNEL_CAPACITY  0x380
#define NO_CHANNEL        0x400
#define V12_TRANSFER_CALL  0x9400118d
#define V11_TRANSFER_CALL  0x9400118c
#define V10_TRANSFER_CALL  0x9400118b
#define V9_TRANSFER_CALL  0x9400118a
#define V8_TRANSFER_CALL  0x94001189
#define V7_TRANSFER_CALL  0x94001188
#define V6_TRANSFER_CALL  0x94001187
#define V5_TRANSFER_CALL  0x94001186
#define V4_TRANSFER_CALL  0x94001185
#define V3_TRANSFER_CALL  0x94001183
STATIC
EFI_STATUS
InspectCache (IN CONST UINT8 *Image, IN UINT8 TargetSid, IN UINT8 TargetPid)
{
  CONST UINT8 *Controller = Image + 0x71f0;
  CONST UINT8 *Bus        = Image + 0x7160;
  CONST UINT8 *Limits     = Image + 0x7124;
  CONST UINT16 *Heads     = (CONST UINT16 *)(Image + 0x7670);
  CONST UINT8 *Records    = Image + 0x7870;
  UINT8  Visited[(CHANNEL_CAPACITY + 7) / 8] = {0};
  UINT16 Selected = NO_CHANNEL;
  UINT8  SelectedOwner = 0x10;
  UINT16 Count, Base, Index, Link;
  UINT8  Ee, Owner, Mask;
  UINT32 TransferCall;
  CONST UINT8 *Record;

  if ((*(CONST UINT32 *)(Image + 0x3640) != 0x14000e50) ||
      (*(CONST UINT32 *)(Image + 0x3740) != 0xb8646869) ||
      (*(CONST UINT32 *)(Image + 0x34c8) != 0xf9400c08)) {
    return EFI_UNSUPPORTED;
  }
  if ((Image[0x72e8] != 1) ||
      (*(CONST UINTN *)(Image + 0x7658) != (UINTN)Controller) ||
      (Controller[0x12] == 0) ||
      (*(CONST UINTN *)(Controller + 0x18) != (UINTN)Bus) ||
      (*(CONST UINT64 *)Bus != 0x0c400000) || (Bus[0x10] != 0) ||
      (*(CONST UINTN *)(Bus + 0x18) != (UINTN)Heads) ||
      (*(CONST UINTN *)(Bus + 0x20) != (UINTN)Records) ||
      (*(CONST UINTN *)(Bus + 0x30) != (UINTN)Limits)) {
    return EFI_NOT_READY;
  }

  TransferCall = *(CONST UINT32 *)(Image + 0x2890);
  switch (TransferCall) {
    case V12_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v12 - live write-owner check\n"));
      }
      break;
    case V11_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v11 - 7:2960 write filter added\n"));
      }
      break;
    case V10_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v10 - 7:2952 write filter added\n"));
      }
      break;
    case V9_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v9 - 7:2951 write filter added\n"));
      }
      break;
    case V8_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v8 - 7:2950 write filter added\n"));
      }
      break;
    case V7_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v7 - 7:2654 write filter added\n"));
      }
      break;
    case V6_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v6 - 7:2650 write filter added\n"));
      }
      break;
    case V5_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v5 - 7:4F44, 7:3E46, 7:2658 and 7:2640 write filters armed\n"));
      }
      break;
    case V4_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v4 - 7:4F44, 7:3E46 and 7:2658 write filters armed\n"));
      }
      break;
    case V3_TRANSFER_CALL:
      if (TargetPid == 0x4f) {
        DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v3 - 7:4F44 and 7:3E46 write filters armed\n"));
      }
      break;
    case 0x94001184:
      DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v2 - 7:4F44 write filter armed\n"));
      break;
    case 0x9400034a:
      DEBUG ((DEBUG_ERROR, "NP05J SPMI DEBUGGER v1 - Mu before PmicDxe\n"));
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  Count = *(CONST UINT16 *)Limits;
  Base  = *(CONST UINT16 *)(Bus + 0x44);
  Ee    = Controller[0];
  if ((Count == 0) || (Count > CHANNEL_CAPACITY) || (Base + Count > NO_CHANNEL)) {
    return EFI_COMPROMISED_DATA;
  }
  DEBUG ((DEBUG_ERROR, "NP05J SPMI bus=0 PPID=%x EE=%x base=%x count=%x\n",
          (TargetSid << 8) | TargetPid, Ee, Base, Count));

  Index = Heads[TargetPid];
  if (Index == NO_CHANNEL) {
    return EFI_NOT_FOUND;
  }
  for (;;) {
    if (Index >= Count) {
      return EFI_COMPROMISED_DATA;
    }
    Mask = (UINT8)(1U << (Index & 7));
    if ((Visited[Index / 8] & Mask) != 0) {
      return EFI_COMPROMISED_DATA;
    }
    Visited[Index / 8] |= Mask;
    Record = Records + 6 * Index;
    if ((Record[0] & 0x1f) == TargetSid) {
      Owner = Record[1] & 0x1f;
      DEBUG ((DEBUG_ERROR, "NP05J candidate local=%x APID=%x writeOwner=%x\n",
              Index, Base + Index, Owner));
      // Stock lookup uses the first matching owner, or the last matching SID.
      if ((Selected == NO_CHANNEL) || (SelectedOwner != Ee)) {
        Selected = Index;
        SelectedOwner = Owner;
      }
    }
    Link = *(CONST UINT16 *)(Record + 4) & 0x7ff;
    if ((Link & NO_CHANNEL) != 0) {
      if ((Link & 0xff) != TargetPid) {
        return EFI_COMPROMISED_DATA;
      }
      break;
    }
    Index = Link;
  }

  if (Selected == NO_CHANNEL) {
    return EFI_NOT_FOUND;
  }
  DEBUG ((DEBUG_ERROR, "NP05J predicted APID=%x owner=%x RW-status addr=%x %a-guard=%a\n",
          Base + Selected, SelectedOwner, 0x0c500008U + ((Base + Selected) << 12),
          (TransferCall == V12_TRANSFER_CALL) ? "cache" : "owner",
          ((SelectedOwner < 16) && (SelectedOwner == Ee)) ? "allow" : "deny"));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FindSpmiCache (VOID)
{
  EFI_HANDLE *Handles;
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  VOID *Interface;
  EFI_STATUS Status, ProbeStatus;
  UINTN Count, Index;
  UINT32 TransferCall;
  BOOLEAN InspectPid26, InspectPid29;

  Status = gBS->LocateProtocol (&mSpmiProtocol, NULL, &Interface);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiLoadedImageProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiLoadedImageProtocolGuid,
                                        (VOID **)&Loaded))) {
      continue;
    }
    if ((Loaded->ImageSize == 0xb000) &&
        ((UINTN)Interface == (UINTN)Loaded->ImageBase + 0x7098)) {
      TransferCall = *(CONST UINT32 *)((CONST UINT8 *)Loaded->ImageBase + 0x2890);
      InspectPid29 = (TransferCall == V8_TRANSFER_CALL) || (TransferCall == V9_TRANSFER_CALL) ||
                     (TransferCall == V10_TRANSFER_CALL) || (TransferCall == V11_TRANSFER_CALL) ||
                     (TransferCall == V12_TRANSFER_CALL);
      InspectPid26 = (TransferCall == V4_TRANSFER_CALL) || (TransferCall == V5_TRANSFER_CALL) ||
                     (TransferCall == V6_TRANSFER_CALL) || (TransferCall == V7_TRANSFER_CALL) ||
                     InspectPid29;
      Status = InspectCache (Loaded->ImageBase, 7, 0x4f);
      if (!EFI_ERROR (Status) &&
          ((TransferCall == V3_TRANSFER_CALL) || InspectPid26)) {
        Status = InspectCache (Loaded->ImageBase, 7, 0x3e);
      }
      if (!EFI_ERROR (Status) && InspectPid26) {
        Status = InspectCache (Loaded->ImageBase, 7, 0x26);
      }
      if (!EFI_ERROR (Status) && InspectPid29) {
        Status = InspectCache (Loaded->ImageBase, 7, 0x29);
      }
      if (TransferCall == V12_TRANSFER_CALL) {
        ProbeStatus = InspectCache (Loaded->ImageBase, 1, 0x0b);
        DEBUG ((DEBUG_ERROR, "NP05J PPID=10B cache snapshot: %r\n", ProbeStatus));
      }
      break;
    }
  }
  gBS->FreePool (Handles);
  return Status;
}

STATIC
VOID
PrintSnapshot (VOID)
{
  EFI_STATUS Status;

  Status = FindSpmiCache ();
  DEBUG ((DEBUG_ERROR, "NP05J snapshot: %r\n", Status));
}

EFI_STATUS
EFIAPI
ShowSpmiSnapshot (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_INPUT_KEY Key;
  EFI_STATUS Status;
  UINTN Tick, Deadline = 40;

  PrintSnapshot ();
  if (SystemTable->ConIn == NULL) {
    DEBUG ((DEBUG_ERROR, "NP05J capture window: 2 seconds\n"));
    gBS->Stall (2000000);
  } else {
    DEBUG ((DEBUG_ERROR, "NP05J R=refresh P=pause 10s C=continue (auto 2s)\n"));
    for (Tick = 0; Tick < Deadline; Tick++) {
      Status = SystemTable->ConIn->ReadKeyStroke (SystemTable->ConIn, &Key);
      if (Status == EFI_SUCCESS) {
        if ((Key.UnicodeChar == L'c') || (Key.UnicodeChar == L'C')) {
          break;
        }
        if ((Key.UnicodeChar == L'p') || (Key.UnicodeChar == L'P')) {
          Deadline = Tick + 200;
          DEBUG ((DEBUG_ERROR, "NP05J paused for 10s; C to continue\n"));
        }
        if ((Key.UnicodeChar == L'r') || (Key.UnicodeChar == L'R')) {
          PrintSnapshot ();
        }
      }
      gBS->Stall (50000);
    }
  }
  DEBUG ((DEBUG_ERROR, "NP05J continuing DXE\n"));
  return EFI_SUCCESS;
}
