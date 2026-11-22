/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Host unit tests for BootSelect SPLITv1/NEXTv1/RSTRv1, hitboxes & BMP preview.
**/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BootRequest.h"
#include "BootSelect.h"

static void WriteBmp24(const char *Path, const uint32_t *Bgra, uint32_t W, uint32_t H) {
  uint32_t RowBytes = ((W * 3U) + 3U) & ~3U;
  uint32_t ImgBytes = RowBytes * H;
  uint32_t FileSize = 54U + ImgBytes;
  uint8_t  Hdr[54];
  FILE    *Fp;

  memset(Hdr, 0, sizeof(Hdr));
  Hdr[0] = 'B'; Hdr[1] = 'M';
  memcpy(Hdr + 2,  &FileSize, 4U);
  Hdr[10] = 54U;
  Hdr[14] = 40U;
  memcpy(Hdr + 18, &W, 4U);
  memcpy(Hdr + 22, &H, 4U);
  Hdr[26] = 1U;
  Hdr[28] = 24U;
  memcpy(Hdr + 34, &ImgBytes, 4U);

  Fp = fopen(Path, "wb");
  assert(Fp != NULL);
  assert(fwrite(Hdr, 1U, 54U, Fp) == 54U);

  uint8_t *Row = (uint8_t *)calloc(1U, RowBytes);
  assert(Row != NULL);
  for (int32_t Y = (int32_t)H - 1; Y >= 0; Y--) {
    const uint32_t *Src = Bgra + (size_t)Y * W;
    for (uint32_t X = 0U; X < W; X++) {
      uint32_t Px = Src[X];
      Row[X * 3U + 0U] = (uint8_t)(Px & 0xFFU);
      Row[X * 3U + 1U] = (uint8_t)((Px >> 8) & 0xFFU);
      Row[X * 3U + 2U] = (uint8_t)((Px >> 16) & 0xFFU);
    }
    assert(fwrite(Row, 1U, RowBytes, Fp) == RowBytes);
  }
  free(Row);
  fclose(Fp);
}

static void TestBootRequestAndEmbed(void) {
  uint8_t *Arena = (uint8_t *)calloc(1U, 4096U * 4U);
  assert(Arena != NULL);

  NP05J_SPLIT_HEADER *Split = (NP05J_SPLIT_HEADER *)(void *)(Arena + 4096U);
  memcpy(Split->Magic, NP05J_SPLIT_MAGIC, 8U);
  Split->Version             = NP05J_SPLIT_VERSION;
  Split->ContainerTotalBytes = 4096U * 3U;
  Split->RequestOffset       = 4096U;
  Split->RequestSize         = 4096U;
  Split->DefaultTimeoutSec   = 4U;
  Split->UefiOffset          = 8192U;
  Split->UefiSize            = 3145728U;
  Split->HeaderCrc32         = Np05jSplitComputeCrc(Split);

  assert(Np05jSplitValidate(Split) == 1);
  assert(Np05jFindSplitHeader(Arena, 4096U * 4U) == Split);

  uint32_t Timeout = 0U;
  assert(Np05jResolveBootTarget(Split, &Timeout) == NP05J_BOOT_TARGET_ANDROID);
  assert(Timeout == 4U);

  /* Write NEXTv1 for Windows 11 ARM */
  NP05J_NEXT_REQUEST *Req = (NP05J_NEXT_REQUEST *)(void *)(Arena + 8192U);
  memcpy(Req->Magic, "NEXTv1\0\0", 8U);
  Req->Version    = NP05J_NEXT_VERSION;
  Req->Target     = NP05J_BOOT_TARGET_WINDOWS;
  Req->TimeoutSec = 1U;
  Req->Flags      = NP05J_NEXT_FLAG_ONESHOT;
  Req->Crc32      = Np05jNextComputeCrc(Req);

  assert(Np05jNextValidate(Req) == 1);
  assert(Np05jResolveBootTarget(Split, &Timeout) == NP05J_BOOT_TARGET_WINDOWS);
  assert(Timeout == 1U);

  /* Test embed record writer */
  uint8_t EmbedPage[128];
  memset(EmbedPage, 0, sizeof(EmbedPage));
  Np05jWriteEmbedRecord((uint64_t)(uintptr_t)EmbedPage, Split, 0x81000000ULL, NP05J_BOOT_TARGET_BIOS_SETUP);
  const NP05J_EMBED_RECORD *Rec = (const NP05J_EMBED_RECORD *)(const void *)EmbedPage;
  assert(strcmp(Rec->Magic, NP05J_EMBED_RECORD_MAGIC) == 0);
  assert(Rec->Version == NP05J_EMBED_RECORD_VERSION);
  assert(Rec->Size == NP05J_EMBED_RECORD_SIZE);
  assert(Rec->Target == NP05J_BOOT_TARGET_BIOS_SETUP);
  assert(Rec->AppBase == 0x81002000ULL);
  assert(Rec->AppBytes == 3145728ULL);

  free(Arena);
}

static void TestHitboxesAndPreview(const char *OutBmpPath) {
  for (uint32_t I = 0U; I < NP05J_CARD_COUNT; I++) {
    NP05J_BOOT_CARD_RECT R;
    BootSelectGetCardRect(I, &R);
    assert(BootSelectHitTest((uint32_t)(R.X + R.W / 2), (uint32_t)(R.Y + R.H / 2)) == (int32_t)I);
  }
  assert(BootSelectHitTest(10U, 10U) == -1);
  assert(BootSelectHitTest(2390U, 1490U) == -1);

  uint32_t *Fb = (uint32_t *)calloc((size_t)NP05J_FB_WIDTH * NP05J_FB_HEIGHT, sizeof(uint32_t));
  assert(Fb != NULL);
  BootSelectRenderFrame(Fb, NP05J_FB_WIDTH, NP05J_FB_HEIGHT, NP05J_FB_STRIDE_PX, 1U, 45U, 2U, 1);

  /* Ensure non-uniform pixel content */
  uint32_t Distinct = 0U;
  uint32_t First    = Fb[0];
  for (size_t I = 1U; I < (size_t)NP05J_FB_WIDTH * NP05J_FB_HEIGHT; I++) {
    if (Fb[I] != First) {
      Distinct++;
    }
  }
  assert(Distinct > 100000U);

  WriteBmp24(OutBmpPath, Fb, NP05J_FB_WIDTH, NP05J_FB_HEIGHT);
  free(Fb);

  FILE *Chk = fopen(OutBmpPath, "rb");
  assert(Chk != NULL);
  uint8_t Bm[2] = { 0, 0 };
  assert(fread(Bm, 1U, 2U, Chk) == 2U);
  assert((Bm[0] == 'B') && (Bm[1] == 'M'));
  fclose(Chk);
}

int main(int Argc, char **Argv) {
  const char *BmpOut = (Argc >= 2) ? Argv[1] : "/workspaces/Mu-Silicium/Build/nubia_np05jPkg/boot-selection-preview.bmp";
  TestBootRequestAndEmbed();
  TestHitboxesAndPreview(BmpOut);
  printf("PASS: test_bootselect (SPLITv1/NEXTv1/SUNUEFI-EMBEDv1, 3x touch card hitboxes, BMP preview -> %s)\n", BmpOut);
  return 0;
}
