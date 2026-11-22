/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Interactive Touch Screen Diagnostics, Live Draw Canvas & 4-Corner Calibration
  screen for nubia NP05J (Synaptics S3930TB0A0 / TCM v2 + USB Touch/Mouse).
**/

#include "Np05jBootMenuApp.h"

#define MAX_TRAIL_POINTS 256U

typedef struct {
  INT32   X;
  INT32   Y;
  BOOLEAN Connected;
} NP05J_TRAIL_PT;

VOID
Np05jRunTouchTest (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  NP05J_TOUCH_INFO_PROTOCOL *TouchInfo = NULL;
  NP05J_TOUCH_TELEMETRY      Telem;
  NP05J_TRAIL_PT             Trail[MAX_TRAIL_POINTS];
  UINTN                      TrailCount  = 0;
  BOOLEAN                    CornerHit[4] = { FALSE, FALSE, FALSE, FALSE };
  BOOLEAN                    NeedRedraw  = TRUE;
  BOOLEAN                    WasDrawing  = FALSE;

  NP05J_RECT CanvasRect   = { 42, 140, 1520, (INT32)Ctx->ScreenH - 280 };
  NP05J_RECT InfoRect     = { 1588, 140, (INT32)Ctx->ScreenW - 1630, (INT32)Ctx->ScreenH - 280 };
  NP05J_RECT BtnSwap      = { 1612, 560, 340, 74 };
  NP05J_RECT BtnInvX      = { 1972, 560, 340, 74 };
  NP05J_RECT BtnInvY      = { 1612, 650, 340, 74 };
  NP05J_RECT BtnReprobe   = { 1972, 650, 340, 74 };
  NP05J_RECT BtnClear     = { 1612, 740, 340, 74 };
  NP05J_RECT BtnSave      = { 1972, 740, 340, 74 };
  NP05J_RECT BtnBack      = { 1612, 830, 700, 84 };

  NP05J_RECT CornerRect[4] = {
    { CanvasRect.X + 24, CanvasRect.Y + 24, 150, 88 },
    { CanvasRect.X + CanvasRect.W - 174, CanvasRect.Y + 24, 150, 88 },
    { CanvasRect.X + 24, CanvasRect.Y + CanvasRect.H - 112, 150, 88 },
    { CanvasRect.X + CanvasRect.W - 174, CanvasRect.Y + CanvasRect.H - 112, 150, 88 }
  };

  ZeroMem (&Telem, sizeof (Telem));
  gBS->LocateProtocol (&gNp05jTouchInfoProtocolGuid, NULL, (VOID **)&TouchInfo);
  Np05jUiDrainInput (Ctx);

  while (TRUE) {
    NP05J_INPUT_EVENT Ev;
    CHAR8             Line[96];

    if (TouchInfo != NULL) {
      TouchInfo->GetTelemetry (TouchInfo, &Telem);
    }

    if (NeedRedraw) {
      Np05jUiClear (Ctx, COL_BG);
      Np05jUiDrawHeader (Ctx, "TOUCHSCREEN DIAGNOSTICS & CALIBRATION", "Draw anywhere inside canvas | Tap 4 corners to verify orientation");

      Np05jUiDrawCard (Ctx, CanvasRect, COL_PANEL, COL_ACCENT, 2);
      Np05jUiDrawText (Ctx, CanvasRect.X + 190, CanvasRect.Y + 34, 2, COL_SUBTEXT, "Draw continuously inside this canvas. Tap [TL] [TR] [BL] [BR] targets.");

      for (UINTN C = 0; C < 4U; C++) {
        STATIC CONST CHAR8 *Labels[4] = { "TL", "TR", "BL", "BR" };
        Np05jUiDrawCard (
          Ctx,
          CornerRect[C],
          CornerHit[C] ? Np05jRgb (209, 250, 229) : COL_CARD,
          CornerHit[C] ? COL_GREEN : COL_GOLD,
          3
          );
        Np05jUiDrawTextCentered (Ctx, CornerRect[C], 3, CornerHit[C] ? COL_GREEN : COL_GOLD, Labels[C]);
      }

      /* Draw trail */
      for (UINTN I = 0; I < TrailCount; I++) {
        if ((I > 0U) && Trail[I].Connected) {
          Np05jUiDrawLine (Ctx, Trail[I - 1].X, Trail[I - 1].Y, Trail[I].X, Trail[I].Y, 6, COL_ACCENT);
        }
        Np05jUiDrawCircle (Ctx, Trail[I].X, Trail[I].Y, 4, COL_GOLD);
      }

      if (Ctx->AnyTouchDown && Np05jRectContains (CanvasRect, Ctx->ActiveTouchX, Ctx->ActiveTouchY)) {
        Np05jUiDrawLine (Ctx, Ctx->ActiveTouchX - 28, Ctx->ActiveTouchY, Ctx->ActiveTouchX + 28, Ctx->ActiveTouchY, 2, COL_GREEN);
        Np05jUiDrawLine (Ctx, Ctx->ActiveTouchX, Ctx->ActiveTouchY - 28, Ctx->ActiveTouchX, Ctx->ActiveTouchY + 28, 2, COL_GREEN);
      }

      /* Right telemetry panel */
      Np05jUiDrawCard (Ctx, InfoRect, COL_PANEL, COL_BORDER, 2);
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 24, 3, COL_ACCENT, "Synaptics S3930 Telemetry");

      AsciiSPrint (Line, sizeof (Line), "Status : %a", Telem.Initialized ? "ONLINE (TCMv2 SPI4)" : "OFFLINE / USB ONLY");
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 84, 2, Telem.Initialized ? COL_GREEN : COL_GOLD, Line);

      AsciiSPrint (Line, sizeof (Line), "Part   : %a", Telem.PartNumber[0] ? Telem.PartNumber : "S3930TB0A0");
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 118, 2, COL_TEXT, Line);

      AsciiSPrint (Line, sizeof (Line), "Build  : 0x%08x  Objs: %u", Telem.BuildId, Telem.MaxObjects);
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 152, 2, COL_TEXT, Line);

      AsciiSPrint (Line, sizeof (Line), "MaxXY  : %u x %u", Telem.MaxX, Telem.MaxY);
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 186, 2, COL_TEXT, Line);

      AsciiSPrint (Line, sizeof (Line), "RawXY  : %u , %u", Telem.LastRawX, Telem.LastRawY);
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 220, 2, COL_GOLD, Line);

      AsciiSPrint (Line, sizeof (Line), "DispXY : %u , %u", Telem.LastDispX, Telem.LastDispY);
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 254, 2, COL_ACCENT, Line);

      AsciiSPrint (Line, sizeof (Line), "Frames : %u   IRQs: %u", Telem.FrameCount, Telem.IrqCount);
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 288, 2, COL_TEXT, Line);

      AsciiSPrint (Line, sizeof (Line), "Errors : %u   Rate: %u Hz", Telem.ErrorCount, Ctx->Config.TouchPollRateHz);
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 322, 2, (Telem.ErrorCount > 0) ? COL_GOLD : COL_SUBTEXT, Line);

      AsciiSPrint (Line, sizeof (Line), "Corners: %u / 4 verified",
        (UINT32)(CornerHit[0] + CornerHit[1] + CornerHit[2] + CornerHit[3]));
      Np05jUiDrawText (Ctx, InfoRect.X + 24, InfoRect.Y + 362, 2, COL_GREEN, Line);

      /* Calibration buttons */
      Np05jUiDrawCard (Ctx, BtnSwap, Ctx->Config.TouchSwapXY ? COL_CARD_HI : COL_CARD, COL_ACCENT, 2);
      AsciiSPrint (Line, sizeof (Line), "SwapXY: %a", Ctx->Config.TouchSwapXY ? "ON" : "OFF");
      Np05jUiDrawTextCentered (Ctx, BtnSwap, 2, COL_TEXT, Line);

      Np05jUiDrawCard (Ctx, BtnInvX, Ctx->Config.TouchInvertX ? COL_CARD_HI : COL_CARD, COL_ACCENT, 2);
      AsciiSPrint (Line, sizeof (Line), "InvertX: %a", Ctx->Config.TouchInvertX ? "ON" : "OFF");
      Np05jUiDrawTextCentered (Ctx, BtnInvX, 2, COL_TEXT, Line);

      Np05jUiDrawCard (Ctx, BtnInvY, Ctx->Config.TouchInvertY ? COL_CARD_HI : COL_CARD, COL_ACCENT, 2);
      AsciiSPrint (Line, sizeof (Line), "InvertY: %a", Ctx->Config.TouchInvertY ? "ON" : "OFF");
      Np05jUiDrawTextCentered (Ctx, BtnInvY, 2, COL_TEXT, Line);

      Np05jUiDrawCard (Ctx, BtnReprobe, COL_CARD, COL_GOLD, 2);
      Np05jUiDrawTextCentered (Ctx, BtnReprobe, 2, COL_GOLD, "Re-Probe Touch IC");

      Np05jUiDrawCard (Ctx, BtnClear, COL_CARD, COL_BORDER, 2);
      Np05jUiDrawTextCentered (Ctx, BtnClear, 2, COL_TEXT, "Clear Canvas");

      Np05jUiDrawCard (Ctx, BtnSave, COL_CARD_HI, COL_GREEN, 2);
      Np05jUiDrawTextCentered (Ctx, BtnSave, 2, COL_GREEN, "Save Calibration");

      Np05jUiDrawCard (Ctx, BtnBack, COL_CARD, COL_RED, 2);
      Np05jUiDrawTextCentered (Ctx, BtnBack, 3, COL_TEXT, "< Back to Boot Menu (ESC)");

      Np05jUiDrawBannerIfAny (Ctx);
      Np05jUiPresent (Ctx);
      NeedRedraw = FALSE;
    }

    if (EFI_ERROR (Np05jUiPollInput (Ctx, 50, &Ev))) {
      WasDrawing = FALSE;
      NeedRedraw = TRUE; /* Keep telemetry live */
      continue;
    }

    if (Ev.Kind == Np05jInputBack) {
      return;
    }

    if ((Ev.Kind == Np05jInputTouchMove) && Np05jRectContains (CanvasRect, Ev.X, Ev.Y)) {
      if (TrailCount >= MAX_TRAIL_POINTS) {
        CopyMem (&Trail[0], &Trail[1], (MAX_TRAIL_POINTS - 1U) * sizeof (NP05J_TRAIL_PT));
        TrailCount = MAX_TRAIL_POINTS - 1U;
      }
      Trail[TrailCount].X         = Ev.X;
      Trail[TrailCount].Y         = Ev.Y;
      Trail[TrailCount].Connected = WasDrawing;
      TrailCount++;
      WasDrawing = TRUE;

      for (UINTN C = 0; C < 4U; C++) {
        if (Np05jRectContains (CornerRect[C], Ev.X, Ev.Y)) {
          CornerHit[C] = TRUE;
        }
      }
      NeedRedraw = TRUE;
      continue;
    }

    if (Ev.Kind == Np05jInputTap) {
      WasDrawing = FALSE;
      for (UINTN C = 0; C < 4U; C++) {
        if (Np05jRectContains (CornerRect[C], Ev.X, Ev.Y)) {
          CornerHit[C] = TRUE;
        }
      }

      if (Np05jRectContains (BtnSwap, Ev.X, Ev.Y)) {
        Ctx->Config.TouchSwapXY = Ctx->Config.TouchSwapXY ? 0U : 1U;
        if (TouchInfo != NULL) {
          TouchInfo->ApplyCalibration (TouchInfo, Ctx->Config.TouchSwapXY, Ctx->Config.TouchInvertX, Ctx->Config.TouchInvertY, Ctx->Config.TouchPollRateHz);
        }
      } else if (Np05jRectContains (BtnInvX, Ev.X, Ev.Y)) {
        Ctx->Config.TouchInvertX = Ctx->Config.TouchInvertX ? 0U : 1U;
        if (TouchInfo != NULL) {
          TouchInfo->ApplyCalibration (TouchInfo, Ctx->Config.TouchSwapXY, Ctx->Config.TouchInvertX, Ctx->Config.TouchInvertY, Ctx->Config.TouchPollRateHz);
        }
      } else if (Np05jRectContains (BtnInvY, Ev.X, Ev.Y)) {
        Ctx->Config.TouchInvertY = Ctx->Config.TouchInvertY ? 0U : 1U;
        if (TouchInfo != NULL) {
          TouchInfo->ApplyCalibration (TouchInfo, Ctx->Config.TouchSwapXY, Ctx->Config.TouchInvertX, Ctx->Config.TouchInvertY, Ctx->Config.TouchPollRateHz);
        }
      } else if (Np05jRectContains (BtnReprobe, Ev.X, Ev.Y)) {
        if (TouchInfo != NULL) {
          EFI_STATUS Re = TouchInfo->Reinit (TouchInfo);
          Np05jUiRefreshHandles (Ctx);
          Np05jUiSetBanner (Ctx, EFI_ERROR (Re) ? COL_RED : COL_GREEN, "Touch Re-Probe: %r", Re);
        } else {
          Np05jUiSetBanner (Ctx, COL_GOLD, "Np05jTouchInfoProtocol not installed");
        }
      } else if (Np05jRectContains (BtnClear, Ev.X, Ev.Y)) {
        TrailCount = 0;
        ZeroMem (CornerHit, sizeof (CornerHit));
      } else if (Np05jRectContains (BtnSave, Ev.X, Ev.Y)) {
        EFI_STATUS Sv = Np05jBiosConfigSave (&Ctx->Config);
        Np05jUiSetBanner (Ctx, EFI_ERROR (Sv) ? COL_RED : COL_GREEN, "Saved touch calibration: %r", Sv);
      } else if (Np05jRectContains (BtnBack, Ev.X, Ev.Y)) {
        return;
      }
      NeedRedraw = TRUE;
    }
  }
}
