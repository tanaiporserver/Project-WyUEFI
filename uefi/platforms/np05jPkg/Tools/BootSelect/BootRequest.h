/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Shared SPLITv1 / NEXTv1 / RSTRv1 boot-selection container ABI for nubia NP05J.
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#ifndef _NP05J_BOOT_REQUEST_H_
#define _NP05J_BOOT_REQUEST_H_

#include <stdint.h>
#include "Np05jBootHandoff.h"

#define NP05J_SPLIT_MAGIC          "SPLITv1"
#define NP05J_SPLIT_VERSION        1U
#define NP05J_NEXT_MAGIC           "NEXTv1\0"
#define NP05J_NEXT_VERSION         1U
#define NP05J_RESTORE_MAGIC        "RSTRv1\0"
#define NP05J_RESTORE_VERSION      1U

#define NP05J_NEXT_FLAG_ONESHOT    (1U << 0)

#pragma pack(push, 1)
typedef struct {
  char     Magic[8];            /* "SPLITv1\0" */
  uint32_t Version;             /* 1 */
  uint32_t HeaderCrc32;         /* IEEE CRC32 with this field = 0 */
  uint64_t ContainerTotalBytes;
  uint64_t RequestOffset;       /* Page-aligned offset from container start */
  uint64_t RequestSize;         /* 4096 */
  uint64_t RestoreOffset;
  uint64_t RestoreSize;
  uint64_t ShimOffset;
  uint64_t ShimSize;
  uint64_t AndroidKernelOffset;
  uint64_t AndroidKernelSize;
  uint64_t UefiOffset;
  uint64_t UefiSize;
  uint32_t DefaultTimeoutSec;
  uint32_t Flags;
  uint32_t AndroidKernelCrc32;
  uint32_t UefiCrc32;
  uint8_t  StockBootSha256[32];
  uint8_t  Reserved[4096 - 152];
} NP05J_SPLIT_HEADER;

typedef struct {
  char     Magic[8];            /* "NEXTv1\0\0" */
  uint32_t Version;             /* 1 */
  uint32_t Target;              /* NP05J_BOOT_TARGET_* */
  uint32_t TimeoutSec;
  uint32_t Flags;
  uint64_t Sequence;
  uint32_t Crc32;               /* IEEE CRC32 with this field = 0 */
  uint8_t  Reserved[64 - 36];
} NP05J_NEXT_REQUEST;

typedef struct {
  char     Magic[8];            /* "RSTRv1\0\0" */
  uint32_t Version;             /* 1 */
  uint32_t OriginalBootBytes;
  uint32_t PrefixBytes;         /* [0 .. KernelOffset) of original boot.img */
  uint32_t SuffixBytes;         /* [KernelOffset + KernelSize .. end) */
  uint32_t KernelOffset;
  uint32_t KernelSize;
  uint8_t  OriginalBootSha256[32];
  uint32_t HeaderCrc32;         /* IEEE CRC32 of header with HeaderCrc32 = 0 */
  uint8_t  Reserved[128 - 68];  /* Exact 128-byte header before Prefix + Suffix */
} NP05J_RESTORE_HEADER;
#pragma pack(pop)

_Static_assert(sizeof(NP05J_SPLIT_HEADER) == 4096U, "NP05J_SPLIT_HEADER must be 4096 bytes");
_Static_assert(sizeof(NP05J_NEXT_REQUEST) == 64U, "NP05J_NEXT_REQUEST must be 64 bytes");
_Static_assert(sizeof(NP05J_RESTORE_HEADER) == 128U, "NP05J_RESTORE_HEADER must be 128 bytes");

uint32_t Np05jCrc32(const void *Data, uint32_t Len);
uint32_t Np05jSplitComputeCrc(const NP05J_SPLIT_HEADER *Hdr);
int Np05jSplitValidate(const NP05J_SPLIT_HEADER *Hdr);
uint32_t Np05jNextComputeCrc(const NP05J_NEXT_REQUEST *Req);
int Np05jNextValidate(const NP05J_NEXT_REQUEST *Req);
uint32_t Np05jRestoreComputeCrc(const NP05J_RESTORE_HEADER *Hdr);
int Np05jRestoreValidate(const NP05J_RESTORE_HEADER *Hdr);

const NP05J_SPLIT_HEADER *Np05jFindSplitHeader(const void *SearchBase, uint32_t MaxScanBytes);
uint32_t Np05jResolveBootTarget(const NP05J_SPLIT_HEADER *Split, uint32_t *TimeoutSecOut);
void Np05jWriteEmbedRecord(uint64_t Addr, const NP05J_SPLIT_HEADER *Split, uint64_t ContainerBase, uint32_t Target);

#endif /* _NP05J_BOOT_REQUEST_H_ */
