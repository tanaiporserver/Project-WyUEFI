#!/usr/bin/env python3
"""สร้างอิมเมจ WinPE ARM64 (4Kn FAT32 + Android Sparse <= 512 MiB) สำหรับ nubia NP05J

รองรับการแฟลชลงพาร์ติชัน sunuefi_esp ผ่าน `fastboot flash sunuefi_esp winpe.img`
พร้อมไดรเวอร์จอสัมผัส Synaptics S3930TB0A0 (Np05jSynaTouch.sys, KMDF + VHF)
และชุดไดรเวอร์ต้นแบบจาก Lenovo Yoga Slim 7x (83ED / Snapdragon X Elite).
"""

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOUCH_SRC_DIR = ROOT / "windows" / "drivers" / "np05j-syna-touch"

# URLs ทางการจาก Microsoft ADK WinPE Add-on (Windows 11 24H2 Build 26100.1 / Bootfiles 28000)
ADK_BASE_URL = "https://download.microsoft.com/download/a4a79e7a-f085-41c4-aebf-2538fd000790/adkwinpeaddons/Installers"
ADK_CABS = {
    "bcd.cab": f"{ADK_BASE_URL}/13334584cdbf659a8dd105fc34851ea3.cab",
    "bootfiles_arm64.cab": f"{ADK_BASE_URL}/e4ef343897232361feeab2810591d8ae.cab",
    "winpe_arm64.cab": f"{ADK_BASE_URL}/8686aa39453398c3a04c1edaac29ca7a.cab",
}

ADK_FILE_IDS = {
    "bcd": "filf6356703147169a9a35ca5b2c66bd584",
    "bootaa64.efi": "fil64d56a4a9083ab078bf24e6c06c7696d",
    "boot.sdi": "fil321a21d291a416daf144125ab2d3ba5d",
    "segmono_boot.ttf": "filcd49672825a00f7eb126b7c4b8cc6211",
    "segoe_slboot.ttf": "fila9173a41a7495d17b9282d42983dd3cd",
    "wgl4_boot.ttf": "fil0044398d0b4b964d55fd6452f25bf0a3",
    "winpe.wim": "fil5f72e8ecdc1570b7c59ebb89c3be8355",
}

LENOVO_ISO_BASE = "https://archive.org/download/lenovo-83ed-x1e-drivers/DriverBackup.iso/"
LENOVO_CORE_FOLDERS = (
    "storufs.inf_arm64_59a76d5636367a81",
    "hidvhf.inf_arm64_f11ae49fc1ea2fe5",
    "hidspi_km.inf_arm64_f49389d17fe373d7",
    "hidi2c.inf_arm64_0be4948cbb2c1f5b",
    "hidinterrupt.inf_arm64_eadc8e55ce0c03d5",
    "mshidkmdf.inf_arm64_7c5613d8edf33056",
    "msgpiowin32.inf_arm64_de50e17c41e74b9a",
    "qcursext.inf_arm64_265000c35437c204",
    "qcxhcifilter8380.inf_arm64_d91ff03caff6172f",
)
LENOVO_OPTIONAL_FOLDERS = (
    "qcgpio8380.inf_arm64_8eaa8d5a094cefff",
    "qcgpi8380.inf_arm64_89891263016c14d5",
    "qcspi8380.inf_arm64_2f5b677929af4d43",
    "qcuart8380.inf_arm64_fe57f9e74ac9be41",
)

WDK_VERSION = "10.0.28000.2526"
SDK_VERSION = "10.0.28000.2957"
KIT_VER_DIR = "10.0.28000.0"
KMDF_VER = "1.31"

MIB = 1024 * 1024
SPARSE_MAGIC = 0xED26FF3A
SPARSE_RAW = 0xCAC1
SPARSE_FILL = 0xCAC2


def run(cmd, **kwargs):
    print("+", " ".join(str(x) for x in cmd), flush=True)
    return subprocess.run(cmd, check=True, **kwargs)


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(4 * MIB), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_file(url: str, dst: pathlib.Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.is_file() and dst.stat().st_size > 0:
        return
    tmp = dst.with_suffix(dst.suffix + ".part")
    run(["curl", "-fSL", url, "-o", str(tmp)])
    tmp.replace(dst)


def ensure_adk_inputs(work_dir: pathlib.Path) -> dict:
    cabs_dir = work_dir / "cabs"
    adk_dir = work_dir / "adk"
    for name, url in ADK_CABS.items():
        ensure_file(url, cabs_dir / name)

    for sub, cab in (("bcd", "bcd.cab"), ("bootfiles", "bootfiles_arm64.cab"), ("wim", "winpe_arm64.cab")):
        out = adk_dir / sub
        if not out.is_dir() or not any(out.iterdir()):
            out.mkdir(parents=True, exist_ok=True)
            run(["cabextract", "-q", "-d", str(out), str(cabs_dir / cab)])

    return {
        "bcd": adk_dir / "bcd" / ADK_FILE_IDS["bcd"],
        "bootaa64.efi": adk_dir / "bootfiles" / ADK_FILE_IDS["bootaa64.efi"],
        "boot.sdi": adk_dir / "bootfiles" / ADK_FILE_IDS["boot.sdi"],
        "segmono_boot.ttf": adk_dir / "bootfiles" / ADK_FILE_IDS["segmono_boot.ttf"],
        "segoe_slboot.ttf": adk_dir / "bootfiles" / ADK_FILE_IDS["segoe_slboot.ttf"],
        "wgl4_boot.ttf": adk_dir / "bootfiles" / ADK_FILE_IDS["wgl4_boot.ttf"],
        "winpe.wim": adk_dir / "wim" / ADK_FILE_IDS["winpe.wim"],
    }


def ensure_wdk_sdk(work_dir: pathlib.Path) -> pathlib.Path:
    wdk_root = work_dir / "wdk"
    wdk_inc = wdk_root / "wdk" / "c" / "Include" / KIT_VER_DIR
    sdk_inc = wdk_root / "sdk" / "c" / "Include" / KIT_VER_DIR
    if wdk_inc.is_dir() and sdk_inc.is_dir():
        return wdk_root

    nupkg_dir = wdk_root / "nupkg"
    wdk_pkg = nupkg_dir / "wdk.nupkg"
    sdk_pkg = nupkg_dir / "sdk.nupkg"
    ensure_file(
        f"https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.arm64/{WDK_VERSION}/microsoft.windows.wdk.arm64.{WDK_VERSION}.nupkg",
        wdk_pkg,
    )
    ensure_file(
        f"https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp/{SDK_VERSION}/microsoft.windows.sdk.cpp.{SDK_VERSION}.nupkg",
        sdk_pkg,
    )
    for pkg, dst in ((wdk_pkg, wdk_root / "wdk"), (sdk_pkg, wdk_root / "sdk")):
        dst.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(pkg) as zf:
            for member in zf.namelist():
                if member.startswith("c/Include/") or member.startswith("c/Lib/"):
                    zf.extract(member, dst)
    return wdk_root


def ensure_lenovo_drivers(work_dir: pathlib.Path) -> pathlib.Path:
    lenovo_dir = work_dir / "lenovo"
    lenovo_dir.mkdir(parents=True, exist_ok=True)
    all_folders = LENOVO_CORE_FOLDERS + LENOVO_OPTIONAL_FOLDERS
    if all((lenovo_dir / folder).is_dir() and any((lenovo_dir / folder).iterdir()) for folder in all_folders):
        return lenovo_dir

    index_html = work_dir / "lenovo_iso_index.html"
    ensure_file(LENOVO_ISO_BASE, index_html)
    html = index_html.read_text(errors="replace")
    hrefs = re.findall(r'href="([^"]+)"', html)
    iso_files = [
        urllib.parse.unquote(h).split("DriverBackup.iso/", 1)[1]
        for h in hrefs
        if "DriverBackup.iso/" in urllib.parse.unquote(h)
    ]
    for folder in all_folders:
        files = [f for f in iso_files if f.startswith(folder + "/") and not f.endswith("/")]
        if not files:
            raise RuntimeError(f"ไม่พบโฟลเดอร์ไดรเวอร์ {folder} ใน archive.org Lenovo 83ED ISO")
        for rel in files:
            ensure_file(LENOVO_ISO_BASE + urllib.parse.quote(rel), lenovo_dir / rel)
    return lenovo_dir


def build_and_sign_touch_driver(wdk_root: pathlib.Path, out_dir: pathlib.Path) -> dict:
    """คอมไพล์ Np05jSynaTouch.sys (ARM64 KMDF + VHF) และลงนาม test-signing certificate"""
    out_dir.mkdir(parents=True, exist_ok=True)
    wdk = wdk_root / "wdk" / "c"
    sdk = wdk_root / "sdk" / "c"
    wdf_inc = wdk / "Include" / "wdf" / "kmdf" / KMDF_VER

    # สร้าง symlink สำหรับชื่อ header ที่ case-sensitive บน Linux (wdf.h อ้างชื่อผสมพิมพ์เล็ก/ใหญ่)
    case_inc = out_dir / "wdf-case-inc"
    case_inc.mkdir(exist_ok=True)
    wdf_h_text = (wdf_inc / "wdf.h").read_text(errors="ignore")
    for hdr in re.findall(r'^\s*#include\s+"([^"]+\.h)"', wdf_h_text, re.M):
        if not (wdf_inc / hdr).exists() and (wdf_inc / hdr.lower()).exists():
            link = case_inc / hdr
            if link.exists() or link.is_symlink():
                link.unlink()
            link.symlink_to(wdf_inc / hdr.lower())

    # เตรียม symlink ตัวพิมพ์เล็กของไลบรารีที่ clang/lld อาจหาด้วยตัวพิมพ์ต่างกัน
    km_lib = wdk / "Lib" / KIT_VER_DIR / "km" / "ARM64"
    wdf_lib = wdk / "Lib" / "wdf" / "kmdf" / "ARM64" / KMDF_VER
    obj = out_dir / "Np05jSynaTouch.obj"
    sys_unsigned = out_dir / "Np05jSynaTouch.unsigned.sys"
    sys_signed = out_dir / "Np05jSynaTouch.sys"

    run([
        "clang",
        "--driver-mode=cl",
        "--target=aarch64-pc-windows-msvc",
        "/nologo",
        "/c",
        "/kernel",
        "/O2",
        "/GS",
        "/W3",
        "/D_ARM64_",
        "/D_KERNEL_MODE",
        "/DWINVER=0x0A00",
        "/D_WIN32_WINNT=0x0A00",
        "/DNTDDI_VERSION=0x0A000008",
        f"/imsvc{wdk / 'Include' / KIT_VER_DIR / 'km'}",
        f"/imsvc{wdk / 'Include' / KIT_VER_DIR / 'km' / 'crt'}",
        f"/imsvc{wdk / 'Include' / KIT_VER_DIR / 'shared'}",
        f"/imsvc{wdf_inc}",
        f"/imsvc{case_inc}",
        f"/imsvc{sdk / 'Include' / KIT_VER_DIR / 'shared'}",
        f"/imsvc{sdk / 'Include' / KIT_VER_DIR / 'ucrt'}",
        f"/imsvc{sdk / 'Include' / KIT_VER_DIR / 'um'}",
        f"/Fo{obj}",
        "--",
        str(TOUCH_SRC_DIR / "Np05jSynaTouch.c"),
    ])

    run([
        "lld-link",
        "-nologo",
        "-machine:arm64",
        "-driver",
        "-subsystem:native",
        "-nodefaultlib",
        "-integritycheck",
        "-release",
        "-entry:FxDriverEntry",
        f"-out:{sys_unsigned}",
        str(obj),
        str(wdf_lib / "wdfdriverentry.lib"),
        str(wdf_lib / "wdfldr.lib"),
        str(km_lib / "vhfkm.lib"),
        str(km_lib / "ntoskrnl.lib"),
        str(km_lib / "hal.lib"),
        str(km_lib / "bufferoverflowfastfailk.lib"),
        str(km_lib / "libcntpr.lib"),
    ])

    # สร้าง Test-Signing Certificate แบบชั่วคราว (ไม่เก็บ private key ลงใน git)
    key_pem = out_dir / "testsign.key.pem"
    cert_pem = out_dir / "testsign.cert.pem"
    cert_der = out_dir / "np05j-testsign.cer"
    pfx_path = out_dir / "testsign.pfx"
    ext_cfg = out_dir / "codesign.cnf"
    ext_cfg.write_text(
        "[req]\n"
        "distinguished_name = dn\n"
        "x509_extensions = v3_ext\n"
        "prompt = no\n"
        "[dn]\n"
        "CN = Project WyUEFI NP05J Test Signing\n"
        "O = Project WyUEFI\n"
        "[v3_ext]\n"
        "basicConstraints = critical,CA:TRUE\n"
        "keyUsage = critical,digitalSignature,keyCertSign\n"
        "extendedKeyUsage = codeSigning\n"
    )
    run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", str(key_pem), "-out", str(cert_pem),
        "-days", "3650", "-sha256", "-config", str(ext_cfg),
    ])
    run(["openssl", "x509", "-in", str(cert_pem), "-outform", "DER", "-out", str(cert_der)])
    run([
        "openssl", "pkcs12", "-export",
        "-inkey", str(key_pem), "-in", str(cert_pem),
        "-out", str(pfx_path), "-passout", "pass:np05jtest",
    ])

    if sys_signed.exists():
        sys_signed.unlink()
    run([
        "osslsigncode", "sign",
        "-pkcs12", str(pfx_path),
        "-pass", "np05jtest",
        "-h", "sha256",
        "-n", "NP05J Synaptics S3930TB0A0 Touchscreen Driver",
        "-in", str(sys_unsigned),
        "-out", str(sys_signed),
    ])
    run(["osslsigncode", "verify", "-CAfile", str(cert_pem), "-in", str(sys_signed)])

    pkg_dir = out_dir / "pkg"
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)
    pkg_dir.mkdir(parents=True)
    shutil.copy2(TOUCH_SRC_DIR / "Np05jSynaTouch.inf", pkg_dir / "Np05jSynaTouch.inf")
    shutil.copy2(sys_signed, pkg_dir / "Np05jSynaTouch.sys")
    shutil.copy2(cert_der, pkg_dir / "np05j-testsign.cer")

    der_bytes = cert_der.read_bytes()
    sha1_hex = hashlib.sha1(der_bytes).hexdigest().upper()
    return {
        "pkg_dir": pkg_dir,
        "sys_path": sys_signed,
        "cert_der": cert_der,
        "thumbprint_sha1": sha1_hex,
    }


def build_cert_registry_blob(der_bytes: bytes) -> bytes:
    """สร้างโครงสร้าง CryptoAPI Serialized Certificate Store Blob สำหรับใส่ใน Registry SOFTWARE hive"""
    sha1 = hashlib.sha1(der_bytes).digest()
    md5 = hashlib.md5(der_bytes).digest()

    def prop(prop_id: int, payload: bytes) -> bytes:
        return struct.pack("<III", prop_id, 1, len(payload)) + payload

    # Property 3 = CERT_HASH_PROP_ID (SHA1), 4 = CERT_MD5_HASH_PROP_ID, 32 = CERT_CERT_PROP_ID (DER)
    return prop(3, sha1) + prop(4, md5) + prop(32, der_bytes)


def inject_cert_into_software_hive(software_hive: pathlib.Path, der_bytes: bytes, thumbprint_sha1: str):
    import hivex

    blob = build_cert_registry_blob(der_bytes)
    h = hivex.Hivex(str(software_hive), write=True)
    root = h.root()

    def ensure_path(parts):
        node = root
        for part in parts:
            child = h.node_get_child(node, part)
            if child is None:
                child = h.node_add_child(node, part)
            node = child
        return node

    for store in ("ROOT", "TrustedPublisher"):
        node = ensure_path(["Microsoft", "SystemCertificates", store, "Certificates", thumbprint_sha1])
        h.node_set_value(node, {"key": "Blob", "t": 3, "value": blob})

    h.commit(str(software_hive))
    del h


def patch_bcd_for_np05j(src_bcd: pathlib.Path, dst_bcd: pathlib.Path):
    """ปรับแต่ง BCD ให้เปิด testsigning, nointegritychecks, ปิด hypervisor, เปิด sos และ bootlog"""
    import hivex

    shutil.copy2(src_bcd, dst_bcd)
    h = hivex.Hivex(str(dst_bcd), write=True)
    root = h.root()
    objs = h.node_get_child(root, "Objects")
    if objs is None:
        raise RuntimeError("ไม่พบ Objects ใน BCD hive")

    def set_elem(obj_guid: str, elem_id: str, val_type: int, raw: bytes):
        obj = h.node_get_child(objs, obj_guid)
        if obj is None:
            raise RuntimeError(f"ไม่พบ BCD Object {obj_guid}")
        elems = h.node_get_child(obj, "Elements")
        if elems is None:
            elems = h.node_add_child(obj, "Elements")
        enode = h.node_get_child(elems, elem_id)
        if enode is None:
            enode = h.node_add_child(elems, elem_id)
        h.node_set_value(enode, {"key": "Element", "t": val_type, "value": raw})

    winpe_guid = "{7619dcc9-fafe-11d9-b411-000476eba25f}"
    bootmgr_guid = "{9dea862c-5cdd-4e70-acc1-f32b344d4795}"

    # Boot Loader Elements (WinPE ARM64)
    set_elem(winpe_guid, "12000004", 1, ("NP05J WinPE ARM64 (Touch)\x00").encode("utf-16-le"))
    set_elem(winpe_guid, "16000048", 3, b"\x01")                           # DisableIntegrityChecks = Yes
    set_elem(winpe_guid, "16000049", 3, b"\x01")                           # AllowPrereleaseSignatures (testsigning) = Yes
    set_elem(winpe_guid, "250000e0", 3, struct.pack("<Q", 1))              # BootStatusPolicy = IgnoreAllFailures
    set_elem(winpe_guid, "250000f0", 3, struct.pack("<Q", 0))              # HypervisorLaunchType = Off
    set_elem(winpe_guid, "26000090", 3, b"\x01")                           # BootLog = Yes
    set_elem(winpe_guid, "26000091", 3, b"\x01")                           # SOS = Yes

    # Boot Manager Timeout = 3 seconds
    set_elem(bootmgr_guid, "25000004", 3, struct.pack("<Q", 3))

    h.commit(str(dst_bcd))
    del h


def customize_boot_wim(
    base_wim: pathlib.Path,
    out_wim: pathlib.Path,
    touch_info: dict,
    lenovo_dir: pathlib.Path,
    stage_dir: pathlib.Path,
):
    """อัปเดต boot.wim ด้วย Test Cert, ไดรเวอร์ทัช NP05J, ไดรเวอร์ Lenovo 83ED, และ startnet.cmd"""
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True)

    # 1. ดึง SOFTWARE hive ออกมาฝัง Test-Signing Certificate ลง ROOT + TrustedPublisher
    hive_dir = stage_dir / "hive"
    hive_dir.mkdir()
    run([
        "wimextract", str(base_wim), "1",
        "/Windows/System32/config/SOFTWARE",
        f"--dest-dir={hive_dir}", "--no-acls",
    ])
    software_hive = hive_dir / "SOFTWARE"
    inject_cert_into_software_hive(
        software_hive,
        touch_info["cert_der"].read_bytes(),
        touch_info["thumbprint_sha1"],
    )

    # 2. เตรียมโครงสร้างโฟลเดอร์ไดรเวอร์ X:\Drivers
    drv_stage = stage_dir / "Drivers"
    shutil.copytree(touch_info["pkg_dir"], drv_stage / "Touch" / "Np05jSynaTouch")

    core_dst = drv_stage / "Lenovo83ED_Core"
    for folder in LENOVO_CORE_FOLDERS:
        shutil.copytree(lenovo_dir / folder, core_dst / folder)

    opt_dst = drv_stage / "Lenovo83ED_Optional"
    for folder in LENOVO_OPTIONAL_FOLDERS:
        shutil.copytree(lenovo_dir / folder, opt_dst / folder)

    # 3. สร้าง X:\Windows\System32\startnet.cmd (CRLF) ให้โหลดไดรเวอร์ทัชและ USB/URS อัตโนมัติตอนบูต WinPE
    startnet = stage_dir / "startnet.cmd"
    startnet.write_bytes(
        (
            "@echo off\r\n"
            "title NP05J WinPE ARM64 (Touch + Lenovo 83ED Drivers)\r\n"
            "echo ============================================================\r\n"
            "echo  nubia NP05J (SM8750P) WinPE ARM64 Bring-up Environment\r\n"
            "echo ============================================================\r\n"
            "wpeinit\r\n"
            "\r\n"
            "echo [1/4] Loading NP05J Synaptics S3930TB0A0 Touchscreen (KMDF + VHF)...\r\n"
            "drvload X:\\Drivers\\Touch\\Np05jSynaTouch\\Np05jSynaTouch.inf\r\n"
            "\r\n"
            "echo [2/4] Loading Lenovo 83ED USB/URS Extensions (QCOMFFE1 / URS)...\r\n"
            "drvload X:\\Drivers\\Lenovo83ED_Core\\qcursext.inf_arm64_265000c35437c204\\qcursext.inf\r\n"
            "drvload X:\\Drivers\\Lenovo83ED_Core\\qcxhcifilter8380.inf_arm64_d91ff03caff6172f\\QcXhciFilter8380.inf\r\n"
            "\r\n"
            "echo [3/4] Checking Np05jSynaTouch diagnostics...\r\n"
            "reg query HKLM\\SYSTEM\\CurrentControlSet\\Services\\Np05jSynaTouch\\Parameters 2>nul\r\n"
            "\r\n"
            "echo [4/4] Connected HID / Storage devices:\r\n"
            "pnputil /enum-devices /class HIDClass /connected 2>nul\r\n"
            "pnputil /enum-devices /class SCSIAdapter /connected 2>nul\r\n"
            "echo.\r\n"
            "echo Optional Qualcomm 8380 bus drivers are in X:\\Drivers\\Lenovo83ED_Optional\r\n"
            "echo Run 'echo list disk ^| diskpart' to inspect UFS/USB disks.\r\n"
            "echo.\r\n"
        ).encode("ascii")
    )

    # 4. คัดลอก base_wim -> out_wim แล้วอัปเดตและบีบอัดใหม่ด้วย LZX:100
    out_wim.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(base_wim, out_wim)
    update_cmds = stage_dir / "wimupdate.txt"
    update_cmds.write_text(
        f"add {software_hive} /Windows/System32/config/SOFTWARE\n"
        f"add {startnet} /Windows/System32/startnet.cmd\n"
        f"add {drv_stage} /Drivers\n"
    )
    with update_cmds.open("rb") as f:
        run(["wimupdate", str(out_wim), "1"], stdin=f)
    run(["wimoptimize", str(out_wim), "--recompress", "--compress=LZX:100"])


def convert_to_android_sparse(src: pathlib.Path, dst: pathlib.Path, block_size: int = 4096, max_raw_mib: int = 32) -> dict:
    """แปลงไฟล์ raw FAT32 4Kn เป็น Android Sparse Image (chunk <= 32 MiB รองรับ SunUEFI Fastboot 64 MiB cap)"""
    size = src.stat().st_size
    if not size or size % block_size != 0:
        raise ValueError("Raw image size must be a nonzero multiple of 4096")
    chunks = 0
    blocks = 0
    expanded = hashlib.sha256()
    zero = bytes(block_size)
    max_raw_bytes = max_raw_mib * MIB

    if dst.exists():
        dst.unlink()
    with src.open("rb") as sf, dst.open("wb+") as df:
        df.write(bytes(28))
        kind = None
        start = 0
        count = 0

        def finish():
            nonlocal chunks
            if not count:
                return
            end = df.tell()
            payload_size = count * block_size if kind == SPARSE_RAW else 4
            df.seek(start)
            df.write(struct.pack("<HHII", kind, 0, count, 12 + payload_size))
            df.seek(end)
            chunks += 1

        while batch := sf.read(4 * MIB):
            expanded.update(batch)
            for off in range(0, len(batch), block_size):
                blk = batch[off : off + block_size]
                next_kind = SPARSE_FILL if blk == zero else SPARSE_RAW
                if next_kind != kind or (kind == SPARSE_RAW and count * block_size >= max_raw_bytes):
                    finish()
                    kind = next_kind
                    count = 0
                    start = df.tell()
                    df.write(bytes(12))
                    if kind == SPARSE_FILL:
                        df.write(bytes(4))
                if kind == SPARSE_RAW:
                    df.write(blk)
                count += 1
                blocks += 1

        finish()
        df.seek(0)
        df.write(struct.pack("<IHHHHIIII", SPARSE_MAGIC, 1, 0, 28, 12, block_size, blocks, chunks, 0))

    return {
        "sparse_bytes": dst.stat().st_size,
        "expanded_bytes": size,
        "sparse_sha256": sha256_file(dst),
        "expanded_sha256": expanded.hexdigest(),
        "block_bytes": block_size,
        "max_raw_chunk_mib": max_raw_mib,
    }


def build_fat32_esp(raw_img: pathlib.Path, size_mib: int, adk_files: dict, bcd_path: pathlib.Path, boot_wim: pathlib.Path):
    """สร้างอิมเมจ FAT32 4Kn (-S 4096) ขนาด size_mib MiB พร้อมไฟล์บูต Windows PE ARM64"""
    raw_img.parent.mkdir(parents=True, exist_ok=True)
    if raw_img.exists():
        raw_img.unlink()
    total_bytes = size_mib * MIB
    with raw_img.open("wb") as f:
        f.truncate(total_bytes)

    run(["mkfs.vfat", "-F", "32", "-S", "4096", "-s", "1", "-n", "SUNUEFI_ESP", str(raw_img)])

    env = os.environ.copy()
    env["MTOOLS_SKIP_CHECK"] = "1"
    for d in (
        "::/EFI",
        "::/EFI/BOOT",
        "::/EFI/Microsoft",
        "::/EFI/Microsoft/Boot",
        "::/EFI/Microsoft/Boot/Fonts",
        "::/Boot",
        "::/sources",
    ):
        run(["mmd", "-i", str(raw_img), d], env=env)

    copies = [
        (adk_files["bootaa64.efi"], "::/EFI/BOOT/BOOTAA64.EFI"),
        (adk_files["bootaa64.efi"], "::/EFI/Microsoft/Boot/bootmgfw.efi"),
        (bcd_path, "::/EFI/Microsoft/Boot/BCD"),
        (adk_files["segmono_boot.ttf"], "::/EFI/Microsoft/Boot/Fonts/segmono_boot.ttf"),
        (adk_files["segoe_slboot.ttf"], "::/EFI/Microsoft/Boot/Fonts/segoe_slboot.ttf"),
        (adk_files["wgl4_boot.ttf"], "::/EFI/Microsoft/Boot/Fonts/wgl4_boot.ttf"),
        (adk_files["boot.sdi"], "::/Boot/boot.sdi"),
        (boot_wim, "::/sources/boot.wim"),
    ]
    for src, dst in copies:
        run(["mcopy", "-i", str(raw_img), str(src), dst], env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=pathlib.Path, default=pathlib.Path("/tmp/winpe-work"))
    parser.add_argument("--output-dir", type=pathlib.Path, default=ROOT / "artifacts" / "np05j")
    parser.add_argument("--size-mib", type=int, default=512, help="ขนาดพาร์ติชัน sunuefi_esp ปลายทาง (MiB, สูงสุด 512)")
    parser.add_argument("--keep-raw", action="store_true", help="เก็บไฟล์ raw FAT32 4Kn (winpe.raw.img) ไว้ด้วย")
    args = parser.parse_args()

    if args.size_mib < 384 or args.size_mib > 512:
        raise SystemExit(f"--size-mib ต้องอยู่ระหว่าง 384..512 (ได้รับ {args.size_mib})")

    work = args.work_dir.resolve()
    out_dir = args.output_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)

    adk_files = ensure_adk_inputs(work)
    wdk_root = ensure_wdk_sdk(work)
    lenovo_dir = ensure_lenovo_drivers(work)

    touch_info = build_and_sign_touch_driver(wdk_root, work / "touch-build")

    bcd_patched = work / "BCD.np05j"
    patch_bcd_for_np05j(adk_files["bcd"], bcd_patched)

    boot_wim = work / "boot.wim"
    customize_boot_wim(adk_files["winpe.wim"], boot_wim, touch_info, lenovo_dir, work / "wim-stage")

    wim_bytes = boot_wim.stat().st_size
    if wim_bytes > (args.size_mib - 24) * MIB:
        raise RuntimeError(f"boot.wim ใหญ่เกินไป ({wim_bytes / MIB:.1f} MiB) สำหรับ ESP {args.size_mib} MiB")

    raw_img = out_dir / "winpe.raw.img" if args.keep_raw else work / "winpe.raw.img"
    build_fat32_esp(raw_img, args.size_mib, adk_files, bcd_patched, boot_wim)

    sparse_img = out_dir / "winpe.img"
    sparse_meta = convert_to_android_sparse(raw_img, sparse_img, block_size=4096, max_raw_mib=32)
    if sparse_meta["sparse_bytes"] > 512 * MIB:
        raise RuntimeError(f"winpe.img เกิน 512 MiB ({sparse_meta['sparse_bytes']} bytes)")

    manifest = {
        "schema_version": 1,
        "device": "np05j",
        "target_partition": "sunuefi_esp",
        "flash_command": "fastboot flash sunuefi_esp winpe.img",
        "winpe_base": "Microsoft Windows PE ARM64 10.0.26100.1 (ADK)",
        "driver_pack_source": "https://archive.org/details/lenovo-83ed-x1e-drivers",
        "touch_driver": {
            "name": "Np05jSynaTouch",
            "hardware_id": "ACPI\\SYNA3930",
            "protocol": "Synaptics TCM v2 (TLMM bit-bang SPI4 + Windows VHF)",
            "test_cert_sha1": touch_info["thumbprint_sha1"],
            "sys_bytes": touch_info["sys_path"].stat().st_size,
            "sys_sha256": sha256_file(touch_info["sys_path"]),
        },
        "boot_wim_bytes": wim_bytes,
        "boot_wim_sha256": sha256_file(boot_wim),
        "winpe_img": sparse_meta,
    }
    (out_dir / "winpe-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
