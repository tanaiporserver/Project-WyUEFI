#!/usr/bin/env python3
"""Derive the NP05J product-staging ClockDxe compatibility image from pinned ROM bytes.

Modeled directly on Project-WyUEFI's tools/piano_inherited_clock.py:
defers the automatic CESTA/media startup pass in NP05J slot_b ClockDxe.efi
(RVA 0xC270: BL 0xC02C -> MOV w0, #0) while keeping runtime clock protocols intact.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ORIGINAL_SHA256 = "7c21166224cf71f2f902d6f6f00eaa677e933a1fada24139f2ace08eb7aa1738"
DERIVED_SHA256 = "3b76bda865353e35fdaad3b29f5948bd2c7d74a98ca13bcd01b5ab1f130bb7a3"
PATCH_RVA = 0xC270
ORIGINAL_INSTRUCTION = bytes.fromhex("6fffff97")  # BL 0xC02C
REPLACEMENT_INSTRUCTION = bytes.fromhex("00008052")  # MOV w0, #0
CONTEXT_RVA = 0xC26C
ORIGINAL_CONTEXT = bytes.fromhex("210100546fffff9760000034")  # b.ne +8; bl 0xc02c; cbz w0, +0xc
DCD_STARTUP_CONTROL_RVA = 0x3A4E0
DCD_BRANCH_RVA = 0x46AC
DCD_BRANCH_BYTES = bytes.fromhex("08e144b928070034")  # ldr w8, [x8, #0x4e0]; cbz w8, 0x4794
DCD_ENABLE_STR_RVA = 0x21782
DCD_DISABLE_STR_RVA = 0x2179D
REQUIRED_STRINGS = (
    b"disp_cesta\x00",
    b"qcom,disp_cesta-pakala\x00",
    b"/soc/cesta@af27000\x00",
    b"disp_cc_pll0\x00",
)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _section_offset(data: bytes, rva: int, size: int) -> int:
    pe = struct.unpack_from("<I", data, 60)[0]
    if data[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", data, pe + 4)[0] != 0xAA64:
        raise ValueError("Expected an ARM64 PE32+ image")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    headers = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    for i in range(count):
        _, _, virtual, raw_size, raw_offset = struct.unpack_from("<8s4I", data, headers + i * 40)
        if virtual <= rva and rva - virtual + size <= raw_size:
            return raw_offset + rva - virtual
    raise ValueError(f"RVA 0x{rva:x} outside PE raw sections")


def _update_pe_checksum(image: bytearray) -> int:
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    opt = pe + 24
    if struct.unpack_from("<H", image, opt)[0] != 0x20B:
        raise ValueError("Expected PE32+ optional header")
    csum_off = opt + 64
    struct.pack_into("<I", image, csum_off, 0)
    total = 0
    padded = bytes(image) + (b"\x00" if len(image) % 2 else b"")
    for (word,) in struct.iter_unpack("<H", padded):
        total += word
        total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    checksum = (total + len(image)) & 0xFFFFFFFF
    struct.pack_into("<I", image, csum_off, checksum)
    return checksum


def derive(original: bytes) -> tuple[bytes, dict]:
    if not isinstance(original, bytes) or _sha(original) != ORIGINAL_SHA256:
        raise ValueError("Original NP05J ClockDxe.efi SHA256 pin mismatch")
    for s in REQUIRED_STRINGS:
        if s not in original:
            raise ValueError(f"Missing expected CESTA/PLL identifier: {s!r}")
    ctx_off = _section_offset(original, CONTEXT_RVA, len(ORIGINAL_CONTEXT))
    if original[ctx_off:ctx_off + len(ORIGINAL_CONTEXT)] != ORIGINAL_CONTEXT:
        raise ValueError("Unexpected instruction context at 0xC26C")
    patch_off = _section_offset(original, PATCH_RVA, 4)
    if original[patch_off:patch_off + 4] != ORIGINAL_INSTRUCTION:
        raise ValueError("Unexpected BL instruction at 0xC270")
    # ตรวจสอบสาขา DCD startup dependencies และสวิตช์ควบคุมที่ 0x3A4E0 เพื่อรักษาหน้าจอจาก ABL
    dcd_branch_off = _section_offset(original, DCD_BRANCH_RVA, len(DCD_BRANCH_BYTES))
    if original[dcd_branch_off:dcd_branch_off + len(DCD_BRANCH_BYTES)] != DCD_BRANCH_BYTES:
        raise ValueError("Unexpected DCD startup branch at 0x46AC")
    enable_off = _section_offset(original, DCD_ENABLE_STR_RVA, len(b"ClockDCDEnableDependencies\x00"))
    disable_off = _section_offset(original, DCD_DISABLE_STR_RVA, len(b"ClockDCDDisableDependencies\x00"))
    if (
        original[enable_off:enable_off + len(b"ClockDCDEnableDependencies\x00")] != b"ClockDCDEnableDependencies\x00"
        or original[disable_off:disable_off + len(b"ClockDCDDisableDependencies\x00")] != b"ClockDCDDisableDependencies\x00"
    ):
        raise ValueError("Missing ClockDCDEnableDependencies/ClockDCDDisableDependencies strings")
    dcd_off = _section_offset(original, DCD_STARTUP_CONTROL_RVA, 4)
    if struct.unpack_from("<I", original, dcd_off)[0] != 1:
        raise ValueError("Unexpected DCD startup control initial value at 0x3A4E0")

    out = bytearray(original)
    out[patch_off:patch_off + 4] = REPLACEMENT_INSTRUCTION
    # ปิดสวิตช์ DCD startup dependencies (1 -> 0) ไม่ให้รบกวน display pipeline ที่สืบทอดมา
    struct.pack_into("<I", out, dcd_off, 0)
    checksum = _update_pe_checksum(out)
    derived = bytes(out)
    derived_sha = _sha(derived)
    if derived_sha != DERIVED_SHA256:
        raise ValueError("Derived NP05J ClockDxe.efi SHA256 pin mismatch")
    return derived, {
        "original_sha256": ORIGINAL_SHA256,
        "derived_sha256": derived_sha,
        "patch_rva": f"0x{PATCH_RVA:x}",
        "patch_file_offset": f"0x{patch_off:x}",
        "dcd_startup_dependencies_deferred": True,
        "dcd_startup_control_rva": f"0x{DCD_STARTUP_CONTROL_RVA:x}",
        "pe_checksum": f"0x{checksum:08x}",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Path to unmodified NP05J ClockDxe.efi")
    parser.add_argument("--output", "-o", type=Path, help="Optional output path for derived ClockDxe.efi")
    args = parser.parse_args()
    derived, meta = derive(args.input.read_bytes())
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(derived)
        meta["output"] = str(args.output)
    print(json.dumps(meta, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
