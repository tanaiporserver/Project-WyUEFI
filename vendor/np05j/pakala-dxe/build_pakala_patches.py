#!/usr/bin/env python3
"""Generate and verify the SM8750 (Pakala) patched DXE binaries for nubia NP05J.

Every patch is derived directly from the NP05J slot B (BOOT.MXF.2.5.1) stock PE32+
binaries and aligned with the verified Pakala donor patches (dodge / annibale).
Original stock .efi files are never modified; outputs are written beside them as
<Driver>.patched.efi with recalculated PE32+ Optional Header checksums.
"""

from hashlib import sha256
from pathlib import Path
import struct

NOP = 0xD503201F
MOV_W0_WZR = 0x2A1F03E0
MOV_X0_XZR = 0xAA1F03E0
RET = 0xD65F03C0


def encode_b(pc: int, target: int) -> int:
    delta = target - pc
    assert delta % 4 == 0
    imm26 = delta >> 2
    assert -(1 << 25) <= imm26 < (1 << 25)
    return 0x14000000 | (imm26 & 0x03FFFFFF)


def encode_bl(pc: int, target: int) -> int:
    delta = target - pc
    assert delta % 4 == 0
    imm26 = delta >> 2
    assert -(1 << 25) <= imm26 < (1 << 25)
    return 0x94000000 | (imm26 & 0x03FFFFFF)


def pack_insns(*insns: int) -> bytes:
    return struct.pack(f"<{len(insns)}I", *insns)


def update_pe_checksum(image: bytearray) -> int:
    pe_off = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise ValueError("Invalid PE signature")
    opt_off = pe_off + 24
    if struct.unpack_from("<H", image, opt_off)[0] != 0x20B:
        raise ValueError("Expected PE32+ optional header")

    csum_off = opt_off + 64
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


# UFSDxe link-wakeup trampoline placed in the 256-byte zero padding at end of .text
# (0x1FF00..0x1FFFF). Mirrors dodge/annibale Pakala Patch #2 with NP05J's verified
# struct offset #0x4A1 (observed at 0x106E8 and 0x11904).
UFS_CAVE = 0x1FF00
UFS_WAKEUP_TRAMPOLINE = pack_insns(
    0x2A1F03E9,                            # 1ff00: mov  w9, wzr
    0x39128409,                            # 1ff04: strb w9, [x0, #0x4a1]
    0x2A1F03E1,                            # 1ff08: mov  w1, wzr
    encode_bl(UFS_CAVE + 0x0C, 0x106D0),   # 1ff0c: bl   0x106d0 (UFS link wakeup)
    0x35000080,                            # 1ff10: cbnz w0, 0x1ff20
    0xAA1303E0,                            # 1ff14: mov  x0, x19
    encode_bl(UFS_CAVE + 0x18, 0x11284),   # 1ff18: bl   0x11284 (gear check)
    encode_b(UFS_CAVE + 0x1C, 0x11920),    # 1ff1c: b    0x11920
    0x2A0003F6,                            # 1ff20: mov  w22, w0
    0x528008E1,                            # 1ff24: mov  w1, #0x47
    encode_b(UFS_CAVE + 0x28, 0x11A04),    # 1ff28: b    0x11a04
)

PATCH_SPECS = (
    (
        "SPMIDxe/SPMIDxe",
        "b54536c0a1f2298811bb1f160505ffc225ac8dd9e00cf7f52803571719f303ef",
        (
            # Skip SPMI PIC init (matches dodge @ 0x2DA8 / annibale @ 0x2DC0).
            (0x02DA8, bytes.fromhex("fd7bbda9f65701a9"), pack_insns(MOV_W0_WZR, RET)),
        ),
    ),
    (
        "PmicDxe/PmicDxeLa",
        "bcdf689c0c6b4e52ffb22f66d92603fded4f577f6a58ec565f95e3bc781379d9",
        (
            # Skip two PMIC init calls in PmicInit @ 0x4124 (matches dodge 0x41B4/0x41BC, annibale 0x4184/0x418C).
            (0x04138, bytes.fromhex("753d0094"), pack_insns(NOP)),
            (0x04140, bytes.fromhex("ce370094"), pack_insns(NOP)),
            # Skip minimal PMIC init called from 0x16F0 (matches dodge 0x1ACFC, annibale 0x15D9C).
            (0x13294, bytes.fromhex("fd7bbea9f30b00f9"), pack_insns(MOV_W0_WZR, RET)),
            # Skip PMIC peripheral disable call inside 0x1E138 loop (matches dodge 0x26410, annibale 0x21258).
            (0x1E148, bytes.fromhex("20adff97"), pack_insns(MOV_W0_WZR)),
        ),
    ),
    (
        "UFSDxe/UFSDxe",
        "20f961dff1ac34c18b4e17d2fc7545c4bd304e170b9cc862cbf6b8da3789eafb",
        (
            # Patch 1: replace UFS sleep/ready check with Active wakeup (matches dodge 0xA998, annibale 0xAFA8).
            (0x0A998, bytes.fromhex("8305009480fdff35"), pack_insns(0x52800021, encode_bl(0x0A99C, 0x0C0D0))),
            # Patch 2: force UFS link wakeup before gear check (matches dodge 0x1191C + .extra).
            (0x1191C, bytes.fromhex("5afeff97"), pack_insns(encode_b(0x1191C, UFS_CAVE))),
            (UFS_CAVE, bytes(len(UFS_WAKEUP_TRAMPOLINE)), UFS_WAKEUP_TRAMPOLINE),
        ),
    ),
    (
        "TzDxe/TzDxeLA",
        "cff4cdcb726a42df70385d776eb374dcc2016abad3cb5a2589a02e6e6a289abc",
        (
            # Mark TZ Applet global initialized flag TRUE (matches dodge & annibale @ 0xF5B0).
            (0x0F5B0, b"\x00", b"\x01"),
        ),
    ),
    (
        "QcomChargerDxe/QcomChargerDxeLA",
        "9449a16e9197714ca03220279f33fee9558428676287bef76287ea5c69f66914",
        (
            # Bypass crashing charger protocol calls (matches dodge 0xECCC/0xECF0, annibale 0xD0EC/0xD110/0xD134).
            (0x09BC8, bytes.fromhex("00013fd6"), pack_insns(MOV_X0_XZR)),
            (0x09BEC, bytes.fromhex("00013fd6"), pack_insns(MOV_X0_XZR)),
            (0x09C10, bytes.fromhex("00013fd6"), pack_insns(MOV_X0_XZR)),
            # Return EFI_SUCCESS from charger protocol init helper called via 0x8FA0 (matches dodge 0x11A8C, annibale 0xF768).
            (0x0C2B8, bytes.fromhex("fd7bbea9f44f01a9"), pack_insns(MOV_X0_XZR, RET)),
        ),
    ),
    (
        "UsbConfigDxe/UsbConfigDxe",
        "dc7e3cda7ae7a9ce20c5125a8a75985830003043a34da467ae63c8dea0894708",
        (
            # Keep Dual-Role ([0x14794] = 1) armed across plug/unplug cycles so Detach stops Host
            # and subsequent Attach re-evaluates DFP (Host) vs UFP (Client).
            (0x04948, bytes.fromhex("1f511e39"), pack_insns(NOP)),
            # Compare new role w21 (1=Host, 4=Device) instead of old mode w8 so Host attach calls EnableVbus.
            (0x0495C, bytes.fromhex("1f090071"), pack_insns(0x71000ABF)),  # cmp w21, #2
            # Auto-start both Host (DFP, w21=1) and Client (UFP, w21=4) in HandlePortPartnerXtach.
            (0x04A54, bytes.fromhex("d6000037"), pack_insns(NOP)),
            # Skip USB deinit in ExitBootServices (matches dodge @ 0x4BB0).
            (0x04BB0, bytes.fromhex("28010034"), pack_insns(encode_b(0x04BB0, 0x04BD4))),
            # Enable StartControllerOnAttach feature flag for the 100ms Type-C polling handler.
            (0x0E456, b"\x00", b"\x01"),
        ),
    ),
    (
        "UsbPwrCtrlDxe/UsbPwrCtrlDxe",
        "e8be9562d8008b8c788dad3381997f61ce5264572eeab0981b1ee58d7fa231e6",
        (
            # Switch Port 0 charger port type detection from PMIC (2) to fixed SDP (6) (matches annibale @ 0x819C).
            (0x0819C, b"\x02", b"\x06"),
        ),
    ),
    (
        "ButtonsDxe/ButtonsDxe",
        "910ce8adb1d8501264a7a55e019334fcde5010e584e45cb47dd09f93f4a3fec5",
        (
            # Remap Power button scan code 0x102 -> CHAR_CARRIAGE_RETURN (0x000D Enter).
            (0x036B8, bytes.fromhex("48208052d6ffff17"), pack_insns(0x528001A8, encode_b(0x036BC, 0x03670))),
        ),
    ),
)


def main() -> None:
    drivers_dir = Path(__file__).resolve().parent / "QcomPkg" / "Drivers"

    for rel_stem, expected_sha256, edits in PATCH_SPECS:
        src = drivers_dir / f"{rel_stem}.efi"
        dst = drivers_dir / f"{rel_stem}.patched.efi"

        data = bytearray(src.read_bytes())
        actual_sha256 = sha256(data).hexdigest()
        if actual_sha256 != expected_sha256:
            raise SystemExit(
                f"{src}: SHA-256 mismatch (got {actual_sha256}, expected {expected_sha256})"
            )

        for offset, expected_bytes, replacement in edits:
            assert len(expected_bytes) == len(replacement)
            actual_bytes = bytes(data[offset:offset + len(expected_bytes)])
            if actual_bytes != expected_bytes:
                raise SystemExit(
                    f"{src} @ 0x{offset:x}: expected {expected_bytes.hex()}, got {actual_bytes.hex()}"
                )
            data[offset:offset + len(replacement)] = replacement

        checksum = update_pe_checksum(data)
        dst.write_bytes(data)
        print(f"Wrote {dst.relative_to(drivers_dir)} ({len(data)}B, PE CheckSum=0x{checksum:08x}, sha256={sha256(data).hexdigest()[:16]}...)")


if __name__ == "__main__":
    main()
