/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  FIPS 180-4 SHA-256 implementation for np05j-boot-repack.
**/

#include "sha256.h"
#include <string.h>

#define ROR32(X, N) (((X) >> (N)) | ((X) << (32U - (N))))

static const uint32_t mK[64] = {
  0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
  0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
  0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
  0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
  0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
  0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
  0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
  0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static void Transform(NP05J_SHA256_CTX *Ctx, const uint8_t Block[64]) {
  uint32_t W[64];
  uint32_t A, B, C, D, E, F, G, H;

  for (size_t I = 0; I < 16U; I++) {
    W[I] = ((uint32_t)Block[I * 4U] << 24) |
           ((uint32_t)Block[I * 4U + 1U] << 16) |
           ((uint32_t)Block[I * 4U + 2U] << 8) |
           ((uint32_t)Block[I * 4U + 3U]);
  }
  for (size_t I = 16U; I < 64U; I++) {
    uint32_t S0 = ROR32(W[I - 15U], 7U) ^ ROR32(W[I - 15U], 18U) ^ (W[I - 15U] >> 3U);
    uint32_t S1 = ROR32(W[I - 2U], 17U) ^ ROR32(W[I - 2U], 19U) ^ (W[I - 2U] >> 10U);
    W[I] = W[I - 16U] + S0 + W[I - 7U] + S1;
  }

  A = Ctx->State[0]; B = Ctx->State[1]; C = Ctx->State[2]; D = Ctx->State[3];
  E = Ctx->State[4]; F = Ctx->State[5]; G = Ctx->State[6]; H = Ctx->State[7];

  for (size_t I = 0; I < 64U; I++) {
    uint32_t S1    = ROR32(E, 6U) ^ ROR32(E, 11U) ^ ROR32(E, 25U);
    uint32_t Ch    = (E & F) ^ ((~E) & G);
    uint32_t Temp1 = H + S1 + Ch + mK[I] + W[I];
    uint32_t S0    = ROR32(A, 2U) ^ ROR32(A, 13U) ^ ROR32(A, 22U);
    uint32_t Maj   = (A & B) ^ (A & C) ^ (B & C);
    uint32_t Temp2 = S0 + Maj;

    H = G; G = F; F = E; E = D + Temp1;
    D = C; C = B; B = A; A = Temp1 + Temp2;
  }

  Ctx->State[0] += A; Ctx->State[1] += B; Ctx->State[2] += C; Ctx->State[3] += D;
  Ctx->State[4] += E; Ctx->State[5] += F; Ctx->State[6] += G; Ctx->State[7] += H;
}

void Np05jSha256Init(NP05J_SHA256_CTX *Ctx) {
  Ctx->State[0] = 0x6a09e667U; Ctx->State[1] = 0xbb67ae85U;
  Ctx->State[2] = 0x3c6ef372U; Ctx->State[3] = 0xa54ff53aU;
  Ctx->State[4] = 0x510e527fU; Ctx->State[5] = 0x9b05688cU;
  Ctx->State[6] = 0x1f83d9abU; Ctx->State[7] = 0x5be0cd19U;
  Ctx->BitCount = 0ULL;
  Ctx->BufLen   = 0U;
}

void Np05jSha256Update(NP05J_SHA256_CTX *Ctx, const void *Data, size_t Len) {
  const uint8_t *P = (const uint8_t *)Data;
  while (Len > 0U) {
    size_t Take = 64U - Ctx->BufLen;
    if (Take > Len) {
      Take = Len;
    }
    memcpy(Ctx->Buf + Ctx->BufLen, P, Take);
    Ctx->BufLen   += Take;
    Ctx->BitCount += (uint64_t)Take * 8ULL;
    P             += Take;
    Len           -= Take;
    if (Ctx->BufLen == 64U) {
      Transform(Ctx, Ctx->Buf);
      Ctx->BufLen = 0U;
    }
  }
}

void Np05jSha256Final(NP05J_SHA256_CTX *Ctx, uint8_t Digest[32]) {
  uint64_t TotalBits = Ctx->BitCount;
  uint8_t  Pad = 0x80U;
  uint8_t  Zero = 0x00U;
  uint8_t  BeLen[8];

  Np05jSha256Update(Ctx, &Pad, 1U);
  while (Ctx->BufLen != 56U) {
    Np05jSha256Update(Ctx, &Zero, 1U);
  }
  for (int I = 0; I < 8; I++) {
    BeLen[I] = (uint8_t)(TotalBits >> (56 - I * 8));
  }
  memcpy(Ctx->Buf + 56U, BeLen, 8U);
  Transform(Ctx, Ctx->Buf);

  for (size_t I = 0; I < 8U; I++) {
    Digest[I * 4U]      = (uint8_t)(Ctx->State[I] >> 24);
    Digest[I * 4U + 1U] = (uint8_t)(Ctx->State[I] >> 16);
    Digest[I * 4U + 2U] = (uint8_t)(Ctx->State[I] >> 8);
    Digest[I * 4U + 3U] = (uint8_t)(Ctx->State[I]);
  }
}

void Np05jSha256Buffer(const void *Data, size_t Len, uint8_t Digest[32]) {
  NP05J_SHA256_CTX Ctx;
  Np05jSha256Init(&Ctx);
  Np05jSha256Update(&Ctx, Data, Len);
  Np05jSha256Final(&Ctx, Digest);
}
