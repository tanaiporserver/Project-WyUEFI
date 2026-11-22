/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Direct 2400x1504 BGRA32 framebuffer renderer & hitboxes for NP05J BootSelect.
  Ported from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  uefi/handoff/bootselect/EarlySplash.c + ProductSplash.c / ProductSplashAssets.h.
  Note: Uses 2D static arrays and integer-only math for freestanding EL2 PIC safety.
**/

#include "BootSelect.h"

typedef struct {
  int32_t X;
  int32_t Y;
} SPLASH_POINT;

/* Native vector interpretation of the TianoCore six-spoke mark */
static const SPLASH_POINT mSpokes[6] = {
  { 0, -1000 }, { 866, -500 }, { 866, 500 },
  { 0,  1000 }, { -866, 500 }, { -866, -500 }
};

static const SPLASH_POINT mCircle[32] = {
  { 1000, 0 }, { 981, 195 }, { 924, 383 }, { 831, 556 },
  { 707, 707 }, { 556, 831 }, { 383, 924 }, { 195, 981 },
  { 0, 1000 }, { -195, 981 }, { -383, 924 }, { -556, 831 },
  { -707, 707 }, { -831, 556 }, { -924, 383 }, { -981, 195 },
  { -1000, 0 }, { -981, -195 }, { -924, -383 }, { -831, -556 },
  { -707, -707 }, { -556, -831 }, { -383, -924 }, { -195, -981 },
  { 0, -1000 }, { 195, -981 }, { 383, -924 }, { 556, -831 },
  { 707, -707 }, { 831, -556 }, { 924, -383 }, { 981, -195 }
};

/* Five-column vector glyphs; bit0 is top row (Project SunUEFI ProductSplashAssets.h) */
static const uint8_t mLetters[26][5] = {
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

static const uint8_t mDigits[10][5] = {
  { 0x3e, 0x51, 0x49, 0x45, 0x3e }, { 0x00, 0x42, 0x7f, 0x40, 0x00 }, { 0x42, 0x61, 0x51, 0x49, 0x46 },
  { 0x21, 0x41, 0x45, 0x4b, 0x31 }, { 0x18, 0x14, 0x12, 0x7f, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
  { 0x3c, 0x4a, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 }, { 0x36, 0x49, 0x49, 0x49, 0x36 },
  { 0x06, 0x49, 0x49, 0x29, 0x1e }
};

static uint32_t IntSqrt(uint32_t Value) {
  uint32_t Bit = 1U << 30;
  uint32_t Res = 0U;

  while (Bit > Value) {
    Bit >>= 2;
  }
  while (Bit != 0U) {
    if (Value >= Res + Bit) {
      Value -= Res + Bit;
      Res    = (Res >> 1) + Bit;
    } else {
      Res >>= 1;
    }
    Bit >>= 2;
  }
  return Res;
}

static void FillRect(
  uint32_t  *Fb,
  uint32_t  StridePx,
  uint32_t  W,
  uint32_t  H,
  uint32_t  X,
  uint32_t  Y,
  uint32_t  RectW,
  uint32_t  RectH,
  uint32_t  Color
  )
{
  uint32_t EndX;
  uint32_t EndY;
  uint32_t Row;
  uint32_t Col;

  if (Fb == 0 || RectW == 0U || RectH == 0U || X >= W || Y >= H) {
    return;
  }
  EndX = (RectW > W - X) ? W : (X + RectW);
  EndY = (RectH > H - Y) ? H : (Y + RectH);

  uint64_t ColorPair = ((uint64_t)Color << 32) | (uint64_t)Color;
  for (Row = Y; Row < EndY; Row++) {
    uint32_t *Line = Fb + (uintptr_t)Row * StridePx;
    Col = X;
    /* จัดแนว 8 ไบต์ให้ปลอดภัยกับ -mstrict-align แล้วเติมทีละ 2 พิกเซล (64-bit store) */
    if ((Col < EndX) && (((uintptr_t)(Line + Col) & 7U) != 0U)) {
      Line[Col++] = Color;
    }
    while ((Col + 2U) <= EndX) {
      *(uint64_t *)(void *)(Line + Col) = ColorPair;
      Col += 2U;
    }
    if (Col < EndX) {
      Line[Col] = Color;
    }
  }
}

static void DrawDisc(
  uint32_t  *Fb,
  uint32_t  StridePx,
  uint32_t  W,
  uint32_t  H,
  int32_t   Cx,
  int32_t   Cy,
  uint32_t  Radius,
  uint32_t  Color
  )
{
  int32_t Dy;

  if (Radius == 0U) {
    if (Cx >= 0 && Cy >= 0) {
      FillRect(Fb, StridePx, W, H, (uint32_t)Cx, (uint32_t)Cy, 1U, 1U, Color);
    }
    return;
  }

  for (Dy = -(int32_t)Radius; Dy <= (int32_t)Radius; Dy++) {
    int32_t  Py = Cy + Dy;
    int32_t  Dx = (int32_t)IntSqrt(Radius * Radius - (uint32_t)(Dy * Dy));
    int32_t  X0 = Cx - Dx;
    int32_t  X1 = Cx + Dx;

    if (Py < 0 || Py >= (int32_t)H || X1 < 0 || X0 >= (int32_t)W) {
      continue;
    }
    if (X0 < 0) {
      X0 = 0;
    }
    if (X1 >= (int32_t)W) {
      X1 = (int32_t)W - 1;
    }
    FillRect(Fb, StridePx, W, H, (uint32_t)X0, (uint32_t)Py, (uint32_t)(X1 - X0 + 1), 1U, Color);
  }
}

static void DrawPolygon4(
  uint32_t            *Fb,
  uint32_t            StridePx,
  uint32_t            W,
  uint32_t            H,
  const SPLASH_POINT  V[4],
  uint32_t            Color
  )
{
  int32_t Top    = V[0].Y;
  int32_t Bottom = Top;
  int32_t Y;
  uint32_t I;

  for (I = 1U; I < 4U; I++) {
    if (V[I].Y < Top) {
      Top = V[I].Y;
    }
    if (V[I].Y > Bottom) {
      Bottom = V[I].Y;
    }
  }

  for (Y = Top; Y < Bottom; Y++) {
    int32_t Left  = 0x7FFFFFFF;
    int32_t Right = -0x7FFFFFFF;

    if (Y < 0 || Y >= (int32_t)H) {
      continue;
    }

    for (I = 0U; I < 4U; I++) {
      SPLASH_POINT A = V[I];
      SPLASH_POINT B = V[(I + 1U) & 3U];
      int32_t      X;

      if (A.Y > B.Y) {
        SPLASH_POINT T = A;
        A = B;
        B = T;
      }
      if (A.Y == B.Y || Y < A.Y || Y >= B.Y) {
        continue;
      }
      X = A.X + (int32_t)(((int64_t)(B.X - A.X) * (Y - A.Y)) / (B.Y - A.Y));
      if (X < Left) {
        Left = X;
      }
      if (X > Right) {
        Right = X;
      }
    }

    if (Left <= Right && Right >= 0 && Left < (int32_t)W) {
      if (Left < 0) {
        Left = 0;
      }
      if (Right >= (int32_t)W) {
        Right = (int32_t)W - 1;
      }
      FillRect(Fb, StridePx, W, H, (uint32_t)Left, (uint32_t)Y, (uint32_t)(Right - Left + 1), 1U, Color);
    }
  }
}

static void DrawStroke(
  uint32_t      *Fb,
  uint32_t      StridePx,
  uint32_t      W,
  uint32_t      H,
  SPLASH_POINT  A,
  SPLASH_POINT  B,
  uint32_t      Radius,
  uint32_t      Color
  )
{
  int32_t  Dx = B.X - A.X;
  int32_t  Dy = B.Y - A.Y;
  uint32_t L  = IntSqrt((uint32_t)(Dx * Dx + Dy * Dy));

  if (L == 0U) {
    DrawDisc(Fb, StridePx, W, H, A.X, A.Y, Radius, Color);
    return;
  }

  {
    int32_t      Nx   = (int32_t)(((int64_t)Dy * (int64_t)Radius) / L);
    int32_t      Ny   = (int32_t)(-((int64_t)Dx * (int64_t)Radius) / L);
    SPLASH_POINT V[4] = {
      { A.X + Nx, A.Y + Ny },
      { B.X + Nx, B.Y + Ny },
      { B.X - Nx, B.Y - Ny },
      { A.X - Nx, A.Y - Ny }
    };
    DrawPolygon4(Fb, StridePx, W, H, V, Color);
    DrawDisc(Fb, StridePx, W, H, A.X, A.Y, Radius, Color);
    DrawDisc(Fb, StridePx, W, H, B.X, B.Y, Radius, Color);
  }
}

static uint32_t BrandWidth(uint32_t Height) {
  static const char mBrand[9] = { 't', 'i', 'a', 'n', 'o', 'c', 'o', 'r', 'e' };
  uint32_t Width = (Height * 2U) / 3U;
  uint32_t Total = (Height / 25U) * 8U;
  uint32_t I;

  for (I = 0U; I < 9U; I++) {
    uint32_t Factor = (mBrand[I] == 'i') ? 38U :
                      ((mBrand[I] == 't') ? 76U :
                      ((mBrand[I] == 'r') ? 82U : 100U));
    Total += (Width * Factor) / 100U;
  }
  return Total;
}

static void DrawBrandWordmark(
  uint32_t  *Fb,
  uint32_t  StridePx,
  uint32_t  W,
  uint32_t  H,
  uint32_t  X,
  uint32_t  Y,
  uint32_t  Height,
  uint32_t  Color
  )
{
  static const char mBrand[9] = { 't', 'i', 'a', 'n', 'o', 'c', 'o', 'r', 'e' };
  uint32_t BaseWidth = (Height * 2U) / 3U;
  uint32_t Gap       = Height / 25U;
  uint32_t Radius    = Height / 32U;
  uint32_t I;

  if (Radius < 1U) {
    Radius = 1U;
  }

  for (I = 0U; I < 9U; I++) {
    SPLASH_POINT Path[40];
    uint32_t     Count  = 0U;
    char         Ch     = mBrand[I];
    uint32_t     Factor = (Ch == 'i') ? 38U : ((Ch == 't') ? 76U : ((Ch == 'r') ? 82U : 100U));
    uint32_t     Width  = (BaseWidth * Factor) / 100U;
    uint32_t     J;

    if (Ch == 'o' || Ch == 'a' || Ch == 'c' || Ch == 'e') {
      uint32_t First = (Ch == 'c') ? 4U : ((Ch == 'e') ? 2U : 0U);
      uint32_t Last  = (Ch == 'c') ? 28U : ((Ch == 'e') ? 30U : 32U);
      for (J = First; J <= Last; J++) {
        Path[Count].X = 500 + (mCircle[J & 31U].X * 390) / 1000;
        Path[Count].Y = 750 + (mCircle[J & 31U].Y * 450) / 1000;
        Count++;
      }
    } else if (Ch == 'n') {
      Path[Count].X = 110; Path[Count].Y = 1200; Count++;
      Path[Count].X = 110; Path[Count].Y = 300;  Count++;
      Path[Count].X = 110; Path[Count].Y = 700;  Count++;
      for (J = 16U; J <= 32U; J++) {
        Path[Count].X = 500 + (mCircle[J & 31U].X * 390) / 1000;
        Path[Count].Y = 700 + (mCircle[J & 31U].Y * 400) / 1000;
        Count++;
      }
      Path[Count].X = 890; Path[Count].Y = 1200; Count++;
    } else if (Ch == 't') {
      Path[Count].X = 430; Path[Count].Y = 0;    Count++;
      Path[Count].X = 430; Path[Count].Y = 1050; Count++;
      Path[Count].X = 500; Path[Count].Y = 1150; Count++;
      Path[Count].X = 640; Path[Count].Y = 1200; Count++;
      Path[Count].X = 840; Path[Count].Y = 1200; Count++;
    } else if (Ch == 'i') {
      Path[Count].X = 500; Path[Count].Y = 350;  Count++;
      Path[Count].X = 500; Path[Count].Y = 1200; Count++;
    } else if (Ch == 'r') {
      Path[Count].X = 110; Path[Count].Y = 1200; Count++;
      Path[Count].X = 110; Path[Count].Y = 300;  Count++;
      Path[Count].X = 110; Path[Count].Y = 700;  Count++;
      Path[Count].X = 270; Path[Count].Y = 430;  Count++;
      Path[Count].X = 520; Path[Count].Y = 300;  Count++;
      Path[Count].X = 760; Path[Count].Y = 320;  Count++;
    }

    for (J = 1U; J < Count; J++) {
      SPLASH_POINT A = Path[J - 1U];
      SPLASH_POINT B = Path[J];
      A.X = (int32_t)X + (A.X * (int32_t)Width) / 1000;
      B.X = (int32_t)X + (B.X * (int32_t)Width) / 1000;
      A.Y = (int32_t)Y + (A.Y * (int32_t)Height) / 1200;
      B.Y = (int32_t)Y + (B.Y * (int32_t)Height) / 1200;
      DrawStroke(Fb, StridePx, W, H, A, B, Radius, Color);
    }

    if (Ch == 't' || Ch == 'a' || Ch == 'e') {
      SPLASH_POINT A;
      SPLASH_POINT B;
      A.X = (Ch == 't') ? 100 : ((Ch == 'a') ? 890 : 200);
      A.Y = (Ch == 't') ? 350 : ((Ch == 'a') ? 300 : 1050);
      B.X = (Ch == 't') ? 850 : ((Ch == 'a') ? 890 : 855);
      B.Y = (Ch == 't') ? 350 : ((Ch == 'a') ? 1200 : 500);
      A.X = (int32_t)X + (A.X * (int32_t)Width) / 1000;
      B.X = (int32_t)X + (B.X * (int32_t)Width) / 1000;
      A.Y = (int32_t)Y + (A.Y * (int32_t)Height) / 1200;
      B.Y = (int32_t)Y + (B.Y * (int32_t)Height) / 1200;
      DrawStroke(Fb, StridePx, W, H, A, B, Radius, Color);
    }

    if (Ch == 'i') {
      DrawDisc(Fb, StridePx, W, H, (int32_t)(X + Width / 2U), (int32_t)(Y + Height / 12U), Radius * 2U, Color);
    }

    X += Width + Gap;
  }
}

static const uint8_t *LookupGlyph5x7(char Ch) {
  static const uint8_t Space[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
  static const uint8_t Plus[5]  = { 0x08, 0x08, 0x3e, 0x08, 0x08 };
  static const uint8_t Minus[5] = { 0x08, 0x08, 0x08, 0x08, 0x08 };
  static const uint8_t Slash[5] = { 0x20, 0x10, 0x08, 0x04, 0x02 };
  static const uint8_t Colon[5] = { 0x00, 0x36, 0x36, 0x00, 0x00 };
  static const uint8_t Dot[5]   = { 0x00, 0x60, 0x60, 0x00, 0x00 };

  if (Ch >= 'a' && Ch <= 'z') {
    Ch = (char)(Ch - ('a' - 'A'));
  }
  if (Ch >= 'A' && Ch <= 'Z') {
    return mLetters[(uint8_t)Ch - (uint8_t)'A'];
  }
  if (Ch >= '0' && Ch <= '9') {
    return mDigits[(uint8_t)Ch - (uint8_t)'0'];
  }
  if (Ch == '+') { return Plus; }
  if (Ch == '-') { return Minus; }
  if (Ch == '/') { return Slash; }
  if (Ch == ':') { return Colon; }
  if (Ch == '.') { return Dot; }
  return Space;
}

static void DrawText5x7(
  uint32_t    *Fb,
  uint32_t    StridePx,
  uint32_t    W,
  uint32_t    H,
  const char  *Text,
  uint32_t    X,
  uint32_t    Y,
  uint32_t    Scale,
  uint32_t    Color
  )
{
  uint32_t I;

  if (Text == 0 || Scale == 0U) {
    return;
  }

  for (I = 0U; Text[I] != '\0'; I++) {
    const uint8_t *G = LookupGlyph5x7(Text[I]);
    uint32_t       Col;
    for (Col = 0U; Col < 5U; Col++) {
      uint32_t Row = 0U;
      while (Row < 7U) {
        uint32_t First;
        if ((G[Col] & (1U << Row)) == 0U) {
          Row++;
          continue;
        }
        First = Row;
        while (Row < 7U && (G[Col] & (1U << Row)) != 0U) {
          Row++;
        }
        FillRect(
          Fb,
          StridePx,
          W,
          H,
          X + (I * 6U + Col) * Scale,
          Y + First * Scale,
          Scale,
          (Row - First) * Scale,
          Color
          );
      }
    }
  }
}

void BootSelectGetCardRect(
  uint32_t             Index,
  NP05J_BOOT_CARD_RECT *Out
  )
{
  static const uint32_t mTargets[NP05J_CARD_COUNT] = {
    NP05J_BOOT_TARGET_ANDROID,
    NP05J_BOOT_TARGET_UEFI_MENU,
    NP05J_BOOT_TARGET_BIOS_SETUP
  };

  if (Out == 0) {
    return;
  }
  if (Index >= NP05J_CARD_COUNT) {
    Out->X      = 0;
    Out->Y      = 0;
    Out->W      = 0;
    Out->H      = 0;
    Out->Target = 0U;
    return;
  }

  Out->X      = 525;
  Out->Y      = 510 + (int32_t)(Index * 164U);
  Out->W      = 1350;
  Out->H      = 140;
  Out->Target = mTargets[Index];
}

int32_t BootSelectHitTest(
  uint32_t  X,
  uint32_t  Y
  )
{
  uint32_t I;

  for (I = 0U; I < NP05J_CARD_COUNT; I++) {
    NP05J_BOOT_CARD_RECT R;
    BootSelectGetCardRect(I, &R);
    if (R.X >= 0 && R.Y >= 0 && R.W > 0 && R.H > 0 &&
        X >= (uint32_t)R.X && X < (uint32_t)(R.X + R.W) &&
        Y >= (uint32_t)R.Y && Y < (uint32_t)(R.Y + R.H)) {
      return (int32_t)I;
    }
  }
  return -1;
}

void BootSelectRenderFrame(
  uint32_t  *FbBgra,
  uint32_t  Width,
  uint32_t  Height,
  uint32_t  StridePx,
  uint32_t  SelectedCard,
  uint32_t  ProgressPct,
  uint32_t  RemainingSec,
  int       TouchOnline
  )
{
  /* Safe static 2D arrays (zero pointers / zero relocations for freestanding EL2 PIC) */
  static const char mTitles[NP05J_CARD_COUNT][12] = {
    "ANDROID",
    "UEFI",
    "BIOS SETUP"
  };
  static const char mStarting[NP05J_CARD_COUNT][20] = {
    "STARTING ANDROID",
    "STARTING UEFI",
    "STARTING BIOS SETUP"
  };

  uint32_t BgWhite     = 0xFFFFFFU;
  uint32_t Gold        = 0xFF6600U;
  uint32_t Shade       = 0xD65200U;
  uint32_t WordDark    = 0x40444AU;
  uint32_t MutedGray   = 0x70747AU;
  uint32_t HeaderGray  = 0x565B63U;
  uint32_t RowUnselBg  = 0xF8F9FBU;
  uint32_t RowUnselBdr = 0xE7EAF0U;
  uint32_t RowUnselInk = 0x383D46U;
  uint32_t RowSelBg    = 0xEEF4FFU;
  uint32_t RowSelBdr   = 0x4273E2U;
  uint32_t RowSelInk   = 0x2254B5U;
  uint32_t StatusInk   = 0x656B75U;
  uint32_t TrackBg     = 0xE9EDF3U;
  uint32_t HintLine2   = 0x848A94U;
  uint32_t TouchGreen  = 0x167D70U;
  uint32_t TouchOrange = 0xD65200U;

  uint32_t Short         = (Width < Height) ? Width : Height;
  uint32_t LogoSize      = 264U;
  uint32_t WordmarkScale = 27U;
  uint32_t TitleHeight   = WordmarkScale * 7U;
  uint32_t BrandGap      = Short / 20U;
  uint32_t GroupWidth    = LogoSize + BrandGap + BrandWidth(TitleHeight);
  uint32_t CenterY       = 232U;
  uint32_t LogoX         = (Width > GroupWidth) ? ((Width - GroupWidth) / 2U) : 16U;
  uint32_t LogoY         = CenterY - (LogoSize / 2U);
  uint32_t WordmarkX     = LogoX + LogoSize + BrandGap;
  uint32_t WordmarkY     = CenterY - (TitleHeight / 2U);

  uint32_t SelIdx = (SelectedCard < NP05J_CARD_COUNT) ? SelectedCard : 0U;
  uint32_t Pct    = (ProgressPct > 100U) ? 100U : ProgressPct;
  uint32_t FillW;
  uint32_t I;

  if (FbBgra == 0 || Width == 0U || Height == 0U || StridePx < Width) {
    return;
  }

  /* 1. Pure crisp SunUEFI white canvas (#FFFFFF) */
  FillRect(FbBgra, StridePx, Width, Height, 0U, 0U, Width, Height, BgWhite);

  /* 2. Authentic Project SunUEFI faceted 6-spoke starburst + vector tianocore + subtitle */
  for (I = 0U; I < 6U; I++) {
    SPLASH_POINT D      = mSpokes[I];
    SPLASH_POINT N      = { -D.Y, D.X };
    SPLASH_POINT Center = { (int32_t)LogoX + (int32_t)(LogoSize / 2U), (int32_t)LogoY + (int32_t)(LogoSize / 2U) };
    SPLASH_POINT End    = {
      Center.X + (D.X * (int32_t)LogoSize * 43) / 100000,
      Center.Y + (D.Y * (int32_t)LogoSize * 43) / 100000
    };
    int32_t      Inner  = ((int32_t)LogoSize * 20) / 1000;
    int32_t      Outer  = ((int32_t)LogoSize * 48) / 1000;
    SPLASH_POINT P[4]   = {
      { Center.X + (N.X * Inner) / 1000, Center.Y + (N.Y * Inner) / 1000 },
      { End.X + (N.X * Outer) / 1000,    End.Y + (N.Y * Outer) / 1000 },
      { End.X - (N.X * Outer) / 1000,    End.Y - (N.Y * Outer) / 1000 },
      { Center.X - (N.X * Inner) / 1000, Center.Y - (N.Y * Inner) / 1000 }
    };
    SPLASH_POINT Face[4] = { Center, End, P[2], P[3] };

    DrawDisc(FbBgra, StridePx, Width, Height, End.X, End.Y, (uint32_t)Outer, Gold);
    DrawPolygon4(FbBgra, StridePx, Width, Height, P, Gold);
    DrawPolygon4(FbBgra, StridePx, Width, Height, Face, Shade);
  }
  DrawBrandWordmark(FbBgra, StridePx, Width, Height, WordmarkX, WordmarkY, TitleHeight, WordDark);
  DrawText5x7(FbBgra, StridePx, Width, Height, "SUNUEFI / NP05J", WordmarkX, WordmarkY + TitleHeight + 22U, 4U, MutedGray);

  /* 3. Section header */
  DrawText5x7(FbBgra, StridePx, Width, Height, "SELECT STARTUP", 525U, 444U, 6U, HeaderGray);

  /* 4. Vertical row cards matching Project SunUEFI EarlySplash.c */
  for (I = 0U; I < NP05J_CARD_COUNT; I++) {
    NP05J_BOOT_CARD_RECT R;
    int                  Sel = (I == SelIdx);
    uint32_t             Bdr = Sel ? RowSelBdr : RowUnselBdr;
    uint32_t             Bg  = Sel ? RowSelBg  : RowUnselBg;
    uint32_t             Ink = Sel ? RowSelInk : RowUnselInk;

    BootSelectGetCardRect(I, &R);
    FillRect(FbBgra, StridePx, Width, Height, (uint32_t)R.X, (uint32_t)R.Y, (uint32_t)R.W, (uint32_t)R.H, Bdr);
    FillRect(
      FbBgra,
      StridePx,
      Width,
      Height,
      (uint32_t)R.X + 4U,
      (uint32_t)R.Y + 4U,
      (uint32_t)R.W - 8U,
      (uint32_t)R.H - 8U,
      Bg
      );
    DrawText5x7(
      FbBgra,
      StridePx,
      Width,
      Height,
      mTitles[I],
      (uint32_t)R.X + 76U,
      (uint32_t)R.Y + 42U,
      8U,
      Ink
      );
    if (Sel) {
      DrawText5x7(
        FbBgra,
        StridePx,
        Width,
        Height,
        "SELECTED",
        (uint32_t)R.X + (uint32_t)R.W - 76U - (8U * 6U * 6U - 6U),
        (uint32_t)R.Y + 50U,
        6U,
        RowSelBdr
        );
    }
  }

  /* 5. Countdown / launch line & progress bar */
  if (RemainingSec > 0U) {
    char CountBuf[32] = "AUTO START IN   SECONDS";
    if (RemainingSec >= 10U) {
      CountBuf[14] = (char)('0' + ((RemainingSec / 10U) % 10U));
      CountBuf[15] = (char)('0' + (RemainingSec % 10U));
      DrawText5x7(FbBgra, StridePx, Width, Height, CountBuf, 525U, 1040U, 6U, StatusInk);
    } else {
      char OneBuf[28] = "AUTO START IN 0 SECONDS";
      OneBuf[14] = (char)('0' + (RemainingSec % 10U));
      DrawText5x7(FbBgra, StridePx, Width, Height, OneBuf, 525U, 1040U, 6U, StatusInk);
    }
  } else {
    DrawText5x7(FbBgra, StridePx, Width, Height, mStarting[SelIdx], 525U, 1040U, 6U, StatusInk);
  }

  FillRect(FbBgra, StridePx, Width, Height, 525U, 1106U, 1350U, 8U, TrackBg);
  FillW = (1350U * Pct) / 100U;
  if (FillW > 0U) {
    FillRect(FbBgra, StridePx, Width, Height, 525U, 1106U, FillW, 8U, RowSelBdr);
  }

  /* 6. Minimalist bottom footer hints & touch indicator */
  DrawText5x7(FbBgra, StridePx, Width, Height, "VOLUME UP  ANDROID     VOLUME DOWN  UEFI", 144U, 1346U, 4U, StatusInk);
  DrawText5x7(FbBgra, StridePx, Width, Height, "POWER  CONFIRM     THIS CHOICE IS FOR THIS BOOT ONLY", 144U, 1408U, 3U, HintLine2);

  if (TouchOnline != 0) {
    DrawText5x7(FbBgra, StridePx, Width, Height, "TOUCH READY", 1940U, 1368U, 4U, TouchGreen);
  } else {
    DrawText5x7(FbBgra, StridePx, Width, Height, "KEYS ONLY", 1940U, 1368U, 4U, TouchOrange);
  }
}
