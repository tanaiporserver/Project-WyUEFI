/** @file
  Shared RAM debug log ring buffer located in the unused tail of Display_Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _NP05J_LOG_RING_H_
#define _NP05J_LOG_RING_H_

#include <Uefi.h>

#define NP05J_LOG_RING_MAGIC       0x30504E55U // 'UNP0'
#define NP05J_LOG_RING_VERSION     1U
#define NP05J_LOG_RING_CAPACITY    0x40000U    // 256 KiB log data
#define NP05J_LOG_RING_GUARD_TAIL  0x1000U     // Preserve last 4 KiB (EFI_FRAME_BUFFER_POSITION)
#define NP05J_LOG_RING_HEADER_SIZE 0x100U      // 256-byte aligned header
#define NP05J_LOG_RING_TOTAL_SIZE  (NP05J_LOG_RING_HEADER_SIZE + NP05J_LOG_RING_CAPACITY)
#define NP05J_LOG_RING_TAIL_OFFSET (NP05J_LOG_RING_TOTAL_SIZE + NP05J_LOG_RING_GUARD_TAIL)

typedef struct {
  UINT32  Magic;
  UINT32  Version;
  UINT32  Capacity;
  UINT32  Flags;
  UINT64  WritePos;
  UINT64  Reserved[29];
} NP05J_LOG_RING_HEADER;

STATIC_ASSERT (sizeof (NP05J_LOG_RING_HEADER) == NP05J_LOG_RING_HEADER_SIZE, "NP05J_LOG_RING_HEADER size mismatch");

#endif // _NP05J_LOG_RING_H_
