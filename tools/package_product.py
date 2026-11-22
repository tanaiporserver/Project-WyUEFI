#!/usr/bin/env python3
"""Package the actual shared product FD and validated APPv1 into one image."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from analyze_capture import parse_fdt
from build_integrity import validate as validate_build
from product_contract import ROOT,validate,validate_build_manifest
from simpleinit_build_identity import inspect as inspect_simpleinit
from firmware_workspace import active_root
from prepare_boot_files import image_info


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',choices=('piano','np05j'),default='piano')
    # ใช้ Android Boot Header v3 เป็นค่าเริ่มต้นทั้ง piano และ np05j เพื่อให้ ABL โหลด
    # ramdisk (app-payload.bin) เข้า linux,initrd-start..end บนเครื่องที่มี init_boot แยก
    parser.add_argument('--header-version',type=int,choices=(3,4),default=3)
    args=parser.parse_args();is_np05j=args.device=='np05j'
    header_version=args.header_version
    profile='product-np05j' if is_np05j else 'product'
    pkg_name='np05jProductPkg' if is_np05j else 'pianoProductPkg'
    contract_rel='config/np05j-product.json' if is_np05j else 'config/piano-product.json'
    prep_rel='build/product-np05j/prepared-manifest.json' if is_np05j else 'build/product/prepared-manifest.json'
    si_rel='artifacts/simpleinit/product-np05j' if is_np05j else 'artifacts/simpleinit/product'
    si_build='build/simpleinit-product-np05j-edk2' if is_np05j else 'build/simpleinit-product-edk2'
    fd_name='Np05jUEFI-product.fd' if is_np05j else 'PianoUEFI-product.fd'
    img_name='Np05jUEFI-product.img' if is_np05j else 'PianoUEFI-product.img'
    dtb_name='np05j.dtb' if is_np05j else 'piano.dtb'
    root=active_root(ROOT);ws=root/'upstream/Mu-Silicium';out=root/'artifacts'/profile
    build=validate_build(root,profile)
    contract=validate(json.loads((root/contract_rel).read_text()))
    prepared=json.loads((root/prep_rel).read_text())
    validate_build_manifest(contract,prepared)
    if set(prepared['backends'])!=set(contract['features']):raise ValueError('Product backend disclosure incomplete')
    app=root/si_rel
    identity=inspect_simpleinit(root/si_build,app,True,device=args.device)
    if identity!=prepared['simpleinit'] or identity!=json.loads((app/'build-ok.json').read_text()):raise ValueError('Product application identity changed')
    core=ws/'Build'/pkg_name/'DEBUG_CLANGPDB/AARCH64'/pkg_name/'Applications/ProductCore/ProductCore'
    core_make=(core/'GNUmakefile').read_text()
    if 'PianoProductPumpLib/PianoProductPumpLib/OUTPUT/PianoProductPumpLib.lib' not in core_make:raise ValueError('Product core linked the wrong Pump library')
    if is_np05j and 'Np05jTouchService.obj' not in core_make:raise ValueError('NP05J product core omitted Np05jTouchService.obj')
    dxe_make=ws/'Build'/pkg_name/'DEBUG_CLANGPDB/AARCH64/MdeModulePkg/Core/Dxe/DxeMain/GNUmakefile'
    if 'PianoProductPumpLib/PianoProductPumpLib/OUTPUT/PianoProductPumpLib.lib' not in dxe_make.read_text():raise ValueError('Product DXE Core linked Null service pump')
    fd=out/fd_name;shim=out/'BootShim.bin';dtb=ws/'Resources/DTBs'/dtb_name
    fd_bytes=fd.read_bytes();shim_bytes=shim.read_bytes()
    if len(fd_bytes)!=0x300000 or shim_bytes[56:60]!=b'ARM\x64':raise ValueError('Unexpected product FD/BootShim')
    symbols=subprocess.check_output([str(Path(os.environ.get('SUNUEFI_TOOLCHAIN_ROOT',str(root/'build/host-tools/usr')))/'bin/aarch64-linux-gnu-nm'),'-n',str(root/'uefi/handoff/bootshim/BootShim.elf')],text=True)
    offsets=[int(row.split()[0],16)for row in symbols.splitlines()if len(row.split())==3 and row.split()[2]=='_Payload']
    if offsets!=[len(shim_bytes)]:raise ValueError('Product BootShim FD payload offset mismatch')
    for path in (fd,ws/'Build'/pkg_name/'DEBUG_CLANGPDB/FV/FVMAIN.Fv'):
        if not struct.unpack_from('<I',path.read_bytes(),44)[0]&4:raise ValueError('Product FV READ_STATUS is clear')
    nodes=parse_fdt(dtb.read_bytes())
    if any(key in nodes.get('/chosen',{})for key in ('linux,initrd-start','linux,initrd-end','kaslr-seed','rng-seed')):raise ValueError('Stale packaged DT handoff')
    payload=(app/'app-payload.bin').read_bytes();raw=(app/'SimpleInit.efi').read_bytes()
    magic,version,header,app_bytes,digest=struct.unpack_from('<16sIIQ32s',payload)
    if magic!=b'SUNUEFI-APPv1\0'.ljust(16,b'\0')or version!=1 or header!=64 or app_bytes!=len(raw)or payload[64:]!=raw or digest!=hashlib.sha256(raw).digest():raise ValueError('Product APPv1 payload mismatch')
    kernel=out/'product-kernel.bin';kernel.write_bytes(gzip.compress(shim_bytes+fd_bytes,mtime=0)+dtb.read_bytes())
    image=out/img_name
    subprocess.run([sys.executable,str(ws/'Resources/Scripts/mkbootimg.py'),'--header_version',str(header_version),'--kernel',str(kernel),'--ramdisk',str(app/'app-payload.bin'),'-o',str(image)],check=True)
    blob=image.read_bytes();ks,rs=struct.unpack_from('<2I',blob,8)
    roff=4096+((ks+4095)//4096)*4096
    if blob[:8]!=b'ANDROID!'or struct.unpack_from('<I',blob,40)[0]!=header_version or blob[4096:4096+ks]!=kernel.read_bytes()or rs!=len(payload)or blob[roff:roff+rs]!=payload:raise ValueError('Product boot image contents mismatch')
    result={**prepared,'build_id':build['build_id'],'build_inputs':build['inputs'],'android_header_version':header_version,
        'status':'INCOMPLETE_NOT_RELEASE','not_release_reasons':[name+': '+entry['status']for name,entry in prepared['backends'].items()],
        'single_image_entry_compatibility':'same bytes; all entry-specific physical boot/recovery/fastboot_boot validation pending',
        'files':{path.name:{'bytes':path.stat().st_size,'sha256':sha(path)}for path in (image,fd,shim,kernel)},
        'core_application_sha256':sha(core/'OUTPUT/PianoProductCore.efi'),'device_boot_performed':False,'os_boot_performed':False}
    (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'artifact':str(image),'sha256':sha(image),'bytes':image.stat().st_size,'status':result['status'],'device_boot_performed':False},indent=2))


if __name__=='__main__':main()
