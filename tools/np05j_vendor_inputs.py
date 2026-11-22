#!/usr/bin/env python3
"""Load and verify the fixed NP05J (REDMAGIC Astra) vendor firmware inputs.

Standalone-compatible with Project-WyUEFI's tools/piano_vendor_inputs.py schema:
uses tools/compose_piano_dtb.py if available, and falls back to a built-in FDT
parser so verification also runs directly inside the extracted migration bundle.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import uuid

try:
    from compose_piano_dtb import read_fdt  # type: ignore
except ImportError:
    def _cstr(buf: bytes, off: int) -> str:
        end = buf.find(b"\x00", off)
        return buf[off:end if end >= 0 else len(buf)].decode("ascii", "replace")

    def read_fdt(data: bytes) -> dict:
        if len(data) < 40 or struct.unpack_from(">I", data, 0)[0] != 0xD00DFEED:
            raise ValueError("Invalid FDT magic")
        totalsize, off_struct, off_strings, off_rsv, ver, last_comp, _boot, sz_str, sz_struct = struct.unpack_from(
            ">9I", data, 4
        )
        if totalsize > len(data) or ver < 16 or last_comp > 17:
            raise ValueError("Invalid FDT header bounds")
        structs = data[off_struct:off_struct + sz_struct]
        strings = data[off_strings:off_strings + sz_str]
        tree = {}
        stack = []
        pos = 0
        while pos + 4 <= len(structs):
            tok = struct.unpack_from(">I", structs, pos)[0]
            pos += 4
            if tok == 1:  # FDT_BEGIN_NODE
                end = structs.find(b"\x00", pos)
                if end < 0:
                    raise ValueError("Unterminated FDT node name")
                name = structs[pos:end].decode("ascii", "replace")
                pos = (end + 4) & ~3
                path = "/" if not stack else ("/" + name if stack[-1] == "/" else stack[-1] + "/" + name)
                stack.append(path)
                tree.setdefault(path, {})
            elif tok == 2:  # FDT_END_NODE
                if not stack:
                    raise ValueError("FDT_END_NODE underflow")
                stack.pop()
            elif tok == 3:  # FDT_PROP
                plen, nameoff = struct.unpack_from(">II", structs, pos)
                pos += 8
                val = structs[pos:pos + plen]
                pos = (pos + plen + 3) & ~3
                if not stack:
                    raise ValueError("FDT_PROP outside node")
                tree[stack[-1]][_cstr(strings, nameoff)] = val
            elif tok == 4:  # FDT_NOP
                continue
            elif tok == 9:  # FDT_END
                break
            else:
                raise ValueError(f"Unknown FDT token 0x{tok:x}")
        return {"tree": tree}

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = "vendor/np05j"
MANIFEST_SHA256 = "39e36ce24f7ca1a98dc5b305c62f13e419bc29593d6407ed7dff6988a44ae70c"
BOARD_DTB = BUNDLE + "/dtb/np05j.dtb"
BOARD_DTB_SHA256 = "b7f6b40bd77ba6baa26bd209b53141882af85bab815cd182910007f43f1155c2"
BOARD_SOURCE_SHA256 = "1d8a756de7b930d9df858eb07a380738a00e5fb44f08f7814f02e829682de5fb"
XBL_DTB = BUNDLE + "/dtb/xbl-config.dtb"
XBL_DTB_SHA256 = "4587131d746da33c1c8da97728ab9318d5e671de742a73b0c312d8a021234621"
NATIVE_NAMES = (
    "SmemDxe", "DALSys", "ChipInfo", "PlatformInfoDxeDriver",
    "HWIODxeDriver", "ULogDxe", "CmdDbDxe", "PwrUtilsDxe",
    "RpmhDxe", "NpaDxe", "VcsDxe", "ClockDxe", "HALIOMMU",
    "DALTLMM", "Qup", "I2C",
)
SNAPSHOT_PROPERTIES = frozenset((
    "linux,initrd-start", "linux,initrd-end", "kaslr-seed", "rng-seed",
    "serial-number", "bootargs", "linux,elfcorehdr", "linux,usable-memory-range",
    "linux,uefi-system-table", "linux,uefi-mmap-start", "linux,uefi-mmap-size",
    "linux,uefi-mmap-desc-size", "linux,uefi-mmap-desc-ver",
))


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate vendor manifest key: " + key)
        result[key] = value
    return result


def _relative(name):
    if not isinstance(name, str) or not name or "\\" in name or ":" in name:
        raise ValueError("Unsafe vendor input path")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in ("", ".", "..") for part in name.split("/")):
        raise ValueError("Unsafe vendor input path: " + name)
    return path


def _path(base: Path, name: str) -> Path:
    path = base
    for part in _relative(name).parts:
        path = path / part
        if path.is_symlink():
            raise ValueError("Vendor input is a symlink: " + name)
    if not path.is_file():
        raise ValueError("Missing vendor input: " + name)
    return path


def _manifest(raw: bytes) -> dict:
    manifest = json.loads(raw, object_pairs_hook=_unique_pairs)
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError("Unsupported vendor manifest")
    required = {"schema_version", "target", "scope", "native_drivers", "board_dtb",
                 "xbl_config_dtb", "provenance", "files"}
    if set(manifest) != required:
        raise ValueError("Vendor manifest field set differs")
    drivers, files = manifest.get("native_drivers"), manifest.get("files")
    if not isinstance(drivers, dict) or set(drivers) != set(NATIVE_NAMES):
        raise ValueError("Vendor native driver set differs")
    referenced = [manifest["board_dtb"]["path"], manifest["xbl_config_dtb"]["path"]]
    for name in NATIVE_NAMES:
        row = drivers[name]
        uuid.UUID(row["file_guid"])
        referenced.append(row["pe"])
        if row["depex"] is not None:
            referenced.append(row["depex"])
    if set(files) != set(referenced):
        raise ValueError("Vendor file inventory mismatch")
    if sha(raw) != MANIFEST_SHA256:
        raise ValueError("Fixed vendor manifest SHA256 mismatch")
    return manifest


def validate_pe(data: bytes) -> None:
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("Native PE DOS header invalid")
    pe = struct.unpack_from("<I", data, 60)[0]
    if pe < 64 or pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Native PE header bounds invalid")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional, table = pe + 24, pe + 24 + optional_size
    if machine != 0xAA64 or not 1 <= count <= 96 or optional_size < 112 or table + count * 40 > len(data):
        raise ValueError("Native PE machine/section table invalid")
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError("Native PE must be PE32+")
    entry = struct.unpack_from("<I", data, optional + 16)[0]
    image_size, header_size = struct.unpack_from("<II", data, optional + 56)
    subsystem = struct.unpack_from("<H", data, optional + 68)[0]
    if not entry or entry & 3 or entry >= image_size or not table + count * 40 <= header_size <= len(data) or subsystem not in (11, 12):
        raise ValueError("Native PE image/header/entry bounds invalid")
    executable_entry = False
    for index in range(count):
        at = table + index * 40
        virtual_size, virtual, raw_size, raw = struct.unpack_from("<IIII", data, at + 8)
        flags = struct.unpack_from("<I", data, at + 36)[0]
        if (flags & 0x20000000) and virtual <= entry and entry + 4 <= virtual + raw_size:
            executable_entry = True
    if not executable_entry:
        raise ValueError("Native PE entry is not backed by executable code")


def validate_depex(data: bytes) -> None:
    depth, offset = 0, 0
    while offset < len(data):
        opcode = data[offset]
        offset += 1
        if opcode in (0, 1):
            if offset != 1 or len(data) != 18 or data[-1] != 8:
                raise ValueError("Native DEPEX BEFORE/AFTER bounds invalid")
            return
        if opcode == 2:
            if offset + 16 > len(data):
                raise ValueError("Native DEPEX GUID truncated")
            offset += 16
            depth += 1
        elif opcode in (3, 4):
            if depth < 2:
                raise ValueError("Native DEPEX binary stack underflow")
            depth -= 1
        elif opcode == 5:
            if not depth:
                raise ValueError("Native DEPEX unary stack underflow")
        elif opcode in (6, 7):
            depth += 1
        elif opcode == 8:
            if offset != len(data) or depth != 1:
                raise ValueError("Native DEPEX end/stack invalid")
            return
        elif opcode != 9 or offset != 1:
            raise ValueError("Native DEPEX opcode invalid")
    raise ValueError("Native DEPEX missing END")


def validate_board_dtb(data: bytes) -> dict:
    parsed = read_fdt(data)
    if any(SNAPSHOT_PROPERTIES & parsed["tree"].get(node, {}).keys() for node in ("/", "/chosen")):
        raise ValueError("Board DTB contains stale/private snapshot fields")
    return parsed


def load(root=ROOT) -> dict:
    root = Path(root)
    base = root / BUNDLE
    raw = _path(base, "manifest.json").read_bytes()
    manifest = _manifest(raw)
    data, paths = {}, {}
    for name, expected in manifest["files"].items():
        path = _path(base, name)
        value = path.read_bytes()
        if len(value) != expected["bytes"] or sha(value) != expected["sha256"]:
            raise ValueError("Vendor input mismatch: " + name)
        data[name], paths[name] = value, path
    drivers = {}
    for name, row in manifest["native_drivers"].items():
        validate_pe(data[row["pe"]])
        if row["depex"] is not None:
            validate_depex(data[row["depex"]])
        drivers[name] = {
            "file_guid": row["file_guid"],
            "pe_path": paths[row["pe"]],
            "pe_sha256": manifest["files"][row["pe"]]["sha256"],
            "depex_path": paths[row["depex"]] if row["depex"] else None,
        }
    board = data[manifest["board_dtb"]["path"]]
    validate_board_dtb(board)
    xbl_tree = read_fdt(data[manifest["xbl_config_dtb"]["path"]])["tree"]
    ss_nodes = [p for p, props in xbl_tree.items() if p.endswith("/usb0/ss_phy_cfg") and "table" in props]
    if len(ss_nodes) != 1:
        raise ValueError("Expected one NP05J XBL /usb0/ss_phy_cfg table")
    return {
        "manifest": manifest,
        "native_drivers": drivers,
        "board_dtb": board,
        "xbl_config_dtb": data[manifest["xbl_config_dtb"]["path"]],
        "paths": paths,
    }


def source_files(root=ROOT):
    root = Path(root)
    bundle = load(root)
    return (
        root / "tools/np05j_vendor_inputs.py",
        root / BUNDLE / "manifest.json",
        *bundle["paths"].values(),
    )


def record(root=ROOT, bundle=None):
    root = Path(root)
    bundle = load(root) if bundle is None else bundle
    return {
        "manifest": BUNDLE + "/manifest.json",
        "manifest_sha256": MANIFEST_SHA256,
        "loader_sha256": sha((root / "tools/np05j_vendor_inputs.py").read_bytes()),
        "files": {BUNDLE + "/" + name: item["sha256"] for name, item in bundle["manifest"]["files"].items()},
        "board_original_scope_sha256": BOARD_SOURCE_SHA256,
        "board_snapshot_properties_removed": bundle["manifest"]["board_dtb"]["removed_properties"],
        "runtime_write_authorized": False,
    }


if __name__ == "__main__":
    loaded = load()
    print(json.dumps({
        "status": "FIXED_VENDOR_INPUTS_VERIFIED_NOT_RUNTIME_AUTHORITY",
        "manifest_sha256": MANIFEST_SHA256,
        "native_drivers": len(loaded["native_drivers"]),
        "files": len(loaded["paths"]),
        "bytes": sum(item["bytes"] for item in loaded["manifest"]["files"].values()),
        "scope": loaded["manifest"]["scope"],
        "runtime_write_authorized": False,
    }, indent=2))
