/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  GOP 24-bit BMP framebuffer screenshot staging for SunUEFI UEFI Fastboot.

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  uefi/core/usb/PianoFastbootScreen.c.
**/

#include "Np05jFastbootCore.h"

#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>

#define SCREEN_MAX_PIXELS  (8U * 1024U * 1024U)

STATIC
VOID
PutLe32 (
  OUT UINT8   *Dst,
  IN  UINT32  Value
  )
{
  for (UINTN I = 0U; I < 4U; I++) {
    Dst[I] = (UINT8)(Value >> (I * 8U));
  }
}

STATIC
UINT32
ComputeCrc32 (
  IN CONST UINT8  *Data,
  IN UINTN        Len
  )
{
  UINT32  Crc = MAX_UINT32;

#ifdef __aarch64__
  /* ใช้คำสั่ง ARMv8 hardware CRC32 (crc32x/crc32b) สำหรับ BMP 10.8 MB */
  UINTN  I = 0U;
  while ((I < Len) && ((((UINTN)(Data + I)) & 7U) != 0U)) {
    UINT8  B = Data[I++];
    __asm__ ("crc32b %w0, %w0, %w1" : "+r"(Crc) : "r"((UINT32)B));
  }
  while ((I + 8U) <= Len) {
    UINT64  W;
    CopyMem (&W, Data + I, sizeof (W));
    __asm__ ("crc32x %w0, %w0, %x1" : "+r"(Crc) : "r"(W));
    I += 8U;
  }
  while (I < Len) {
    UINT8  B = Data[I++];
    __asm__ ("crc32b %w0, %w0, %w1" : "+r"(Crc) : "r"((UINT32)B));
  }
#else
  for (UINTN I = 0U; I < Len; I++) {
    Crc ^= Data[I];
    for (UINTN Bit = 0U; Bit < 8U; Bit++) {
      Crc = (Crc >> 1) ^ (((Crc & 1U) != 0U) ? 0xEDB88320U : 0U);
    }
  }
#endif

  return ~Crc;
}

STATIC
EFI_STATUS
FallbackCopyLinearFb (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop,
  IN  UINT32                         W,
  IN  UINT32                         H,
  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Capture
  )
{
  EFI_GRAPHICS_PIXEL_FORMAT  Fmt;
  UINT32                     Ppsl;
  UINTN                      NeedBytes;
  CONST UINT8                *Fb;

  if ((Gop->Mode->FrameBufferBase == 0ULL) || (Gop->Mode->Info == NULL)) {
    return EFI_UNSUPPORTED;
  }

  Fmt  = Gop->Mode->Info->PixelFormat;
  Ppsl = Gop->Mode->Info->PixelsPerScanLine;
  if ((Fmt != PixelBlueGreenRedReserved8BitPerColor) &&
      (Fmt != PixelRedGreenBlueReserved8BitPerColor))
  {
    return EFI_UNSUPPORTED;
  }

  if (Ppsl < W) {
    return EFI_COMPROMISED_DATA;
  }

  NeedBytes = (UINTN)Ppsl * (UINTN)H * 4U;
  if ((Gop->Mode->FrameBufferSize != 0U) && (Gop->Mode->FrameBufferSize < NeedBytes)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Fb = (CONST UINT8 *)(UINTN)Gop->Mode->FrameBufferBase;
  if (Fmt == PixelBlueGreenRedReserved8BitPerColor) {
    /* รูปแบบ BGRX 32-bit ตรงกับ EFI_GRAPHICS_OUTPUT_BLT_PIXEL คัดลอกตรงด้วย CopyMem */
    if (Ppsl == W) {
      CopyMem (Capture, Fb, (UINTN)W * (UINTN)H * 4U);
    } else {
      for (UINTN Y = 0U; Y < H; Y++) {
        CopyMem (Capture + Y * (UINTN)W, Fb + Y * (UINTN)Ppsl * 4U, (UINTN)W * 4U);
      }
    }
    return EFI_SUCCESS;
  }
  for (UINTN Y = 0U; Y < H; Y++) {
    CONST UINT32  *SrcRow = (CONST UINT32 *)(Fb + Y * (UINTN)Ppsl * 4U);
    for (UINTN X = 0U; X < W; X++) {
      UINT32  Px = SrcRow[X];
      Capture[Y * W + X].Red      = (UINT8)(Px & 0xFFU);
      Capture[Y * W + X].Green    = (UINT8)((Px >> 8) & 0xFFU);
      Capture[Y * W + X].Blue     = (UINT8)((Px >> 16) & 0xFFU);
      Capture[Y * W + X].Reserved = 0U;
    }
  }

  return EFI_SUCCESS;
}

EFI_STATUS
Np05jFastbootCaptureScreen (
  IN OUT NP05J_FASTBOOT  *State,
  OUT    UINT32          *Width,
  OUT    UINT32          *Height,
  OUT    UINT32          *Bytes,
  OUT    UINT32          *Crc32
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop = NULL;
  EFI_STATUS                     Status;
  UINT32                         W;
  UINT32                         H;
  UINTN                          PixelBytes;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Capture;
  UINTN                          Pitch;
  UINTN                          Total;
  UINT8                          *Bmp;
  UINT32                         Check;

  if ((State == NULL) || (Width == NULL) || (Height == NULL) || (Bytes == NULL) || (Crc32 == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Width  = 0U;
  *Height = 0U;
  *Bytes  = 0U;
  *Crc32  = 0U;

  if (gST->ConsoleOutHandle != NULL) {
    Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
  } else {
    Status = EFI_NOT_FOUND;
  }

  if (EFI_ERROR (Status) || (Gop == NULL)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  }

  if (EFI_ERROR (Status) ||
      (Gop == NULL) ||
      (Gop->Mode == NULL) ||
      (Gop->Mode->Info == NULL) ||
      (Gop->Mode->SizeOfInfo < sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)))
  {
    return EFI_NOT_FOUND;
  }

  W = Gop->Mode->Info->HorizontalResolution;
  H = Gop->Mode->Info->VerticalResolution;
  if ((W == 0U) || (H == 0U) || (W > 4096U) || (H > 4096U) || ((UINT64)W * H > SCREEN_MAX_PIXELS)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  PixelBytes = (UINTN)W * (UINTN)H * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
  Capture    = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)AllocateZeroPool (PixelBytes);
  if (Capture == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = EFI_UNSUPPORTED;
  if (Gop->Blt != NULL) {
    Status = Gop->Blt (Gop, Capture, EfiBltVideoToBltBuffer, 0U, 0U, 0U, 0U, W, H, 0U);
  }

  if (EFI_ERROR (Status)) {
    Status = FallbackCopyLinearFb (Gop, W, H, Capture);
  }

  if (EFI_ERROR (Status)) {
    ZeroMem (Capture, PixelBytes);
    FreePool (Capture);
    return Status;
  }

  Pitch = ((UINTN)W * 3U + 3U) & ~(UINTN)3U;
  Total = 54U + Pitch * (UINTN)H;
  if (Total > NP05J_FASTBOOT_MAX_DOWNLOAD) {
    ZeroMem (Capture, PixelBytes);
    FreePool (Capture);
    return EFI_BAD_BUFFER_SIZE;
  }

  Bmp = (UINT8 *)AllocateZeroPool (Total);
  if (Bmp == NULL) {
    ZeroMem (Capture, PixelBytes);
    FreePool (Capture);
    return EFI_OUT_OF_RESOURCES;
  }

  Bmp[0] = 'B';
  Bmp[1] = 'M';
  PutLe32 (Bmp + 2, (UINT32)Total);
  PutLe32 (Bmp + 10, 54U);
  PutLe32 (Bmp + 14, 40U);
  PutLe32 (Bmp + 18, W);
  PutLe32 (Bmp + 22, H);
  Bmp[26] = 1U;
  Bmp[28] = 24U;
  PutLe32 (Bmp + 34, (UINT32)(Pitch * (UINTN)H));

  for (UINTN Y = 0U; Y < H; Y++) {
    UINT8  *Row = Bmp + 54U + ((UINTN)H - 1U - Y) * Pitch;
    for (UINTN X = 0U; X < W; X++) {
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *P = &Capture[Y * (UINTN)W + X];
      Row[X * 3U]      = P->Blue;
      Row[X * 3U + 1U] = P->Green;
      Row[X * 3U + 2U] = P->Red;
    }
  }

  Check  = ComputeCrc32 (Bmp, Total);
  Status = Np05jFastbootStageCopy (State, Bmp, Total);

  ZeroMem (Bmp, Total);
  FreePool (Bmp);
  ZeroMem (Capture, PixelBytes);
  FreePool (Capture);

  if (Status == EFI_SUCCESS) {
    *Width  = W;
    *Height = H;
    *Bytes  = (UINT32)Total;
    *Crc32  = Check;
  }

  return Status;
}
