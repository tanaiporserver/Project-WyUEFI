#!/usr/bin/env python3
"""Prepare one PianoUEFI product integration candidate; host-only, no device."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import uuid

from product_contract import ROOT, validate
from prepare_product_pump import prepare as prepare_pump
from product_payload_digest import digest_header
from simpleinit_build_identity import inspect as inspect_simpleinit
from prepare_gui_profile import SETUP_DSC_ADDITIONS, SETUP_FV_MODULES
from prepare_product_early_memory import prepare as prepare_early_memory, verify as verify_early_memory, unbind_memory_map
from prepare_product_handoff import prepare as prepare_handoff, stage_provider, verify_provider
import piano_display_mapping as display_mapping
import np05j_display_mapping
import piano_vendor_inputs as vendor_inputs
import np05j_vendor_inputs
from core_source import core_source, has_core_source

CORE_GUID='35E0D1B5-93CE-4D6A-9A93-6ADAA3F26C40'
SIMPLEINIT_BOOT_MODULE='Applications/SimpleInitBoot/SimpleInitBoot.inf'
BOOT_PICKER_MODULE='MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf'
OS_BOOT_SOURCE_BUDGET=128*1024*1024
SOURCE_NAMES=(
    'PianoProductCore.c','PianoBootOptions.c','PianoProductBootLog.c','PianoProductDisplayObserve.c','PianoDisplaySmmuObserve.c','PianoFrameBufferMappingObserve.c','PianoDisplayClockObserve.c','PianoProductDisplayOwner.c','PianoDisplayClockLease.c','PianoDisplayClockRead.c','PianoBootPolicy.c','PianoFvApplication.c','PianoProductPayload.c','PianoProductOwners.c','PianoRamPartition.c','PianoProductSmem.c','PianoProductMemory.c',
    'NativeProbe.c','PianoKeys.c','PianoFaultRecovery.c',
    'PianoSmmu.c','PianoDma.c','PianoOwnedSmmu.c','PianoIoPageTable.c',
    'PianoUfsProbe.c','PianoUfsReadOnlyDma.c','PianoUfsDmaLayout.c','PianoGpt.c','PianoBlockIo.c',
    'PianoUfsProductVolume.c','PianoUfsBoundedLayout.c',
    'PianoFastboot.c','PianoFastbootBlockRead.c','PianoFastbootBoot.c','PianoFastbootLaunch.c','PianoFastbootDownloadBlob.c','PianoRawLinuxBoot.c','PianoPanelSelection.c',
    'PianoFastbootScreen.c','PianoDwc3Device.c','PianoUsbControl.c','PianoUsbController.c','PianoUsbPhy.c',
    'PianoPogoReport.c','PianoPogoInput.c','PianoPogoI2c.c','PianoGeniI2cPio.c','PianoUsbHostPci.c',
)
PRODUCT_COMPONENTS={
    'board-identity':('PianoBluetoothAddress.c','PianoBluetoothAddress.h'),
    'rtc':('PianoRtcOffset.c','PianoRtcOffset.h'),
    'nv':('PianoNvFile.c','PianoNvFile.h'),
    'reset':('PianoReset.c','PianoReset.h'),
    'ext4':('PianoStorageIo.h','PianoStorageIo.inc','PianoExt4.h'),
    'pogo-product':('PianoPogoDxe.c','PianoPogoDxe.h','PianoPogoBoard.c','PianoPogoBoard.h',
                    'PianoPogoMcu.c','PianoPogoMcu.h','PianoPogoService.c','PianoPogoService.h'),
    'usb-host':('PianoUsbHost.c','PianoUsbHost.h','PianoUsbTypec.c','PianoUsbTypec.h'),
    'touch-product':('PianoGeniSpiGpi.c','PianoGeniSpiGpi.h','PianoTouchNvt.c','PianoTouchNvt.h',
                     'PianoTouchMatrix.c','PianoTouchMatrix.h','PianoTouchService.c','PianoTouchService.h'),
}
NP05J_TOUCH_COMPONENTS={'touch-np05j':('Np05jTouchService.c','Np05jTouchService.h','Np05jSynapticsTcm2.h')}
def product_components(device='piano'):
    if device=='piano':return PRODUCT_COMPONENTS
    components={k:v for k,v in PRODUCT_COMPONENTS.items() if k not in ('pogo-product','touch-product')}
    components.update(NP05J_TOUCH_COMPONENTS)
    return components
PIANO_FIRMWARE='vendor/firmware'
POGO_FIRMWARE_NAME='nanosic/MCU_Upgrade.bin'
# Indexed by the touch lcd-id level, like firmware-name in the Linux DT
TOUCH_FIRMWARE_NAMES=(('Csot','novatek/novatek_nt36532_piano_fw_csot.bin'),('Boe','novatek/novatek_nt36532_piano_fw_boe.bin'))
COMPONENT_C_SOURCES=tuple(name for names in PRODUCT_COMPONENTS.values() for name in names if name.endswith('.c'))
OS_BOOT_SOURCES=('PianoBootFileSource.c','PianoCpuImageLoan.c','PianoLinuxEfiSession.c','PianoCpuInput.c','PianoUkiImage.c')
OS_BOOT_HEADERS=tuple(name[:-2]+'.h' for name in OS_BOOT_SOURCES)
LATE_HANDOFF_INF_SOURCES=tuple('LateHandoff/'+name for name in ('PianoLateHandoff.c','PianoLateHandoff.h','PianoDeviceTreeFixup.c','PianoDeviceTreeFixup.h'))
OS_BOOT_INF_SOURCES=tuple('Components/os-boot/'+name for name in (*OS_BOOT_SOURCES,*OS_BOOT_HEADERS))
OBSERVATION_FAMILIES={'early-memory':('PianoSmemRam.c','PianoSmemRam.h','PianoMemoryLayout.c','PianoMemoryLayout.h'),
                      'guarded-read':('PianoGuardedRead.c','PianoGuardedRead.h'),
                      'display-rail':('PianoDisplayRailObserve.c','PianoDisplayRailObserve.h','PianoDisplayNonGdscClock.c','PianoDisplayNonGdscClock.h')}
OBSERVATION_INF_SOURCES=tuple(name for names in OBSERVATION_FAMILIES.values() for name in names)
PRODUCT_FLAGS=('PIANO_USB_SERVICE=1','PIANO_USB_EP0=1','PIANO_USB_FASTBOOT=1','PIANO_USB_SCREENSHOT=1',
    'PIANO_USB_UFS_FETCH=1','PIANO_USB_RAM_BOOT=1','PIANO_USB_POWER_PROBE=1','PIANO_UFS_BLOCKIO=1',
    'PIANO_UFS_PRODUCT_STORAGE=1','PIANO_PRODUCT_NATIVE_LATE=1','PIANO_USB_HOST=1')
NATIVE_NAMES=vendor_inputs.NATIVE_NAMES
EARLY_DXE_SOURCE='vendor/piano/early-dxe/EnvDxeEnhanced'
EARLY_DXE_NAMES=('EnvDxeEnhanced.inf','EnvDxeEnhanced.efi','EnvDxeEnhanced.depex')
EARLY_DXE_PROVENANCE_SHA256='7395e9f88c3810aff7014d661569a32faba248c373d68a12c210c02f3c50a694'
NP05J_EARLY_DXE_SOURCE='vendor/np05j/early-dxe/EnvDxeEnhanced'
NP05J_EARLY_DXE_PROVENANCE_SHA256='c3a272086e610cf8585894a47786dc0a3ef2d4039b8b774d60a08ac2baa5f8d4'
DISK_MODULES=('MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf',
    'MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf',
    'MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf','FatPkg/EnhancedFatDxe/Fat.inf')
HOST_MODULES=('MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf','MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf',
    'MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf','MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf',
    'MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf')


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def simpleinit_boot_guid_header(root=ROOT):
    inf=root/'uefi/components/product-support'/SIMPLEINIT_BOOT_MODULE
    guid,=(uuid.UUID(line.split('=',1)[1].strip()) for line in inf.read_text().splitlines()
           if line.strip().startswith('FILE_GUID ='))
    first,second,third=guid.fields[:3]
    tail=','.join(f'0x{byte:02x}' for byte in guid.bytes[8:])
    return ('STATIC CONST EFI_GUID mPianoSimpleInitBootFileGuid='
            f'{{0x{first:08x},0x{second:04x},0x{third:04x},{{{tail}}}}};\n')


def verify_display_mapping(root,target,record,device='piano'):
    if set(record)!={'candidate','compiled_into_product','hardware_verified','register_access_authorized'} or \
       record['compiled_into_product'] is not True or record['hardware_verified'] is not False or \
       record['register_access_authorized'] is not False:
        raise ValueError('Product display mapping binding or readiness metadata changed')
    memory=Path(target)/'Library/MemoryMapLib/MemoryMapLib.c'
    if memory.is_symlink()or not memory.is_file():raise ValueError('Product display mapping source missing or unowned')
    mapper=np05j_display_mapping if device=='np05j' else display_mapping
    return mapper.verify(root,unbind_memory_map(memory.read_text()),record['candidate'])


def observation_directory(root,family):
    return root/('uefi/handoff/early-memory' if family=='early-memory' else 'uefi/components/'+family)


def observation_files(root):
    files={family:{name:observation_directory(root,family)/name for name in names}
           for family,names in OBSERVATION_FAMILIES.items()}
    for family,rows in files.items():
        for name,path in rows.items():
            if not path.is_file() or path.is_symlink():raise ValueError('Canonical DXE observation input missing or unowned: '+family+'/'+name)
    return files


def prepare_observation_families(root,app):
    families=observation_files(root)
    for rows in families.values():
        for name,path in rows.items():
            if has_core_source(name, root):raise ValueError('Flat DXE observation name conflicts with core source: '+name)
            shutil.copyfile(path,app/name)
    return {'status':'READ_ONLY_DXE_OBSERVATION_BOUND_UNTESTED','phase':'DXE_AFTER_FOUNDATION_BEFORE_RAM_INVENTORY_UFS_USB',
            'platform_bound':True,'device_validated':False,'sec_early_ready':False,
            'high_ddr_mapped':False,'memory_ownership_granted':False,
            'sources':list(OBSERVATION_INF_SOURCES),
            'families':{family:{name:sha(path)for name,path in sorted(rows.items())}for family,rows in families.items()}}


def verify_observation_families(root,app,record):
    expected={family:{name:sha(path)for name,path in sorted(rows.items())}for family,rows in observation_files(root).items()}
    if record.get('families')!=expected or record.get('sources')!=list(OBSERVATION_INF_SOURCES):
        raise ValueError('Prepared DXE observation families differ from canonical sources')
    for rows in expected.values():
        for name,digest in rows.items():
            path=app/name
            if not path.is_file() or sha(path)!=digest:raise ValueError('ProductCore DXE observation compiled copy stale or missing: '+name)
    inf=(app/'ProductCore.inf').read_text();sources=inf.split('[Sources]\n',1)[1].split('[Packages]',1)[0].splitlines()
    if not set((*OBSERVATION_INF_SOURCES,'PianoProductSmem.c'))<={line.strip()for line in sources}:
        raise ValueError('ProductCore INF omits actual DXE observation source binding')
    guids=inf.split('[Guids]\n',1)[1].split('[Protocols]',1)[0].splitlines()
    protocols=inf.split('[Protocols]\n',1)[1].splitlines()
    if 'gEfiEventExitBootServicesGuid' not in {line.strip()for line in guids} or 'gEfiCpuArchProtocolGuid' not in {line.strip()for line in protocols}:
        raise ValueError('ProductCore INF omits actual DXE observation event or CPU protocol declaration')
    return True


def os_boot_files(root):
    folder=root/'uefi/components/os-boot'
    files={path.relative_to(folder).as_posix():path for path in folder.rglob('*')
           if path.is_file() and '__pycache__' not in path.parts}
    if any(name not in files for name in (*OS_BOOT_SOURCES,*OS_BOOT_HEADERS)):
        raise ValueError('Canonical OS boot sources or headers missing')
    if {name for name in files if name.endswith('.c')}!=set(OS_BOOT_SOURCES):
        raise ValueError('OS boot C sources must all have explicit ProductCore INF bindings')
    if any(path.is_symlink() for path in files.values()):
        raise ValueError('Canonical OS boot inputs must be ordinary owned files')
    return files


def shared_boot_headers(root):
    """Core headers keyed by subsystem path, matching the canonical includes."""
    folder=root/'uefi/core'
    return {path.relative_to(folder).as_posix():path for path in folder.rglob('*')
            if path.is_file() and path.suffix in ('.h','.inc') and path.name!='NativeProbeTable.h'}


def prepare_os_boot(root,app):
    """Byte-identical shared code; preserve its canonical relative includes."""
    files=os_boot_files(root)
    shutil.copytree(root/'uefi/components/os-boot',app/'Components/os-boot',
                    ignore=shutil.ignore_patterns('__pycache__'))
    # Preserve components/os-boot -> ../../core in the generated module.
    # ordinary quoted PianoFastbootLaunch.h include used by CpuImageLoan.
    headers=shared_boot_headers(root)
    for name,path in headers.items():
        destination=app/'core'/name;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,destination)
    return {'status':'SHARED_IMPLEMENTATION_COMPILED_PLATFORM_NOT_READY',
            'platform_bound':False,'full_ddr_verified':False,'start_enabled':False,
            'configured_source_budget_bytes':OS_BOOT_SOURCE_BUDGET,
            'sources':list(OS_BOOT_INF_SOURCES),
            'canonical_files':{name:sha(path)for name,path in sorted(files.items())},
            'shared_headers':{name:sha(path)for name,path in sorted(headers.items())}}


def verify_os_boot(root,app,record):
    """Freshness includes actual compiled copies, not just a canonical hash."""
    expected={name:sha(path)for name,path in sorted(os_boot_files(root).items())}
    headers={name:sha(path)for name,path in sorted(shared_boot_headers(root).items())}
    if record.get('canonical_files')!=expected or record.get('shared_headers')!=headers or record.get('sources')!=list(OS_BOOT_INF_SOURCES):
        raise ValueError('Prepared shared OS boot identity differs from canonical sources')
    actual={path.relative_to(app/'Components/os-boot').as_posix():sha(path)
            for path in (app/'Components/os-boot').rglob('*')if path.is_file() and '__pycache__' not in path.parts}
    if actual!=expected:raise ValueError('ProductCore OS boot compiled copies are stale or missing')
    for name,digest in headers.items():
        flat=app/Path(name).name
        for path in (app/'core'/name,flat):
            if not path.is_file() or sha(path)!=digest:
                raise ValueError('ProductCore shared OS boot include identity mismatch: '+name)
    sections={};section=None
    for line in (app/'ProductCore.inf').read_text().splitlines():
        line=line.strip()
        if line.startswith('[')and line.endswith(']'):section=line;sections[section]=set()
        elif line and section:sections[section].add(line)
    if not set(OS_BOOT_INF_SOURCES)<=sections.get('[Sources]',set()) or 'SynchronizationLib' not in sections.get('[LibraryClasses]',set()):
        raise ValueError('ProductCore INF omits actual shared OS boot sources or synchronization')
    return True


def fix_product_low_heap(text,dtb):
    """Cold product-only carveout correction; never retag live DXE allocations."""
    from plan_piano_dram import extract_dt
    dt=extract_dt(dtb)
    expected={'/reserved-memory/adspslpi_region@9ee00000':(0xB8200000,0xBD980000),
              '/reserved-memory/hwfence-shmem':(0xD4E23000,0xD5100000)}
    for path,bounds in expected.items():
        rows=[row for row in dt['fixed']if row['path']==path]
        if len(rows)!=1 or (rows[0]['base'],rows[0]['end'])!=bounds or not rows[0]['no_map']:
            raise ValueError('Product low heap fixed owner changed: '+path)
    old='  {"DXE_Heap", 0xBD930000, 0x1A6D0000, AddMem, 0, 0x703C07, 7, WRITE_BACK_XN},'
    new='''  {"Piano_ADSP_Heap_Conflict", 0xBD930000, 0x50000, HobOnlyNoCacheSetting, 5, 0x703C07, 0, WRITE_BACK_XN},
  {"DXE_Heap", 0xBD980000, 0x174A3000, AddMem, 0, 0x703C07, 7, WRITE_BACK_XN},
  {"Piano_HWFence_Reserved", 0xD4E23000, 0x2DD000, HobOnlyNoCacheSetting, 5, 0x703C07, 0, WRITE_BACK_XN},
  {"DXE_Heap_Upper", 0xD5100000, 0x2F00000, AddMem, 0, 0x703C07, 7, WRITE_BACK_XN},'''
    if text.count(old)!=1:raise ValueError('Unexpected original product DXE heap row')
    text=text.replace(old,new)
    # NoHob already excludes DBI from the allocator; remove its misleading
    # Conventional token without altering the inherited UC CPU attribute.
    old='  {"DBI_Dump", 0xB81C0000, 0x5770000, NoHob, 1, 0x2, 7, UNCACHED_UNBUFFERED_XN},'
    if text.count(old)!=1:raise ValueError('Unexpected original DBI owner row')
    text=text.replace(old,old.replace('0x2, 7,','0x2, 0,'))
    return text,{'phase':'COLD_BOOT_ONLY','named_hob_arena':{'base':0xBD980000,'end':0xD4E23000},
                 'upper_resource':{'base':0xD5100000,'end':0xD8000000},
                 'excluded_from_phit_allocator_and_cpu_map':[[0xBD930000,0xBD980000],[0xD4E23000,0xD5100000]],
                 'other_native_cache_attributes_changed':False,'high_ddr_added':False}


def pinned_firmware(root,name):
    """One blob from the tracked firmware components, named by its rootfs path"""
    base=Path(root)/PIANO_FIRMWARE
    for manifest in sorted(base.glob('*/manifest.json')):
        data=json.loads(manifest.read_text())
        directory=PurePosixPath(data.get('directory','.'))
        for filename,item in data['files'].items():
            if (directory/PurePosixPath(filename)).as_posix()!=name:continue
            content=(manifest.parent/filename).read_bytes()
            digest=hashlib.sha256(content).hexdigest()
            if len(content)!=item['bytes'] or digest!=item['sha256']:
                raise ValueError(name+' differs from its firmware manifest')
            return content,digest
    raise ValueError('Firmware not found in tracked components: '+name)


def c_array(name,raw):
    rows=[','.join('0x%02x'%value for value in raw[index:index+24]) for index in range(0,len(raw),24)]
    return 'STATIC CONST UINT8 '+name+'[]={\n'+',\n'.join(rows)+'\n};\n'


def prepare_pogo_firmware(root,app):
    """Embed the cover keyboard MCU RAM firmware that Linux loads"""
    data,digest=pinned_firmware(root,POGO_FIRMWARE_NAME)
    header=data[0x7FC0:0x8000]
    if len(header)!=64 or header[:7]!=b'NANO IC':
        raise ValueError('Unexpected keyboard MCU firmware layout')
    size=struct.unpack_from('<I',header,48)[0]
    if not size or size%4 or size>0x10000 or 0x8000+size>len(data):
        raise ValueError('Unexpected keyboard MCU firmware layout')
    code=data[0x8000:0x8000+size]
    (app/'PianoPogoMcuFirmware.h').write_text(
        '// Generated from '+PIANO_FIRMWARE+'/'+POGO_FIRMWARE_NAME+', not tracked\n'
        '#define PIANO_POGO_MCU_FIRMWARE_SHA256 "'+digest+'"\n'+
        c_array('mPianoPogoMcuImageHeader',header)+c_array('mPianoPogoMcuCode',code))
    return {'source':PIANO_FIRMWARE+'/'+POGO_FIRMWARE_NAME,'sha256':digest,'ram_code_bytes':size,
            'version':header[31:48].split(b'\0')[0].decode('ascii','replace')}


def prepare_touch_firmware(root,app):
    """Embed both per-panel NT36532 touch firmware images that Linux downloads"""
    text='// Generated from '+PIANO_FIRMWARE+'/novatek, not tracked\n';record={}
    for panel,name in TOUCH_FIRMWARE_NAMES:
        data,digest=pinned_firmware(root,name)
        if not 0x2000<=len(data)<=0x100000 or len(data)%0x1000:raise ValueError('Unexpected touch firmware size: '+name)
        text+='#define PIANO_TOUCH_FIRMWARE_'+panel.upper()+'_SHA256 "'+digest+'"\n'+c_array('mPianoTouchFirmware'+panel,data)
        record[panel.lower()]={'source':PIANO_FIRMWARE+'/'+name,'sha256':digest,'bytes':len(data)}
    (app/'PianoTouchFirmware.h').write_text(text)
    return record


def backend_status(device='piano'):
    # Integration/validation state is separate from the required enabled set.
    pogo_status=({'status':'ABSENT_ON_HARDWARE','transport':'none',
                  'physical_evidence':'NP05J has no pogo keyboard connector','missing':'none'}
                 if device=='np05j' else
                 {'status':'IMPLEMENTED_UNTESTED','transport':'QUP1 SE6 GENI I2C FIFO/PIO, 1 MHz, no DMA',
                  'physical_evidence':'MCU RAM firmware verified; cover attachment and runtime commands accepted; clean release before Linux',
                  'missing':'physical keystrokes, touchpad clicks/motion and unplug/replug'})
    touch_status=({'status':'IMPLEMENTED_UNTESTED',
                   'transport':'TLMM GPIO bit-bang SPI4 (GPIO 48..51, RST 161, IRQ 162, no DMA)',
                   'sequence':'RPMh ldob4/ldob14 rails -> TLMM pins -> reset pulse -> S3930 TCM v2 identify/report_cfg/enable',
                   'physical_evidence':'host unit tests passed; BootSelect EarlyTouch and ProductCore Np05jTouchService unverified on NP05J hardware',
                   'missing':'on-device bring-up, touch events, and ExitBootServices TLMM/RPMh restoration'}
                  if device=='np05j' else
                  {'status':'IMPLEMENTED_UNTESTED','transport':'QUP1 SE2 hardware SPI with polled GPI1 DMA and an owned SMMU domain',
                   'sequence':'Linux nt36xxx probe without a DRM panel follower: eng reset, chip-ID trim, per-panel host download, THP frames',
                   'physical_evidence':'the replaced GPIO transport loaded firmware but failed frame checksums during touch; the GPI transport has not run on the tablet',
                   'missing':'GPI register access, chip identity, complete frame checksums, menu clicks and clean DMA/domain retirement on the tablet'})
    return {
      'gop':{'status':'IMPLEMENTED_INHERITED_FRAMEBUFFER','physical_evidence':'test84 real GOP screenshot; product untested' if device=='piano' else '2400x1504 GOP host-bound; product untested'},
      'physical_keys':{'status':'IMPLEMENTED_PMIC_READONLY','physical_evidence':'tests23/24; product input retirement untested'},
      'pogo_keyboard_touchpad':pogo_status,
      'touchscreen':touch_status,
      'dma_smmu':{'status':'IMPLEMENTED_STRICT_OWNERS','physical_evidence':'test91 readonly fetch and exact combined USB/UFS retirement passed; resident product retirement still untested','ram_partition_inventory':'AUDITED_NATIVE_ABI_LINKED_UNTESTED','smem_observation':'READ_ONLY_COLD_SEC_AND_DXE_BOUND_UNTESTED','sec_early_ready':False,'high_ddr_mapped':False,'high_ram_ownership_verified':False},
      'ufs_blockio_read_write':{'status':'RESERVED_VOLUME_BACKEND_UNPROVISIONED','original_media':'READ_ONLY','missing':'explicit permanent reservation, provisioning approval and product physical RW acceptance'},
      'gpt':{'status':'IMPLEMENTED_READ','physical_evidence':'real UFS GPT reads; product untested'},
      'fat_simplefilesystem':{'status':'IMPLEMENTED_READ_ONLY_VOLUMES','physical_evidence':'7 read-only SFS, bounded FAT RW test86; product untested'},
      'persistent_variables':{'status':'PERSIST_FILE_BOOT_SERVICES','path':'persist/sunuefi/nvram-{a,b}.bin','ordinary_nv_restored_via':'SetVariable','physical_evidence':'two CRC-checked copies saved and restored across product reboots; clean ext4 unmount before Linux handoff','runtime_nv_set_supported':False,'authenticated_variables_persisted':False,'standard_fvb_activated':False},
      'uefi_shell':{'status':'RESIDENT_STANDARD_SHELL','physical_evidence':'product Shell and menu navigation with Fastboot responding','startup_options':'-nostartup -nointerrupt -nomap'},
      'setup_hii':{'status':'RESIDENT_STANDARD_UIAPP','physical_evidence':'Chinese scaled setup and cooperative return to menu'},
      'simpleinit':{'status':'RESIDENT_PRODUCT_GUI','defaults':'config/simpleinit.static.uefi.cfg','user_settings':'persist/sunuefi/simpleinit.uefi.cfg','physical_evidence':'landscape Chinese menu; user config created automatically; existing Linux booted'},
      'usb_device_fastboot':{'status':'RESIDENT_SERVICE','physical_evidence':'Fastboot responds in product menu, Setup and Shell; clean DMA/SMMU/clock retirement before Linux','navigation_commands':['oem setup','oem shell','oem simpleinit'],'navigation_physical_validation':True,'current_download_limit_bytes':67108864,'target_download_limit_bytes':1073741824,'known_issue':'first Shell bulk-IN response has stalled on-device; reconnection behavior not resolved'},
      'usb_host':{'status':'IMPLEMENTED_UNTESTED','role':'device (Fastboot) by default; xHCI host while the PMIH010X Type-C status reports an attached sink and a UI waits for input',
        'dma':'owned USB0 SMMU domain; WC common buffers, 1 MiB bounce arena for streaming','mass_storage':'READ_ONLY','vbus_control':False,
        'missing':'VBUS and port-role control (factory UEFI leaves Type-C sink-only); keyboard/mouse/hub/storage enumeration, unplug and host-session release before Linux'},
      'debug_logs_screenshot':{'status':'IMPLEMENTED_FASTBOOT','physical_evidence':'test82 ramlog and84 screenshot; product UI snapshots untested'},
      'efi_android_linux_boot':{'status':'PARTIAL','shared_os_loader':'COMPILED_PLATFORM_NOT_READY','source_budget_bytes':OS_BOOT_SOURCE_BUDGET,'platform_bound':False,'full_ddr_verified':False,'missing':'approved SFS/path source, independent BS fence, actual full-DDR and all-owner EFI handoff binding; generic Android/Recovery/Windows handoff'},
      'os_exit':{'status':'NATIVE_LATE_APP_HOOK_AND_ROOT_PROVIDER_BOUND_UNTESTED','os_image_armed':False,'full_ddr_verified':False,'missing':'live full-DDR authority, actual selected OS Arm and device acceptance'},
    }


def early_dxe_files(root,device='piano'):
    folder=Path(root)/(NP05J_EARLY_DXE_SOURCE if device=='np05j' else EARLY_DXE_SOURCE)
    return tuple(folder/name for name in (*EARLY_DXE_NAMES,'provenance.json'))


def stage_early_dxe(root,device='piano'):
    folder=Path(root)/(NP05J_EARLY_DXE_SOURCE if device=='np05j' else EARLY_DXE_SOURCE)
    provenance=(folder/'provenance.json').read_bytes()
    expected_sha=NP05J_EARLY_DXE_PROVENANCE_SHA256 if device=='np05j' else EARLY_DXE_PROVENANCE_SHA256
    if hashlib.sha256(provenance).hexdigest()!=expected_sha:
        raise ValueError('EnvDxeEnhanced provenance SHA256 mismatch')
    record=json.loads(provenance);data={}
    for name in EARLY_DXE_NAMES:
        raw=(folder/name).read_bytes();expected=record['files'][name]
        if len(raw)!=expected['bytes'] or hashlib.sha256(raw).hexdigest()!=expected['sha256']:
            raise ValueError('EnvDxeEnhanced source length/SHA256 mismatch: '+name)
        data[name]=raw
    v_mod=np05j_vendor_inputs if device=='np05j' else vendor_inputs
    v_mod.validate_pe(data['EnvDxeEnhanced.efi'])
    v_mod.validate_depex(data['EnvDxeEnhanced.depex'])
    # This APRIORI driver still runs through DXE dispatch with its original
    # DEPEX. It is separate from the thirteen runtime-loaded Foundation PEs.
    target=Path(root)/'upstream/Mu-Silicium/Binaries'/device/'Stage0/EnvDxeEnhanced'
    target.mkdir(parents=True,exist_ok=True)
    for name,raw in data.items():(target/name).write_bytes(raw)


def native_modules(root,app,inputs=None,device='piano'):
    stage_early_dxe(root,device)
    v_mod=np05j_vendor_inputs if device=='np05j' else vendor_inputs
    if NATIVE_NAMES!=v_mod.NATIVE_NAMES:raise ValueError('Product native names differ from fixed vendor bundle')
    catalog=(inputs if inputs is not None else v_mod.load(root))['native_drivers']
    table=['typedef struct { CONST CHAR8 *Name; EFI_GUID Guid; CONST UINT8 *Depex; UINTN DepexBytes; } NATIVE_IMAGE;']
    ffs=[];identities={}
    for index,name in enumerate(NATIVE_NAMES):
        row=catalog[name];source=Path(row['pe_path'])
        if sha(source)!=row['pe_sha256']:raise ValueError('Native PE hash mismatch: '+name)
        destination=root/'upstream/Mu-Silicium/Binaries'/device/'ProductFoundation'/name
        destination.mkdir(parents=True,exist_ok=True)
        target=destination/(name+'.efi')
        retention=None
        if name=='ClockDxe':
            # Keep ABL's active display path while the same real provider serves
            # UFS/USB. Original capture stays immutable; expose the derivation.
            if device=='np05j':
                from np05j_inherited_clock import derive
            else:
                from piano_inherited_clock import derive
            derived,retention=derive(source.read_bytes());target.write_bytes(derived)
        else:shutil.copyfile(source,target)
        dep=Path(row['depex_path']).read_bytes() if row['depex_path'] else b'\x06\x08'
        table.append(f'STATIC CONST UINT8 mDepex{index}[]={{'+','.join(hex(value) for value in dep)+'};')
        ffs.append(f'  FILE FREEFORM = {row["file_guid"]} {{\n    SECTION PE32 = Binaries/{device}/ProductFoundation/{name}/{name}.efi\n  }}')
        identities[name]={'pe_sha256':sha(target),'depex_sha256':hashlib.sha256(dep).hexdigest(),'file_guid':row['file_guid']}
        if retention is not None:
            identities[name]['original_pe_sha256']=row['pe_sha256']
            identities[name]['inherited_clock_retention']=retention
    table.append('STATIC CONST NATIVE_IMAGE mNativeImages[]={')
    for index,name in enumerate(NATIVE_NAMES):
        guid=uuid.UUID(catalog[name]['file_guid']);literal='{0x%08X,0x%04X,0x%04X,{'%guid.fields[:3]+','.join(f'0x{value:02X}' for value in guid.bytes[8:])+'}}'
        table.append('{"'+name+'",'+literal+f',mDepex{index},sizeof(mDepex{index})'+'},')
    table.append('};');(app/'NativeProbeTable.h').write_text('\n'.join(table)+'\n')
    return '\n'.join(ffs)+'\n',identities


def core_inf(device='piano'):
    comp_sources=tuple(name for names in product_components(device).values() for name in names if name.endswith('.c'))
    return '''[Defines]
  INF_VERSION = 0x00010005
  BASE_NAME = PianoProductCore
  FILE_GUID = '''+CORE_GUID+'''
  MODULE_TYPE = UEFI_APPLICATION
  VERSION_STRING = 0.1
  ENTRY_POINT = PianoProductCoreEntry
[Sources]
'''+''.join('  '+name+'\n' for name in (*SOURCE_NAMES,*comp_sources,*OS_BOOT_INF_SOURCES,*OBSERVATION_INF_SOURCES,*LATE_HANDOFF_INF_SOURCES))+'''[Packages]
  MdePkg/MdePkg.dec
  MdeModulePkg/MdeModulePkg.dec
  QcomPkg/QcomPkg.dec
  CryptoPkg/CryptoPkg.dec
  SiliciumPkg/SiliciumPkg.dec
[LibraryClasses]
  UefiApplicationEntryPoint
  UefiLib
  UefiBootManagerLib
  UefiBootServicesTableLib
  UefiRuntimeServicesTableLib
  MemoryMapLib
  BaseMemoryLib
  BaseLib
  BaseCryptLib
  FdtLib
  DebugLib
  PrintLib
  IoLib
  CacheMaintenanceLib
  DxeServicesTableLib
  DxeServicesLib
  SerialPortLib
  ArmSmcLib
  TimerLib
  MemoryAllocationLib
  DevicePathLib
  PcdLib
  PianoProductPumpLib
  SynchronizationLib
  HobLib
[Guids]
  gEfiGlobalVariableGuid
  gEfiConsoleInDeviceGuid
  gEfiEventExitBootServicesGuid
  gEfiEventBeforeExitBootServicesGuid
  gEfiFileInfoGuid
  gEfiFileSystemInfoGuid
  gFdtTableGuid
  gLinuxEfiInitrdMediaGuid
[Protocols]
  gEfiGraphicsOutputProtocolGuid
  gEfiAbsolutePointerProtocolGuid
  gEfiSimplePointerProtocolGuid
  gEfiSerialIoProtocolGuid
  gEfiUsb2HcProtocolGuid
  gEfiUsbIoProtocolGuid
  gEfiBlockIoProtocolGuid
  gEfiDiskIoProtocolGuid
  gEfiSimpleTextInProtocolGuid
  gEfiSimpleTextInputExProtocolGuid
  gEfiDevicePathProtocolGuid
  gEfiCpuArchProtocolGuid
  gEfiLoadedImageProtocolGuid
  gEfiLoadFileProtocolGuid
  gEfiLoadFile2ProtocolGuid
  gEfiWatchdogTimerArchProtocolGuid
  gEfiPartitionInfoProtocolGuid
  gEfiPciIoProtocolGuid
  gEfiSimpleFileSystemProtocolGuid
  gEfiFirmwareVolume2ProtocolGuid
  gEfiHiiDatabaseProtocolGuid
  gEfiHiiStringProtocolGuid
  gEfiHiiFontProtocolGuid
  gEfiHiiConfigRoutingProtocolGuid
  gEfiFormBrowser2ProtocolGuid
  gEdkiiFormDisplayEngineProtocolGuid
  gEfiVariableArchProtocolGuid
  gEfiVariableWriteArchProtocolGuid
'''


NP05J_MEMORY_MAP = '''// ข้อมูลแผนผังหน่วยความจำตั้งต้นสำหรับ NP05J ProductCore (อ้างอิง xbl-config / np05j.dtb)
// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/MemoryMapLib.h>
STATIC EFI_MEMORY_REGION_DESCRIPTOR gMemoryDescriptor[] = {
  {"RSRV0", 0x81960000, 0xA0000, AddMem, 0, 0x703C07, 0, WRITE_BACK_XN},
  {"XBL_DT", 0x81A00000, 0x40000, AddMem, 5, 0x703C07, 0, WRITE_BACK_XN},
  {"AOP", 0x81C00000, 0xA0000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"UEFI_Log", 0x81CE4000, 0x10000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"SMEM", 0x81D00000, 0x200000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"PDP_NS", 0x81F00000, 0x100000, AddMem, 5, 0x400, 0, NS_DEVICE},
  {"PvmFw", 0x824A0000, 0x100000, AddMem, 0, 0x703C07, 0, WRITE_BACK_XN},
  {"PIL_Reserved", 0x89E00000, 0x19700000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"Global_SYNC", 0x82600000, 0x100000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"Display_Demura", 0xA3500000, 0x2C80000, AddMem, 5, 0x703C07, 0, WRITE_THROUGH_XN},
  {"FD_Reserved", 0xA7000000, 0x100000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI_FD", 0xA7100000, 0x300000, AddMem, 0, 0x703C07, 4, WRITE_BACK},
  {"UEFI_FD_Reserved", 0xA7400000, 0x200000, AddMem, 0, 0x703C07, 4, WRITE_BACK},
  {"CPU_Vectors", 0xA7600000, 0x1000, AddMem, 0, 0x703C07, 4, WRITE_BACK},
  {"Info_Blk", 0xA7601000, 0x1000, AddMem, 0, 0x703C07, 6, WRITE_BACK_XN},
  {"MMU_PageTables", 0xA7602000, 0x3000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"Log_Buffer", 0xA7605000, 0x8000, AddMem, 0, 0x703C07, 6, WRITE_BACK_XN},
  {"UEFI_Stack", 0xA760D000, 0x40000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"SEC_Heap", 0xA764D000, 0x8C000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"Sched_Heap", 0xA76D9000, 0x400000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"FV_Region", 0xA7AD9000, 0x400000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"UEFI_RESV", 0xA7ED9000, 0x126000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"BootHandoff", 0xA7FFF000, 0x1000, AddMem, 0, 0x703C07, 4, WRITE_BACK_XN},
  {"Kernel", 0xA8000000, 0x10000000, AddMem, 0, 0x703C07, 0, WRITE_BACK_XN},
  {"XBL_Ramdump", 0xB8000000, 0x1C0000, AddMem, 5, 0x400, 0, UNCACHED_UNBUFFERED_XN},
  {"DBI_Dump", 0xB81C0000, 0xF00000, NoHob, 1, 0x2, 0, UNCACHED_UNBUFFERED_XN},
  {"DXE_Heap", 0xB90C0000, 0x1BD63000, AddMem, 0, 0x703C07, 7, WRITE_BACK_XN},
  {"Piano_HWFence_Reserved", 0xD4E23000, 0x2DD000, HobOnlyNoCacheSetting, 5, 0x703C07, 0, WRITE_BACK_XN},
  {"DXE_Heap_Upper", 0xD5100000, 0x2F00000, AddMem, 0, 0x703C07, 7, WRITE_BACK_XN},
  {"RamConsole", 0xE4A00000, 0x200000, AddMem, 5, 0x703C07, 0, UNCACHED_UNBUFFERED_XN},
  {"Display_Reserved", 0xFC800000, 0x2B00000, AddMem, 5, 0x703C07, 0, WRITE_THROUGH_XN},
  {"IMEM_Base", 0x14680000, 0x2A000, NoHob, 1, 0x2, 7, NS_DEVICE},
  {"IMEM_Cookie_Base", 0x146AA000, 0x16000, AddDev, 1, 0x2, 7, NS_DEVICE},
  {"IPC_ROUTER_TOP", 0x400000, 0x100000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"SECURITY_CONTROL", 0x780000, 0x7000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"QUP", 0x800000, 0x300000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"PRNG_CFG_PRNG", 0x10C0000, 0xC000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"UFS_HCI", 0x1D84000, 0x3000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"Piano_USB2_PHY", 0x88E3000, 0x1000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"Piano_USB3_PHY", 0x88E8000, 0x3000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"CRYPTO0_CRYPTO", 0x1DC0000, 0x40000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"CLK_TCSR_TCSR_REGS", 0x1F00000, 0x100000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"PERIPH_SS", 0x8800000, 0x100000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"USB", 0xA600000, 0x200000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"AOSS", 0xB000000, 0x4000000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"TLMM", 0xF000000, 0x1000000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"SMMU", 0x15000000, 0x200000, AddDev, 1, 0x400, 11, NS_DEVICE},
  {"APSS_HM", 0x16000000, 0x6000000, AddDev, 1, 0x400, 11, NS_DEVICE}
};
VOID GetMemoryMap (OUT EFI_MEMORY_REGION_DESCRIPTOR **Map, OUT UINT8 *Count) {
  *Map = gMemoryDescriptor; *Count = ARRAY_SIZE (gMemoryDescriptor);
}
'''


def fix_np05j_low_heap(dtb):
    """ตรวจสอบ carveout ของ NP05J กับ DTB จริงและสร้างตารางหน่วยความจำตั้งต้น"""
    from plan_piano_dram import extract_dt
    dt = extract_dt(dtb)
    hwfence = [row for row in dt['fixed'] if row['path'] == '/reserved-memory/hwfence-shmem']
    if len(hwfence) != 1 or (hwfence[0]['base'], hwfence[0]['end']) != (0xD4E23000, 0xD5100000) or not hwfence[0]['no_map']:
        raise ValueError('NP05J product hwfence-shmem reservation differs from expected bounds')
    ramoops = [row for row in dt['fixed'] if row['path'].lower() == '/reserved-memory/ramoops@e4a00000']
    if len(ramoops) != 1 or (ramoops[0]['base'], ramoops[0]['end']) != (0xE4A00000, 0xE4C00000):
        raise ValueError('NP05J product ramoops@E4A00000 reservation differs from expected bounds')
    return NP05J_MEMORY_MAP, {
        'phase': 'COLD_BOOT_ONLY',
        'named_hob_arena': {'base': 0xB90C0000, 'end': 0xD4E23000},
        'upper_resource': {'base': 0xD5100000, 'end': 0xD8000000},
        'ramoops': {'base': 0xE4A00000, 'end': 0xE4C00000},
        'excluded_from_phit_allocator_and_cpu_map': [[0xD4E23000, 0xD5100000]],
        'other_native_cache_attributes_changed': False,
        'high_ddr_added': False,
    }


def prepare(root=ROOT,device='piano'):
    if device not in ('piano','np05j'):raise ValueError('Unsupported product device: '+str(device))
    is_np05j=device=='np05j'
    v_mod=np05j_vendor_inputs if is_np05j else vendor_inputs
    d_mod=np05j_display_mapping if is_np05j else display_mapping
    contract_file='config/np05j-product.json' if is_np05j else 'config/piano-product.json'
    pkg_name='np05jProductPkg' if is_np05j else 'pianoProductPkg'
    stem='np05jProduct' if is_np05j else 'pianoProduct'
    vendor_dir='ZTE' if is_np05j else 'Xiaomi'
    si_subdir='product-np05j' if is_np05j else 'product'
    si_build='build/simpleinit-product-np05j-edk2' if is_np05j else 'build/simpleinit-product-edk2'
    out_dir='product-np05j' if is_np05j else 'product'
    contract=validate(json.loads((root/contract_file).read_text()))
    inputs=v_mod.load(root)
    vendor_record=v_mod.record(root,inputs)
    source=root/'uefi/core'
    missing=[name for name in SOURCE_NAMES if not core_source(name, root).is_file()]
    if missing:raise ValueError('Actual product core sources missing: '+', '.join(missing))
    os_boot_files(root)
    observation_files(root)
    handoff_hooks=prepare_handoff(root,apply=True)
    prepare_pump(root,apply=True)
    from prepare_product_ui import prepare as prepare_ui
    ui=prepare_ui(root,apply=True)
    product_app=root/'artifacts/simpleinit'/si_subdir
    simpleinit=inspect_simpleinit(root/si_build,product_app,True,device=device)
    if json.loads((product_app/'build-ok.json').read_text())!=simpleinit:raise ValueError('Product SimpleInit build identity stale')
    header,app_identity=digest_header(product_app/'SimpleInit.efi')
    raw=(product_app/'SimpleInit.efi').read_bytes()
    (product_app/'app-payload.bin').write_bytes(struct.pack('<16sIIQ32s',b'SUNUEFI-APPv1\0',1,64,len(raw),hashlib.sha256(raw).digest())+raw)
    target=root/'uefi/platforms'/pkg_name
    if target.exists():shutil.rmtree(target)
    shutil.copytree(root/'uefi/platforms/pianoProbePkg',target)
    for path in target.rglob('*'):
        if path.is_file() and path.suffix in ('.c','.h','.inf','.dsc','.dec','.fdf','.py'):
            path.write_text(path.read_text().replace('pianoProbe',stem))
    for suffix in ('dsc','dec','fdf'):(target/f'pianoProbe.{suffix}').rename(target/f'{stem}.{suffix}')
    shutil.rmtree(target/'Library/Stage0BootManagerLib')
    shutil.rmtree(target/'Library/RamLogSerialPortLib')
    shutil.copytree(root/'uefi/components/product-support',target,dirs_exist_ok=True)
    app=target/'Applications/ProductCore';app.mkdir(parents=True)
    for name in SOURCE_NAMES:shutil.copyfile(core_source(name, root),app/name)
    from compose_piano_dtb import read_fdt
    nodes=read_fdt((root/v_mod.XBL_DTB).read_bytes())['tree']
    tables=[props['table'] for path,props in nodes.items()
            if path.endswith('/usb0/ss_phy_cfg') and 'table' in props]
    if len(tables)!=1 or len(tables[0])%8:raise ValueError('Expected one XBL USB3 PHY table')
    ss_pairs=[]
    for address,value in struct.iter_unpack('>II',tables[0]):
        if address==0 and value==0:break
        if address<0x88e8000 or address>=0x88eb000 or address%4 or value>255:
            raise ValueError('XBL USB3 PHY table is outside QMP registers')
        ss_pairs.append((address-0x88e8000,value))
    if not ss_pairs:raise ValueError('Empty XBL USB3 PHY table')
    (app/'PianoUsbPhyConfig.h').write_text(
        '// Generated from the fixed XBL DTB; no per-device identity.\n'
        'STATIC CONST struct {UINT32 Offset,Value;} mPianoUsbSsPhy[]={\n'+
        ''.join(f'  {{0x{offset:x},0x{value:x}}},\n' for offset,value in ss_pairs)+'};\n')
    shared=[path for path in source.rglob('*') if path.is_file() and path.suffix in ('.h','.inc')]
    if len({path.name for path in shared})!=len(shared):
        raise ValueError('Duplicate core header name across subsystems')
    for path in shared:
        shutil.copyfile(path,app/path.name)
        nested=app/'core'/path.relative_to(source)
        nested.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,nested)
    shutil.copytree(source/'Protocol',app/'Protocol')
    shutil.copyfile(source/'Protocol/PianoRawPartitionWrite.h',
                    root/'upstream/Mu-Silicium/Mu_Basecore/MdePkg/Include/Protocol/PianoRawPartitionWrite.h')
    components=product_components(device)
    for component,names in components.items():
        for name in names:shutil.copyfile(root/'uefi/components'/component/name,app/name)
    pogo_firmware=None if is_np05j else prepare_pogo_firmware(root,app)
    touch_firmware=None if is_np05j else prepare_touch_firmware(root,app)
    shutil.copyfile(root/'uefi/components/nv/Include/Protocol/PianoNvFile.h',app/'Protocol/PianoNvFile.h')
    os_boot=prepare_os_boot(root,app)
    late_provider=stage_provider(root,app)
    late_provider['root_initialization_bound']=True
    late_provider['os_image_armed']=False
    observation=prepare_observation_families(root,app)
    (app/'PianoProductSimpleInitDigest.h').write_text(header)
    (app/'PianoSimpleInitBootGuid.h').write_text(simpleinit_boot_guid_header(root))
    shutil.copyfile(root/'upstream/Mu-Silicium/Mu_Basecore/MdeModulePkg/Universal/Console/GraphicsConsoleDxe/PianoChineseGlyphs.h',app/'PianoChineseGlyphs.h')
    (app/'ProductCore.inf').write_text(core_inf(device))
    from firmware.prepare_ext4 import prepare as prepare_ext4
    ext4_library=prepare_ext4(root,target)
    verify_os_boot(root,app,os_boot)
    verify_observation_families(root,app,observation)
    verify_provider(root,app,late_provider)
    native_fdf,native_id=native_modules(root,app,inputs,device=device)
    memory=target/'Library/MemoryMapLib/MemoryMapLib.c';text=memory.read_text()
    if is_np05j:
        text,low_memory_contract=fix_np05j_low_heap(inputs['board_dtb'])
    else:
        text,low_memory_contract=fix_product_low_heap(text,inputs['board_dtb'])
        anchor='  {"CRYPTO0_CRYPTO",'
        if text.count(anchor)!=1:raise ValueError('Unexpected product MMIO map anchor')
        text=text.replace(anchor,'  {"UFS_HCI", 0x1D84000, 0x3000, AddDev, 1, 0x400, 11, NS_DEVICE},\n'
            '  {"Piano_USB2_PHY", 0x88E3000, 0x1000, AddDev, 1, 0x400, 11, NS_DEVICE},\n'
            '  {"Piano_USB3_PHY", 0x88E8000, 0x3000, AddDev, 1, 0x400, 11, NS_DEVICE},\n'+anchor)
        old='  {"Display_Demura", 0xA3500000, 0x2C80000, AddMem, 5, 0x703C07, 0, WRITE_THROUGH_XN},'
        if text.count(old)!=1:raise ValueError('Unexpected product ramoops map')
        text=text.replace(old,'  {"Piano_Ramoops", 0xA3500000, 0x400000, AddMem, 5, 0x703C07, 0, UNCACHED_UNBUFFERED_XN},\n'
            '  {"Display_Demura_Tail", 0xA3900000, 0x2880000, AddMem, 5, 0x703C07, 0, WRITE_THROUGH_XN},')
    text,display_candidate=d_mod.prepare(root,text)
    display_contract={'candidate':display_candidate,'compiled_into_product':True,
                      'hardware_verified':False,'register_access_authorized':False}
    memory.write_text(text)
    verify_display_mapping(root,target,display_contract,device=device)
    dsc=target/f'{stem}.dsc'
    target_guid='A1E46001-9B1C-4F2A-8C3D-10054E503035' if is_np05j else '5E182CB1-63D4-44B5-AF6E-C23959625BA1'
    text=dsc.read_text().replace('183A8587-C1F1-5FDD-8E2A-127FB6BD81A4',target_guid)
    text=text.replace(f'{pkg_name}/Library/Stage0BootManagerLib/Stage0BootManagerLib.inf',f'{pkg_name}/Library/ProductBootManagerLib/ProductBootManagerLib.inf')
    text=text.replace(f'{pkg_name}/Library/RamLogSerialPortLib/FrameBufferSerialPortLib.inf',f'{pkg_name}/Library/RamOnlySerialPortLib/RamOnlySerialPortLib.inf')
    text=text.replace('# Stage 0: no persistent variables, storage, capsules or OS boot.','# Single product integration candidate; unfinished backends reported explicitly.')
    text+='''
[LibraryClasses.common.DXE_CORE, LibraryClasses.common.DXE_DRIVER, LibraryClasses.common.UEFI_DRIVER, LibraryClasses.common.UEFI_APPLICATION]
  PianoProductPumpLib|MdePkg/Library/PianoProductPumpLib/PianoProductPumpLib.inf
[LibraryClasses.common.DXE_CORE]
  PianoProductExitLib|MdePkg/Library/PianoProductExitLib/PianoProductExitLib.inf
[LibraryClasses]
  RealTimeClockLib|'''+pkg_name+'''/Library/UnsupportedRealTimeClockLib/UnsupportedRealTimeClockLib.inf
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf
  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  ShellCommandLib|ShellPkg/Library/UefiShellCommandLib/UefiShellCommandLib.inf
  HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf
  BcfgCommandLib|ShellPkg/Library/UefiShellBcfgCommandLib/UefiShellBcfgCommandLib.inf
[Components]
  # FAT filename collation falls back to the provided EnglishDxe service.
  # Keep zh-CN as the platform/UI default; it must not prevent FAT mounting.
  FatPkg/EnhancedFatDxe/Fat.inf {
    <PcdsFixedAtBuild>
      gEfiMdePkgTokenSpaceGuid.PcdUefiVariableDefaultPlatformLang|"en-US"
  }
  MdeModulePkg/Logo/LogoDxe.inf
  MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
  '''+pkg_name+'''/Drivers/PianoGopDxe/PianoGopDxe.inf
  '''+pkg_name+'''/Applications/ProductCore/ProductCore.inf {
    <LibraryClasses>
      NULL|'''+ext4_library+'''
      BaseCryptLib|OpensslPkg/Library/BaseCryptLib/BaseCryptLib.inf
      OpensslLib|OpensslPkg/Library/OpensslLib/OpensslLib.inf
      IntrinsicLib|CryptoPkg/Library/IntrinsicLib/IntrinsicLib.inf
      RngLib|MdePkg/Library/BaseRngLibNull/BaseRngLibNull.inf
  }
  ShellPkg/Application/Shell/Shell.inf {
    <PcdsFixedAtBuild>
      gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE
      gEfiShellPkgTokenSpaceGuid.PcdShellSupportLevel|3
      gEfiShellPkgTokenSpaceGuid.PcdShellProfileMask|0x01
    <LibraryClasses>
      NULL|ShellPkg/Library/UefiShellLevel1CommandsLib/UefiShellLevel1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel2CommandsLib/UefiShellLevel2CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel3CommandsLib/UefiShellLevel3CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDriver1CommandsLib/UefiShellDriver1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellInstall1CommandsLib/UefiShellInstall1CommandsLib.inf
  }
'''
    text+='  '+pkg_name+'/'+SIMPLEINIT_BOOT_MODULE+'\n'
    text+=SETUP_DSC_ADDITIONS
    if is_np05j:
        # ใช้ไลบรารีหน่วยความจำ ARM64 assembly เฉพาะโมดูลหลัง CpuDxe (UEFI_DRIVER/UEFI_APPLICATION และ PianoGopDxe)
        text = text.replace(
            '  '+pkg_name+'/Drivers/PianoGopDxe/PianoGopDxe.inf\n',
            '  '+pkg_name+'/Drivers/PianoGopDxe/PianoGopDxe.inf {\n'
            '    <LibraryClasses>\n'
            '      BaseMemoryLib|MdePkg/Library/BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf\n'
            '  }\n',
            1,
        )
        text += '''
[LibraryClasses.common.UEFI_DRIVER, LibraryClasses.common.UEFI_APPLICATION]
  BaseMemoryLib|MdePkg/Library/BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf
'''
        text=text.replace('gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"','gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"nubia"')
        text=text.replace('gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Pad 8 Pro"','gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"REDMAGIC Astra"')
        text=text.replace('gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"piano"','gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"np05j"')
        text=text.replace('gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"25091RP04C"','gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"NP05J"')
        text=text.replace('gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"piano"','gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"np05j"')
        text=text.replace('gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|3200','gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|2400')
        text=text.replace('gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2136','gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1504')
        text=text.replace('gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|3200','gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|2400')
        text=text.replace('gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|2136','gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|1504')
        text=text.replace('gEfiMdePkgTokenSpaceGuid.PcdUefiVariableDefaultPlatformLangCodes|"zh-CN;en-US;en;fr-FR;fr"','gEfiMdePkgTokenSpaceGuid.PcdUefiVariableDefaultPlatformLangCodes|"en-US;en;zh-CN;fr-FR;fr"')
        text=text.replace('gEfiMdePkgTokenSpaceGuid.PcdUefiVariableDefaultPlatformLang|"zh-CN"','gEfiMdePkgTokenSpaceGuid.PcdUefiVariableDefaultPlatformLang|"en-US"')
    # All service flags bind actual product implementations; diagnostics are
    # not dispatched. FETCH enables the shared live SMMU storage guard.
    display_test=os.environ.get('SUNUEFI_DISPLAY_DIAGNOSTIC','0')
    if display_test not in ('0','1','2'):raise ValueError('SUNUEFI_DISPLAY_DIAGNOSTIC must be 0, 1 or 2')
    flags=PRODUCT_FLAGS+(('SUNUEFI_NP05J=1','PIANO_USB_CONSOLE_BASE=0xE4A00000U') if is_np05j else ())+('PIANO_PRODUCT_DISPLAY_TEST='+display_test,)
    opt_flags=('-O2 -march=armv8.7-a+crypto -mtune=oryon-1 ' if is_np05j else '')
    text+='\n[BuildOptions]\n  GCC:*_CLANGPDB_AARCH64_CC_FLAGS = '+opt_flags+' '.join('-D'+flag for flag in flags)+'\n'
    dsc.write_text(text)
    fdf=target/f'{stem}.fdf';text=fdf.read_text().replace('# Host-only stage-0 diagnostic; NOT hardware validated.',f'# Single {stem} product integration candidate; INCOMPLETE_NOT_RELEASE.')
    text=text.replace('SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf',f'{pkg_name}/Drivers/PianoGopDxe/PianoGopDxe.inf')
    if is_np05j:
        text=text.replace('Binaries/piano/Stage0/EnvDxeEnhanced','Binaries/np05j/Stage0/EnvDxeEnhanced')
    modules=DISK_MODULES+HOST_MODULES+SETUP_FV_MODULES+(
        BOOT_PICKER_MODULE,f'{pkg_name}/'+SIMPLEINIT_BOOT_MODULE,'MdeModulePkg/Logo/LogoDxe.inf',
        'ShellPkg/Application/Shell/Shell.inf',f'{pkg_name}/Applications/ProductCore/ProductCore.inf')
    text=text.replace('!include SiliciumPkg/Common.fdf.inc',''.join('  INF '+module+'\n' for module in modules)+native_fdf+'\n!include SiliciumPkg/Common.fdf.inc')
    fdf.write_text(text)
    early_memory=prepare_early_memory(root,target)
    verify_early_memory(root,target,early_memory)
    staged=root/'upstream/Mu-Silicium/Platforms'/vendor_dir/pkg_name
    if staged.exists():shutil.rmtree(staged)
    shutil.copytree(target,staged)
    verify_early_memory(root,staged,early_memory)
    verify_display_mapping(root,staged,display_contract,device=device)
    verify_os_boot(root,staged/'Applications/ProductCore',os_boot)
    verify_observation_families(root,staged/'Applications/ProductCore',observation)
    verify_provider(root,staged/'Applications/ProductCore',late_provider)
    comp_c_sources=tuple(name for names in components.values() for name in names if name.endswith('.c'))
    manifest={'target':'product','device':device,'artifact':contract['artifact'],'status':'INCOMPLETE_NOT_RELEASE',
      'features':contract['features'],'shared_core':True,'entry_points':contract['entry_points'],'entry_policy_only':True,
      'fastboot_mode':'resident_background','fastboot_surfaces':contract['fastboot']['available_in'],
      'default_application':'SimpleInit','setup_key':'F12','diagnostic_reboot_timer':False,
      'backend_initialization_required':True,'backends':backend_status(device),'runtime_readiness':'NOT_PRODUCT_DEVICE_VALIDATED',
      'service_compile_flags':list(flags),'display_diagnostic':display_test!='0','clock_init_trace':display_test=='2','sources':list((*SOURCE_NAMES,*comp_c_sources,*OS_BOOT_INF_SOURCES,*OBSERVATION_INF_SOURCES)),'native_foundation':native_id,'os_boot':os_boot,'dxe_observation':observation,'early_memory':early_memory,
      'simpleinit':simpleinit,'simpleinit_payload':app_identity,'ui_hooks':ui,'pump_hooks':prepare_pump(root,apply=False),
      'low_memory_contract':low_memory_contract,'pogo_mcu_firmware':pogo_firmware,'touch_firmware':touch_firmware,
      'display_mapping':display_contract,
      'native_late_handoff':handoff_hooks,'late_provider':late_provider,
      'vendor_inputs':vendor_record,
      'platform_files':{str(path.relative_to(target)):sha(path)for path in sorted(target.rglob('*'))if path.is_file()},
      'device_boot_performed':False,'permanent_storage_writes':False}
    out=root/'build'/out_dir;out.mkdir(parents=True,exist_ok=True)
    if v_mod.record(root)!=vendor_record:raise ValueError('Vendor inputs changed during product preparation')
    (out/'prepared-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',choices=('piano','np05j'),default='piano')
    args=parser.parse_args()
    result=prepare(device=args.device)
    payload={'status':result['status'],'artifact':result['artifact'],'sources':len(result['sources']),'diagnostic_reboot_timer':False}
    if args.device!='piano':payload['device']=args.device
    print(json.dumps(payload,indent=2))


if __name__=='__main__':main()
