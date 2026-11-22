/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Portable SunUEFI Fastboot 0.4 protocol core, Android sparse validator/flasher,
  and SHA-256 helper for nubia NP05J.

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  uefi/core/usb/PianoFastboot.h, PianoFastboot.c, and PianoFastbootFlash.inc.
**/

#ifndef _NP05J_FASTBOOT_CORE_H_
#define _NP05J_FASTBOOT_CORE_H_

#ifdef NP05J_HOST_TEST

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef STATIC
#define STATIC static
#endif
#ifndef VOID
#define VOID void
#endif

typedef uint8_t   BOOLEAN;
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int32_t   INT32;
typedef int64_t   INT64;
typedef size_t    UINTN;
typedef intptr_t  INTN;
typedef char      CHAR8;
typedef uint16_t  CHAR16;
typedef intptr_t  EFI_STATUS;

#ifndef TRUE
#define TRUE ((BOOLEAN)1U)
#endif
#ifndef FALSE
#define FALSE ((BOOLEAN)0U)
#endif

#define MAX_UINT8   ((UINT8)0xFFU)
#define MAX_UINT16  ((UINT16)0xFFFFU)
#define MAX_UINT32  ((UINT32)0xFFFFFFFFU)
#define MAX_UINT64  ((UINT64)0xFFFFFFFFFFFFFFFFULL)
#define MAX_UINTN   ((UINTN)~(UINTN)0U)

#define EFI_SUCCESS            ((EFI_STATUS)0)
#define EFI_LOAD_ERROR         ((EFI_STATUS)(-1))
#define EFI_INVALID_PARAMETER  ((EFI_STATUS)(-2))
#define EFI_UNSUPPORTED        ((EFI_STATUS)(-3))
#define EFI_BAD_BUFFER_SIZE    ((EFI_STATUS)(-4))
#define EFI_BUFFER_TOO_SMALL   ((EFI_STATUS)(-5))
#define EFI_NOT_READY          ((EFI_STATUS)(-6))
#define EFI_DEVICE_ERROR       ((EFI_STATUS)(-7))
#define EFI_WRITE_PROTECTED    ((EFI_STATUS)(-8))
#define EFI_OUT_OF_RESOURCES   ((EFI_STATUS)(-9))
#define EFI_NO_MEDIA           ((EFI_STATUS)(-12))
#define EFI_MEDIA_CHANGED      ((EFI_STATUS)(-13))
#define EFI_NOT_FOUND          ((EFI_STATUS)(-14))
#define EFI_ACCESS_DENIED      ((EFI_STATUS)(-15))
#define EFI_NO_MAPPING         ((EFI_STATUS)(-17))
#define EFI_TIMEOUT            ((EFI_STATUS)(-18))
#define EFI_ALREADY_STARTED    ((EFI_STATUS)(-20))
#define EFI_ABORTED            ((EFI_STATUS)(-21))
#define EFI_CRC_ERROR          ((EFI_STATUS)(-27))
#define EFI_COMPROMISED_DATA   ((EFI_STATUS)(-33))

#define EFI_ERROR(Status) ((Status) < 0)

#ifndef MIN
#define MIN(A, B) (((A) < (B)) ? (A) : (B))
#endif
#ifndef MAX
#define MAX(A, B) (((A) > (B)) ? (A) : (B))
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(Arr) (sizeof (Arr) / sizeof ((Arr)[0]))
#endif

static inline VOID *
AllocateZeroPool (
  IN UINTN AllocationSize
  )
{
  return calloc (1U, (AllocationSize != 0U) ? AllocationSize : 1U);
}

static inline VOID
FreePool (
  IN VOID *Buffer
  )
{
  free (Buffer);
}

static inline VOID *
CopyMem (
  OUT VOID       *DestinationBuffer,
  IN  CONST VOID *SourceBuffer,
  IN  UINTN       Length
  )
{
  if (Length > 0U) {
    return memcpy (DestinationBuffer, SourceBuffer, Length);
  }

  return DestinationBuffer;
}

static inline VOID *
ZeroMem (
  OUT VOID *Buffer,
  IN  UINTN Length
  )
{
  if (Length > 0U) {
    return memset (Buffer, 0, Length);
  }

  return Buffer;
}

static inline INTN
CompareMem (
  IN CONST VOID *DestinationBuffer,
  IN CONST VOID *SourceBuffer,
  IN UINTN       Length
  )
{
  if (Length == 0U) {
    return 0;
  }

  return (INTN)memcmp (DestinationBuffer, SourceBuffer, Length);
}

static inline UINTN
AsciiStrLen (
  IN CONST CHAR8 *String
  )
{
  return strlen (String);
}

static inline INTN
AsciiStrCmp (
  IN CONST CHAR8 *FirstString,
  IN CONST CHAR8 *SecondString
  )
{
  return (INTN)strcmp (FirstString, SecondString);
}

static inline INTN
AsciiStrnCmp (
  IN CONST CHAR8 *FirstString,
  IN CONST CHAR8 *SecondString,
  IN UINTN       Length
  )
{
  return (INTN)strncmp (FirstString, SecondString, Length);
}

#else

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#endif

#define NP05J_FASTBOOT_MAX_DOWNLOAD       (64U * 1024U * 1024U)
#define NP05J_FASTBOOT_DEFAULT_FETCH_BYTES 65536U
#define NP05J_FASTBOOT_MAX_FETCH_BYTES    (256U * 1024U)
#define NP05J_FASTBOOT_MAX_BLOCK_IO_BYTES (1024U * 1024U)
#define NP05J_FASTBOOT_PARTITION_NAME     36U

#define NP05J_FASTBOOT_PRODUCT            "np05j-sunuefi"
#define NP05J_FASTBOOT_SERIAL             "SunUEFI-np05j"
#define NP05J_FASTBOOT_BOOTLOADER_VER     "4.1-SunUEFI-v5"

typedef struct {
  CHAR8    Name[NP05J_FASTBOOT_PARTITION_NAME + 1U];
  UINT64   Bytes;
  UINT32   BlockSize;
  BOOLEAN  ReadOnly;
  VOID     *Token;
} NP05J_FB_PARTITION_INFO;

typedef struct {
  VOID       *Context;
  EFI_STATUS (*Ready) (VOID *Context);
  EFI_STATUS (*Info) (VOID *Context, CONST CHAR8 *ExactName, NP05J_FB_PARTITION_INFO *Info);
  EFI_STATUS (*ReadBlocks) (VOID *Context, CONST NP05J_FB_PARTITION_INFO *Info, UINT64 Lba, UINTN Bytes, VOID *Buffer);
  EFI_STATUS (*PrepareWrite) (VOID *Context, CONST NP05J_FB_PARTITION_INFO *Info);
  EFI_STATUS (*WriteBlocks) (VOID *Context, CONST NP05J_FB_PARTITION_INFO *Info, UINT64 Lba, UINTN Bytes, CONST VOID *Buffer);
  EFI_STATUS (*Flush) (VOID *Context, CONST NP05J_FB_PARTITION_INFO *Info);
  UINT32     MaxFetchBytes;
} NP05J_FB_STORAGE;

typedef EFI_STATUS (*NP05J_FB_SEND) (VOID *Context, CONST VOID *Data, UINTN Bytes);
typedef EFI_STATUS (*NP05J_FB_LOG) (VOID *Context, NP05J_FB_SEND Send);

typedef struct _NP05J_FASTBOOT  NP05J_FASTBOOT;
typedef struct _NP05J_FB_FLASH  NP05J_FB_FLASH;

typedef struct {
  UINT64  Bytes;
  UINT64  TotalUs;
  UINT64  PrepareUs;
  UINT64  WriteUs;
  UINT64  ReadUs;
  UINT64  FlushUs;
} NP05J_FB_FLASH_TIMING;

typedef EFI_STATUS (*NP05J_FB_QUERY) (VOID *Context, CONST CHAR8 *Name, CHAR8 Value[60]);
typedef EFI_STATUS (*NP05J_FB_DIAGNOSTIC) (VOID *Context, NP05J_FASTBOOT *State, CONST CHAR8 *Command);

struct _NP05J_FASTBOOT {
  VOID                 *Context;
  NP05J_FB_SEND        Send;
  NP05J_FB_LOG         Log;
  UINT8                *Download;
  UINT8                *Upload;
  UINTN                UploadBytes;
  BOOLEAN              UploadBorrowed;
  NP05J_FB_QUERY       Query;
  NP05J_FB_DIAGNOSTIC  Diagnostic;
  UINT64               (*NowUs) (VOID *Context);
  VOID                 (*ReportFlash) (CONST CHAR8 *Partition, CONST NP05J_FB_FLASH_TIMING *Timing, CONST CHAR8 *Result);
  NP05J_FB_STORAGE     Storage;
  NP05J_FB_FLASH       *Flash;
  BOOLEAN              FlashExecuting;
  BOOLEAN              FlashCancelRequested;
  UINTN                Expected;
  UINTN                Received;
  BOOLEAN              Receiving;
  BOOLEAN              Complete;
  BOOLEAN              RebootRequested;
  BOOLEAN              ExitRequested;
  BOOLEAN              DiagnosticsOnly;
  BOOLEAN              DiagnosticsRebootBlocked;
  UINT64               CommandsHandled;
  CHAR8                LastCommand[65];
};

VOID
Np05jFastbootSha256 (
  IN  CONST VOID  *Data,
  IN  UINTN       Len,
  OUT UINT8       Digest[32]
  );

EFI_STATUS
Np05jFastbootInit (
  OUT NP05J_FASTBOOT  *State,
  IN  VOID            *Context,
  IN  NP05J_FB_SEND   Send,
  IN  NP05J_FB_LOG    Log OPTIONAL
  );

VOID
Np05jFastbootReset (
  IN OUT NP05J_FASTBOOT  *State
  );

EFI_STATUS
Np05jFastbootRestrictToDiagnostics (
  IN OUT NP05J_FASTBOOT  *State,
  IN     BOOLEAN         AllowReboot
  );

EFI_STATUS
Np05jFastbootSetStorage (
  IN OUT NP05J_FASTBOOT          *State,
  IN     CONST NP05J_FB_STORAGE  *Storage OPTIONAL
  );

BOOLEAN
Np05jFastbootStorageWritable (
  IN NP05J_FASTBOOT  *State
  );

EFI_STATUS
Np05jFastbootStageCopy (
  IN OUT NP05J_FASTBOOT  *State,
  IN     CONST VOID      *Data,
  IN     UINTN           Bytes
  );

EFI_STATUS
Np05jFastbootPacket (
  IN OUT NP05J_FASTBOOT  *State,
  IN     CONST VOID      *Data,
  IN     UINTN           Bytes
  );

BOOLEAN
Np05jFastbootFlashPending (
  IN CONST NP05J_FASTBOOT  *State
  );

EFI_STATUS
Np05jFastbootFlashStep (
  IN OUT NP05J_FASTBOOT  *State
  );

#ifndef NP05J_HOST_TEST

EFI_STATUS
Np05jFastbootCaptureScreen (
  IN OUT NP05J_FASTBOOT  *State,
  OUT    UINT32          *Width,
  OUT    UINT32          *Height,
  OUT    UINT32          *Bytes,
  OUT    UINT32          *Crc32
  );

EFI_STATUS
Np05jFastbootStorageInit (
  VOID
  );

VOID
Np05jFastbootStorageMarkDirty (
  VOID
  );

VOID
Np05jFastbootStorageStop (
  VOID
  );

CONST NP05J_FB_STORAGE *
Np05jFastbootStorageGet (
  VOID
  );

VOID
Np05jFastbootStorageGetStats (
  OUT UINT32  *Total,
  OUT UINT32  *Writable
  );

#endif /* NP05J_HOST_TEST */

#endif /* _NP05J_FASTBOOT_CORE_H_ */
