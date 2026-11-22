/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Double-buffered 2D vector/bitmap canvas and unified touch+mouse+keypad
  input engine for NP05J UEFI Boot Menu & Unlocked BIOS Setup.
**/

#include "Np05jBootMenuApp.h"

/* Crisp 8x8 monochrome bitmap font covering ASCII 0x20..0x7E */
STATIC CONST UINT8 mFont8x8[95][8] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ' ' */
  { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 }, /* '!' */
  { 0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* '"' */
  { 0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00 }, /* '#' */
  { 0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00 }, /* '$' */
  { 0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00 }, /* '%' */
  { 0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00 }, /* '&' */
  { 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* '\'' */
  { 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00 }, /* '(' */
  { 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00 }, /* ')' */
  { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 }, /* '*' */
  { 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00 }, /* '+' */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 }, /* ',' */
  { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* '-' */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 }, /* '.' */
  { 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00 }, /* '/' */
  { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00 }, /* '0' */
  { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 }, /* '1' */
  { 0x3C, 0x66, 0x06, 0x1C, 0x30, 0x60, 0x7E, 0x00 }, /* '2' */
  { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00 }, /* '3' */
  { 0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00 }, /* '4' */
  { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00 }, /* '5' */
  { 0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00 }, /* '6' */
  { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 }, /* '7' */
  { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00 }, /* '8' */
  { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00 }, /* '9' */
  { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00 }, /* ':' */
  { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30, 0x00 }, /* ';' */
  { 0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00 }, /* '<' */
  { 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00 }, /* '=' */
  { 0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00 }, /* '>' */
  { 0x3C, 0x66, 0x06, 0x1C, 0x18, 0x00, 0x18, 0x00 }, /* '?' */
  { 0x3C, 0x66, 0x6E, 0x6A, 0x6E, 0x60, 0x3C, 0x00 }, /* '@' */
  { 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00 }, /* 'A' */
  { 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00 }, /* 'B' */
  { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00 }, /* 'C' */
  { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00 }, /* 'D' */
  { 0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00 }, /* 'E' */
  { 0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00 }, /* 'F' */
  { 0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3E, 0x00 }, /* 'G' */
  { 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 }, /* 'H' */
  { 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 }, /* 'I' */
  { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00 }, /* 'J' */
  { 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00 }, /* 'K' */
  { 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00 }, /* 'L' */
  { 0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00 }, /* 'M' */
  { 0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00 }, /* 'N' */
  { 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 }, /* 'O' */
  { 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00 }, /* 'P' */
  { 0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00 }, /* 'Q' */
  { 0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00 }, /* 'R' */
  { 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00 }, /* 'S' */
  { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 }, /* 'T' */
  { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 }, /* 'U' */
  { 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 }, /* 'V' */
  { 0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00 }, /* 'W' */
  { 0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00 }, /* 'X' */
  { 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00 }, /* 'Y' */
  { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00 }, /* 'Z' */
  { 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00 }, /* '[' */
  { 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00 }, /* '\\' */
  { 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00 }, /* ']' */
  { 0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* '^' */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00 }, /* '_' */
  { 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* '`' */
  { 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00 }, /* 'a' */
  { 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00 }, /* 'b' */
  { 0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00 }, /* 'c' */
  { 0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00 }, /* 'd' */
  { 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00 }, /* 'e' */
  { 0x1C, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00 }, /* 'f' */
  { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C }, /* 'g' */
  { 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00 }, /* 'h' */
  { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00 }, /* 'i' */
  { 0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x6C, 0x38, 0x00 }, /* 'j' */
  { 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00 }, /* 'k' */
  { 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 }, /* 'l' */
  { 0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xC6, 0x00 }, /* 'm' */
  { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00 }, /* 'n' */
  { 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00 }, /* 'o' */
  { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60 }, /* 'p' */
  { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06 }, /* 'q' */
  { 0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00 }, /* 'r' */
  { 0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00 }, /* 's' */
  { 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00 }, /* 't' */
  { 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00 }, /* 'u' */
  { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 }, /* 'v' */
  { 0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00 }, /* 'w' */
  { 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00 }, /* 'x' */
  { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C }, /* 'y' */
  { 0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00 }, /* 'z' */
  { 0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00 }, /* '{' */
  { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 }, /* '|' */
  { 0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00 }, /* '}' */
  { 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }  /* '~' */
};

STATIC INT32 mPrevCursorX = -1;
STATIC INT32 mPrevCursorY = -1;

typedef struct {
  INT32 X;
  INT32 Y;
} NP05J_UI_POINT;

/* Project SunUEFI (ProductSplashAssets.h) 6-spoke starburst, 32-pt circle & 5x7 glyphs */
STATIC CONST NP05J_UI_POINT mSunSpokes[6] = {
  { 0, -1000 }, { 866, -500 }, { 866, 500 },
  { 0,  1000 }, { -866, 500 }, { -866, -500 }
};

STATIC CONST NP05J_UI_POINT mSunCircle[32] = {
  { 1000, 0 }, { 981, 195 }, { 924, 383 }, { 831, 556 },
  { 707, 707 }, { 556, 831 }, { 383, 924 }, { 195, 981 },
  { 0, 1000 }, { -195, 981 }, { -383, 924 }, { -556, 831 },
  { -707, 707 }, { -831, 556 }, { -924, 383 }, { -981, 195 },
  { -1000, 0 }, { -981, -195 }, { -924, -383 }, { -831, -556 },
  { -707, -707 }, { -556, -831 }, { -383, -924 }, { -195, -981 },
  { 0, -1000 }, { 195, -981 }, { 383, -924 }, { 556, -831 },
  { 707, -707 }, { 831, -556 }, { 924, -383 }, { 981, -195 }
};

STATIC CONST UINT8 mSunLetters[26][5] = {
  { 0x7e, 0x11, 0x11, 0x11, 0x7e }, { 0x7f, 0x49, 0x49, 0x49, 0x36 }, { 0x3e, 0x41, 0x41, 0x41, 0x22 },
  { 0x7f, 0x41, 0x41, 0x22, 0x1c }, { 0x7f, 0x49, 0x49, 0x49, 0x41 }, { 0x7f, 0x09, 0x09, 0x09, 0x01 },
  { 0x3e, 0x41, 0x49, 0x49, 0x7a }, { 0x7f, 0x08, 0x08, 0x08, 0x7f }, { 0x00, 0x41, 0x7f, 0x41, 0x00 },
  { 0x20, 0x40, 0x41, 0x3f, 0x01 }, { 0x7f, 0x08, 0x14, 0x22, 0x41 }, { 0x7f, 0x40, 0x40, 0x40, 0x40 },
  { 0x7f, 0x02, 0x0c, 0x02, 0x7f }, { 0x7f, 0x04, 0x08, 0x10, 0x7f }, { 0x3e, 0x41, 0x41, 0x41, 0x3e },
  { 0x7f, 0x09, 0x09, 0x09, 0x06 }, { 0x3e, 0x41, 0x51, 0x21, 0x5e }, { 0x7f, 0x09, 0x19, 0x29, 0x46 },
  { 0x46, 0x49, 0x49, 0x49, 0x31 }, { 0x01, 0x01, 0x7f, 0x01, 0x01 }, { 0x3f, 0x40, 0x40, 0x40, 0x3f },
  { 0x1f, 0x20, 0x40, 0x20, 0x1f }, { 0x3f, 0x40, 0x38, 0x40, 0x3f }, { 0x63, 0x14, 0x08, 0x14, 0x63 },
  { 0x07, 0x08, 0x70, 0x08, 0x07 }, { 0x61, 0x51, 0x49, 0x45, 0x43 }
};

STATIC CONST UINT8 mSunDigits[10][5] = {
  { 0x3e, 0x51, 0x49, 0x45, 0x3e }, { 0x00, 0x42, 0x7f, 0x40, 0x00 }, { 0x42, 0x61, 0x51, 0x49, 0x46 },
  { 0x21, 0x41, 0x45, 0x4b, 0x31 }, { 0x18, 0x14, 0x12, 0x7f, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
  { 0x3c, 0x4a, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 }, { 0x36, 0x49, 0x49, 0x49, 0x36 },
  { 0x06, 0x49, 0x49, 0x29, 0x1e }
};

STATIC
UINT32
CanvasSqrt (
  IN UINT32 N
  )
{
  UINT32 Root = 0;
  UINT32 Bit  = 1U << 30;

  while (Bit > N) {
    Bit >>= 2;
  }
  while (Bit != 0U) {
    if (N >= Root + Bit) {
      N    -= Root + Bit;
      Root  = (Root >> 1) + Bit;
    } else {
      Root >>= 1;
    }
    Bit >>= 2;
  }
  return Root;
}

BOOLEAN
Np05jRectContains (
  IN NP05J_RECT R,
  IN INT32      X,
  IN INT32      Y
  )
{
  return (X >= R.X) && (X < (R.X + R.W)) && (Y >= R.Y) && (Y < (R.Y + R.H));
}

STATIC
VOID
PutPixel (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X,
  IN     INT32                          Y,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  if ((X >= 0) && (Y >= 0) && ((UINT32)X < Ctx->ScreenW) && ((UINT32)Y < Ctx->ScreenH)) {
    Ctx->BackBuf[(UINTN)Y * Ctx->ScreenW + (UINTN)X] = Color;
  }
}

VOID
Np05jUiClear (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  UINTN Total = (UINTN)Ctx->ScreenW * Ctx->ScreenH;
  for (UINTN I = 0; I < Total; I++) {
    Ctx->BackBuf[I] = Color;
  }
}

VOID
Np05jUiFillRect (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  INT32 X0 = (Rect.X < 0) ? 0 : Rect.X;
  INT32 Y0 = (Rect.Y < 0) ? 0 : Rect.Y;
  INT32 X1 = Rect.X + Rect.W;
  INT32 Y1 = Rect.Y + Rect.H;

  if (X1 > (INT32)Ctx->ScreenW) {
    X1 = (INT32)Ctx->ScreenW;
  }
  if (Y1 > (INT32)Ctx->ScreenH) {
    Y1 = (INT32)Ctx->ScreenH;
  }
  if ((X0 >= X1) || (Y0 >= Y1)) {
    return;
  }

  for (INT32 Y = Y0; Y < Y1; Y++) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Row = Ctx->BackBuf + (UINTN)Y * Ctx->ScreenW;
    for (INT32 X = X0; X < X1; X++) {
      Row[X] = Color;
    }
  }
}

VOID
Np05jUiDrawBorder (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     INT32                          Thickness,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  if (Thickness <= 0) {
    return;
  }
  Np05jUiFillRect (Ctx, (NP05J_RECT){ Rect.X, Rect.Y, Rect.W, Thickness }, Color);
  Np05jUiFillRect (Ctx, (NP05J_RECT){ Rect.X, Rect.Y + Rect.H - Thickness, Rect.W, Thickness }, Color);
  Np05jUiFillRect (Ctx, (NP05J_RECT){ Rect.X, Rect.Y, Thickness, Rect.H }, Color);
  Np05jUiFillRect (Ctx, (NP05J_RECT){ Rect.X + Rect.W - Thickness, Rect.Y, Thickness, Rect.H }, Color);
}

VOID
Np05jUiDrawCard (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Fill,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Border,
  IN     INT32                          BorderThickness
  )
{
  Np05jUiFillRect (Ctx, Rect, Fill);
  Np05jUiDrawBorder (Ctx, Rect, BorderThickness, Border);
}

STATIC
VOID
FillRoundedRect (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     INT32                          Radius,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  INT32 R = Radius;

  if ((Rect.W <= 0) || (Rect.H <= 0)) {
    return;
  }
  if (R <= 0) {
    Np05jUiFillRect (Ctx, Rect, Color);
    return;
  }
  if ((R * 2) > Rect.W) {
    R = Rect.W / 2;
  }
  if ((R * 2) > Rect.H) {
    R = Rect.H / 2;
  }

  for (INT32 Y = 0; Y < Rect.H; Y++) {
    INT32 Inset = 0;
    if (Y < R) {
      INT32  Dy = R - 1 - Y;
      UINT32 Dx = CanvasSqrt ((UINT32)(R * R - Dy * Dy));
      Inset = R - (INT32)Dx;
    } else if (Y >= (Rect.H - R)) {
      INT32  Dy = Y - (Rect.H - R);
      UINT32 Dx = CanvasSqrt ((UINT32)(R * R - Dy * Dy));
      Inset = R - (INT32)Dx;
    }
    if ((Rect.W - 2 * Inset) > 0) {
      Np05jUiFillRect (
        Ctx,
        (NP05J_RECT){ Rect.X + Inset, Rect.Y + Y, Rect.W - 2 * Inset, 1 },
        Color
        );
    }
  }
}

VOID
Np05jUiDrawRoundedCard (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     INT32                          Radius,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Fill,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Border,
  IN     INT32                          BorderThickness
  )
{
  if ((Rect.W <= 0) || (Rect.H <= 0)) {
    return;
  }
  if (BorderThickness <= 0) {
    FillRoundedRect (Ctx, Rect, Radius, Fill);
    return;
  }
  FillRoundedRect (Ctx, Rect, Radius, Border);
  {
    NP05J_RECT Inner = {
      Rect.X + BorderThickness,
      Rect.Y + BorderThickness,
      Rect.W - 2 * BorderThickness,
      Rect.H - 2 * BorderThickness
    };
    INT32 InnerR = (Radius > BorderThickness) ? (Radius - BorderThickness) : 0;
    FillRoundedRect (Ctx, Inner, InnerR, Fill);
  }
}

VOID
Np05jUiDrawCircle (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          Cx,
  IN     INT32                          Cy,
  IN     INT32                          Radius,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  INT32 R2 = Radius * Radius;
  for (INT32 Dy = -Radius; Dy <= Radius; Dy++) {
    for (INT32 Dx = -Radius; Dx <= Radius; Dx++) {
      if ((Dx * Dx + Dy * Dy) <= R2) {
        PutPixel (Ctx, Cx + Dx, Cy + Dy, Color);
      }
    }
  }
}

VOID
Np05jUiDrawLine (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X0,
  IN     INT32                          Y0,
  IN     INT32                          X1,
  IN     INT32                          Y1,
  IN     INT32                          Thickness,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  INT32 Dx  = (X1 >= X0) ? (X1 - X0) : (X0 - X1);
  INT32 Sx  = (X0 < X1) ? 1 : -1;
  INT32 Dy  = -((Y1 >= Y0) ? (Y1 - Y0) : (Y0 - Y1));
  INT32 Sy  = (Y0 < Y1) ? 1 : -1;
  INT32 Err = Dx + Dy;
  INT32 Rad = (Thickness > 1) ? (Thickness / 2) : 0;

  while (TRUE) {
    if (Rad > 0) {
      Np05jUiFillRect (Ctx, (NP05J_RECT){ X0 - Rad, Y0 - Rad, Thickness, Thickness }, Color);
    } else {
      PutPixel (Ctx, X0, Y0, Color);
    }
    if ((X0 == X1) && (Y0 == Y1)) {
      break;
    }
    {
      INT32 E2 = 2 * Err;
      if (E2 >= Dy) {
        Err += Dy;
        X0  += Sx;
      }
      if (E2 <= Dx) {
        Err += Dx;
        Y0  += Sy;
      }
    }
  }
}

STATIC
VOID
DrawChar (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X,
  IN     INT32                          Y,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CHAR8                          Ch
  )
{
  UINT8        Idx = ((Ch >= 0x20) && (Ch <= 0x7E)) ? (UINT8)(Ch - 0x20) : 0U;
  CONST UINT8 *G   = mFont8x8[Idx];

  for (UINT32 Row = 0; Row < 8U; Row++) {
    UINT8 Bits = G[Row];
    if (Bits == 0U) {
      continue;
    }
    for (UINT32 Col = 0; Col < 8U; Col++) {
      if ((Bits & (0x80U >> Col)) != 0U) {
        Np05jUiFillRect (
          Ctx,
          (NP05J_RECT){
            X + (INT32)(Col * Scale),
            Y + (INT32)(Row * Scale),
            (INT32)Scale,
            (INT32)Scale
          },
          Color
          );
      }
    }
  }
}

VOID
Np05jUiDrawText (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X,
  IN     INT32                          Y,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Text
  )
{
  INT32 CurX = X;

  if ((Text == NULL) || (Scale == 0U)) {
    return;
  }

  while (*Text != '\0') {
    if (*Text == '\n') {
      CurX = X;
      Y   += (INT32)(10U * Scale);
    } else {
      DrawChar (Ctx, CurX, Y, Scale, Color, *Text);
      CurX += (INT32)(8U * Scale);
    }
    Text++;
  }
}

VOID
Np05jUiDrawTextCentered (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Text
  )
{
  UINTN Len;
  INT32 TextW;
  INT32 TextH;

  if (Text == NULL) {
    return;
  }

  Len   = AsciiStrLen (Text);
  TextW = (INT32)(Len * 8U * Scale);
  TextH = (INT32)(8U * Scale);

  Np05jUiDrawText (
    Ctx,
    Rect.X + (Rect.W - TextW) / 2,
    Rect.Y + (Rect.H - TextH) / 2,
    Scale,
    Color,
    Text
    );
}

STATIC
CONST UINT8 *
LookupSunGlyph5x7 (
  IN CHAR8 Ch
  )
{
  STATIC CONST UINT8 Space[5]  = { 0x00, 0x00, 0x00, 0x00, 0x00 };
  STATIC CONST UINT8 Plus[5]   = { 0x08, 0x08, 0x3e, 0x08, 0x08 };
  STATIC CONST UINT8 Minus[5]  = { 0x08, 0x08, 0x08, 0x08, 0x08 };
  STATIC CONST UINT8 Slash[5]  = { 0x20, 0x10, 0x08, 0x04, 0x02 };
  STATIC CONST UINT8 Colon[5]  = { 0x00, 0x36, 0x36, 0x00, 0x00 };
  STATIC CONST UINT8 Dot[5]    = { 0x00, 0x60, 0x60, 0x00, 0x00 };
  STATIC CONST UINT8 Comma[5]  = { 0x00, 0x50, 0x30, 0x00, 0x00 };
  STATIC CONST UINT8 LParen[5] = { 0x00, 0x1c, 0x22, 0x41, 0x00 };
  STATIC CONST UINT8 RParen[5] = { 0x00, 0x41, 0x22, 0x1c, 0x00 };
  STATIC CONST UINT8 Pipe[5]   = { 0x00, 0x00, 0x7f, 0x00, 0x00 };
  STATIC CONST UINT8 Star[5]   = { 0x14, 0x08, 0x3e, 0x08, 0x14 };
  STATIC CONST UINT8 Eq[5]     = { 0x14, 0x14, 0x14, 0x14, 0x14 };
  STATIC CONST UINT8 Under[5]  = { 0x40, 0x40, 0x40, 0x40, 0x40 };
  STATIC CONST UINT8 Bang[5]   = { 0x00, 0x00, 0x5f, 0x00, 0x00 };

  if ((Ch >= 'a') && (Ch <= 'z')) {
    Ch = (CHAR8)(Ch - 'a' + 'A');
  }
  if ((Ch >= 'A') && (Ch <= 'Z')) {
    return mSunLetters[Ch - 'A'];
  }
  if ((Ch >= '0') && (Ch <= '9')) {
    return mSunDigits[Ch - '0'];
  }
  switch (Ch) {
    case '+': return Plus;
    case '-': return Minus;
    case '/': return Slash;
    case ':': return Colon;
    case '.': return Dot;
    case ',': return Comma;
    case '(': return LParen;
    case ')': return RParen;
    case '|': return Pipe;
    case '*': return Star;
    case '=': return Eq;
    case '_': return Under;
    case '!': return Bang;
    default:  return Space;
  }
}

VOID
Np05jUiDrawText5x7 (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X,
  IN     INT32                          Y,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Text
  )
{
  UINTN Len;

  if ((Text == NULL) || (Scale == 0U)) {
    return;
  }

  Len = AsciiStrLen (Text);
  for (UINTN I = 0; I < Len; I++) {
    CONST UINT8 *G = LookupSunGlyph5x7 (Text[I]);
    for (UINT32 Col = 0; Col < 5U; Col++) {
      UINT32 Row = 0;
      while (Row < 7U) {
        UINT32 First;
        if ((G[Col] & (1U << Row)) == 0U) {
          Row++;
          continue;
        }
        First = Row;
        while ((Row < 7U) && ((G[Col] & (1U << Row)) != 0U)) {
          Row++;
        }
        Np05jUiFillRect (
          Ctx,
          (NP05J_RECT){
            X + (INT32)((I * 6U + Col) * Scale),
            Y + (INT32)(First * Scale),
            (INT32)Scale,
            (INT32)((Row - First) * Scale)
          },
          Color
          );
      }
    }
  }
}

VOID
Np05jUiDrawText5x7Centered (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Text
  )
{
  UINTN Len;
  INT32 TextW;
  INT32 TextH;

  if ((Text == NULL) || (Scale == 0U)) {
    return;
  }

  Len   = AsciiStrLen (Text);
  TextW = (Len > 0U) ? (INT32)((Len * 6U - 1U) * Scale) : 0;
  TextH = (INT32)(7U * Scale);

  Np05jUiDrawText5x7 (
    Ctx,
    Rect.X + (Rect.W - TextW) / 2,
    Rect.Y + (Rect.H - TextH) / 2,
    Scale,
    Color,
    Text
    );
}

STATIC
VOID
FillPolygon4 (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     CONST NP05J_UI_POINT           V[4],
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  INT32 Top    = V[0].Y;
  INT32 Bottom = Top;

  for (UINTN I = 1; I < 4U; I++) {
    if (V[I].Y < Top) {
      Top = V[I].Y;
    }
    if (V[I].Y > Bottom) {
      Bottom = V[I].Y;
    }
  }

  for (INT32 Y = Top; Y < Bottom; Y++) {
    INT32 Left  = MAX_INT32;
    INT32 Right = MIN_INT32;

    for (UINTN I = 0; I < 4U; I++) {
      NP05J_UI_POINT A = V[I];
      NP05J_UI_POINT B = V[(I + 1U) & 3U];
      INT32          X;

      if (A.Y > B.Y) {
        NP05J_UI_POINT T = A;
        A = B;
        B = T;
      }
      if ((A.Y == B.Y) || (Y < A.Y) || (Y >= B.Y)) {
        continue;
      }
      X = A.X + (INT32)(((INT64)(B.X - A.X) * (Y - A.Y)) / (B.Y - A.Y));
      if (X < Left) {
        Left = X;
      }
      if (X > Right) {
        Right = X;
      }
    }

    if (Left <= Right) {
      Np05jUiFillRect (Ctx, (NP05J_RECT){ Left, Y, Right - Left + 1, 1 }, Color);
    }
  }
}

STATIC
VOID
DrawVectorStroke (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_UI_POINT                 A,
  IN     NP05J_UI_POINT                 B,
  IN     UINT32                         Radius,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  INT32  Dx = B.X - A.X;
  INT32  Dy = B.Y - A.Y;
  UINT32 L  = CanvasSqrt ((UINT32)(Dx * Dx + Dy * Dy));

  if (L == 0U) {
    Np05jUiDrawCircle (Ctx, A.X, A.Y, (INT32)Radius, Color);
    return;
  }

  {
    INT32          Nx   = (INT32)(((INT64)Dy * Radius) / L);
    INT32          Ny   = (INT32)(-((INT64)Dx * Radius) / L);
    NP05J_UI_POINT V[4] = {
      { A.X + Nx, A.Y + Ny },
      { B.X + Nx, B.Y + Ny },
      { B.X - Nx, B.Y - Ny },
      { A.X - Nx, A.Y - Ny }
    };
    FillPolygon4 (Ctx, V, Color);
    Np05jUiDrawCircle (Ctx, A.X, A.Y, (INT32)Radius, Color);
    Np05jUiDrawCircle (Ctx, B.X, B.Y, (INT32)Radius, Color);
  }
}

STATIC
UINT32
SunBrandWidth (
  IN UINT32 Height
  )
{
  STATIC CONST CHAR8 mBrand[9] = "tianocore";
  UINT32             Width     = Height * 2U / 3U;
  UINT32             Total     = (Height / 25U) * 8U;

  for (UINTN I = 0; I < 9U; I++) {
    UINT32 Factor = (mBrand[I] == 'i') ? 38U :
                    ((mBrand[I] == 't') ? 76U :
                    ((mBrand[I] == 'r') ? 82U : 100U));
    Total += Width * Factor / 100U;
  }
  return Total;
}

STATIC
VOID
DrawSunWordmark (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X,
  IN     INT32                          Y,
  IN     UINT32                         Height,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  STATIC CONST CHAR8 mBrand[9] = "tianocore";
  UINT32             BaseWidth = Height * 2U / 3U;
  UINT32             Gap       = Height / 25U;
  UINT32             Radius    = Height / 32U;

  if (Radius < 1U) {
    Radius = 1U;
  }

  for (UINTN I = 0; I < 9U; I++) {
    NP05J_UI_POINT Path[40];
    UINT32         Count  = 0;
    CHAR8          Ch     = mBrand[I];
    UINT32         Factor = (Ch == 'i') ? 38U : ((Ch == 't') ? 76U : ((Ch == 'r') ? 82U : 100U));
    UINT32         Width  = BaseWidth * Factor / 100U;

    if ((Ch == 'o') || (Ch == 'a') || (Ch == 'c') || (Ch == 'e')) {
      UINT32 First = (Ch == 'c') ? 4U : ((Ch == 'e') ? 2U : 0U);
      UINT32 Last  = (Ch == 'c') ? 28U : ((Ch == 'e') ? 30U : 32U);
      for (UINT32 J = First; J <= Last; J++) {
        Path[Count++] = (NP05J_UI_POINT){
          500 + mSunCircle[J % 32U].X * 390 / 1000,
          750 + mSunCircle[J % 32U].Y * 450 / 1000
        };
      }
    } else if (Ch == 'n') {
      Path[Count++] = (NP05J_UI_POINT){ 110, 1200 };
      Path[Count++] = (NP05J_UI_POINT){ 110, 300 };
      Path[Count++] = (NP05J_UI_POINT){ 110, 700 };
      for (UINT32 J = 16; J <= 32; J++) {
        Path[Count++] = (NP05J_UI_POINT){
          500 + mSunCircle[J % 32U].X * 390 / 1000,
          700 + mSunCircle[J % 32U].Y * 400 / 1000
        };
      }
      Path[Count++] = (NP05J_UI_POINT){ 890, 1200 };
    } else if (Ch == 't') {
      Path[Count++] = (NP05J_UI_POINT){ 430, 0 };
      Path[Count++] = (NP05J_UI_POINT){ 430, 1050 };
      Path[Count++] = (NP05J_UI_POINT){ 500, 1150 };
      Path[Count++] = (NP05J_UI_POINT){ 640, 1200 };
      Path[Count++] = (NP05J_UI_POINT){ 840, 1200 };
    } else if (Ch == 'i') {
      Path[Count++] = (NP05J_UI_POINT){ 500, 350 };
      Path[Count++] = (NP05J_UI_POINT){ 500, 1200 };
    } else if (Ch == 'r') {
      Path[Count++] = (NP05J_UI_POINT){ 110, 1200 };
      Path[Count++] = (NP05J_UI_POINT){ 110, 300 };
      Path[Count++] = (NP05J_UI_POINT){ 110, 700 };
      Path[Count++] = (NP05J_UI_POINT){ 270, 430 };
      Path[Count++] = (NP05J_UI_POINT){ 520, 300 };
      Path[Count++] = (NP05J_UI_POINT){ 760, 320 };
    }

    for (UINTN J = 1; J < Count; J++) {
      NP05J_UI_POINT A = Path[J - 1];
      NP05J_UI_POINT B = Path[J];
      A.X = X + A.X * (INT32)Width / 1000;
      B.X = X + B.X * (INT32)Width / 1000;
      A.Y = Y + A.Y * (INT32)Height / 1200;
      B.Y = Y + B.Y * (INT32)Height / 1200;
      DrawVectorStroke (Ctx, A, B, Radius, Color);
    }

    if ((Ch == 't') || (Ch == 'a') || (Ch == 'e')) {
      NP05J_UI_POINT A = (Ch == 't') ? (NP05J_UI_POINT){ 100, 350 } :
                         (Ch == 'a') ? (NP05J_UI_POINT){ 890, 300 } :
                                       (NP05J_UI_POINT){ 200, 1050 };
      NP05J_UI_POINT B = (Ch == 't') ? (NP05J_UI_POINT){ 850, 350 } :
                         (Ch == 'a') ? (NP05J_UI_POINT){ 890, 1200 } :
                                       (NP05J_UI_POINT){ 855, 500 };
      A.X = X + A.X * (INT32)Width / 1000;
      B.X = X + B.X * (INT32)Width / 1000;
      A.Y = Y + A.Y * (INT32)Height / 1200;
      B.Y = Y + B.Y * (INT32)Height / 1200;
      DrawVectorStroke (Ctx, A, B, Radius, Color);
    }

    if (Ch == 'i') {
      Np05jUiDrawCircle (Ctx, X + (INT32)(Width / 2U), Y + (INT32)(Height / 12U), (INT32)(Radius * 2U), Color);
    }

    X += (INT32)(Width + Gap);
  }
}

VOID
Np05jUiDrawSunMark (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     INT32         Cx,
  IN     INT32         Cy,
  IN     INT32         Radius
  )
{
  INT32 LogoSize = Radius * 2;
  INT32 Inner    = (LogoSize * 20) / 1000;
  INT32 Outer    = (LogoSize * 48) / 1000;

  if (Inner < 1) {
    Inner = 1;
  }
  if (Outer < 2) {
    Outer = 2;
  }

  for (UINTN I = 0; I < 6U; I++) {
    NP05J_UI_POINT D      = mSunSpokes[I];
    NP05J_UI_POINT N      = { -D.Y, D.X };
    NP05J_UI_POINT Center = { Cx, Cy };
    NP05J_UI_POINT End    = {
      Center.X + (D.X * LogoSize * 43) / 100000,
      Center.Y + (D.Y * LogoSize * 43) / 100000
    };
    NP05J_UI_POINT P[4] = {
      { Center.X + (N.X * Inner) / 1000, Center.Y + (N.Y * Inner) / 1000 },
      { End.X + (N.X * Outer) / 1000,    End.Y + (N.Y * Outer) / 1000 },
      { End.X - (N.X * Outer) / 1000,    End.Y - (N.Y * Outer) / 1000 },
      { Center.X - (N.X * Inner) / 1000, Center.Y - (N.Y * Inner) / 1000 }
    };
    NP05J_UI_POINT Face[4] = { Center, End, P[2], P[3] };

    Np05jUiDrawCircle (Ctx, End.X, End.Y, Outer, COL_GOLD);
    FillPolygon4 (Ctx, P, COL_GOLD);
    FillPolygon4 (Ctx, Face, COL_GOLD_SHADE);
  }
}

VOID
Np05jUiDrawSunBrand (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     INT32         CenterY,
  IN     UINT32        LogoSize,
  IN     UINT32        WordmarkScale,
  IN     CONST CHAR8  *Subtitle
  )
{
  UINT32                        Short       = (Ctx->ScreenW < Ctx->ScreenH) ? Ctx->ScreenW : Ctx->ScreenH;
  UINT32                        TitleHeight = WordmarkScale * 7U;
  UINT32                        Gap         = Short / 22U;
  UINT32                        GroupWidth  = LogoSize + Gap + SunBrandWidth (TitleHeight);
  INT32                         LogoX       = ((INT32)Ctx->ScreenW - (INT32)GroupWidth) / 2;
  INT32                         WordmarkX   = LogoX + (INT32)LogoSize + (INT32)Gap;
  INT32                         WordmarkY   = CenterY - (INT32)(TitleHeight / 2U);
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL WordC       = Np05jRgb (64, 68, 74);
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Muted       = Np05jRgb (112, 116, 122);

  Np05jUiDrawSunMark (Ctx, LogoX + (INT32)(LogoSize / 2U), CenterY, (INT32)(LogoSize / 2U));
  DrawSunWordmark (Ctx, WordmarkX, WordmarkY, TitleHeight, WordC);
  if (Subtitle != NULL) {
    Np05jUiDrawText5x7 (Ctx, WordmarkX, WordmarkY + (INT32)TitleHeight + 14, 3U, Muted, Subtitle);
  }
}

VOID
Np05jQueryPowerStatus (
  OUT BOOLEAN *BatteryPresent,
  OUT BOOLEAN *ChargerPresent,
  OUT UINT32  *VoltageMv
  )
{
  STATIC BOOLEAN mCachedValid  = FALSE;
  STATIC BOOLEAN mCachedBat    = FALSE;
  STATIC BOOLEAN mCachedChg    = FALSE;
  STATIC UINT32  mCachedMv     = 0;
  STATIC UINT32  mLastQuerySec = 0;
  EFI_TIME       Now;
  UINT32         NowSec  = 0;
  UINT32         Elapsed = 5U;

  ZeroMem (&Now, sizeof (Now));
  if ((gRT != NULL) && !EFI_ERROR (gRT->GetTime (&Now, NULL))) {
    NowSec = (UINT32)Now.Hour * 3600U + (UINT32)Now.Minute * 60U + (UINT32)Now.Second;
    if (mCachedValid) {
      Elapsed = (NowSec >= mLastQuerySec) ? (NowSec - mLastQuerySec) : (86400U - mLastQuerySec + NowSec);
    }
  }

  if (!mCachedValid || (Elapsed >= 5U)) {
    EFI_CHARGER_EX_PROTOCOL *Charger = NULL;
    BOOLEAN                  Bat     = FALSE;
    BOOLEAN                  Chg     = FALSE;
    UINT32                   Mv      = 0;

    if ((gBS->LocateProtocol (&gChargerExProtocolGuid, NULL, (VOID **)&Charger) == EFI_SUCCESS) &&
        (Charger != NULL))
    {
      if (Charger->GetBatteryPresence != NULL) {
        Charger->GetBatteryPresence (&Bat);
      }
      if (Charger->GetChargerPresence != NULL) {
        Charger->GetChargerPresence (&Chg);
      }
      if (Charger->GetBatteryVoltage != NULL) {
        Charger->GetBatteryVoltage (&Mv);
      }
    }

    mCachedBat    = Bat;
    mCachedChg    = Chg;
    mCachedMv     = Mv;
    mLastQuerySec = NowSec;
    mCachedValid  = TRUE;
  }

  if (BatteryPresent != NULL) {
    *BatteryPresent = mCachedBat;
  }
  if (ChargerPresent != NULL) {
    *ChargerPresent = mCachedChg;
  }
  if (VoltageMv != NULL) {
    *VoltageMv = mCachedMv;
  }
}

VOID
Np05jUiDrawHeader (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     CONST CHAR8  *Title,
  IN     CONST CHAR8  *Subtitle
  )
{
  EFI_TIME Now;
  BOOLEAN  Bat = FALSE;
  BOOLEAN  Chg = FALSE;
  UINT32   Mv  = 0;
  CHAR8    RightBuf[96];

  Np05jUiFillRect (Ctx, (NP05J_RECT){ 0, 0, (INT32)Ctx->ScreenW, 114 }, COL_PANEL);
  Np05jUiFillRect (Ctx, (NP05J_RECT){ 0, 114, (INT32)Ctx->ScreenW, 2 }, COL_BORDER);

  Np05jUiDrawSunMark (Ctx, 66, 57, 32);
  Np05jUiDrawText5x7 (Ctx, 118, 28, 4U, COL_TEXT, Title);
  if (Subtitle != NULL) {
    Np05jUiDrawText5x7 (Ctx, 118, 72, 2U, COL_MUTED, Subtitle);
  }

  ZeroMem (&Now, sizeof (Now));
  if (gRT != NULL) {
    gRT->GetTime (&Now, NULL);
  }
  Np05jQueryPowerStatus (&Bat, &Chg, &Mv);

  AsciiSPrint (
    RightBuf,
    sizeof (RightBuf),
    "SM8750P 24GB | %04u-%02u-%02u %02u:%02u | %umV%a | TOUCH:%u",
    (UINT32)Now.Year,
    (UINT32)Now.Month,
    (UINT32)Now.Day,
    (UINT32)Now.Hour,
    (UINT32)Now.Minute,
    Mv,
    Chg ? "+" : "",
    (UINT32)Ctx->AbsCount
    );
  {
    INT32 Tw = (INT32)((AsciiStrLen (RightBuf) * 6U - 1U) * 2U);
    Np05jUiDrawText5x7 (Ctx, (INT32)Ctx->ScreenW - Tw - 42, 48, 2U, COL_SUBTEXT, RightBuf);
  }
}

VOID
Np05jUiSetBanner (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Format,
  ...
  )
{
  VA_LIST Marker;

  VA_START (Marker, Format);
  AsciiVSPrint (Ctx->StatusBanner, sizeof (Ctx->StatusBanner), Format, Marker);
  VA_END (Marker);
  Ctx->StatusValid = TRUE;
  Ctx->StatusColor = Color;
}

VOID
Np05jUiDrawBannerIfAny (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  if (!Ctx->StatusValid || (Ctx->StatusBanner[0] == '\0')) {
    return;
  }
  Np05jUiDrawRoundedCard (
    Ctx,
    (NP05J_RECT){ 42, (INT32)Ctx->ScreenH - 128, (INT32)Ctx->ScreenW - 84, 54 },
    14,
    COL_PANEL,
    Ctx->StatusColor,
    3
    );
  Np05jUiDrawText5x7Centered (
    Ctx,
    (NP05J_RECT){ 42, (INT32)Ctx->ScreenH - 128, (INT32)Ctx->ScreenW - 84, 54 },
    2U,
    Ctx->StatusColor,
    Ctx->StatusBanner
    );
}

STATIC
VOID
RestoreMouseBackground (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  if ((mPrevCursorX >= 0) && (mPrevCursorY >= 0)) {
    UINTN W = 20;
    UINTN H = 20;
    if ((UINT32)mPrevCursorX + W > Ctx->ScreenW) {
      W = Ctx->ScreenW - (UINT32)mPrevCursorX;
    }
    if ((UINT32)mPrevCursorY + H > Ctx->ScreenH) {
      H = Ctx->ScreenH - (UINT32)mPrevCursorY;
    }
    if ((W > 0) && (H > 0)) {
      Ctx->Gop->Blt (
                  Ctx->Gop,
                  Ctx->BackBuf,
                  EfiBltBufferToVideo,
                  (UINTN)mPrevCursorX,
                  (UINTN)mPrevCursorY,
                  (UINTN)mPrevCursorX,
                  (UINTN)mPrevCursorY,
                  W,
                  H,
                  Ctx->ScreenW * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                  );
    }
  }
}

STATIC
VOID
DrawCursorOverlay (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL White   = COL_WHITE;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Outline = COL_TEXT;

  if (!Ctx->MouseVisible) {
    mPrevCursorX = -1;
    mPrevCursorY = -1;
    return;
  }

  RestoreMouseBackground (Ctx);

  for (INT32 Y = 0; Y < 18; Y++) {
    INT32 Width = (Y < 12) ? (Y + 1) : (18 - Y);
    if (Width < 1) {
      Width = 1;
    }
    if ((Ctx->CursorX >= 0) && ((UINT32)Ctx->CursorX < Ctx->ScreenW) &&
        (Ctx->CursorY + Y >= 0) && ((UINT32)(Ctx->CursorY + Y) < Ctx->ScreenH))
    {
      INT32 W = Width;
      if ((UINT32)(Ctx->CursorX + W) > Ctx->ScreenW) {
        W = (INT32)Ctx->ScreenW - Ctx->CursorX;
      }
      if (W > 0) {
        Ctx->Gop->Blt (Ctx->Gop, &Outline, EfiBltVideoFill, 0, 0, (UINTN)Ctx->CursorX, (UINTN)(Ctx->CursorY + Y), (UINTN)W, 1, 0);
        if (W > 3) {
          Ctx->Gop->Blt (Ctx->Gop, &White, EfiBltVideoFill, 0, 0, (UINTN)(Ctx->CursorX + 1), (UINTN)(Ctx->CursorY + Y), (UINTN)(W - 2), 1, 0);
        }
      }
    }
  }

  mPrevCursorX = Ctx->CursorX;
  mPrevCursorY = Ctx->CursorY;
}

VOID
Np05jUiPresent (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  mPrevCursorX = -1;
  mPrevCursorY = -1;
  Ctx->Gop->Blt (
              Ctx->Gop,
              Ctx->BackBuf,
              EfiBltBufferToVideo,
              0,
              0,
              0,
              0,
              Ctx->ScreenW,
              Ctx->ScreenH,
              Ctx->ScreenW * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
              );
  DrawCursorOverlay (Ctx);
  Ctx->Dirty = FALSE;
}

VOID
Np05jUiRefreshHandles (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  EFI_HANDLE *Handles = NULL;
  UINTN       Count   = 0;
  EFI_STATUS  Status;

  Ctx->AbsCount = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiAbsolutePointerProtocolGuid, NULL, &Count, &Handles);
  if (!EFI_ERROR (Status) && (Handles != NULL)) {
    for (UINTN I = 0; (I < Count) && (Ctx->AbsCount < NP05J_MAX_ABS_HANDLES); I++) {
      EFI_ABSOLUTE_POINTER_PROTOCOL *Ptr = NULL;
      if (Handles[I] == gST->ConsoleInHandle) {
        continue;
      }
      if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiAbsolutePointerProtocolGuid, (VOID **)&Ptr)) && (Ptr != NULL)) {
        Ctx->AbsPtrs[Ctx->AbsCount++] = Ptr;
      }
    }
    if (Ctx->AbsCount == 0U) {
      for (UINTN I = 0; (I < Count) && (Ctx->AbsCount < NP05J_MAX_ABS_HANDLES); I++) {
        EFI_ABSOLUTE_POINTER_PROTOCOL *Ptr = NULL;
        if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiAbsolutePointerProtocolGuid, (VOID **)&Ptr)) && (Ptr != NULL)) {
          Ctx->AbsPtrs[Ctx->AbsCount++] = Ptr;
        }
      }
    }
    FreePool (Handles);
  }

  Handles         = NULL;
  Count           = 0;
  Ctx->MouseCount = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimplePointerProtocolGuid, NULL, &Count, &Handles);
  if (!EFI_ERROR (Status) && (Handles != NULL)) {
    for (UINTN I = 0; (I < Count) && (Ctx->MouseCount < NP05J_MAX_MOUSE_HANDLES); I++) {
      EFI_SIMPLE_POINTER_PROTOCOL *Ptr = NULL;
      if (Handles[I] == gST->ConsoleInHandle) {
        continue;
      }
      if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimplePointerProtocolGuid, (VOID **)&Ptr)) && (Ptr != NULL)) {
        Ctx->MousePtrs[Ctx->MouseCount++] = Ptr;
      }
    }
    if (Ctx->MouseCount == 0U) {
      for (UINTN I = 0; (I < Count) && (Ctx->MouseCount < NP05J_MAX_MOUSE_HANDLES); I++) {
        EFI_SIMPLE_POINTER_PROTOCOL *Ptr = NULL;
        if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimplePointerProtocolGuid, (VOID **)&Ptr)) && (Ptr != NULL)) {
          Ctx->MousePtrs[Ctx->MouseCount++] = Ptr;
        }
      }
    }
    FreePool (Handles);
  }
}

VOID
Np05jUiDrainInput (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  EFI_INPUT_KEY Key;

  for (UINTN I = 0; I < Ctx->AbsCount; I++) {
    EFI_ABSOLUTE_POINTER_STATE S;
    while (!EFI_ERROR (Ctx->AbsPtrs[I]->GetState (Ctx->AbsPtrs[I], &S))) {
    }
    Ctx->AbsWasDown[I] = FALSE;
  }

  for (UINTN I = 0; I < Ctx->MouseCount; I++) {
    EFI_SIMPLE_POINTER_STATE S;
    while (!EFI_ERROR (Ctx->MousePtrs[I]->GetState (Ctx->MousePtrs[I], &S))) {
    }
  }
  Ctx->MouseWasLeftDown = FALSE;
  Ctx->AnyTouchDown     = FALSE;

  if (gST->ConIn != NULL) {
    while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    }
  }
}

EFI_STATUS
Np05jUiInit (
  OUT NP05J_UI_CTX *Ctx
  )
{
  EFI_STATUS Status;

  ZeroMem (Ctx, sizeof (*Ctx));

  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Ctx->Gop);
  if (EFI_ERROR (Status) || (Ctx->Gop == NULL)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Ctx->Gop);
  }
  if (EFI_ERROR (Status) || (Ctx->Gop == NULL) || (Ctx->Gop->Mode == NULL) || (Ctx->Gop->Mode->Info == NULL)) {
    return EFI_NOT_FOUND;
  }

  Ctx->ScreenW = Ctx->Gop->Mode->Info->HorizontalResolution;
  Ctx->ScreenH = Ctx->Gop->Mode->Info->VerticalResolution;
  Ctx->BackBuf = AllocateZeroPool ((UINTN)Ctx->ScreenW * Ctx->ScreenH * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  if (Ctx->BackBuf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Ctx->CursorX = (INT32)(Ctx->ScreenW / 2U);
  Ctx->CursorY = (INT32)(Ctx->ScreenH / 2U);

  gBS->LocateProtocol (&gNp05jFastbootProtocolGuid, NULL, (VOID **)&Ctx->Fastboot);

  Np05jBiosConfigLoad (&Ctx->Config);
  Np05jUiRefreshHandles (Ctx);
  Np05jUiDrainInput (Ctx);

  if (gST->ConOut != NULL) {
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }

  return EFI_SUCCESS;
}

VOID
Np05jUiFree (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  if (Ctx->BackBuf != NULL) {
    FreePool (Ctx->BackBuf);
    Ctx->BackBuf = NULL;
  }
}

STATIC
INT32
MapAbsCoord (
  IN UINT64 Val,
  IN UINT64 Min,
  IN UINT64 Max,
  IN UINT32 DisplaySize
  )
{
  UINT64 Span;

  if ((Max <= Min) || (DisplaySize == 0U)) {
    return 0;
  }
  if (Val <= Min) {
    return 0;
  }
  if (Val >= Max) {
    return (INT32)(DisplaySize - 1U);
  }
  Span = Max - Min;
  return (INT32)(((Val - Min) * (UINT64)(DisplaySize - 1U)) / Span);
}

EFI_STATUS
Np05jUiPollInput (
  IN OUT NP05J_UI_CTX      *Ctx,
  IN     UINTN              TimeoutMs,
  OUT    NP05J_INPUT_EVENT *Event
  )
{
  UINTN ElapsedMs = 0;

  ZeroMem (Event, sizeof (*Event));

  while (TRUE) {
    /* 0. Pump SunUEFI resident Fastboot service at application TPL */
    if (Ctx->Fastboot != NULL) {
      NP05J_FASTBOOT_ACTION FbAct = Np05jFbActionNone;
      if (!EFI_ERROR (Ctx->Fastboot->ServiceStep (Ctx->Fastboot, &FbAct)) &&
          (FbAct != Np05jFbActionNone))
      {
        Event->Kind     = Np05jInputFastbootAction;
        Event->FbAction = FbAct;
        return EFI_SUCCESS;
      }
    }

    /* 1. Poll touchscreen & absolute pointer handles */
    for (UINTN I = 0; I < Ctx->AbsCount; I++) {
      EFI_ABSOLUTE_POINTER_PROTOCOL *Ptr = Ctx->AbsPtrs[I];
      EFI_ABSOLUTE_POINTER_STATE     St;
      EFI_ABSOLUTE_POINTER_STATE     Latest;
      BOOLEAN                        GotAny = FALSE;

      while (!EFI_ERROR (Ptr->GetState (Ptr, &St))) {
        BOOLEAN SampleDown = ((St.ActiveButtons & EFI_ABSP_TouchActive) != 0U);
        Latest = St;
        GotAny = TRUE;
        /* Never coalesce across press/release edges or fast taps get swallowed */
        if (SampleDown != Ctx->AbsWasDown[I]) {
          break;
        }
      }

      if (GotAny) {
        INT32   X    = MapAbsCoord (Latest.CurrentX, Ptr->Mode->AbsoluteMinX, Ptr->Mode->AbsoluteMaxX, Ctx->ScreenW);
        INT32   Y    = MapAbsCoord (Latest.CurrentY, Ptr->Mode->AbsoluteMinY, Ptr->Mode->AbsoluteMaxY, Ctx->ScreenH);
        BOOLEAN Down = ((Latest.ActiveButtons & EFI_ABSP_TouchActive) != 0U);

        Ctx->AbsCurX[I] = X;
        Ctx->AbsCurY[I] = Y;

        if (Down && !Ctx->AbsWasDown[I]) {
          Ctx->AbsWasDown[I] = TRUE;
          Ctx->AbsStartX[I]  = X;
          Ctx->AbsStartY[I]  = Y;
          Ctx->AnyTouchDown  = TRUE;
          Ctx->ActiveTouchX  = X;
          Ctx->ActiveTouchY  = Y;
          Event->Kind        = Np05jInputTouchMove;
          Event->X           = X;
          Event->Y           = Y;
          Event->PointerDown = TRUE;
          return EFI_SUCCESS;
        } else if (Down) {
          Ctx->AnyTouchDown  = TRUE;
          Ctx->ActiveTouchX  = X;
          Ctx->ActiveTouchY  = Y;
          Event->Kind        = Np05jInputTouchMove;
          Event->X           = X;
          Event->Y           = Y;
          Event->PointerDown = TRUE;
          return EFI_SUCCESS;
        } else if (!Down && Ctx->AbsWasDown[I]) {
          Ctx->AbsWasDown[I] = FALSE;
          Ctx->AnyTouchDown  = FALSE;
          Event->Kind        = Np05jInputTap;
          Event->X           = X;
          Event->Y           = Y;
          Event->PointerDown = FALSE;
          return EFI_SUCCESS;
        }
      }
    }

    /* 2. Poll USB mice / SimplePointer handles */
    for (UINTN I = 0; I < Ctx->MouseCount; I++) {
      EFI_SIMPLE_POINTER_PROTOCOL *Ptr = Ctx->MousePtrs[I];
      EFI_SIMPLE_POINTER_STATE     St;

      if (!EFI_ERROR (Ptr->GetState (Ptr, &St))) {
        INT64 ResX = (Ptr->Mode->ResolutionX > 0) ? (INT64)Ptr->Mode->ResolutionX : 1LL;
        INT64 ResY = (Ptr->Mode->ResolutionY > 0) ? (INT64)Ptr->Mode->ResolutionY : 1LL;
        INT32 Dx   = (INT32)(((INT64)St.RelativeMovementX * 4LL) / ResX);
        INT32 Dy   = (INT32)(((INT64)St.RelativeMovementY * 4LL) / ResY);

        if ((Dx == 0) && (St.RelativeMovementX != 0)) {
          Dx = (St.RelativeMovementX > 0) ? 2 : -2;
        }
        if ((Dy == 0) && (St.RelativeMovementY != 0)) {
          Dy = (St.RelativeMovementY > 0) ? 2 : -2;
        }

        Ctx->MouseVisible = TRUE;
        Ctx->CursorX     += Dx;
        Ctx->CursorY     += Dy;
        if (Ctx->CursorX < 0) {
          Ctx->CursorX = 0;
        }
        if (Ctx->CursorX >= (INT32)Ctx->ScreenW) {
          Ctx->CursorX = (INT32)Ctx->ScreenW - 1;
        }
        if (Ctx->CursorY < 0) {
          Ctx->CursorY = 0;
        }
        if (Ctx->CursorY >= (INT32)Ctx->ScreenH) {
          Ctx->CursorY = (INT32)Ctx->ScreenH - 1;
        }

        if (St.RightButton) {
          Event->Kind = Np05jInputBack;
          return EFI_SUCCESS;
        }

        if (Ctx->MouseWasLeftDown && !St.LeftButton) {
          Ctx->MouseWasLeftDown = FALSE;
          Event->Kind           = Np05jInputTap;
          Event->X              = Ctx->CursorX;
          Event->Y              = Ctx->CursorY;
          return EFI_SUCCESS;
        }
        Ctx->MouseWasLeftDown = St.LeftButton;
        DrawCursorOverlay (Ctx);
      }
    }

    /* 3. Poll keypad / keyboard / serial ConIn */
    if (gST->ConIn != NULL) {
      EFI_INPUT_KEY Key;
      if (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
        Event->X = 0;
        Event->Y = 0;
        if ((Key.ScanCode == SCAN_UP) || (Key.UnicodeChar == L'k') || (Key.UnicodeChar == L'K')) {
          Event->Kind = Np05jInputUp;
          return EFI_SUCCESS;
        }
        if ((Key.ScanCode == SCAN_DOWN) || (Key.UnicodeChar == L'j') || (Key.UnicodeChar == L'J')) {
          Event->Kind = Np05jInputDown;
          return EFI_SUCCESS;
        }
        if ((Key.ScanCode == SCAN_LEFT) || (Key.UnicodeChar == L'h') || (Key.UnicodeChar == L'-')) {
          Event->Kind = Np05jInputLeft;
          return EFI_SUCCESS;
        }
        if ((Key.ScanCode == SCAN_RIGHT) || (Key.UnicodeChar == L'l') || (Key.UnicodeChar == L'+') || (Key.UnicodeChar == L'=')) {
          Event->Kind = Np05jInputRight;
          return EFI_SUCCESS;
        }
        if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key.UnicodeChar == CHAR_LINEFEED) || (Key.UnicodeChar == L' ')) {
          Event->Kind = Np05jInputEnter;
          return EFI_SUCCESS;
        }
        if ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == CHAR_BACKSPACE) || (Key.UnicodeChar == L'q') || (Key.UnicodeChar == L'Q')) {
          Event->Kind = Np05jInputBack;
          return EFI_SUCCESS;
        }
        if (Key.ScanCode == SCAN_F12) {
          Event->Kind = Np05jInputF12;
          return EFI_SUCCESS;
        }
      }
    }

    if ((TimeoutMs != MAX_UINTN) && (ElapsedMs >= TimeoutMs)) {
      return EFI_TIMEOUT;
    }

    gBS->Stall (10000); /* 10 ms slice */
    ElapsedMs += 10;
  }
}
