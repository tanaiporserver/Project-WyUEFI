/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Scaled logical text-console surface for high-DPI NP05J panel (2400x1504).
  Physical GOP resolution stays untouched so graphical apps and OS handoff
  retain full native 2400x1504 framebuffer geometry.

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  uefi/components/product-support/Library/ConsoleScale/PianoConsoleScale.h.
**/

#pragma once

#include <Library/UefiRuntimeServicesTableLib.h>
#include <Np05jBiosConfig.h>

extern EFI_GUID gNp05jBiosConfigGuid;

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL         Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE    Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
  EFI_GRAPHICS_OUTPUT_PROTOCOL        *Physical;
  UINTN                                Scale;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL       *Scratch;
  UINTN                                ScratchPixels;
} NP05J_CONSOLE_SURFACE;

STATIC
EFI_STATUS
EFIAPI
Np05jConsoleQuery (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                 ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
  )
{
  NP05J_CONSOLE_SURFACE *S = (NP05J_CONSOLE_SURFACE *)This;

  if ((SizeOfInfo == NULL) || (Info == NULL) || (ModeNumber != S->Mode.Mode)) {
    return EFI_UNSUPPORTED;
  }

  *Info = AllocateCopyPool (sizeof (S->Info), &S->Info);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (S->Info);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jConsoleSet (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN UINT32                        ModeNumber
  )
{
  NP05J_CONSOLE_SURFACE *S = (NP05J_CONSOLE_SURFACE *)This;
  return (ModeNumber == S->Mode.Mode) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
Np05jConsoleBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL      *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL     *Buffer,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  Op,
  IN UINTN                              Sx,
  IN UINTN                              Sy,
  IN UINTN                              Dx,
  IN UINTN                              Dy,
  IN UINTN                              W,
  IN UINTN                              H,
  IN UINTN                              Delta OPTIONAL
  )
{
  NP05J_CONSOLE_SURFACE         *S = (NP05J_CONSOLE_SURFACE *)This;
  UINTN                          N = S->Scale;
  UINTN                          RowPixels;
  UINTN                          Rows;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Scaled;
  EFI_STATUS                     Status;

  if ((W == 0) || (H == 0) || (Op >= EfiGraphicsOutputBltOperationMax)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Op != EfiBltVideoToBltBuffer) &&
      ((Dx >= S->Info.HorizontalResolution) ||
       (Dy >= S->Info.VerticalResolution) ||
       (W > S->Info.HorizontalResolution - Dx) ||
       (H > S->Info.VerticalResolution - Dy)))
  {
    return EFI_INVALID_PARAMETER;
  }

  if (Op == EfiBltVideoFill) {
    return S->Physical->Blt (S->Physical, Buffer, Op, 0, 0, Dx * N, Dy * N, W * N, H * N, 0);
  }

  if (Op == EfiBltVideoToVideo) {
    return S->Physical->Blt (S->Physical, NULL, Op, Sx * N, Sy * N, Dx * N, Dy * N, W * N, H * N, 0);
  }

  if ((Buffer == NULL) || (W > MAX_UINTN / N) || (H > MAX_UINTN / N)) {
    return EFI_INVALID_PARAMETER;
  }

  RowPixels = W * N;
  Rows      = H * N;
  if (RowPixels > MAX_UINTN / sizeof (*Buffer) / Rows) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (Delta == 0) {
    Delta = W * sizeof (*Buffer);
  }

  if (((Delta % sizeof (*Buffer)) != 0) ||
      (Delta / sizeof (*Buffer) < ((Op == EfiBltBufferToVideo) ? Sx : Dx) + W))
  {
    return EFI_INVALID_PARAMETER;
  }

  UINTN TotalPixels = RowPixels * Rows;
  if (TotalPixels > S->ScratchPixels) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Next = AllocatePool (TotalPixels * sizeof (*Buffer));
    if (Next == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    if (S->Scratch != NULL) {
      FreePool (S->Scratch);
    }
    S->Scratch       = Next;
    S->ScratchPixels = TotalPixels;
  }
  Scaled = S->Scratch;

  if (Op == EfiBltBufferToVideo) {
    /* แตกพิกเซลตามแนวแกน X แบบไม่ต้องหารในลูป และคัดลอกแถวซ้ำด้วย CopyMem */
    UINTN RowBytes = RowPixels * sizeof (*Buffer);
    for (UINTN SrcY = 0; SrcY < H; SrcY++) {
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Src = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)Buffer + (Sy + SrcY) * Delta) + Sx;
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Dst = Scaled + SrcY * N * RowPixels;
      if (N == 3U) {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Out = Dst;
        for (UINTN SrcX = 0; SrcX < W; SrcX++) {
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL Px = Src[SrcX];
          Out[0] = Px;
          Out[1] = Px;
          Out[2] = Px;
          Out   += 3U;
        }
      } else if (N == 2U) {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Out = Dst;
        for (UINTN SrcX = 0; SrcX < W; SrcX++) {
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL Px = Src[SrcX];
          Out[0] = Px;
          Out[1] = Px;
          Out   += 2U;
        }
      } else {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Out = Dst;
        for (UINTN SrcX = 0; SrcX < W; SrcX++) {
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL Px = Src[SrcX];
          for (UINTN Rep = 0; Rep < N; Rep++) {
            *Out++ = Px;
          }
        }
      }
      for (UINTN RepY = 1U; RepY < N; RepY++) {
        CopyMem (Dst + RepY * RowPixels, Dst, RowBytes);
      }
    }
    Status = S->Physical->Blt (S->Physical, Scaled, Op, 0, 0, Dx * N, Dy * N, RowPixels, Rows, RowBytes);
  } else {
    Status = S->Physical->Blt (S->Physical, Scaled, Op, Sx * N, Sy * N, 0, 0, RowPixels, Rows, RowPixels * sizeof (*Buffer));
    if (!EFI_ERROR (Status)) {
      for (UINTN Y = 0; Y < H; Y++) {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Dst = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)Buffer + (Dy + Y) * Delta);
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Src = Scaled + Y * N * RowPixels;
        for (UINTN X = 0; X < W; X++) {
          Dst[Dx + X] = Src[X * N];
        }
      }
    }
  }

  return Status;
}

STATIC
UINTN
Np05jGetConfiguredScale (
  IN UINT32 Width,
  IN UINT32 Height
  )
{
  UINTN             Scale = 3U;
  NP05J_BIOS_CONFIG Config;
  UINTN             Size  = sizeof (Config);

  if ((gRT != NULL) &&
      !EFI_ERROR (gRT->GetVariable (NP05J_BIOS_CONFIG_VAR_NAME, &gNp05jBiosConfigGuid, NULL, &Size, &Config)) &&
      (Size == sizeof (Config)) &&
      (Config.Magic == NP05J_BIOS_CONFIG_MAGIC))
  {
    if ((Config.ConsoleScale >= 1U) && (Config.ConsoleScale <= 3U)) {
      Scale = Config.ConsoleScale;
    }
  }

  while ((Scale > 1U) && (((Width / Scale) < 640U) || ((Height / Scale) < 475U))) {
    Scale--;
  }

  return Scale;
}

STATIC
EFI_STATUS
Np05jConsoleAttach (
  IN OUT GRAPHICS_CONSOLE_DEV *Private,
  IN OUT UINT32               *Width,
  IN OUT UINT32               *Height
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Physical = Private->GraphicsOutput;
  UINTN                         Scale;
  NP05J_CONSOLE_SURFACE        *S;

  if ((Physical == NULL) || (*Width < 1280U) || (*Height < 950U)) {
    return EFI_SUCCESS;
  }

  Scale = Np05jGetConfiguredScale (*Width, *Height);
  if (Scale <= 1U) {
    return EFI_SUCCESS;
  }

  S = AllocateZeroPool (sizeof (*S));
  if (S == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  S->Physical                  = Physical;
  S->Scale                     = Scale;
  S->Mode                      = *Physical->Mode;
  S->Info                      = *Physical->Mode->Info;
  S->Mode.MaxMode              = 1U;
  S->Mode.Mode                 = 0U;
  S->Info.HorizontalResolution = *Width / (UINT32)Scale;
  S->Info.VerticalResolution   = *Height / (UINT32)Scale;
  S->Info.PixelsPerScanLine    = S->Info.HorizontalResolution;
  S->Info.PixelFormat          = PixelBltOnly;
  S->Mode.Info                 = &S->Info;
  S->Mode.SizeOfInfo           = sizeof (S->Info);
  S->Mode.FrameBufferBase      = 0;
  S->Mode.FrameBufferSize      = 0;

  S->Gop.QueryMode = Np05jConsoleQuery;
  S->Gop.SetMode   = Np05jConsoleSet;
  S->Gop.Blt       = Np05jConsoleBlt;
  S->Gop.Mode      = &S->Mode;

  Private->GraphicsOutput     = &S->Gop;
  Private->Np05jConsoleSurface = S;
  *Width  = S->Info.HorizontalResolution;
  *Height = S->Info.VerticalResolution;

  DEBUG ((DEBUG_INFO, "NP05J_CONSOLE_SCALE factor=%u logical=%ux%u\n", (UINT32)Scale, *Width, *Height));
  return EFI_SUCCESS;
}

STATIC
VOID
Np05jConsoleDetach (
  IN OUT GRAPHICS_CONSOLE_DEV *Private
  )
{
  NP05J_CONSOLE_SURFACE *S = (NP05J_CONSOLE_SURFACE *)Private->Np05jConsoleSurface;
  if (S != NULL) {
    Private->GraphicsOutput      = S->Physical;
    Private->Np05jConsoleSurface = NULL;
    if (S->Scratch != NULL) {
      FreePool (S->Scratch);
    }
    FreePool (S);
  }
}
