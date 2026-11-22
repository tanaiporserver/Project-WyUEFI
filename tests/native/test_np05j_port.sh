#!/usr/bin/env bash
# ชุดทดสอบภาษา C บนโฮสต์สำหรับพอร์ต nubia NP05J (Phase 1 & PianoConsoleScale)
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
CC="${CC:-cc}"
CFLAGS=(-O2 -Wall -Wextra -Werror)

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# 1. ทดสอบ TCM v2 Touch Protocol ของ NP05J
"$CC" "${CFLAGS[@]}" \
  -I"$ROOT/uefi/platforms/np05jPkg/Include" \
  "$ROOT/uefi/platforms/np05jPkg/Tests/test_touch_tcm2.c" \
  -o "$tmp/test_touch_tcm2"
"$tmp/test_touch_tcm2"

# 2. ทดสอบ BootSelect & EarlySplash พร้อมตรวจผลลัพธ์ BMP
"$CC" "${CFLAGS[@]}" \
  -I"$ROOT/uefi/platforms/np05jPkg/Include" \
  -I"$ROOT/uefi/platforms/np05jPkg/Tools/BootSelect" \
  "$ROOT/uefi/platforms/np05jPkg/Tests/test_bootselect.c" \
  "$ROOT/uefi/platforms/np05jPkg/Tools/BootSelect/BootRequest.c" \
  "$ROOT/uefi/platforms/np05jPkg/Tools/BootSelect/EarlySplash.c" \
  -o "$tmp/test_bootselect"
"$tmp/test_bootselect" "$tmp/boot-selection-preview.bmp"
test -s "$tmp/boot-selection-preview.bmp"

# 3. ทดสอบ NP05J Fastboot Core
"$CC" "${CFLAGS[@]}" -DNP05J_HOST_TEST \
  -I"$ROOT/uefi/platforms/np05jPkg/Include" \
  -I"$ROOT/uefi/platforms/np05jPkg/Drivers/Np05jUsbSerialDxe" \
  "$ROOT/uefi/platforms/np05jPkg/Tests/test_fastboot.c" \
  "$ROOT/uefi/platforms/np05jPkg/Drivers/Np05jUsbSerialDxe/Np05jFastbootCore.c" \
  -o "$tmp/test_fastboot"
"$tmp/test_fastboot"

# 4. ทดสอบ PianoConsoleScale.h สำหรับความละเอียด 3200x2136@4x, 2400x1504@3x และ 1920x1080 passthrough
cat > "$tmp/test_console_scale.c" <<'EOF'
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATIC static
#define CONST const
#define IN
#define OUT
#define VOID void
#define EFIAPI
typedef int EFI_STATUS;
typedef uint32_t UINT32;
typedef uint8_t UINT8;
typedef uintptr_t UINTN;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
#define MAX_UINTN UINTPTR_MAX
#define EFI_SUCCESS 0
#define EFI_INVALID_PARAMETER 2
#define EFI_UNSUPPORTED 3
#define EFI_BAD_BUFFER_SIZE 4
#define EFI_OUT_OF_RESOURCES 9
#define EFI_ERROR(s) ((s) != 0)
#define DEBUG_INFO 1
#define DEBUG(arg) do { (void)0; } while (0)

typedef struct {
  UINT8 Blue, Green, Red, Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef enum {
  PixelRedGreenBlueReserved8BitPerColor = 0,
  PixelBlueGreenRedReserved8BitPerColor = 1,
  PixelBitMask = 2,
  PixelBltOnly = 3
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef enum {
  EfiBltVideoFill = 0,
  EfiBltVideoToBltBuffer = 1,
  EfiBltBufferToVideo = 2,
  EfiBltVideoToVideo = 3,
  EfiGraphicsOutputBltOperationMax = 4
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef struct {
  UINT32 Version;
  UINT32 HorizontalResolution;
  UINT32 VerticalResolution;
  EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
  UINT32 PixelInformation[4];
  UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
  UINT32 MaxMode;
  UINT32 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN SizeOfInfo;
  EFI_PHYSICAL_ADDRESS FrameBufferBase;
  UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
  EFI_STATUS (EFIAPI *QueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32, UINTN *, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
  EFI_STATUS (EFIAPI *SetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32);
  EFI_STATUS (EFIAPI *Blt)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *, EFI_GRAPHICS_OUTPUT_BLT_OPERATION, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN);
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput;
  VOID *PianoConsoleSurface;
} GRAPHICS_CONSOLE_DEV;

static VOID *AllocatePool(UINTN Size) { return malloc(Size); }
static VOID *AllocateZeroPool(UINTN Size) { return calloc(1, Size); }
static VOID *AllocateCopyPool(UINTN Size, CONST VOID *Buf) {
  VOID *Out = malloc(Size);
  if (Out) { memcpy(Out, Buf, Size); }
  return Out;
}
static VOID *CopyMem(VOID *Dst, CONST VOID *Src, UINTN Size) { return memmove(Dst, Src, Size); }
static VOID FreePool(VOID *Buf) { free(Buf); }

static UINTN mLastW = 0, mLastH = 0;
static EFI_GRAPHICS_OUTPUT_BLT_PIXEL mLastCopy[256];
static EFI_STATUS EFIAPI StubQuery(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 Mode, UINTN *Bytes, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info) {
  (void)This; (void)Mode; (void)Bytes; (void)Info; return EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI StubSet(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 Mode) {
  (void)This; (void)Mode; return EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI StubBlt(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buf,
  EFI_GRAPHICS_OUTPUT_BLT_OPERATION Op, UINTN Sx, UINTN Sy, UINTN Dx, UINTN Dy,
  UINTN W, UINTN H, UINTN Delta) {
  (void)This; (void)Sx; (void)Sy; (void)Dx; (void)Dy; (void)Delta;
  mLastW = W; mLastH = H;
  if (Op == EfiBltBufferToVideo && Buf && W * H <= 256U) {
    memcpy(mLastCopy, Buf, W * H * sizeof(*Buf));
  }
  return EFI_SUCCESS;
}

#include "PianoConsoleScale.h"

static void CheckProfile(UINT32 InW, UINT32 InH, UINT32 ExpW, UINT32 ExpH, UINTN ExpScale, int ExpectScaled) {
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info = {
    .Version = 0, .HorizontalResolution = InW, .VerticalResolution = InH,
    .PixelFormat = PixelBlueGreenRedReserved8BitPerColor, .PixelsPerScanLine = InW
  };
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode = {
    .MaxMode = 1, .Mode = 0, .Info = &Info, .SizeOfInfo = sizeof(Info),
    .FrameBufferBase = 0xFC800000ULL, .FrameBufferSize = (UINTN)InW * InH * 4U
  };
  EFI_GRAPHICS_OUTPUT_PROTOCOL Phys = { StubQuery, StubSet, StubBlt, &Mode };
  GRAPHICS_CONSOLE_DEV Dev = { &Phys, NULL };
  UINT32 W = InW, H = InH;

  assert(PianoConsoleAttach(&Dev, &W, &H) == EFI_SUCCESS);
  assert(W == ExpW && H == ExpH);
  if (ExpectScaled) {
    assert(Dev.PianoConsoleSurface != NULL);
    assert(Dev.GraphicsOutput != &Phys);
    assert(((PIANO_CONSOLE_SURFACE *)Dev.PianoConsoleSurface)->Scale == ExpScale);
    assert(Dev.GraphicsOutput->Mode->Info->HorizontalResolution == ExpW);
    assert(Dev.GraphicsOutput->Mode->Info->VerticalResolution == ExpH);
    assert(Dev.GraphicsOutput->Mode->Info->PixelFormat == PixelBltOnly);
    mLastW = 0; mLastH = 0;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Px = {0};
    assert(Dev.GraphicsOutput->Blt(Dev.GraphicsOutput, &Px, EfiBltVideoFill, 0, 0, 1, 2, 3, 4, 0) == EFI_SUCCESS);
    assert(mLastW == 3 * ExpScale && mLastH == 4 * ExpScale);
    // ตรวจสอบความถูกต้องระดับพิกเซลของการสเกล 3x/4x และการใช้บัฟเฟอร์ซ้ำ
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Src[4] = {
      {1, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 0}, {10, 11, 12, 0}
    };
    assert(Dev.GraphicsOutput->Blt(Dev.GraphicsOutput, Src, EfiBltBufferToVideo, 0, 0, 0, 0, 2, 2, 0) == EFI_SUCCESS);
    assert(mLastW == 2 * ExpScale && mLastH == 2 * ExpScale);
    for (UINTN Sy = 0; Sy < 2; ++Sy) {
      for (UINTN Sx = 0; Sx < 2; ++Sx) {
        for (UINTN Dy = 0; Dy < ExpScale; ++Dy) {
          for (UINTN Dx = 0; Dx < ExpScale; ++Dx) {
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL Got = mLastCopy[(Sy * ExpScale + Dy) * (2 * ExpScale) + (Sx * ExpScale + Dx)];
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL Exp = Src[Sy * 2 + Sx];
            assert(Got.Blue == Exp.Blue && Got.Green == Exp.Green && Got.Red == Exp.Red);
          }
        }
      }
    }
  } else {
    assert(Dev.PianoConsoleSurface == NULL);
    assert(Dev.GraphicsOutput == &Phys);
  }
  PianoConsoleDetach(&Dev);
  assert(Dev.PianoConsoleSurface == NULL);
  assert(Dev.GraphicsOutput == &Phys);
}

int main(void) {
  CheckProfile(3200U, 2136U, 800U, 534U, 4U, 1);
  CheckProfile(2400U, 1504U, 800U, 501U, 3U, 1);
  CheckProfile(1920U, 1080U, 1920U, 1080U, 0U, 0);
  printf("PASS: test_console_scale (3200x2136@4x -> 800x534, 2400x1504@3x -> 800x501, 1920x1080 passthrough)\n");
  return 0;
}
EOF

"$CC" "${CFLAGS[@]}" \
  -I"$ROOT/uefi/components/product-support/Library/ConsoleScale" \
  "$tmp/test_console_scale.c" \
  -o "$tmp/test_console_scale"
"$tmp/test_console_scale"
