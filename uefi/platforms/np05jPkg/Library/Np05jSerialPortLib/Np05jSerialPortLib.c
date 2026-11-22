/** @file
  NP05J SerialPortLib that renders debug text to the pre-initialized framebuffer
  AND mirrors every byte into a 256 KiB shared RAM ring buffer in Display_Reserved
  so Np05jUsbSerialDxe can stream complete SEC/DXE/vendor logs over USB CDC-ACM.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/SerialPortLib.h>
#include <Library/TimerLib.h>

#include <Np05jLogRing.h>

#include "FrameBuffer.h"
#include "Font.h"

STATIC
inline
UINT8 *
DrawPixel (
  IN UINT8   *Pixels,
  IN UINT32  PixelColor,
  IN UINT32  FontScale
  )
{
  for (UINT8 i = 0; i < FontScale; i++) {
    UINT32  Color = PixelColor;

    for (UINT8 j = 0; j < FB_BPP; j++) {
      *Pixels++ = (UINT8)Color;
      Color   >>= 8;
    }
  }

  return Pixels;
}

STATIC
inline
UINT8 *
DrawRow (
  IN UINT8   *Pixels,
  IN UINT32  RowData,
  IN UINT32  FontScale,
  IN UINTN   Stride
  )
{
  for (UINT8 i = 0; i < FontScale; i++) {
    UINT32  Data = RowData;

    for (UINT8 j = 0; j < FONT_WIDTH; j++) {
      UINT32  PixelColor = (Data & 1) ? 0xFFFFFFFF : 0x00000000;

      Pixels = DrawPixel (Pixels, PixelColor, FontScale);
      Data >>= 1;
    }

    Pixels += Stride;
  }

  return Pixels;
}

STATIC
VOID
DrawGlyph (
  IN UINT8   *Pixels,
  IN UINT64  Glyph,
  IN UINT32  FontScale
  )
{
  UINTN   Stride      = (FB_WIDTH - (FONT_WIDTH * FontScale)) * FB_BPP;
  UINT32  TopGlyph    = (UINT32)(Glyph >> 32);
  UINT32  BottomGlyph = (UINT32)Glyph;
  UINT32  RowData     = TopGlyph;

  for (UINT8 Row = 0; Row < 12; Row++) {
    if (Row == 6) {
      RowData = BottomGlyph;
    }

    Pixels   = DrawRow (Pixels, RowData, FontScale, Stride);
    RowData >>= FONT_WIDTH;
  }
}

STATIC
VOID
AdvanceNewLine (
  IN EFI_PHYSICAL_ADDRESS       FbBase,
  IN UINT64                     FbLength,
  IN EFI_FRAME_BUFFER_POSITION  *CurrentPosition,
  IN EFI_FRAME_BUFFER_POSITION  MaxPosition
  )
{
  MicroSecondDelay (FB_DELAY_US);

  CurrentPosition->XPos  = 0;
  CurrentPosition->YPos += 1;

  if (CurrentPosition->YPos >= MaxPosition.YPos) {
    ZeroMem ((VOID *)(UINTN)FbBase, FB_WIDTH * FB_HEIGHT * FB_BPP);
    CurrentPosition->XPos = 0;
    CurrentPosition->YPos = 0;
  }
}

STATIC
UINT8
GetFontScale (
  VOID
  )
{
  UINT32  ShorterDimension = (FB_WIDTH < FB_HEIGHT) ? FB_WIDTH : FB_HEIGHT;

  return (ShorterDimension < 426) ? 1 : (UINT8)(ShorterDimension / 426);
}

STATIC
VOID
GetFbPositions (
  IN  EFI_PHYSICAL_ADDRESS       FbBase,
  IN  UINT64                     FbLength,
  IN  UINT8                      FontScale,
  OUT EFI_FRAME_BUFFER_POSITION  **CurrentPosition,
  OUT EFI_FRAME_BUFFER_POSITION  *MaxPosition
  )
{
  EFI_FRAME_BUFFER_POSITION  LocalMaxPosition;

  *CurrentPosition      = (EFI_FRAME_BUFFER_POSITION *)(UINTN)(FbBase + FbLength - sizeof (EFI_FRAME_BUFFER_POSITION));
  LocalMaxPosition.XPos = FB_WIDTH  / ((FONT_WIDTH  + 1) * FontScale);
  LocalMaxPosition.YPos = FB_HEIGHT / ((FONT_HEIGHT - 4) * FontScale);
  *MaxPosition          = LocalMaxPosition;
}

STATIC
VOID
WriteFrameBuffer (
  IN EFI_PHYSICAL_ADDRESS  FbBase,
  IN UINT64                FbLength,
  IN UINT8                 Character
  )
{
  EFI_FRAME_BUFFER_POSITION  *CurrentPosition;
  EFI_FRAME_BUFFER_POSITION  MaxPosition;
  UINT8                      *Pixels;

  STATIC CONST UINT64  GlyphFont[] = GLYPH_FONT;
  UINT8                GlyphFontScale = GetFontScale ();

  GetFbPositions (FbBase, FbLength, GlyphFontScale, &CurrentPosition, &MaxPosition);

  if (Character >= 127) {
    return;
  }

  if (Character < 32) {
    if (Character == '\n') {
      AdvanceNewLine (FbBase, FbLength, CurrentPosition, MaxPosition);
    } else if (Character == '\r') {
      CurrentPosition->XPos = 0;
    }

    return;
  }

  if ((CurrentPosition->XPos == 0) && (Character == ' ')) {
    return;
  }

  UINT64  Glyph       = GlyphFont[Character - 32];
  UINTN   GlyphWidth  = (FONT_WIDTH  + 1) * GlyphFontScale;
  UINTN   GlyphHeight = (FONT_HEIGHT - 4) * GlyphFontScale;

  Pixels  = (UINT8 *)(UINTN)FbBase;
  Pixels += (UINTN)CurrentPosition->YPos * GlyphHeight * (FB_WIDTH * FB_BPP);
  Pixels += (UINTN)CurrentPosition->XPos * GlyphWidth  * FB_BPP;

  DrawGlyph (Pixels, Glyph, GlyphFontScale);

  CurrentPosition->XPos++;
  if (CurrentPosition->XPos >= MaxPosition.XPos) {
    AdvanceNewLine (FbBase, FbLength, CurrentPosition, MaxPosition);
  }
}

STATIC
VOID
AppendLogRing (
  IN EFI_PHYSICAL_ADDRESS  FbBase,
  IN UINT64                FbLength,
  IN CONST UINT8           *Buffer,
  IN UINTN                 NumberOfBytes
  )
{
  UINT64                 PixelBytes;
  EFI_PHYSICAL_ADDRESS   RingAddr;
  NP05J_LOG_RING_HEADER  *Header;
  UINT8                  *Data;
  UINT64                 Pos;
  UINT32                 Mask;

  PixelBytes = (UINT64)FB_WIDTH * (UINT64)FB_HEIGHT * (UINT64)FB_BPP;
  if (FbLength < (PixelBytes + NP05J_LOG_RING_TAIL_OFFSET)) {
    return;
  }

  RingAddr = FbBase + FbLength - NP05J_LOG_RING_TAIL_OFFSET;
  Header   = (NP05J_LOG_RING_HEADER *)(UINTN)RingAddr;
  Data     = (UINT8 *)(UINTN)(RingAddr + NP05J_LOG_RING_HEADER_SIZE);

  if ((Header->Magic != NP05J_LOG_RING_MAGIC) ||
      (Header->Version != NP05J_LOG_RING_VERSION) ||
      (Header->Capacity != NP05J_LOG_RING_CAPACITY))
  {
    Header->Version  = NP05J_LOG_RING_VERSION;
    Header->Capacity = NP05J_LOG_RING_CAPACITY;
    Header->Flags    = 0;
    Header->WritePos = 0;
    Header->Magic    = NP05J_LOG_RING_MAGIC;
  }

  Mask = NP05J_LOG_RING_CAPACITY - 1U;
  Pos  = Header->WritePos;

  for (UINTN Index = 0; Index < NumberOfBytes; Index++) {
    Data[(UINTN)(Pos & Mask)] = Buffer[Index];
    Pos++;
  }

  Header->WritePos = Pos;
}

STATIC
EFI_STATUS
GetFrameBufferMemory (
  OUT EFI_PHYSICAL_ADDRESS  *Base,
  OUT UINT64                *Length
  )
{
  EFI_MEMORY_REGION_DESCRIPTOR  FbRegion;

  ZeroMem (&FbRegion, sizeof (FbRegion));
  LocateMemoryRegionByName ("Display Reserved", &FbRegion);
  LocateMemoryRegionByName ("Display_Reserved", &FbRegion);
  if ((FbRegion.Address == 0) || (FbRegion.Length == 0)) {
    return EFI_NOT_FOUND;
  }

  *Base   = FbRegion.Address;
  *Length = FbRegion.Length;
  return EFI_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  FbBase;
  UINT64                FbLength;
  BOOLEAN               InterruptState;
  UINTN                 PixelBytes;

  if ((Buffer == NULL) || (NumberOfBytes == 0)) {
    return 0;
  }

  Status = GetFrameBufferMemory (&FbBase, &FbLength);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  InterruptState = ArmGetInterruptState ();
  if (InterruptState) {
    ArmDisableInterrupts ();
  }

  AppendLogRing (FbBase, FbLength, Buffer, NumberOfBytes);

  for (UINTN i = 0; i < NumberOfBytes; i++) {
    WriteFrameBuffer (FbBase, FbLength, Buffer[i]);
  }

  if (InterruptState) {
    ArmEnableInterrupts ();
  }

  PixelBytes = (UINTN)FB_WIDTH * (UINTN)FB_HEIGHT * (UINTN)FB_BPP;
  if (PixelBytes <= FbLength) {
    WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)FbBase, PixelBytes);
  }

  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

EFI_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  return EFI_SUCCESS;
}
