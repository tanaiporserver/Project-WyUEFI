#!/usr/bin/env python3
"""Host-only build freshness checks. Never invokes adb or fastboot."""
import argparse
import hashlib
import json
from pathlib import Path
import uuid
from firmware_workspace import active_root, validate_sources

NAMES={'stage0':'piano','probe':'pianoProbe','linux':'pianoLinux','gui':'pianoGui','product':'pianoProduct','product-np05j':'np05jProduct'}

def output_names(profile):
    if profile=='product':return ('PianoUEFI-product.fd','BootShim.bin')
    if profile=='product-np05j':return ('Np05jUEFI-product.fd','BootShim.bin')
    return ('piano-stage0.fd','BootShim.bin')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs(root,profile):
    original_root=root
    is_product=profile in ('product','product-np05j')
    is_np05j=profile=='product-np05j'
    vendor_dir='ZTE' if is_np05j else 'Xiaomi'
    pkg_dir='np05jProductPkg' if is_np05j else 'pianoProductPkg'
    device='np05j' if is_np05j else 'piano'
    if is_product:root=active_root(root)
    ws=root/'upstream/Mu-Silicium'
    platform=ws/'Platforms'/vendor_dir/f'{NAMES[profile]}Pkg'
    if not platform.is_dir():
        raise ValueError('Prepared platform is missing')
    files=set(p for p in platform.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    binaries=ws/'Binaries'/device
    if binaries.exists():
        files.update(p for p in binaries.rglob('*') if p.is_file())
    shim=ws/'BootShim' if profile=='stage0' else root/'uefi/handoff/bootshim'
    dtb_name='np05j.dtb' if is_np05j else 'piano.dtb'
    for p in (shim/'BootShim.S',shim/'Makefile',ws/'Resources/DTBs'/dtb_name,
              root/'tools/build_stage0.sh',root/'tools/build_integrity.py'):
        if p.is_file():files.add(p)
    if is_product:
        if is_np05j:
            import np05j_vendor_inputs as vendor_inputs
        else:
            import piano_vendor_inputs as vendor_inputs
        vendor_record=vendor_inputs.record(root)
        files.update(vendor_inputs.source_files(root))
        files.update(path for path in (root/'patches/firmware').rglob('*') if path.is_file())
        files.add(root/'tools/apply_firmware_patches.py')
        from prepare_product_pump import prepare as pump
        from prepare_product_ui import prepare as ui
        from prepare_nv_runtime_guard import prepare as nv_guard
        pump_record=pump(root,apply=False);ui_record=ui(root,apply=False)
        files.update(root/path for path in (*pump_record['files'],*ui_record['files']))
        files.update(root/path for path in nv_guard(root,apply=False)['files'])
        from prepare_product import (SOURCE_NAMES, product_components, os_boot_files,
                                     verify_os_boot, observation_files,
                                     verify_observation_families, verify_display_mapping,
                                     early_dxe_files)
        files.update(early_dxe_files(root,device))
        canonical=root/'uefi/core'
        from core_source import core_source
        files.update(core_source(name, root) for name in SOURCE_NAMES)
        files.update(root/'uefi/components'/component/name
                     for component,names in product_components(device).items() for name in names)
        files.update(path for path in canonical.rglob('*') if path.is_file() and path.suffix in ('.h','.inc'))
        files.update(os_boot_files(root).values())
        build_dir='build/product-np05j' if is_np05j else 'build/product'
        prepared=json.loads((root/build_dir/'prepared-manifest.json').read_text());os_boot=prepared.get('os_boot',{})
        if prepared.get('vendor_inputs')!=vendor_record:
            raise ValueError('Prepared vendor inputs differ from the fixed input bundle')
        for rows in observation_files(root).values():files.update(rows.values())
        for app in (platform/'Applications/ProductCore',root/'uefi/platforms'/pkg_dir/'Applications/ProductCore'):
            verify_os_boot(root,app,os_boot)
            verify_observation_families(root,app,prepared.get('dxe_observation',{}))
        from prepare_product_early_memory import verify as verify_early_memory, EARLY_FILES
        for target in (platform,root/'uefi/platforms'/pkg_dir):
            verify_early_memory(root,target,prepared.get('early_memory',{}))
            verify_display_mapping(root,target,prepared.get('display_mapping',{}),device=device)
        if is_np05j:
            from np05j_display_mapping import source_files as display_sources
        else:
            from piano_display_mapping import source_files as display_sources
        files.update(display_sources(root))
        files.update(root/path for path in ('tools/compose_piano_dtb.py','tools/analyze_capture.py'))
        files.update(root/'uefi/handoff/early-memory'/name for name in EARLY_FILES)
        from prepare_product_handoff import prepare as handoff, verify_provider
        handoff_record=handoff(root,apply=False)
        if prepared.get('native_late_handoff',{}).get('files')!=handoff_record['files']:
            raise ValueError('Native late handoff preparation differs from canonical sources')
        for name,digest in handoff_record['files'].items():
            path=root/name
            if not path.is_file()or sha(path)!=digest:raise ValueError('Native late handoff compiled hook stale: '+name)
            files.add(path)
        for target in (platform,root/'uefi/platforms'/pkg_dir):
            verify_provider(root,target/'Applications/ProductCore',prepared.get('late_provider',{}))
        touch_folder='uefi/components/touch-np05j' if is_np05j else 'uefi/components/touch-product'
        comp_folders=['uefi/components/product-pump','uefi/components/product-support','uefi/components/product-handoff','uefi/components/board-identity','uefi/components/ext4','uefi/components/simple-init','uefi/components/nv','uefi/components/reset','uefi/components/usb-host',touch_folder]
        if not is_np05j:comp_folders.append('uefi/components/pogo-product')
        for folder in comp_folders:
            files.update(path for path in (root/folder).rglob('*') if path.is_file())
        contract_path='config/np05j-product.json' if is_np05j else 'config/piano-product.json'
        si_dir='artifacts/simpleinit/product-np05j' if is_np05j else 'artifacts/simpleinit/product'
        si_cfg='config/simpleinit.np05j.uefi.cfg' if is_np05j else 'config/simpleinit.static.uefi.cfg'
        for relative in (contract_path,build_dir+'/prepared-manifest.json',
                         'tools/prepare_product.py','tools/build_product.sh','tools/firmware_workspace.py','tools/package_product.py',
                         'tools/prepare_product_pump.py','tools/prepare_product_ui.py','tools/prepare_product_console.py','tools/prepare_nv_runtime_guard.py','tools/prepare_product_early_memory.py','tools/prepare_product_handoff.py','tools/source_input_tail.py','tools/simpleinit_build_identity.py',
                          'tools/build_simpleinit.sh','tools/prepare_simpleinit.py','tools/firmware/prepare_ext4.py','tools/product_payload_digest.py',
                          'config/uefi-ext4.json',si_cfg,
                         si_dir+'/SimpleInit.efi',si_dir+'/app-payload.bin',
                         si_dir+'/build-ok.json'):
            p=root/relative
            if not p.is_file():raise ValueError('Missing product build input: '+relative)
            files.add(p)
    if is_product:
        validate_sources(original_root, [str(p.relative_to(root)) for p in files])
    hashes={str(p.relative_to(root)):sha(p) for p in sorted(files)}
    return {'sha256':hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest(),
            'file_count':len(hashes)}


def marker(root,profile):
    return root/'artifacts'/profile/'build-ok.json'


def pending(root,profile):
    return root/'build/integrity'/f'{profile}-pending.json'


def start(root,profile):
    marker(root,profile).unlink(missing_ok=True)
    p=pending(root,profile);p.parent.mkdir(parents=True,exist_ok=True)
    record={'profile':profile,'build_id':str(uuid.uuid4()),'inputs':inputs(root,profile)}
    p.write_text(json.dumps(record,indent=2)+'\n')
    return record


def finish(root,profile):
    p=pending(root,profile)
    record=json.loads(p.read_text())
    if record['profile']!=profile or record['inputs']!=inputs(root,profile):
        raise ValueError('Build inputs changed during compilation; rebuild before packaging')
    folder=root/'artifacts'/profile
    record['outputs']={name:sha(folder/name) for name in output_names(profile)}
    record['status']='successful build, matching inputs and outputs'
    marker(root,profile).write_text(json.dumps(record,indent=2)+'\n')
    p.unlink()
    return record


def validate(root,profile,manifest=None):
    p=marker(root,profile)
    if not p.is_file():
        raise ValueError('No completed build marker; old artifacts cannot be packaged or booted')
    record=json.loads(p.read_text())
    if record['profile']!=profile or record['inputs']!=inputs(root,profile):
        raise ValueError('Prepared sources differ from the completed build; rebuild first')
    for name,digest in record['outputs'].items():
        if sha(root/'artifacts'/profile/name)!=digest:
            raise ValueError('Built output hash mismatch: '+name)
    if manifest is not None and manifest.get('build_id')!=record['build_id']:
        raise ValueError('Boot package belongs to an older build; repackage the completed build')
    return record


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=('start','finish','validate'))
    parser.add_argument('--profile',choices=tuple(NAMES),required=True)
    args=parser.parse_args();root=Path(__file__).resolve().parent.parent
    try:
        record={'start':start,'finish':finish,'validate':validate}[args.operation](root,args.profile)
    except (ValueError,OSError,KeyError) as error:
        raise SystemExit(str(error))
    print(f'{args.operation}: {args.profile} build {record["build_id"]}')


if __name__=='__main__':main()
