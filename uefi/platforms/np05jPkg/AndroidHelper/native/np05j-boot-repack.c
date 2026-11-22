/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Lossless Android Boot Image v4 inspect / SPLITv1 repack / NEXTv1 request /
  RSTRv1 restore utility for nubia NP05J (SM8750P).

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  android/native/piano-boot-repack.c.
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "sha256.h"
#include "../../Include/Np05jBootHandoff.h"
#include "../../Tools/BootSelect/BootRequest.h"

#define BOOT_MAGIC        "ANDROID!"
#define BOOT_MAGIC_SIZE   8U
#define BOOT_PAGE_SIZE    4096U
#define BOOT_CMDLINE_SIZE 1536U

#define ALIGN_UP(X, A)    (((X) + ((A) - 1U)) & ~((A) - 1U))

#pragma pack(push, 1)
typedef struct {
  uint8_t  Magic[BOOT_MAGIC_SIZE];
  uint32_t KernelSize;
  uint32_t RamdiskSize;
  uint32_t OsVersion;
  uint32_t HeaderSize;
  uint32_t Reserved[4];
  uint32_t HeaderVersion;
  uint8_t  Cmdline[BOOT_CMDLINE_SIZE];
  uint32_t SignatureSize;
} ANDROID_BOOT_HDR_V4;
#pragma pack(pop)

static uint8_t *ReadWholeFile(const char *Path, size_t *OutLen) {
  FILE   *Fp = fopen(Path, "rb");
  long    Sz;
  uint8_t *Buf;

  if (!Fp) {
    perror(Path);
    return NULL;
  }
  if ((fseek(Fp, 0, SEEK_END) != 0) || ((Sz = ftell(Fp)) < 0)) {
    fclose(Fp);
    return NULL;
  }
  rewind(Fp);

  Buf = (uint8_t *)malloc((size_t)Sz);
  if (!Buf) {
    fclose(Fp);
    return NULL;
  }
  if ((Sz > 0) && (fread(Buf, 1U, (size_t)Sz, Fp) != (size_t)Sz)) {
    free(Buf);
    fclose(Fp);
    return NULL;
  }
  fclose(Fp);
  *OutLen = (size_t)Sz;
  return Buf;
}

static int WriteWholeFile(const char *Path, const void *Data, size_t Len) {
  FILE *Fp = fopen(Path, "wb");
  if (!Fp) {
    perror(Path);
    return -1;
  }
  if ((Len > 0U) && (fwrite(Data, 1U, Len, Fp) != Len)) {
    perror("fwrite");
    fclose(Fp);
    return -1;
  }
  if (fclose(Fp) != 0) {
    perror("fclose");
    return -1;
  }
  return 0;
}

static void PrintHex(const uint8_t *D, size_t N) {
  for (size_t I = 0; I < N; I++) {
    printf("%02x", D[I]);
  }
}

static const char *TargetName(uint32_t Target) {
  switch (Target) {
    case NP05J_BOOT_TARGET_ANDROID:    return "android";
    case NP05J_BOOT_TARGET_UEFI_MENU:  return "uefi";
    case NP05J_BOOT_TARGET_LINUX:      return "linux";
    case NP05J_BOOT_TARGET_BIOS_SETUP: return "bios";
    case NP05J_BOOT_TARGET_WINDOWS:    return "windows";
    default:                           return "unknown";
  }
}

static int ParseTarget(const char *Str, uint32_t *Out) {
  if ((strcmp(Str, "android") == 0) || (strcmp(Str, "0") == 0)) {
    *Out = NP05J_BOOT_TARGET_ANDROID;
  } else if ((strcmp(Str, "uefi") == 0) || (strcmp(Str, "menu") == 0) || (strcmp(Str, "1") == 0)) {
    *Out = NP05J_BOOT_TARGET_UEFI_MENU;
  } else if ((strcmp(Str, "linux") == 0) || (strcmp(Str, "2") == 0)) {
    *Out = NP05J_BOOT_TARGET_LINUX;
  } else if ((strcmp(Str, "bios") == 0) || (strcmp(Str, "setup") == 0) || (strcmp(Str, "3") == 0)) {
    *Out = NP05J_BOOT_TARGET_BIOS_SETUP;
  } else if ((strcmp(Str, "windows") == 0) || (strcmp(Str, "win11") == 0) || (strcmp(Str, "4") == 0)) {
    *Out = NP05J_BOOT_TARGET_WINDOWS;
  } else {
    return -1;
  }
  return 0;
}

static int FindSplitInBootImage(const uint8_t *Img, size_t ImgLen, size_t *SplitOffOut) {
  if (ImgLen < (BOOT_PAGE_SIZE * 2U)) {
    return -1;
  }
  size_t MaxScan = ImgLen - BOOT_PAGE_SIZE;
  if (MaxScan > (512U * 1024U)) {
    MaxScan = 512U * 1024U;
  }
  for (size_t Off = BOOT_PAGE_SIZE; Off <= MaxScan; Off += BOOT_PAGE_SIZE) {
    const NP05J_SPLIT_HEADER *Hdr = (const NP05J_SPLIT_HEADER *)(const void *)(Img + Off);
    if (Np05jSplitValidate(Hdr)) {
      *SplitOffOut = Off;
      return 0;
    }
  }
  return -1;
}

static int CmdInspect(const char *BootPath) {
  size_t                     Len = 0;
  uint8_t                   *Img = ReadWholeFile(BootPath, &Len);
  const ANDROID_BOOT_HDR_V4 *Hdr;
  size_t                     SplitOff = 0;
  uint8_t                    Digest[32];

  if (!Img) {
    return 1;
  }
  if ((Len < BOOT_PAGE_SIZE) || (memcmp(Img, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0)) {
    fprintf(stderr, "Error: %s is not a valid ANDROID! boot image\n", BootPath);
    free(Img);
    return 1;
  }

  Hdr = (const ANDROID_BOOT_HDR_V4 *)(const void *)Img;
  Np05jSha256Buffer(Img, Len, Digest);

  printf("file=%s\n", BootPath);
  printf("size=%zu\n", Len);
  printf("header_version=%u\n", Hdr->HeaderVersion);
  printf("header_size=%u\n", Hdr->HeaderSize);
  printf("kernel_size=%u\n", Hdr->KernelSize);
  printf("ramdisk_size=%u\n", Hdr->RamdiskSize);
  printf("os_version=0x%08x\n", Hdr->OsVersion);
  printf("signature_size=%u\n", Hdr->SignatureSize);
  printf("sha256="); PrintHex(Digest, 32U); printf("\n");

  if (FindSplitInBootImage(Img, Len, &SplitOff) == 0) {
    const NP05J_SPLIT_HEADER *Split = (const NP05J_SPLIT_HEADER *)(const void *)(Img + SplitOff);
    printf("sunuefi_combined=1\n");
    printf("split_offset=%zu\n", SplitOff);
    printf("android_kernel_bytes=%llu\n", (unsigned long long)Split->AndroidKernelSize);
    printf("uefi_bytes=%llu\n", (unsigned long long)Split->UefiSize);
    printf("default_timeout_sec=%u\n", Split->DefaultTimeoutSec);

    if ((SplitOff + Split->RequestOffset + sizeof(NP05J_NEXT_REQUEST)) <= Len) {
      const NP05J_NEXT_REQUEST *Req = (const NP05J_NEXT_REQUEST *)(const void *)(Img + SplitOff + Split->RequestOffset);
      if (Np05jNextValidate(Req)) {
        printf("next_boot=%s (%u) timeout=%u flags=0x%x seq=%llu\n",
          TargetName(Req->Target), Req->Target, Req->TimeoutSec, Req->Flags, (unsigned long long)Req->Sequence);
      } else {
        printf("next_boot=none\n");
      }
    }
  } else {
    printf("sunuefi_combined=0 (stock/raw)\n");
  }

  free(Img);
  return 0;
}

static int CmdRepack(int Argc, char **Argv) {
  const char *StockPath    = NULL;
  const char *SelectorPath = NULL;
  const char *ShimPath     = NULL;
  const char *UefiPath     = NULL;
  const char *OutPath      = NULL;
  uint32_t    TimeoutSec   = 3U;

  for (int I = 2; I < Argc; I++) {
    if ((strcmp(Argv[I], "--stock") == 0) && (I + 1 < Argc)) {
      StockPath = Argv[++I];
    } else if ((strcmp(Argv[I], "--selector") == 0) && (I + 1 < Argc)) {
      SelectorPath = Argv[++I];
    } else if ((strcmp(Argv[I], "--shim") == 0) && (I + 1 < Argc)) {
      ShimPath = Argv[++I];
    } else if ((strcmp(Argv[I], "--uefi") == 0) && (I + 1 < Argc)) {
      UefiPath = Argv[++I];
    } else if ((strcmp(Argv[I], "--out") == 0) && (I + 1 < Argc)) {
      OutPath = Argv[++I];
    } else if ((strcmp(Argv[I], "--timeout") == 0) && (I + 1 < Argc)) {
      TimeoutSec = (uint32_t)strtoul(Argv[++I], NULL, 0);
    }
  }

  if (!StockPath || !SelectorPath || !ShimPath || !UefiPath || !OutPath) {
    fprintf(stderr, "Usage: repack --stock <boot.img> --selector <bootselect.bin> --shim <reloc-shim.bin> --uefi <uefi.img> --out <out.img> [--timeout N]\n");
    return 1;
  }

  size_t   StockLen = 0, SelLen = 0, ShimLen = 0, UefiLen = 0;
  uint8_t *Stock    = ReadWholeFile(StockPath, &StockLen);
  uint8_t *Sel      = ReadWholeFile(SelectorPath, &SelLen);
  uint8_t *Shim     = ReadWholeFile(ShimPath, &ShimLen);
  uint8_t *Uefi     = ReadWholeFile(UefiPath, &UefiLen);

  if (!Stock || !Sel || !Shim || !Uefi) {
    free(Stock); free(Sel); free(Shim); free(Uefi);
    return 1;
  }

  if ((StockLen < BOOT_PAGE_SIZE) || (memcmp(Stock, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0)) {
    fprintf(stderr, "Error: Stock image invalid\n");
    free(Stock); free(Sel); free(Shim); free(Uefi);
    return 1;
  }

  size_t ExistingSplit = 0;
  if (FindSplitInBootImage(Stock, StockLen, &ExistingSplit) == 0) {
    fprintf(stderr, "Error: Stock image already contains SPLITv1 container at offset %zu\n", ExistingSplit);
    free(Stock); free(Sel); free(Shim); free(Uefi);
    return 1;
  }

  const ANDROID_BOOT_HDR_V4 *OrigHdr = (const ANDROID_BOOT_HDR_V4 *)(const void *)Stock;
  if (OrigHdr->HeaderVersion < 3U) {
    fprintf(stderr, "Error: Expected Boot Image header_version >= 3 (got %u)\n", OrigHdr->HeaderVersion);
    free(Stock); free(Sel); free(Shim); free(Uefi);
    return 1;
  }

  uint32_t OrigKernelSz  = OrigHdr->KernelSize;
  uint32_t OrigRamdiskSz = OrigHdr->RamdiskSize;
  size_t   KernelOff     = BOOT_PAGE_SIZE;
  size_t   KernelEnd     = KernelOff + OrigKernelSz;

  if (KernelEnd > StockLen) {
    fprintf(stderr, "Error: Stock boot image truncated (kernel ends at %zu > %zu)\n", KernelEnd, StockLen);
    free(Stock); free(Sel); free(Shim); free(Uefi);
    return 1;
  }

  /* Lossless restore slices:
     Prefix = [0 .. BOOT_PAGE_SIZE) (exact original v4 header page)
     Suffix = [KernelOff + OrigKernelSz .. StockLen) (kernel align pad + ramdisk + any tail/AVB) */
  uint32_t PrefixBytes = BOOT_PAGE_SIZE;
  uint32_t SuffixBytes = (uint32_t)(StockLen - KernelEnd);

  size_t SelPadded     = ALIGN_UP(SelLen, BOOT_PAGE_SIZE);
  size_t SplitHdrSz    = BOOT_PAGE_SIZE;
  size_t ReqPageSz     = BOOT_PAGE_SIZE;
  size_t RstrRawSz     = sizeof(NP05J_RESTORE_HEADER) + PrefixBytes + SuffixBytes;
  size_t RstrPadded    = ALIGN_UP(RstrRawSz, BOOT_PAGE_SIZE);
  size_t ShimPadded    = ALIGN_UP(ShimLen, BOOT_PAGE_SIZE);
  size_t KernPadded    = ALIGN_UP(OrigKernelSz, BOOT_PAGE_SIZE);
  size_t UefiPadded    = ALIGN_UP(UefiLen, BOOT_PAGE_SIZE);

  size_t SplitOffInKern = SelPadded;
  size_t ReqOffInSplit  = SplitHdrSz;
  size_t RstrOffInSplit = ReqOffInSplit + ReqPageSz;
  size_t ShimOffInSplit = RstrOffInSplit + RstrPadded;
  size_t KernOffInSplit = ShimOffInSplit + ShimPadded;
  size_t UefiOffInSplit = KernOffInSplit + KernPadded;
  size_t ContainerTotal = UefiOffInSplit + UefiPadded;

  size_t CombinedKernelSz = SelPadded + ContainerTotal;
  size_t RamdiskPadSz     = ALIGN_UP((size_t)OrigRamdiskSz, BOOT_PAGE_SIZE);
  size_t TotalOutLen      = BOOT_PAGE_SIZE + CombinedKernelSz + RamdiskPadSz;
  if (TotalOutLen < StockLen) {
    TotalOutLen = StockLen;
  }
  TotalOutLen = ALIGN_UP(TotalOutLen, BOOT_PAGE_SIZE);

  uint8_t *Out = (uint8_t *)calloc(1U, TotalOutLen);
  if (!Out) {
    free(Stock); free(Sel); free(Shim); free(Uefi);
    return 1;
  }

  /* Copy v4 header page and update kernel size */
  memcpy(Out, Stock, BOOT_PAGE_SIZE);
  {
    ANDROID_BOOT_HDR_V4 *NewHdr = (ANDROID_BOOT_HDR_V4 *)(void *)Out;
    NewHdr->KernelSize    = (uint32_t)CombinedKernelSz;
    NewHdr->SignatureSize = 0U;
  }

  /* Copy BootSelect + patch Linux Image header image_size (@ 0x10) so ABL maps entire payload */
  memcpy(Out + BOOT_PAGE_SIZE, Sel, SelLen);
  {
    uint64_t ImgSpan = (uint64_t)CombinedKernelSz;
    memcpy(Out + BOOT_PAGE_SIZE + 0x10U, &ImgSpan, sizeof(ImgSpan));
  }

  /* Build SPLITv1 header */
  uint8_t *SplitPtr = Out + BOOT_PAGE_SIZE + SplitOffInKern;
  {
    NP05J_SPLIT_HEADER Split;
    memset(&Split, 0, sizeof(Split));
    memcpy(Split.Magic, NP05J_SPLIT_MAGIC, 8U);
    Split.Version             = NP05J_SPLIT_VERSION;
    Split.ContainerTotalBytes = ContainerTotal;
    Split.RequestOffset       = ReqOffInSplit;
    Split.RequestSize         = ReqPageSz;
    Split.RestoreOffset       = RstrOffInSplit;
    Split.RestoreSize         = RstrPadded;
    Split.ShimOffset          = ShimOffInSplit;
    Split.ShimSize            = ShimLen;
    Split.AndroidKernelOffset = KernOffInSplit;
    Split.AndroidKernelSize   = OrigKernelSz;
    Split.UefiOffset          = UefiOffInSplit;
    Split.UefiSize            = UefiLen;
    Split.DefaultTimeoutSec   = TimeoutSec;
    Split.AndroidKernelCrc32  = Np05jCrc32(Stock + KernelOff, OrigKernelSz);
    Split.UefiCrc32           = Np05jCrc32(Uefi, (uint32_t)UefiLen);
    Np05jSha256Buffer(Stock, StockLen, Split.StockBootSha256);
    Split.HeaderCrc32         = Np05jSplitComputeCrc(&Split);
    memcpy(SplitPtr, &Split, sizeof(Split));
  }

  /* Write RSTRv1 restore blob */
  {
    uint8_t             *RstrPtr = SplitPtr + RstrOffInSplit;
    NP05J_RESTORE_HEADER Rh;
    memset(&Rh, 0, sizeof(Rh));
    memcpy(Rh.Magic, "RSTRv1\0\0", 8U);
    Rh.Version           = NP05J_RESTORE_VERSION;
    Rh.OriginalBootBytes = (uint32_t)StockLen;
    Rh.PrefixBytes       = PrefixBytes;
    Rh.SuffixBytes       = SuffixBytes;
    Rh.KernelOffset      = (uint32_t)KernelOff;
    Rh.KernelSize        = OrigKernelSz;
    Np05jSha256Buffer(Stock, StockLen, Rh.OriginalBootSha256);
    Rh.HeaderCrc32       = Np05jRestoreComputeCrc(&Rh);

    memcpy(RstrPtr, &Rh, sizeof(Rh));
    memcpy(RstrPtr + sizeof(Rh), Stock, PrefixBytes);
    if (SuffixBytes > 0U) {
      memcpy(RstrPtr + sizeof(Rh) + PrefixBytes, Stock + KernelEnd, SuffixBytes);
    }
  }

  /* Copy RelocShim, Stock Kernel, and UEFI image */
  memcpy(SplitPtr + ShimOffInSplit, Shim, ShimLen);
  memcpy(SplitPtr + KernOffInSplit, Stock + KernelOff, OrigKernelSz);
  memcpy(SplitPtr + UefiOffInSplit, Uefi, UefiLen);

  /* Preserve ramdisk payload cleanly right after CombinedKernelSz if present */
  if (OrigRamdiskSz > 0U) {
    size_t OrigRamdiskOff = BOOT_PAGE_SIZE + ALIGN_UP((size_t)OrigKernelSz, BOOT_PAGE_SIZE);
    if ((OrigRamdiskOff + OrigRamdiskSz) <= StockLen) {
      memcpy(Out + BOOT_PAGE_SIZE + CombinedKernelSz, Stock + OrigRamdiskOff, OrigRamdiskSz);
    }
  }

  int Rc = WriteWholeFile(OutPath, Out, TotalOutLen);
  if (Rc == 0) {
    uint8_t Digest[32];
    Np05jSha256Buffer(Out, TotalOutLen, Digest);
    printf("Repacked %s (%zu bytes) + UEFI (%zu bytes) -> %s (%zu bytes, sha256=",
      StockPath, StockLen, UefiLen, OutPath, TotalOutLen);
    PrintHex(Digest, 8U);
    printf("...)\n");
  }

  free(Out); free(Stock); free(Sel); free(Shim); free(Uefi);
  return (Rc == 0) ? 0 : 1;
}

static int CmdRequest(const char *BootPath, const char *TargetStr, uint32_t TimeoutSec, int ClearMode) {
  FILE     *Fp = fopen(BootPath, "r+b");
  uint8_t   ScanBuf[512U * 1024U];
  size_t    ReadLen;
  size_t    SplitOff = 0;

  if (!Fp) {
    perror(BootPath);
    return 1;
  }

  memset(ScanBuf, 0, sizeof(ScanBuf));
  ReadLen = fread(ScanBuf, 1U, sizeof(ScanBuf), Fp);
  if (FindSplitInBootImage(ScanBuf, ReadLen, &SplitOff) != 0) {
    fprintf(stderr, "Error: %s does not contain a valid SPLITv1 SunUEFI container\n", BootPath);
    fclose(Fp);
    return 1;
  }

  const NP05J_SPLIT_HEADER *Split = (const NP05J_SPLIT_HEADER *)(const void *)(ScanBuf + SplitOff);
  long ReqFileOff = (long)(SplitOff + Split->RequestOffset);
  uint8_t Page[BOOT_PAGE_SIZE];
  memset(Page, 0, sizeof(Page));

  if (!ClearMode) {
    uint32_t          Target = 0U;
    NP05J_NEXT_REQUEST Req;

    if (ParseTarget(TargetStr, &Target) != 0) {
      fprintf(stderr, "Error: Unknown target '%s'\n", TargetStr);
      fclose(Fp);
      return 1;
    }

    memset(&Req, 0, sizeof(Req));
    memcpy(Req.Magic, "NEXTv1\0\0", 8U);
    Req.Version    = NP05J_NEXT_VERSION;
    Req.Target     = Target;
    Req.TimeoutSec = TimeoutSec;
    Req.Flags      = NP05J_NEXT_FLAG_ONESHOT;
    Req.Sequence   = 1ULL;
    Req.Crc32      = Np05jNextComputeCrc(&Req);
    memcpy(Page, &Req, sizeof(Req));
  }

  if ((fseek(Fp, ReqFileOff, SEEK_SET) != 0) ||
      (fwrite(Page, 1U, sizeof(Page), Fp) != sizeof(Page)) ||
      (fflush(Fp) != 0))
  {
    perror("write NEXTv1");
    fclose(Fp);
    return 1;
  }

  fclose(Fp);
  if (ClearMode) {
    printf("Cleared NEXTv1 request in %s (offset %ld)\n", BootPath, ReqFileOff);
  } else {
    printf("Set NEXTv1 target=%s timeout=%us in %s (offset %ld)\n", TargetStr, TimeoutSec, BootPath, ReqFileOff);
  }
  return 0;
}

static int CmdRestore(const char *CombinedPath, const char *OutPath) {
  size_t   Len = 0;
  uint8_t *Img = ReadWholeFile(CombinedPath, &Len);
  size_t   SplitOff = 0;

  if (!Img) {
    return 1;
  }
  if (FindSplitInBootImage(Img, Len, &SplitOff) != 0) {
    fprintf(stderr, "Error: %s is not a SunUEFI SPLITv1 boot image\n", CombinedPath);
    free(Img);
    return 1;
  }

  const NP05J_SPLIT_HEADER *Split = (const NP05J_SPLIT_HEADER *)(const void *)(Img + SplitOff);
  size_t RstrOff = SplitOff + (size_t)Split->RestoreOffset;
  size_t KernOff = SplitOff + (size_t)Split->AndroidKernelOffset;

  if (((RstrOff + sizeof(NP05J_RESTORE_HEADER)) > Len) ||
      ((KernOff + (size_t)Split->AndroidKernelSize) > Len))
  {
    fprintf(stderr, "Error: Corrupted SPLITv1 bounds\n");
    free(Img);
    return 1;
  }

  const NP05J_RESTORE_HEADER *Rh = (const NP05J_RESTORE_HEADER *)(const void *)(Img + RstrOff);
  if (!Np05jRestoreValidate(Rh)) {
    fprintf(stderr, "Error: RSTRv1 restore header CRC mismatch\n");
    free(Img);
    return 1;
  }

  if (((size_t)Rh->PrefixBytes + (size_t)Rh->KernelSize + (size_t)Rh->SuffixBytes) != (size_t)Rh->OriginalBootBytes) {
    fprintf(stderr, "Error: RSTRv1 byte accounting mismatch\n");
    free(Img);
    return 1;
  }

  const uint8_t *Prefix = Img + RstrOff + sizeof(*Rh);
  const uint8_t *Suffix = Prefix + Rh->PrefixBytes;
  const uint8_t *Kernel = Img + KernOff;

  uint8_t *Out = (uint8_t *)malloc(Rh->OriginalBootBytes);
  if (!Out) {
    free(Img);
    return 1;
  }

  memcpy(Out, Prefix, Rh->PrefixBytes);
  memcpy(Out + Rh->KernelOffset, Kernel, Rh->KernelSize);
  if (Rh->SuffixBytes > 0U) {
    memcpy(Out + Rh->KernelOffset + Rh->KernelSize, Suffix, Rh->SuffixBytes);
  }

  uint8_t ActualSha[32];
  Np05jSha256Buffer(Out, Rh->OriginalBootBytes, ActualSha);
  if (memcmp(ActualSha, Rh->OriginalBootSha256, 32U) != 0) {
    fprintf(stderr, "Error: Restored image SHA-256 verification FAILED!\n");
    free(Out);
    free(Img);
    return 1;
  }

  int Rc = WriteWholeFile(OutPath, Out, Rh->OriginalBootBytes);
  if (Rc == 0) {
    printf("Restored byte-for-byte stock boot image -> %s (%u bytes, sha256=", OutPath, Rh->OriginalBootBytes);
    PrintHex(ActualSha, 32U);
    printf(")\n");
  }

  free(Out);
  free(Img);
  return (Rc == 0) ? 0 : 1;
}

int main(int Argc, char **Argv) {
  if (Argc < 2) {
    fprintf(stderr,
      "Usage:\n"
      "  %s inspect <boot.img>\n"
      "  %s repack --stock <in.img> --selector <bootselect.bin> --shim <reloc-shim.bin> --uefi <uefi.img> --out <out.img> [--timeout N]\n"
      "  %s request <combined.img> <android|uefi|linux|bios|windows> [timeout_sec]\n"
      "  %s clear-request <combined.img>\n"
      "  %s restore <combined.img> <restored.img>\n",
      Argv[0], Argv[0], Argv[0], Argv[0], Argv[0]);
    return 1;
  }

  if ((strcmp(Argv[1], "inspect") == 0) && (Argc >= 3)) {
    return CmdInspect(Argv[2]);
  }
  if (strcmp(Argv[1], "repack") == 0) {
    return CmdRepack(Argc, Argv);
  }
  if ((strcmp(Argv[1], "request") == 0) && (Argc >= 4)) {
    uint32_t To = (Argc >= 5) ? (uint32_t)strtoul(Argv[4], NULL, 0) : 0U;
    return CmdRequest(Argv[2], Argv[3], To, 0);
  }
  if ((strcmp(Argv[1], "clear-request") == 0) && (Argc >= 3)) {
    return CmdRequest(Argv[2], "", 0U, 1);
  }
  if ((strcmp(Argv[1], "restore") == 0) && (Argc >= 4)) {
    return CmdRestore(Argv[2], Argv[3]);
  }

  fprintf(stderr, "Invalid arguments for command '%s'\n", Argv[1]);
  return 1;
}
