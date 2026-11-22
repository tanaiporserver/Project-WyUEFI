#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Package NP05J SunUEFI KernelSU/SukiSU/Magisk Helper flashable ZIP."""

from pathlib import Path
import stat
import sys
import zipfile


def add_file(zf: zipfile.ZipFile, src: Path, arcname: str, executable: bool = False) -> None:
    info = zipfile.ZipInfo(arcname)
    info.compress_type = zipfile.ZIP_DEFLATED
    mode = 0o755 if executable else 0o644
    info.external_attr = (stat.S_IFREG | mode) << 16
    zf.writestr(info, src.read_bytes())


def main() -> int:
    here = Path(__file__).resolve().parent
    repo = here.parents[3]
    mod_dir = here / "module"
    out_dir = repo / "Build" / "nubia_np05jPkg"
    bootselect_dir = out_dir / "BootSelect"
    repack_arm64 = out_dir / "AndroidHelper" / "np05j-boot-repack-arm64"
    uefi_kernel = out_dir / "nubia_np05j-kernel"
    out_zip = out_dir / "NP05J-SunUEFI-Helper.zip"

    required = [
        repack_arm64,
        bootselect_dir / "np05j-bootselect.bin",
        bootselect_dir / "np05j-reloc-shim.bin",
        bootselect_dir / "selector.json",
        uefi_kernel,
    ]
    for req in required:
        if not req.is_file():
            print(f"Missing build artifact: {req}", file=sys.stderr)
            return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(mod_dir.rglob("*")):
            if f.is_file():
                rel = f.relative_to(mod_dir).as_posix()
                add_file(zf, f, rel, executable=(f.suffix == ".sh"))

        add_file(zf, repack_arm64, "bin/np05j-boot-repack", executable=True)
        add_file(zf, bootselect_dir / "np05j-bootselect.bin", "payloads/np05j-bootselect.bin")
        add_file(zf, bootselect_dir / "np05j-reloc-shim.bin", "payloads/np05j-reloc-shim.bin")
        add_file(zf, bootselect_dir / "selector.json", "payloads/selector.json")
        add_file(zf, uefi_kernel, "payloads/nubia_np05j-kernel")

    print(f"Created {out_zip} ({out_zip.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
