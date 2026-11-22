/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  High-DPI vector UEFI splash screen & touch pill API for nubia NP05J.
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#ifndef _NP05J_SPLASH_LIB_H_
#define _NP05J_SPLASH_LIB_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

typedef enum {
  Np05jSplashActionNone = 0,
  Np05jSplashActionBootMenu,
  Np05jSplashActionBiosSetup,
  Np05jSplashActionWindows,
  Np05jSplashActionLinux
} NP05J_SPLASH_ACTION;

typedef struct {
  UINT32              X;
  UINT32              Y;
  UINT32              Width;
  UINT32              Height;
  NP05J_SPLASH_ACTION Action;
} NP05J_SPLASH_HITBOX;

EFI_STATUS
EFIAPI
Np05jDrawSplash (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
  IN UINT8                         Style
  );

EFI_STATUS
EFIAPI
Np05jSplashSetProgress (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
  IN UINT8                         Percent,
  IN CONST CHAR8                  *StatusText OPTIONAL
  );

NP05J_SPLASH_ACTION
EFIAPI
Np05jSplashHitTest (
  IN UINT32 DisplayX,
  IN UINT32 DisplayY
  );

VOID
EFIAPI
Np05jSplashRegisterBootLogo2 (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop
  );

#endif /* _NP05J_SPLASH_LIB_H_ */
