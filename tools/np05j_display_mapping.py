#!/usr/bin/env python3
"""Host-only display MMIO mapping candidate for nubia NP05J."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from compose_piano_dtb import read_fdt
import np05j_vendor_inputs as vendor_inputs

ROOT = Path(__file__).resolve().parents[1]
NATIVE = 'uefi/platforms/np05jProductPkg/Library/MemoryMapLib/MemoryMapLib.c'
DTB = vendor_inputs.BOARD_DTB
NATIVE_DTB = vendor_inputs.XBL_DTB
CLOCK_PE = 'upstream/Mu-Silicium/Binaries/np05j/ProductFoundation/ClockDxe/ClockDxe.efi'
DERIVED_CLOCK_SHA256 = '3b76bda865353e35fdaad3b29f5948bd2c7d74a98ca13bcd01b5ab1f130bb7a3'
EXPECTED_BASE_ROWS = 48
PINS = {
    DTB: vendor_inputs.BOARD_DTB_SHA256,
    NATIVE_DTB: vendor_inputs.XBL_DTB_SHA256,
    CLOCK_PE: DERIVED_CLOCK_SHA256,
}
WINDOWS = (
    ('Piano_Display_GCC', '/soc/clock-controller@100000', 0x100000, 0x1f4200, 0x1f5000),
    ('Piano_Display_DPU', '/soc/qcom,mdss_mdp@ae00000', 0xae00000, 0x93800, 0x94000),
    ('Piano_Display_DISPCC', '/soc/clock-controller@af00000', 0xaf00000, 0x20000, 0x20000),
    ('Piano_Display_CESTA', '/soc/cesta@af27000', 0xaf27000, 0x3000, 0x3000),
)
CESTA_NAMES = ('SDE_CRMB', 'SDE_CRMB_PT', 'SDE_CRMC', 'SDE_CRMV', 'SDE_CRM_COMMON')
CESTA_REGS = ((0xaf27000, 0x400), (0xaf27400, 0x400), (0xaf27800, 0x2000), (0xaf29800, 0x700), (0xaf29f00, 0x100))
SYMBOLS = {'NoHob': 0, 'AddMem': 1, 'AddDev': 2, 'HobOnlyNoCacheSetting': 3, 'AllocOnly': 4,
           'SYS_MEM': 0, 'MMAP_IO': 1, 'MEM_RES': 5, 'SYS_MEM_CAP': 0x703c07, 'EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE': 0x400,
           'BsData': 4, 'EfiLoaderData': 2, 'EfiConventionalMemory': 7, 'EfiMemoryMappedIO': 11,
           'UNCACHED_UNBUFFERED_XN': 0, 'WRITE_BACK': 1, 'WRITE_BACK_XN': 1,
           'WRITE_THROUGH_XN': 5, 'NS_DEVICE': 6, 'DEVICE': 6, 'ARM_MEMORY_REGION_ATTRIBUTE_DEVICE': 6}
ROW = re.compile(r'\{\s*"([^"\n]+)"\s*,\s*(0x[\da-fA-F]+)\s*,\s*(0x[\da-fA-F]+)\s*,\s*([^,{}]+),\s*([^,{}]+),\s*([^,{}]+),\s*([^,{}]+),\s*([^,{}]+)\}')
MARKER = '\n  // แผนผัง MMIO จอภาพที่ตรวจสอบกับ DTB บนโฮสต์สำหรับ NP05J (2400x1504).\n'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def number(token):
    token = token.strip()
    if token in SYMBOLS:
        return SYMBOLS[token]
    if re.fullmatch(r'0x[\da-fA-F]+|\d+', token):
        return int(token, 0)
    raise ValueError('Unsupported typed descriptor token: ' + token)


def span(base, size):
    if type(base) is not int or type(size) is not int or base < 0 or size <= 0 or base > (1 << 64) - 1 - size:
        raise ValueError('Invalid/overflowing span')
    return base, base + size


def overlaps(a, b):
    return a[0] < b[1] and b[0] < a[1]


def parse_native(data):
    text = data.decode()
    array = re.search(r'STATIC\s+EFI_MEMORY_REGION_DESCRIPTOR\s+gMemoryDescriptor\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not array:
        raise ValueError('Native typed array boundary changed')
    rows = []
    for match in ROW.finditer(array[1]):
        name, base, size, *tokens = match.groups()
        values = [number(token) for token in tokens]
        bounds = span(int(base, 16), int(size, 16))
        if len(name) >= 32 or (bounds[0] | bounds[1]) & 4095:
            raise ValueError('Native name/page bounds invalid')
        rows.append({'name': name, 'base': bounds[0], 'end': bounds[1], 'size': bounds[1] - bounds[0],
                     'hob': values[0], 'resource': values[1], 'resource_attrs': values[2],
                     'memory_type': values[3], 'arm_attrs': values[4], 'source_text': match[0]})
    if len(rows) != array[1].count('{') or len({r['name'] for r in rows}) != len(rows):
        raise ValueError('Skipped/duplicate native descriptors')
    for i, a in enumerate(rows):
        for b in rows[i + 1:]:
            if not overlaps((a['base'], a['end']), (b['base'], b['end'])):
                continue
            role = all(r['hob'] == 2 and r['resource'] == 1 and r['memory_type'] == 11 and r['arm_attrs'] == 6 for r in (a, b))
            contains = (a['base'] <= b['base'] and b['end'] <= a['end']) or (b['base'] <= a['base'] and a['end'] <= b['end'])
            if not role or not contains or a['resource_attrs'] != b['resource_attrs']:
                raise ValueError('Conflicting existing native descriptors')
    return text, array, rows


def validate(inputs, native_data, owners=()):
    tree = read_fdt(inputs[DTB])['tree']
    native_tree = read_fdt(inputs[NATIVE_DTB])['tree']
    _, _, native = parse_native(native_data)
    if len(native) != EXPECTED_BASE_ROWS:
        raise ValueError(f'Expected the {EXPECTED_BASE_ROWS}-row NP05J product table, got {len(native)}')
    protected = [(r['name'], (r['base'], r['end'])) for r in native]
    if len(owners) > 64:
        raise ValueError('Too many explicit owners')
    for owner in owners:
        if set(owner) != {'name', 'base', 'size'} or not isinstance(owner['name'], str):
            raise ValueError('Malformed owner record')
        protected.append((owner['name'], span(owner['base'], owner['size'])))
    windows = []
    for name, path, base, raw_size, page_size in WINDOWS:
        cesta = name == 'Piano_Display_CESTA'
        props = (native_tree if cesta else tree).get(path)
        if cesta:
            expected_reg = b''.join(struct.pack('>II', *r) for r in CESTA_REGS)
            expected_names = '\0'.join(CESTA_NAMES).encode() + b'\0'
            if not props or props.get('reg') != expected_reg or props.get('reg-names') != expected_names:
                raise ValueError('Native CESTA exact reg/name changed')
            cursor = base
            for start, length in CESTA_REGS:
                if start != cursor:
                    raise ValueError('CESTA role has a gap or overlap')
                cursor = span(start, length)[1]
            if cursor != base + raw_size:
                raise ValueError('CESTA role union changed')
            crm = tree.get('/soc/crm@af21000', {})
            corroborated = b''.join(struct.pack('>II', *r) for r in ((0xaf21000, 0x6000), *CESTA_REGS))
            if crm.get('reg') != corroborated or tree.get('/soc/syscon@af27800', {}).get('reg') != struct.pack('>II', 0xaf27800, 0x2000):
                raise ValueError('NP05J CRM/CRMC corroboration changed')
        elif not props or props.get('reg', b'')[:8] != struct.pack('>II', base, raw_size):
            raise ValueError('NP05J ROM exact reg changed: ' + path)
        if path.endswith('mdss_mdp@ae00000') and props.get('reg-names', b'').split(b'\0')[0] != b'mdp_phys':
            raise ValueError('NP05J DPU resource identity changed')
        if not cesta and path != WINDOWS[1][1] and len(props['reg']) != 8:
            raise ValueError('NP05J clock-controller reg tuple changed')
        bounds = span(base, page_size)
        if base & 4095 or page_size != (raw_size + 4095) & ~4095 or bounds[1] > 0x80000000:
            raise ValueError('Display envelope/page boundary invalid')
        for owner, occupied in protected:
            if overlaps(bounds, occupied):
                raise ValueError('Display candidate conflicts with ' + owner)
        for previous in windows:
            if overlaps(bounds, (previous['base'], previous['end'])):
                raise ValueError('Display candidate windows overlap')
        windows.append({'name': name, 'path': path, 'base': base, 'size': page_size, 'end': bounds[1],
                        'raw_size': raw_size, 'padding_bytes': page_size - raw_size,
                        'hob': 'AddDev', 'resource_type': 'MMAP_IO', 'resource_attrs': 'EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE',
                        'memory_type': 'EfiMemoryMappedIO', 'arm_attrs': 'ARM_MEMORY_REGION_ATTRIBUTE_DEVICE'})
    return windows


def render(native_data, windows):
    text, array, before = parse_native(native_data)
    added = MARKER
    added += '\n'.join('  {"%s", 0x%X, 0x%X, AddDev, MMAP_IO, EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE, EfiMemoryMappedIO, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE},' %
                       (r['name'], r['base'], r['size']) for r in windows)
    body = array[1].rstrip()
    if not body.endswith(','):
        body += ','
    generated = (text[:array.start(1)] + body + added + text[array.end(1):]).encode()
    _, _, after = parse_native(generated)
    if after[:len(before)] != before or len(after) != len(before) + len(windows) or len(after) >= 128:
        raise ValueError('Generated typed rows altered the native table')
    if any(r['hob'] != 2 or r['resource'] != 1 or r['resource_attrs'] != 0x400 or r['memory_type'] != 11 or r['arm_attrs'] != 6 for r in after[len(before):]):
        raise ValueError('Generated MMIO descriptor semantics differ')
    return generated


def source_files(root=ROOT):
    root = Path(root)
    files = [root / 'tools/np05j_display_mapping.py', *vendor_inputs.source_files(root)]
    for p in PINS:
        path = root / p
        if path.is_file():
            files.append(path)
    return tuple(dict.fromkeys(files))


def prepare(root, input_native_text, owners=()):
    root = Path(root)
    vendor_record = vendor_inputs.record(root)
    generator = root / 'tools/np05j_display_mapping.py'
    if generator.is_symlink():
        raise ValueError('NP05J display mapping generator is a symlink')
    generator_bytes = generator.read_bytes()
    native = input_native_text.encode()
    inputs = {}
    for relative, expected in PINS.items():
        path = root / relative
        if path.is_symlink():
            raise ValueError('Pinned input is a symlink: ' + relative)
        data = path.read_bytes()
        if digest(data) != expected:
            raise ValueError('NP05J display mapping source SHA drift: ' + relative)
        inputs[relative] = data
    windows = validate(inputs, native, owners)
    source = render(native, windows)
    metadata = {'status': 'HOST_DISPLAY_MMIO_CANDIDATE_REQUIRES_LIVE_AT_GCD',
                'original_rows': EXPECTED_BASE_ROWS, 'candidate_rows': EXPECTED_BASE_ROWS + len(windows),
                'windows': windows,
                'original_native_sha256': digest(native),
                'generator_sha256': digest(generator_bytes),
                'source_inputs': {p: {'sha256': digest(b), 'bytes': len(b)} for p, b in inputs.items()},
                'vendor_inputs': vendor_record,
                'candidate_sha256': digest(source), 'owners': list(owners),
                'conventional_bytes_added': 0, 'framebuffer_changed': False, 'high_ddr_changed': False,
                'applied_to_product': False, 'hardware_verified': False, 'register_access_authorized': False,
                'limits': ['UC resource HOB capabilities are not live GCD attributes.',
                           'Mapped page padding and DISPCC TZ offsets are not a register-read whitelist.',
                           'Actual AT/PTE/GCD and guarded-register admission remain required.']}
    if any((root / p).read_bytes() != b for p, b in inputs.items()):
        raise ValueError('NP05J display mapping source changed during generation')
    if generator.read_bytes() != generator_bytes:
        raise ValueError('NP05J display mapping generator changed during generation')
    if vendor_inputs.record(root) != vendor_record:
        raise ValueError('Vendor inputs changed during display generation')
    return source.decode(), metadata


def recover_original(expanded_text):
    text, array, rows = parse_native(expanded_text.encode())
    if len(rows) != EXPECTED_BASE_ROWS + len(WINDOWS) or array[1].count(MARKER) != 1:
        raise ValueError('Expanded NP05J display table block missing/duplicated')
    original_body = array[1].split(MARKER)[0]
    if not original_body.endswith(','):
        raise ValueError('Expanded NP05J display table separator changed')
    return text[:array.start(1)] + original_body[:-1] + text[array.end(1):]


def verify(root, expanded_text, record):
    if len(parse_native(expanded_text.encode())[2]) != EXPECTED_BASE_ROWS + len(WINDOWS):
        raise ValueError('Current NP05J display verification requires 52 rows')
    original = recover_original(expanded_text)
    if digest(original.encode()) != record.get('original_native_sha256'):
        raise ValueError('Expanded NP05J display original SHA drift')
    expected, expected_record = prepare(root, original, record.get('owners', ()))
    if expected != expanded_text or record != expected_record:
        raise ValueError('Expanded NP05J display source/record differs from pinned reconstruction')
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--memory-map', type=Path, required=True)
    args = parser.parse_args()
    source, record = prepare(ROOT, args.memory_map.read_text())
    print(json.dumps({'rows': record['candidate_rows'], 'sha256': record['candidate_sha256'], 'bytes': len(source)}, indent=2))


if __name__ == '__main__':
    main()
