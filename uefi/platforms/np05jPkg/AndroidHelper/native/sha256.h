/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Minimal dependency-free SHA-256 implementation for np05j-boot-repack.
**/

#ifndef _NP05J_SHA256_H_
#define _NP05J_SHA256_H_

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t State[8];
  uint64_t BitCount;
  uint8_t  Buf[64];
  size_t   BufLen;
} NP05J_SHA256_CTX;

void Np05jSha256Init(NP05J_SHA256_CTX *Ctx);
void Np05jSha256Update(NP05J_SHA256_CTX *Ctx, const void *Data, size_t Len);
void Np05jSha256Final(NP05J_SHA256_CTX *Ctx, uint8_t Digest[32]);
void Np05jSha256Buffer(const void *Data, size_t Len, uint8_t Digest[32]);

#endif /* _NP05J_SHA256_H_ */
