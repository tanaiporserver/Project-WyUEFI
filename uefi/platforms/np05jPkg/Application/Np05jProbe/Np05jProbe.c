#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>

STATIC
EFI_STATUS
ReportGop (IN EFI_HANDLE Handle)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  EFI_STATUS                            Status;

  Status = gBS->HandleProtocol (Handle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) {
    return EFI_NOT_STARTED;
  }

  Info = Gop->Mode->Info;
  Print (L"%s GOP mode %u: %ux%u, %u pixels per scan line, pixel format %u\r\n",
         Handle == gST->ConsoleOutHandle ? L"Console" : L"Device",
         Gop->Mode->Mode, Info->HorizontalResolution, Info->VerticalResolution,
         Info->PixelsPerScanLine, Info->PixelFormat);
  Print (L"Framebuffer base 0x%lx, size 0x%lx\r\n",
         Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize);
  if (Info->PixelFormat == PixelBitMask) {
    Print (L"Pixel masks R/G/B/reserved: %08x %08x %08x %08x\r\n",
           Info->PixelInformation.RedMask, Info->PixelInformation.GreenMask,
           Info->PixelInformation.BlueMask, Info->PixelInformation.ReservedMask);
  }

  if ((Info->PixelFormat == PixelBltOnly) || (Gop->Mode->FrameBufferSize == 0)) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
CheckGop (VOID)
{
  EFI_HANDLE *Handles;
  EFI_STATUS  Status;
  EFI_STATUS  Result;
  UINTN       Count;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // ConSplitter can advertise a BLT-only console even when a physical GOP has a framebuffer.
  Result = EFI_NOT_FOUND;
  for (UINTN Index = 0; Index < Count; Index++) {
    Status = ReportGop (Handles[Index]);
    if (EFI_ERROR (Status)) {
      Print (L"GOP handle %u: %r\r\n", (UINT32)Index, Status);
    }

    if (EFI_ERROR (Result)) {
      Result = Status;
    }
  }

  FreePool (Handles);
  return Result;
}

STATIC
BOOLEAN
IsUfsLu0Node (IN EFI_DEVICE_PATH_PROTOCOL *Node)
{
  if ((DevicePathType (Node) == HARDWARE_DEVICE_PATH) && (DevicePathSubType (Node) == HW_VENDOR_DP) &&
      (DevicePathNodeLength (Node) >= sizeof (VENDOR_DEVICE_PATH))) {
    return CompareGuid (&((VENDOR_DEVICE_PATH *)Node)->Guid, &gEfiUfsLU0Guid);
  }

  return (DevicePathType (Node) == MESSAGING_DEVICE_PATH) && (DevicePathSubType (Node) == MSG_UFS_DP) &&
         (DevicePathNodeLength (Node) == sizeof (UFS_DEVICE_PATH)) && (((UFS_DEVICE_PATH *)Node)->Lun == 0);
}

STATIC
EFI_STATUS
FindUfsLu0 (OUT EFI_BLOCK_IO_PROTOCOL **BlockIo)
{
  EFI_DEVICE_PATH_PROTOCOL *Node;
  EFI_HANDLE               *Handles;
  UINTN                     Count;
  EFI_STATUS                Status;
  EFI_BLOCK_IO_PROTOCOL    *Candidate;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;
  for (UINTN Index = 0; Index < Count; Index++) {
    Node = DevicePathFromHandle (Handles[Index]);
    if ((Node == NULL) || EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&Candidate))) {
      continue;
    }

    if ((Candidate->Media == NULL) || Candidate->Media->LogicalPartition || Candidate->Media->RemovableMedia) {
      continue;
    }

    while (!IsDevicePathEnd (Node)) {
      // UFSDxe uses a Qualcomm vendor path. Also accept the standard UFS messaging node.
      if (IsUfsLu0Node (Node)) {
        *BlockIo = Candidate;
        Status = EFI_SUCCESS;
        break;
      }

      Node = NextDevicePathNode (Node);
    }

    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  FreePool (Handles);
  return Status;
}

STATIC
EFI_STATUS
CheckUfs (VOID)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_BLOCK_IO_MEDIA    *Media;
  EFI_STATUS             Status;
  UINT32                 Crc[2];
  UINT32                 MediaId;
  UINT32                 BlockSize;
  VOID                  *Buffer;
  UINTN                  Pages;

  Status = FindUfsLu0 (&BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Media = BlockIo->Media;
  if (!Media->MediaPresent) {
    return EFI_NO_MEDIA;
  }

  if ((Media->BlockSize == 0) || (Media->LastBlock < 1)) {
    return EFI_UNSUPPORTED;
  }

  BlockSize = Media->BlockSize;
  MediaId = Media->MediaId;
  Pages = EFI_SIZE_TO_PAGES ((UINTN)BlockSize);
  Buffer = AllocateAlignedPages (Pages, Media->IoAlign);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Print (L"Internal UFS LU0, media %u, LBA 1, %u bytes\r\n", MediaId, BlockSize);
  for (UINTN Pass = 0; Pass < ARRAY_SIZE (Crc); Pass++) {
    // Different initial bytes expose a transfer that returns success without filling the buffer.
    SetMem (Buffer, BlockSize, (UINT8)(Pass * 0xFF));
    Status = BlockIo->ReadBlocks (BlockIo, MediaId, 1, BlockSize, Buffer);
    if (EFI_ERROR (Status)) {
      break;
    }

    Status = gBS->CalculateCrc32 (Buffer, BlockSize, &Crc[Pass]);
    if (EFI_ERROR (Status)) {
      break;
    }

    Print (L"Read %u CRC32 %08x\r\n", (UINT32)Pass + 1, Crc[Pass]);
  }

  FreeAlignedPages (Buffer, Pages);
  if (!EFI_ERROR (Status) && (Crc[0] != Crc[1])) {
    return EFI_CRC_ERROR;
  }

  return Status;
}

STATIC
VOID
ObserveButton (VOID)
{
  EFI_EVENT     Events[2];
  EFI_INPUT_KEY Key;
  EFI_STATUS    Status;
  UINTN         Index;

  if ((gST->ConIn == NULL) || (gST->ConIn->WaitForKey == NULL)) {
    return;
  }

  Events[0] = gST->ConIn->WaitForKey;
  Status = gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &Events[1]);
  if (EFI_ERROR (Status)) {
    return;
  }

  Print (L"Press a tablet button within 30 seconds. Photograph the results.\r\n");
  Status = gBS->SetTimer (Events[1], TimerRelative, EFI_TIMER_PERIOD_SECONDS (30));
  if (!EFI_ERROR (Status)) {
    Status = gBS->WaitForEvent (ARRAY_SIZE (Events), Events, &Index);
    if (!EFI_ERROR (Status) && (Index == 0)) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (!EFI_ERROR (Status)) {
        Print (L"Key scan 0x%04x, Unicode 0x%04x\r\n", Key.ScanCode, Key.UnicodeChar);
        gBS->Stall (5000000);
      }
    }
  }

  gBS->CloseEvent (Events[1]);
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS GopStatus;
  EFI_STATUS UfsStatus;

  GopStatus = CheckGop ();
  UfsStatus = CheckUfs ();
  Print (L"GOP: %r\r\nUFS read comparison: %r\r\n", GopStatus, UfsStatus);
  ObserveButton ();
  return EFI_ERROR (UfsStatus) ? UfsStatus : GopStatus;
}
