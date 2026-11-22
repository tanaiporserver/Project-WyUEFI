/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Touch-friendly FAT/ESP File Browser (.efi launcher) for nubia NP05J.
**/

#include "Np05jBootMenuApp.h"

#define MAX_ENTRIES  128U
#define VISIBLE_ROWS 9U

typedef struct {
  CHAR16  Name[96];
  UINT64  Size;
  BOOLEAN IsDir;
  BOOLEAN IsEfi;
} NP05J_FILE_ENTRY;

STATIC
BOOLEAN
EndsWithEfi (
  IN CONST CHAR16 *Name
  )
{
  STATIC CONST CHAR16 mLower[5] = L".efi";
  STATIC CONST CHAR16 mUpper[5] = L".EFI";
  UINTN               Len       = StrLen (Name);

  if (Len < 4U) {
    return FALSE;
  }
  for (UINTN I = 0; I < 4U; I++) {
    CHAR16 C = Name[Len - 4U + I];
    if ((C != mLower[I]) && (C != mUpper[I])) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC
UINTN
ReadDirectoryEntries (
  IN  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs,
  IN  CONST CHAR16                    *DirPath,
  OUT NP05J_FILE_ENTRY                *Entries,
  IN  UINTN                            MaxEntries
  )
{
  EFI_FILE_PROTOCOL *Root   = NULL;
  EFI_FILE_PROTOCOL *Dir    = NULL;
  EFI_FILE_INFO     *Info   = NULL;
  UINTN              BufSz  = 2048U;
  UINTN              Count  = 0U;

  if (EFI_ERROR (Fs->OpenVolume (Fs, &Root)) || (Root == NULL)) {
    return 0;
  }

  if (EFI_ERROR (Root->Open (Root, &Dir, (CHAR16 *)DirPath, EFI_FILE_MODE_READ, 0)) || (Dir == NULL)) {
    Root->Close (Root);
    return 0;
  }

  Info = AllocateZeroPool (BufSz);
  if (Info == NULL) {
    Dir->Close (Dir);
    Root->Close (Root);
    return 0;
  }

  /* Parent entry when not at root */
  if (StrCmp (DirPath, L"\\") != 0) {
    StrCpyS (Entries[Count].Name, 96, L"..");
    Entries[Count].IsDir = TRUE;
    Entries[Count].IsEfi = FALSE;
    Entries[Count].Size  = 0;
    Count++;
  }

  while (Count < MaxEntries) {
    UINTN      ReadSz = BufSz;
    EFI_STATUS Status = Dir->Read (Dir, &ReadSz, Info);

    if (Status == EFI_BUFFER_TOO_SMALL) {
      FreePool (Info);
      BufSz = ReadSz + 256U;
      Info  = AllocateZeroPool (BufSz);
      if (Info == NULL) {
        break;
      }
      Status = Dir->Read (Dir, &ReadSz, Info);
    }

    if (EFI_ERROR (Status) || (ReadSz == 0U)) {
      break;
    }
    if ((StrCmp (Info->FileName, L".") == 0) || (StrCmp (Info->FileName, L"..") == 0)) {
      continue;
    }

    StrnCpyS (Entries[Count].Name, 96, Info->FileName, 95);
    Entries[Count].Size  = Info->FileSize;
    Entries[Count].IsDir = ((Info->Attribute & EFI_FILE_DIRECTORY) != 0ULL);
    Entries[Count].IsEfi = (!Entries[Count].IsDir) && EndsWithEfi (Info->FileName);
    Count++;
  }

  FreePool (Info);
  Dir->Close (Dir);
  Root->Close (Root);
  return Count;
}

VOID
Np05jRunFileBrowser (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  EFI_HANDLE       *VolHandles = NULL;
  UINTN             VolCount   = 0;
  UINTN             CurVol     = 0;
  CHAR16            CurPath[260];
  NP05J_FILE_ENTRY *Entries;
  UINTN             EntryCount = 0;
  UINTN             Scroll     = 0;
  INTN              Selected   = 0;
  BOOLEAN           NeedReload = TRUE;
  BOOLEAN           NeedRedraw = TRUE;

  EfiBootManagerConnectAll ();
  gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &VolCount, &VolHandles);

  Entries = AllocateZeroPool (MAX_ENTRIES * sizeof (NP05J_FILE_ENTRY));
  if (Entries == NULL) {
    if (VolHandles != NULL) {
      FreePool (VolHandles);
    }
    return;
  }

  StrCpyS (CurPath, 260, L"\\");
  Np05jUiDrainInput (Ctx);

  while (TRUE) {
    NP05J_INPUT_EVENT Ev;
    NP05J_RECT        VolBtns[6];
    NP05J_RECT        RowRect[VISIBLE_ROWS];
    NP05J_RECT        BtnUp   = { 1960, 148, 190, 68 };
    NP05J_RECT        BtnBack = { 2166, 148, 192, 68 };
    NP05J_RECT        BtnPgUp = { 42, (INT32)Ctx->ScreenH - 196, 300, 64 };
    NP05J_RECT        BtnPgDn = { 362, (INT32)Ctx->ScreenH - 196, 300, 64 };
    NP05J_RECT        BtnRun  = { 1900, (INT32)Ctx->ScreenH - 196, 458, 64 };

    if (NeedReload) {
      EntryCount = 0;
      if ((VolCount > 0U) && (CurVol < VolCount)) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
        if (!EFI_ERROR (gBS->HandleProtocol (VolHandles[CurVol], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) &&
            (Fs != NULL))
        {
          EntryCount = ReadDirectoryEntries (Fs, CurPath, Entries, MAX_ENTRIES);
        }
      }
      Scroll     = 0;
      Selected   = (EntryCount > 0U) ? 0 : -1;
      NeedReload = FALSE;
      NeedRedraw = TRUE;
    }

    if (NeedRedraw) {
      CHAR8 PathLine[160];

      Np05jUiClear (Ctx, COL_BG);
      AsciiSPrint (PathLine, sizeof (PathLine), "Volume FS%u of %u | Path: %s",
        (UINT32)CurVol, (UINT32)VolCount, CurPath);
      Np05jUiDrawHeader (Ctx, "BOOT FROM FILE (.EFI)", PathLine);

      /* Volume selector strip */
      for (UINTN V = 0; (V < VolCount) && (V < 6U); V++) {
        CHAR8 VLab[24];
        VolBtns[V] = (NP05J_RECT){ 42 + (INT32)V * 240, 148, 224, 68 };
        AsciiSPrint (VLab, sizeof (VLab), "FS%u", (UINT32)V);
        Np05jUiDrawCard (
          Ctx,
          VolBtns[V],
          (V == CurVol) ? COL_CARD_HI : COL_CARD,
          (V == CurVol) ? COL_ACCENT : COL_BORDER,
          (V == CurVol) ? 3 : 2
          );
        Np05jUiDrawTextCentered (Ctx, VolBtns[V], 2, (V == CurVol) ? COL_ACCENT : COL_TEXT, VLab);
      }

      Np05jUiDrawCard (Ctx, BtnUp, COL_CARD, COL_GOLD, 2);
      Np05jUiDrawTextCentered (Ctx, BtnUp, 2, COL_GOLD, "Parent");

      Np05jUiDrawCard (Ctx, BtnBack, COL_CARD, COL_RED, 2);
      Np05jUiDrawTextCentered (Ctx, BtnBack, 2, COL_TEXT, "Back");

      if ((VolCount == 0U) || (EntryCount == 0U)) {
        Np05jUiDrawCard (Ctx, (NP05J_RECT){ 42, 240, (INT32)Ctx->ScreenW - 84, 220 }, COL_PANEL, COL_BORDER, 2);
        Np05jUiDrawTextCentered (
          Ctx,
          (NP05J_RECT){ 42, 240, (INT32)Ctx->ScreenW - 84, 220 },
          3,
          COL_SUBTEXT,
          (VolCount == 0U) ? "No readable FAT/ESP partitions detected" : "Directory is empty"
          );
      } else {
        for (UINTN R = 0; R < VISIBLE_ROWS; R++) {
          UINTN Idx = Scroll + R;
          if (Idx >= EntryCount) {
            break;
          }
          RowRect[R] = (NP05J_RECT){ 42, 236 + (INT32)R * 112, (INT32)Ctx->ScreenW - 84, 98 };
          {
            BOOLEAN                     IsSel  = ((INTN)Idx == Selected);
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL Bdr  = IsSel ? COL_ACCENT : (Entries[Idx].IsEfi ? COL_GREEN : COL_BORDER);
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL Fill = IsSel ? COL_CARD_HI : COL_CARD;
            CHAR8                         Line[160];

            Np05jUiDrawCard (Ctx, RowRect[R], Fill, Bdr, IsSel ? 3 : 2);
            AsciiSPrint (
              Line,
              sizeof (Line),
              "%a  %s",
              Entries[Idx].IsDir ? "[DIR]" : (Entries[Idx].IsEfi ? "[EFI]" : "[FIL]"),
              Entries[Idx].Name
              );
            Np05jUiDrawText (Ctx, RowRect[R].X + 28, RowRect[R].Y + 34, 2, Entries[Idx].IsEfi ? COL_GREEN : COL_TEXT, Line);

            if (!Entries[Idx].IsDir) {
              CHAR8 SzBuf[32];
              AsciiSPrint (SzBuf, sizeof (SzBuf), "%lu B", Entries[Idx].Size);
              Np05jUiDrawText (Ctx, RowRect[R].X + RowRect[R].W - 260, RowRect[R].Y + 34, 2, COL_SUBTEXT, SzBuf);
            }
          }
        }
      }

      Np05jUiDrawCard (Ctx, BtnPgUp, COL_CARD, COL_BORDER, 2);
      Np05jUiDrawTextCentered (Ctx, BtnPgUp, 2, COL_TEXT, "< Prev Page");

      Np05jUiDrawCard (Ctx, BtnPgDn, COL_CARD, COL_BORDER, 2);
      Np05jUiDrawTextCentered (Ctx, BtnPgDn, 2, COL_TEXT, "Next Page >");

      Np05jUiDrawCard (Ctx, BtnRun, COL_CARD_HI, COL_GREEN, 2);
      Np05jUiDrawTextCentered (Ctx, BtnRun, 2, COL_GREEN, "Open / Launch Selected");

      Np05jUiDrawBannerIfAny (Ctx);
      Np05jUiPresent (Ctx);
      NeedRedraw = FALSE;
    }

    if (EFI_ERROR (Np05jUiPollInput (Ctx, 100, &Ev))) {
      continue;
    }

    if (Ev.Kind == Np05jInputBack) {
      break;
    }

    if (Ev.Kind == Np05jInputUp) {
      if (Selected > 0) {
        Selected--;
        if ((UINTN)Selected < Scroll) {
          Scroll = (UINTN)Selected;
        }
        NeedRedraw = TRUE;
      }
      continue;
    }

    if (Ev.Kind == Np05jInputDown) {
      if ((Selected >= 0) && ((UINTN)(Selected + 1) < EntryCount)) {
        Selected++;
        if ((UINTN)Selected >= (Scroll + VISIBLE_ROWS)) {
          Scroll = (UINTN)Selected - VISIBLE_ROWS + 1U;
        }
        NeedRedraw = TRUE;
      }
      continue;
    }

    if (Ev.Kind == Np05jInputLeft) {
      if (CurVol > 0U) {
        CurVol--;
        StrCpyS (CurPath, 260, L"\\");
        NeedReload = TRUE;
      }
      continue;
    }

    if (Ev.Kind == Np05jInputRight) {
      if ((CurVol + 1U) < VolCount) {
        CurVol++;
        StrCpyS (CurPath, 260, L"\\");
        NeedReload = TRUE;
      }
      continue;
    }

    BOOLEAN TriggerSelected = (Ev.Kind == Np05jInputEnter);

    if (Ev.Kind == Np05jInputTap) {
      if (Np05jRectContains (BtnBack, Ev.X, Ev.Y)) {
        break;
      }
      if (Np05jRectContains (BtnPgUp, Ev.X, Ev.Y)) {
        Scroll     = (Scroll >= VISIBLE_ROWS) ? (Scroll - VISIBLE_ROWS) : 0U;
        NeedRedraw = TRUE;
        continue;
      }
      if (Np05jRectContains (BtnPgDn, Ev.X, Ev.Y)) {
        if ((Scroll + VISIBLE_ROWS) < EntryCount) {
          Scroll += VISIBLE_ROWS;
        }
        NeedRedraw = TRUE;
        continue;
      }
      if (Np05jRectContains (BtnUp, Ev.X, Ev.Y)) {
        UINTN L = StrLen (CurPath);
        if (L > 1U) {
          L--;
          while ((L > 1U) && (CurPath[L] != L'\\')) {
            L--;
          }
          CurPath[(L == 0U) ? 1U : L] = L'\0';
          NeedReload = TRUE;
        }
        continue;
      }
      for (UINTN V = 0; (V < VolCount) && (V < 6U); V++) {
        if (Np05jRectContains (VolBtns[V], Ev.X, Ev.Y)) {
          CurVol = V;
          StrCpyS (CurPath, 260, L"\\");
          NeedReload = TRUE;
        }
      }
      if (NeedReload) {
        continue;
      }
      for (UINTN R = 0; (R < VISIBLE_ROWS) && ((Scroll + R) < EntryCount); R++) {
        if (Np05jRectContains (RowRect[R], Ev.X, Ev.Y)) {
          Selected        = (INTN)(Scroll + R);
          TriggerSelected = TRUE;
          break;
        }
      }
      if (Np05jRectContains (BtnRun, Ev.X, Ev.Y)) {
        TriggerSelected = TRUE;
      }
    }

    if (TriggerSelected && (Selected >= 0) && ((UINTN)Selected < EntryCount)) {
      NP05J_FILE_ENTRY *E = &Entries[Selected];
      if (E->IsDir) {
        if (StrCmp (E->Name, L"..") == 0) {
          UINTN L = StrLen (CurPath);
          if (L > 1U) {
            L--;
            while ((L > 1U) && (CurPath[L] != L'\\')) {
              L--;
            }
            CurPath[(L == 0U) ? 1U : L] = L'\0';
          }
        } else {
          if (StrCmp (CurPath, L"\\") != 0) {
            StrCatS (CurPath, 260, L"\\");
          }
          StrCatS (CurPath, 260, E->Name);
        }
        NeedReload = TRUE;
      } else {
        CHAR16     FullPath[360];
        EFI_STATUS RunSt;

        StrCpyS (FullPath, 360, CurPath);
        if (StrCmp (FullPath, L"\\") != 0) {
          StrCatS (FullPath, 360, L"\\");
        }
        StrCatS (FullPath, 360, E->Name);

        RunSt = Np05jLaunchEfiPathOnVolume (VolHandles[CurVol], FullPath, TRUE);
        Np05jUiSetBanner (Ctx, EFI_ERROR (RunSt) ? COL_RED : COL_ACCENT, "%s returned %r", E->Name, RunSt);
        NeedRedraw = TRUE;
      }
    }
  }

  FreePool (Entries);
  if (VolHandles != NULL) {
    FreePool (VolHandles);
  }
}
