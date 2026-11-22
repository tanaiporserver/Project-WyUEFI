/* AArch64 Image entry for boards whose Android loader supplies a fixed kernel window. */
#define PAYLOAD_OFFSET 0x10000

#if REQUIRES_KERNEL_HEADER != 1
#error "In-place boot requires an AArch64 Image header"
#endif

.text
_Image:
  b     _Start
  nop
  .quad 0                         /* Image starts at the loader's aligned base. */
  .quad PAYLOAD_OFFSET + FD_SIZE  /* Bytes required from the Image start. */
  .quad 0xa                       /* Little endian, 4 KiB pages, aligned RAM base. */
  .quad 0, 0, 0
  .ascii "ARM\x64"
  .long 0

_Start:
  adr   x1, _Payload
  ldr   x2, =FD_BASE
  cmp   x1, x2
  b.ne  _UnexpectedPlacement
  br    x1

_UnexpectedPlacement:
  wfe
  b     _UnexpectedPlacement

.ltorg
.org PAYLOAD_OFFSET
_Payload:
