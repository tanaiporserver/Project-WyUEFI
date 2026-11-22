/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Shared handoff ABI between NP05J pre-UEFI boot-selection, Android boot helper,
  and UEFI BootManager / BootMenuApp.

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  uefi/handoff/bootselect/BootRequest.h and Entry.S.
**/

#ifndef _NP05J_BOOT_HANDOFF_H_
#define _NP05J_BOOT_HANDOFF_H_

#define NP05J_EMBED_RECORD_ADDR        0xA7FFF100ULL
#define NP05J_EMBED_RECORD_MAGIC       "SUNUEFI-EMBEDv1"
#define NP05J_EMBED_RECORD_VERSION     2U
#define NP05J_EMBED_RECORD_SIZE        64U

#define NP05J_BOOT_REQUEST_RECORD_BYTES 64U
#define NP05J_BOOT_REQUEST_PAGE_BYTES   4096U
#define NP05J_BOOT_REQUEST_PAGES_BYTES  8192U

#define NP05J_BOOT_TARGET_ANDROID      0U
#define NP05J_BOOT_TARGET_UEFI_MENU    1U
#define NP05J_BOOT_TARGET_LINUX        2U
#define NP05J_BOOT_TARGET_BIOS_SETUP   3U
#define NP05J_BOOT_TARGET_WINDOWS      4U
#define NP05J_BOOT_TARGET_MAX          4U

#if defined(MAX_UINT8)
typedef UINT8  np05j_u8_t;
typedef UINT32 np05j_u32_t;
typedef UINT64 np05j_u64_t;
#else
#include <stdint.h>
typedef uint8_t  np05j_u8_t;
typedef uint32_t np05j_u32_t;
typedef uint64_t np05j_u64_t;
#endif

#pragma pack(1)
typedef struct {
  char        Magic[16];     /* "SUNUEFI-EMBEDv1\0" */
  np05j_u32_t Version;       /* 2 */
  np05j_u32_t Size;          /* 64 */
  np05j_u64_t AppBase;
  np05j_u64_t AppBytes;
  np05j_u64_t ImageBase;
  np05j_u64_t ContainerBytes;
  np05j_u64_t Target;        /* NP05J_BOOT_TARGET_* */
} NP05J_EMBED_RECORD;
#pragma pack()

#endif /* _NP05J_BOOT_HANDOFF_H_ */
