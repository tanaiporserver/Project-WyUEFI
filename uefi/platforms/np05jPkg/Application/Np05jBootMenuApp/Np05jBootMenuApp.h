/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Shared declarations for NP05J touch-first UEFI Boot Menu & Unlocked BIOS Setup.
**/

#ifndef _NP05J_BOOT_MENU_APP_H_
#define _NP05J_BOOT_MENU_APP_H_

#include <PiDxe.h>
#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/Np05jBiosConfigLib.h>
#include <Library/Np05jResetLib.h>
#include <Library/Np05jSplashLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>
#include <Protocol/EFIChargerEx.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Np05jBiosConfig.h>
#include <Protocol/Np05jFastboot.h>
#include <Protocol/Np05jTouchInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimplePointer.h>

#include <Np05jBiosConfig.h>
#include <Np05jBootHandoff.h>

#define NP05J_MAX_ABS_HANDLES    8U
#define NP05J_MAX_MOUSE_HANDLES  8U
#define NP05J_UEFI_SHELL_GUID    { 0x7C04A583, 0x9E3E, 0x4F1C, { 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 } }
#define NP05J_UIAPP_GUID         { 0x462CAA21, 0x7614, 0x4503, { 0x83, 0x6E, 0x8A, 0xB6, 0xF4, 0x66, 0x23, 0x31 } }

typedef struct {
  INT32 X;
  INT32 Y;
  INT32 W;
  INT32 H;
} NP05J_RECT;

typedef enum {
  Np05jInputNone = 0,
  Np05jInputTap,
  Np05jInputUp,
  Np05jInputDown,
  Np05jInputLeft,
  Np05jInputRight,
  Np05jInputEnter,
  Np05jInputBack,
  Np05jInputTouchMove,
  Np05jInputF12,
  Np05jInputFastbootAction
} NP05J_INPUT_KIND;

typedef struct {
  NP05J_INPUT_KIND       Kind;
  INT32                  X;
  INT32                  Y;
  BOOLEAN                PointerDown;
  NP05J_FASTBOOT_ACTION  FbAction;
} NP05J_INPUT_EVENT;

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BackBuf;
  UINT32                         ScreenW;
  UINT32                         ScreenH;
  BOOLEAN                        Dirty;

  NP05J_BIOS_CONFIG              Config;
  BOOLEAN                        ConfigModified;
  BOOLEAN                        StatusValid;
  CHAR8                          StatusBanner[128];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  StatusColor;

  /* Input tracking across touch + mouse + keypad */
  EFI_ABSOLUTE_POINTER_PROTOCOL *AbsPtrs[NP05J_MAX_ABS_HANDLES];
  UINTN                          AbsCount;
  BOOLEAN                        AbsWasDown[NP05J_MAX_ABS_HANDLES];
  INT32                          AbsStartX[NP05J_MAX_ABS_HANDLES];
  INT32                          AbsStartY[NP05J_MAX_ABS_HANDLES];
  INT32                          AbsCurX[NP05J_MAX_ABS_HANDLES];
  INT32                          AbsCurY[NP05J_MAX_ABS_HANDLES];

  EFI_SIMPLE_POINTER_PROTOCOL   *MousePtrs[NP05J_MAX_MOUSE_HANDLES];
  UINTN                          MouseCount;
  BOOLEAN                        MouseWasLeftDown;
  BOOLEAN                        MouseVisible;
  INT32                          CursorX;
  INT32                          CursorY;

  BOOLEAN                        AnyTouchDown;
  INT32                          ActiveTouchX;
  INT32                          ActiveTouchY;
  NP05J_FASTBOOT_PROTOCOL       *Fastboot;
} NP05J_UI_CTX;

/* Standard SunUEFI high-contrast palette */
static inline
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
Np05jRgb (
  IN UINT8 R,
  IN UINT8 G,
  IN UINT8 B
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL P = { B, G, R, 0 };
  return P;
}

/* Authentic Project SunUEFI palette (ProductSplash.c, EarlySplash.c & webroot/style.css) */
#define COL_BG          Np05jRgb (255, 255, 255)
#define COL_SURFACE     Np05jRgb (243, 244, 246)
#define COL_RAIL        Np05jRgb (234, 240, 238)
#define COL_RAIL_HI     Np05jRgb (215, 234, 227)
#define COL_PANEL       Np05jRgb (255, 255, 255)
#define COL_CARD        Np05jRgb (248, 249, 251)
#define COL_CARD_HI     Np05jRgb (238, 244, 255)
#define COL_CARD_PRESS  Np05jRgb (220, 234, 255)
#define COL_BORDER      Np05jRgb (231, 234, 240)
#define COL_ACCENT      Np05jRgb (66, 115, 226)
#define COL_ACCENT_DARK Np05jRgb (34, 84, 181)
#define COL_GOLD        Np05jRgb (255, 102, 0)
#define COL_GOLD_SHADE  Np05jRgb (214, 82, 0)
#define COL_GREEN       Np05jRgb (22, 125, 112)
#define COL_GREEN_BG    Np05jRgb (231, 243, 239)
#define COL_RED         Np05jRgb (167, 62, 53)
#define COL_RED_BG      Np05jRgb (251, 241, 240)
#define COL_PURPLE      Np05jRgb (102, 100, 151)
#define COL_TEXT        Np05jRgb (56, 61, 70)
#define COL_SUBTEXT     Np05jRgb (101, 107, 117)
#define COL_MUTED       Np05jRgb (132, 138, 148)
#define COL_BLACK       Np05jRgb (0, 0, 0)
#define COL_WHITE       Np05jRgb (255, 255, 255)

/* Canvas & Input (Np05jUiCanvas.c) */
EFI_STATUS
Np05jUiInit (
  OUT NP05J_UI_CTX *Ctx
  );

VOID
Np05jUiFree (
  IN OUT NP05J_UI_CTX *Ctx
  );

VOID
Np05jUiRefreshHandles (
  IN OUT NP05J_UI_CTX *Ctx
  );

VOID
Np05jUiDrainInput (
  IN OUT NP05J_UI_CTX *Ctx
  );

BOOLEAN
Np05jRectContains (
  IN NP05J_RECT R,
  IN INT32      X,
  IN INT32      Y
  );

VOID
Np05jUiClear (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  );

VOID
Np05jUiFillRect (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  );

VOID
Np05jUiDrawBorder (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     INT32                          Thickness,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  );

VOID
Np05jUiDrawCard (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Fill,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Border,
  IN     INT32                          BorderThickness
  );

VOID
Np05jUiDrawRoundedCard (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     INT32                          Radius,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Fill,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Border,
  IN     INT32                          BorderThickness
  );

VOID
Np05jUiDrawCircle (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          Cx,
  IN     INT32                          Cy,
  IN     INT32                          Radius,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  );

VOID
Np05jUiDrawLine (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X0,
  IN     INT32                          Y0,
  IN     INT32                          X1,
  IN     INT32                          Y1,
  IN     INT32                          Thickness,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  );

VOID
Np05jUiDrawText (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X,
  IN     INT32                          Y,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Text
  );

VOID
Np05jUiDrawTextCentered (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Text
  );

VOID
Np05jUiDrawText5x7 (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     INT32                          X,
  IN     INT32                          Y,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Text
  );

VOID
Np05jUiDrawText5x7Centered (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     NP05J_RECT                     Rect,
  IN     UINT32                         Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Text
  );

VOID
Np05jUiDrawSunMark (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     INT32         Cx,
  IN     INT32         Cy,
  IN     INT32         Radius
  );

VOID
Np05jUiDrawSunBrand (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     INT32         CenterY,
  IN     UINT32        LogoSize,
  IN     UINT32        WordmarkScale,
  IN     CONST CHAR8  *Subtitle
  );

VOID
Np05jUiDrawHeader (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     CONST CHAR8  *Title,
  IN     CONST CHAR8  *Subtitle
  );

VOID
Np05jUiSetBanner (
  IN OUT NP05J_UI_CTX                  *Ctx,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color,
  IN     CONST CHAR8                   *Format,
  ...
  );

VOID
Np05jUiDrawBannerIfAny (
  IN OUT NP05J_UI_CTX *Ctx
  );

VOID
Np05jUiPresent (
  IN OUT NP05J_UI_CTX *Ctx
  );

EFI_STATUS
Np05jUiPollInput (
  IN OUT NP05J_UI_CTX      *Ctx,
  IN     UINTN              TimeoutMs,
  OUT    NP05J_INPUT_EVENT *Event
  );

VOID
Np05jQueryPowerStatus (
  OUT BOOLEAN *BatteryPresent,
  OUT BOOLEAN *ChargerPresent,
  OUT UINT32  *VoltageMv
  );

/* Screens & OS Launchers */
VOID
Np05jRunBiosSetup (
  IN OUT NP05J_UI_CTX *Ctx,
  OUT    BOOLEAN      *RequestReboot
  );

VOID
Np05jRunFileBrowser (
  IN OUT NP05J_UI_CTX *Ctx
  );

VOID
Np05jRunTouchTest (
  IN OUT NP05J_UI_CTX *Ctx
  );

EFI_STATUS
Np05jLaunchWindowsArm (
  IN OUT NP05J_UI_CTX *Ctx
  );

EFI_STATUS
Np05jLaunchLinux (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     BOOLEAN       UseAcpi
  );

EFI_STATUS
Np05jLaunchEfiShell (
  IN OUT NP05J_UI_CTX *Ctx
  );

EFI_STATUS
Np05jLaunchTianoSetup (
  IN OUT NP05J_UI_CTX *Ctx
  );

EFI_STATUS
Np05jLaunchEfiPathOnVolume (
  IN EFI_HANDLE    FsHandle,
  IN CONST CHAR16 *FilePath,
  IN BOOLEAN       ConnectDrivers
  );

UINTN
Np05jCountFileSystems (
  VOID
  );

#endif /* _NP05J_BOOT_MENU_APP_H_ */
