/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Freestanding memcpy/memset/memcmp symbols for bare-metal BootSelect.
**/

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *Dst, const void *Src, size_t Len) {
  uint8_t       *D = (uint8_t *)Dst;
  const uint8_t *S = (const uint8_t *)Src;
  size_t         I = 0;
  /* คัดลอกทีละ 8 ไบต์เมื่อ pointer จัดแนวตรงกัน (ปลอดภัยกับ -mstrict-align) */
  if ((((uintptr_t)D | (uintptr_t)S) & 7U) == 0U) {
    while ((I + 8U) <= Len) {
      *(uint64_t *)(void *)(D + I) = *(const uint64_t *)(const void *)(S + I);
      I += 8U;
    }
  }
  for (; I < Len; I++) {
    D[I] = S[I];
  }
  return Dst;
}

void *memset(void *Dst, int Val, size_t Len) {
  uint8_t *D = (uint8_t *)Dst;
  uint8_t  B = (uint8_t)Val;
  size_t   I = 0;
  if ((Len >= 8U) && (((uintptr_t)D & 7U) == 0U)) {
    uint64_t W = (uint64_t)B * 0x0101010101010101ULL;
    while ((I + 8U) <= Len) {
      *(uint64_t *)(void *)(D + I) = W;
      I += 8U;
    }
  }
  for (; I < Len; I++) {
    D[I] = B;
  }
  return Dst;
}

int memcmp(const void *A, const void *B, size_t Len) {
  const uint8_t *Pa = (const uint8_t *)A;
  const uint8_t *Pb = (const uint8_t *)B;
  for (size_t I = 0; I < Len; I++) {
    if (Pa[I] != Pb[I]) {
      return (Pa[I] < Pb[I]) ? -1 : 1;
    }
  }
  return 0;
}
