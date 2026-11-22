/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  SPLITv1 / NEXTv1 / RSTRv1 validation and handoff logic for nubia NP05J.
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#include "BootRequest.h"

static void MemZero(void *Dst, uint32_t Len) {
  uint8_t *P = (uint8_t *)Dst;
  for (uint32_t I = 0; I < Len; I++) {
    P[I] = 0U;
  }
}

static void MemCopy(void *Dst, const void *Src, uint32_t Len) {
  uint8_t       *D = (uint8_t *)Dst;
  const uint8_t *S = (const uint8_t *)Src;
  for (uint32_t I = 0; I < Len; I++) {
    D[I] = S[I];
  }
}

static int MagicEquals(const char *A, const char *B, uint32_t Len) {
  for (uint32_t I = 0; I < Len; I++) {
    if (A[I] != B[I]) {
      return 0;
    }
  }
  return 1;
}

uint32_t Np05jCrc32(const void *Data, uint32_t Len) {
  const uint8_t *P   = (const uint8_t *)Data;
  uint32_t       Crc = 0xFFFFFFFFU;

  for (uint32_t I = 0; I < Len; I++) {
    Crc ^= (uint32_t)P[I];
    for (uint32_t Bit = 0; Bit < 8U; Bit++) {
      uint32_t Mask = (uint32_t)(-(int32_t)(Crc & 1U));
      Crc = (Crc >> 1U) ^ (0xEDB88320U & Mask);
    }
  }
  return ~Crc;
}

uint32_t Np05jSplitComputeCrc(const NP05J_SPLIT_HEADER *Hdr) {
  NP05J_SPLIT_HEADER Copy;
  MemCopy(&Copy, Hdr, (uint32_t)sizeof(Copy));
  Copy.HeaderCrc32 = 0U;
  return Np05jCrc32(&Copy, (uint32_t)sizeof(Copy));
}

int Np05jSplitValidate(const NP05J_SPLIT_HEADER *Hdr) {
  if (!Hdr) {
    return 0;
  }
  if (!MagicEquals(Hdr->Magic, "SPLITv1\0", 8U)) {
    return 0;
  }
  if (Hdr->Version != NP05J_SPLIT_VERSION) {
    return 0;
  }
  if (Hdr->ContainerTotalBytes < sizeof(*Hdr)) {
    return 0;
  }
  return Hdr->HeaderCrc32 == Np05jSplitComputeCrc(Hdr);
}

uint32_t Np05jNextComputeCrc(const NP05J_NEXT_REQUEST *Req) {
  NP05J_NEXT_REQUEST Copy;
  MemCopy(&Copy, Req, (uint32_t)sizeof(Copy));
  Copy.Crc32 = 0U;
  return Np05jCrc32(&Copy, (uint32_t)sizeof(Copy));
}

int Np05jNextValidate(const NP05J_NEXT_REQUEST *Req) {
  if (!Req) {
    return 0;
  }
  if (!MagicEquals(Req->Magic, "NEXTv1\0\0", 8U)) {
    return 0;
  }
  if (Req->Version != NP05J_NEXT_VERSION) {
    return 0;
  }
  if (Req->Target > NP05J_BOOT_TARGET_MAX) {
    return 0;
  }
  return Req->Crc32 == Np05jNextComputeCrc(Req);
}

uint32_t Np05jRestoreComputeCrc(const NP05J_RESTORE_HEADER *Hdr) {
  NP05J_RESTORE_HEADER Copy;
  MemCopy(&Copy, Hdr, (uint32_t)sizeof(Copy));
  Copy.HeaderCrc32 = 0U;
  return Np05jCrc32(&Copy, (uint32_t)sizeof(Copy));
}

int Np05jRestoreValidate(const NP05J_RESTORE_HEADER *Hdr) {
  if (!Hdr) {
    return 0;
  }
  if (!MagicEquals(Hdr->Magic, "RSTRv1\0\0", 8U)) {
    return 0;
  }
  if (Hdr->Version != NP05J_RESTORE_VERSION) {
    return 0;
  }
  return Hdr->HeaderCrc32 == Np05jRestoreComputeCrc(Hdr);
}

const NP05J_SPLIT_HEADER *Np05jFindSplitHeader(const void *SearchBase, uint32_t MaxScanBytes) {
  const uint8_t *Base = (const uint8_t *)SearchBase;

  for (uint32_t Off = 0; (Off + sizeof(NP05J_SPLIT_HEADER)) <= MaxScanBytes; Off += 4096U) {
    const NP05J_SPLIT_HEADER *Hdr = (const NP05J_SPLIT_HEADER *)(const void *)(Base + Off);
    if (Np05jSplitValidate(Hdr)) {
      return Hdr;
    }
  }
  return 0;
}

uint32_t Np05jResolveBootTarget(const NP05J_SPLIT_HEADER *Split, uint32_t *TimeoutSecOut) {
  uint32_t Timeout = 3U;
  uint32_t Target  = NP05J_BOOT_TARGET_ANDROID;

  if (!Split) {
    if (TimeoutSecOut) {
      *TimeoutSecOut = Timeout;
    }
    return Target;
  }

  Timeout = (Split->DefaultTimeoutSec <= 15U) ? Split->DefaultTimeoutSec : 3U;

  if ((Split->RequestOffset >= sizeof(*Split)) &&
      (Split->RequestSize >= sizeof(NP05J_NEXT_REQUEST)))
  {
    const NP05J_NEXT_REQUEST *Req = (const NP05J_NEXT_REQUEST *)(const void *)((const uint8_t *)Split + Split->RequestOffset);
    if (Np05jNextValidate(Req)) {
      Target = Req->Target;
      if (Req->TimeoutSec <= 15U) {
        Timeout = Req->TimeoutSec;
      }
    }
  }

  if (TimeoutSecOut) {
    *TimeoutSecOut = Timeout;
  }
  return Target;
}

void Np05jWriteEmbedRecord(uint64_t Addr, const NP05J_SPLIT_HEADER *Split, uint64_t ContainerBase, uint32_t Target) {
  volatile NP05J_EMBED_RECORD *Rec = (volatile NP05J_EMBED_RECORD *)(uintptr_t)Addr;
  NP05J_EMBED_RECORD           Local;

  MemZero(&Local, (uint32_t)sizeof(Local));
  MemCopy(Local.Magic, NP05J_EMBED_RECORD_MAGIC, 16U);
  Local.Version        = NP05J_EMBED_RECORD_VERSION;
  Local.Size           = NP05J_EMBED_RECORD_SIZE;
  Local.AppBase        = Split ? (ContainerBase + Split->UefiOffset) : 0ULL;
  Local.AppBytes       = Split ? Split->UefiSize : 0ULL;
  Local.ImageBase      = ContainerBase;
  Local.ContainerBytes = Split ? Split->ContainerTotalBytes : 0ULL;
  Local.Target         = (uint64_t)Target;

  {
    const uint8_t    *S = (const uint8_t *)&Local;
    volatile uint8_t *D = (volatile uint8_t *)(void *)Rec;
    for (uint32_t I = 0; I < (uint32_t)sizeof(Local); I++) {
      D[I] = S[I];
    }
  }
}
