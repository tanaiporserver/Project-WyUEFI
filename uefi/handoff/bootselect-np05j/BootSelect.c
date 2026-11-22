/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Main loop for NP05J pre-UEFI BootSelect (Touch + SPMI keys + NEXTv1 + SPLITv1).
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#include "BootSelect.h"
#include "EarlyKeys.h"
#include "EarlyTouch.h"
#include "EarlyTrace.h"

extern void EarlyDelayUs(uint64_t Us);
extern void EarlyFlushRange(uint64_t Addr, uint64_t Len);

static void FlushFb(uint32_t *Fb) {
  EarlyFlushRange((uint64_t)(uintptr_t)Fb, (uint64_t)NP05J_FB_STRIDE_PX * NP05J_FB_HEIGHT * 4ULL);
}

static void MemCopySimple(void *Dst, const void *Src, uint64_t Len) {
  uint8_t       *D = (uint8_t *)Dst;
  const uint8_t *S = (const uint8_t *)Src;
  for (uint64_t I = 0ULL; I < Len; I++) {
    D[I] = S[I];
  }
}

uint64_t BootSelectMain(uint64_t LoadBase, uint64_t ImageEnd, uint64_t DtbPtr) {
  const NP05J_SPLIT_HEADER *Split = 0;
  uint32_t                  Target;
  uint32_t                  TimeoutSec = 3U;
  uint32_t                  SelectedCard = 0U;
  uint32_t                 *Fb = (uint32_t *)(uintptr_t)NP05J_FB_BASE;
  int                       TouchOk;
  int                       WasDown = 0;
  uint32_t                  PrevKeys;

  (void)DtbPtr;

  EarlyTraceInit();
  EarlyTraceLog("NP05J BootSelect start");

  Split  = Np05jFindSplitHeader((const void *)(uintptr_t)ImageEnd, 512U * 1024U);
  Target = Np05jResolveBootTarget(Split, &TimeoutSec);

  if ((Target == NP05J_BOOT_TARGET_UEFI_MENU) ||
      (Target == NP05J_BOOT_TARGET_LINUX) ||
      (Target == NP05J_BOOT_TARGET_WINDOWS))
  {
    SelectedCard = 1U;
  } else if (Target == NP05J_BOOT_TARGET_BIOS_SETUP) {
    SelectedCard = 2U;
  } else {
    SelectedCard = 0U;
  }

  /* Fast-path if NEXTv1 set 0s timeout */
  if (Split && (TimeoutSec == 0U)) {
    goto Launch;
  }

  TouchOk  = EarlyTouchInit();
  PrevKeys = EarlyKeysPoll();

  {
    uint32_t TotalTicks = ((TimeoutSec == 0U) ? 1U : TimeoutSec) * 40U; /* 25 ms per tick */
    uint32_t Tick       = 0U;
    uint32_t LastSec    = 0xFFFFFFFFU;

    BootSelectRenderFrame(Fb, NP05J_FB_WIDTH, NP05J_FB_HEIGHT, NP05J_FB_STRIDE_PX,
      SelectedCard, 0U, TimeoutSec, TouchOk);
    FlushFb(Fb);

    while (Tick < TotalTicks) {
      uint32_t RemSec = ((TotalTicks - Tick) + 39U) / 40U;
      uint32_t Pct    = (Tick * 100U) / TotalTicks;
      int      Redraw = 0;

      if (RemSec != LastSec) {
        LastSec = RemSec;
        Redraw  = 1;
      }

      if (TouchOk) {
        NP05J_EARLY_TOUCH_EVENT Te;
        if (EarlyTouchPoll(&Te)) {
          if (Te.TouchDown) {
            int32_t Hit = BootSelectHitTest(Te.DispX, Te.DispY);
            if (Hit >= 0) {
              SelectedCard = (uint32_t)Hit;
              Redraw       = 1;
            }
            WasDown = 1;
          } else if (WasDown) {
            int32_t Hit = BootSelectHitTest(Te.DispX, Te.DispY);
            WasDown = 0;
            if (Hit >= 0) {
              SelectedCard = (uint32_t)Hit;
              break;
            }
          }
        }
      }

      {
        uint32_t Keys = EarlyKeysPoll();
        uint32_t Edge = Keys & ~PrevKeys;
        PrevKeys      = Keys;

        if ((Edge & NP05J_KEY_VOL_UP) != 0U) {
          SelectedCard = (SelectedCard == 0U) ? (NP05J_CARD_COUNT - 1U) : (SelectedCard - 1U);
          Tick         = 0U; /* Reset timeout when user actively navigates */
          Redraw       = 1;
        } else if ((Edge & NP05J_KEY_VOL_DOWN) != 0U) {
          SelectedCard = (SelectedCard + 1U) % NP05J_CARD_COUNT;
          Tick         = 0U;
          Redraw       = 1;
        } else if ((Edge & NP05J_KEY_POWER) != 0U) {
          break;
        }
      }

      if (Redraw || ((Tick % 4U) == 0U)) {
        BootSelectRenderFrame(Fb, NP05J_FB_WIDTH, NP05J_FB_HEIGHT, NP05J_FB_STRIDE_PX,
          SelectedCard, Pct, RemSec, TouchOk);
        FlushFb(Fb);
      }

      EarlyDelayUs(25000U);
      Tick++;
    }
  }

  EarlyTouchShutdown();

Launch:
  {
    NP05J_BOOT_CARD_RECT R;
    uint32_t FinalTarget = Target;

    BootSelectGetCardRect(SelectedCard, &R);
    if ((SelectedCard == 0U) ||
        !((Target == NP05J_BOOT_TARGET_WINDOWS) || (Target == NP05J_BOOT_TARGET_LINUX) || (Target == NP05J_BOOT_TARGET_BIOS_SETUP)))
    {
      FinalTarget = R.Target;
    } else if (SelectedCard == 2U) {
      FinalTarget = NP05J_BOOT_TARGET_BIOS_SETUP;
    }

    Np05jWriteEmbedRecord(NP05J_EMBED_RECORD_ADDR, Split, (uint64_t)(uintptr_t)Split, FinalTarget);

    if (!Split) {
      EarlyTraceLog("BootSelect: no SPLITv1 container found, halting");
      for (;;) {
      }
    }

    if (FinalTarget == NP05J_BOOT_TARGET_ANDROID) {
      uint64_t ShimSrc  = (uint64_t)(uintptr_t)Split + Split->ShimOffset;
      uint64_t ShimDst  = 0xA7FFE000ULL;
      uint64_t KernSrc  = (uint64_t)(uintptr_t)Split + Split->AndroidKernelOffset;
      typedef void (*RELOC_FN)(uint64_t Dtb, uint64_t Dst, uint64_t Src, uint64_t Size);
      RELOC_FN Reloc;

      EarlyTraceLog("BootSelect: Launching Android kernel via RelocShim");
      MemCopySimple((void *)(uintptr_t)ShimDst, (const void *)(uintptr_t)ShimSrc, Split->ShimSize);
      EarlyFlushRange(ShimDst, Split->ShimSize);
      Reloc = (RELOC_FN)(uintptr_t)ShimDst;
      Reloc(DtbPtr, LoadBase, KernSrc, Split->AndroidKernelSize);
      for (;;) {
      }
    }

    /* UEFI payload (Mu-nubia_np05j.img = 64KiB InPlace BootShim + SILICIUM_UEFI.fd):
       InPlace.S expects _Payload == FD_BASE (0xA8010000), so image base is 0xA8000000. */
    {
      uint64_t UefiSrc  = (uint64_t)(uintptr_t)Split + Split->UefiOffset;
      uint64_t UefiDst  = 0xA8000000ULL;

      EarlyTraceLog("BootSelect: Copying Mu-Silicium UEFI image to 0xA8000000");
      MemCopySimple((void *)(uintptr_t)UefiDst, (const void *)(uintptr_t)UefiSrc, Split->UefiSize);
      EarlyFlushRange(UefiDst, Split->UefiSize);
      return UefiDst;
    }
  }
}
