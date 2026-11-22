#!/usr/bin/env python3
"""เตรียม source worktree และคอมไพล์ Linux 7.3 Kernel + DTBs สำหรับ nubia NP05J."""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))

import build_piano_full_kernel as piano_kbuild
from desktop.package_build import default_jobs
from prepare_boot_files import fdt_info, image_info

DEFAULT_WORKTREE = ROOT / 'build/kernel-worktrees/np05j'
DEFAULT_BUILD = ROOT / 'build/kernels/np05j'
DEFAULT_ARTIFACTS = ROOT / 'artifacts/kernels/np05j'
DEFAULT_SOURCE_MANIFEST = ROOT / 'build/release/source-manifest.json'
NP05J_SERIES = ROOT / 'patches/linux/7.3/np05j/series.json'
FRAGMENTS = (
    ROOT / 'linux/configs/np05j/np05j.frag',
    ROOT / 'linux/configs/np05j/phase2.frag',
    ROOT / 'linux/configs/np05j/phase3.frag',
    ROOT / 'linux/configs/np05j/gpi_dma.frag',
    ROOT / 'linux/configs/np05j/fan_i2c.frag',
    ROOT / 'linux/configs/np05j/fix_psci_pd.frag',
    ROOT / 'linux/configs/np05j/desktop.frag',
)
DTB_TARGETS = (
    'qcom/sm8750-nubia-np05j.dtb',
    'qcom/sm8750-nubia-np05j-otg.dtb',
    'qcom/sm8750-nubia-np05j-gpu.dtb',
)
REQUIRED = {
    'TOUCHSCREEN_SYNAPTICS_TCM2_SPI': 'y',
    'SENSORS_PWM_FAN': 'y',
    'LEDS_QCOM_LPG': 'y',
    'QCOM_GPI_DMA': 'y',
    'ATH12K': 'y',
    'BT_HCIUART_QCA': 'y',
    'EFI_STUB': 'y',
    'DRM_MSM': 'y',
    'DRM_SIMPLEDRM': 'y',
    'SYSFB_SIMPLEFB': 'y',
    'DRM_FBDEV_EMULATION': 'y',
    'IPV6': 'y',
    'CMDLINE': '""',
    'CMDLINE_FORCE': 'n',
}


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def run(cmd, cwd=None, env=None, capture=False):
    if capture:
        return subprocess.run(list(map(str, cmd)), cwd=cwd, env=env,
                              check=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True).stdout.strip()
    subprocess.run(list(map(str, cmd)), cwd=cwd, env=env, check=True)
    return ''


def prepare_worktree(base_manifest, worktree, epoch):
    """สร้างและรีเซ็ต worktree แยกของ NP05J จาก commit ที่ผ่านแพตช์ 0001..0140 แล้ว"""
    base_worktree = Path(base_manifest['worktree'])
    base_commit = base_manifest['actual_commit']
    base_tree = base_manifest['actual_tree']
    if not base_worktree.is_dir():
        raise ValueError('Missing release kernel worktree: ' + str(base_worktree))
    if run(['git', '-C', str(base_worktree), 'rev-parse', 'HEAD'], capture=True) != base_commit:
        raise ValueError('Release kernel worktree HEAD differs from source-manifest.json')
    if run(['git', '-C', str(base_worktree), 'rev-parse', 'HEAD^{tree}'], capture=True) != base_tree:
        raise ValueError('Release kernel worktree tree differs from source-manifest.json')

    worktree.parent.mkdir(parents=True, exist_ok=True)
    owner = worktree.with_name(worktree.name + '.np05j.json')
    common = run(['git', '-C', str(base_worktree), 'rev-parse',
                  '--path-format=absolute', '--git-common-dir'], capture=True)
    identity = {'schema': 1, 'worktree': str(worktree), 'git_common_dir': common}

    if worktree.exists():
        if not owner.is_file() or json.loads(owner.read_text()) != identity:
            raise ValueError('Existing NP05J worktree is not build-owned: ' + str(worktree))
        if run(['git', '-C', str(worktree), 'rev-parse', '--path-format=absolute', '--git-common-dir'], capture=True) != common:
            raise ValueError('NP05J worktree belongs to another git repository')
    else:
        run(['git', '-C', str(base_worktree), 'worktree', 'add', '--detach', '--force', str(worktree), base_commit])
        owner.write_text(json.dumps(identity, indent=2) + '\n')

    run(['git', '-C', str(worktree), '-c', 'core.hooksPath=/dev/null', 'checkout', '--detach', '--force', base_commit])
    run(['git', '-C', str(worktree), 'clean', '-ffdx'])

    series = json.loads(NP05J_SERIES.read_text())
    if series.get('schema_version') != 1 or series.get('targets') != ['linux']:
        raise ValueError('Unexpected NP05J patch series schema/targets')

    git_env = os.environ.copy()
    ts = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).isoformat()
    git_env.update({
        'GIT_AUTHOR_NAME': 'sunuefi-np05j',
        'GIT_AUTHOR_EMAIL': 'sunuefi-np05j@local',
        'GIT_AUTHOR_DATE': ts,
        'GIT_COMMITTER_NAME': 'sunuefi-np05j',
        'GIT_COMMITTER_EMAIL': 'sunuefi-np05j@local',
        'GIT_COMMITTER_DATE': ts,
    })

    applied = []
    for row in series['patches']:
        patch_path = NP05J_SERIES.parent / row['file']
        if not patch_path.is_file() or sha(patch_path) != row['sha256']:
            raise ValueError('NP05J patch missing or SHA-256 mismatch: ' + row['file'])
        run(['git', '-C', str(worktree), 'apply', '--index', '--whitespace=nowarn', str(patch_path)])
        applied.append({'file': row['file'], 'sha256': row['sha256']})

    run(['git', '-C', str(worktree), '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null',
         'commit', '--no-verify', '-m', f'NP05J: apply {len(applied)} Linux 7.3 patches'], env=git_env)
    if run(['git', '-C', str(worktree), 'status', '--porcelain=v1', '--untracked-files=all'], capture=True):
        raise ValueError('Prepared NP05J kernel worktree is not clean after patch commit')

    return {
        'base_commit': base_commit,
        'base_tree': base_tree,
        'np05j_commit': run(['git', '-C', str(worktree), 'rev-parse', 'HEAD'], capture=True),
        'np05j_tree': run(['git', '-C', str(worktree), 'rev-parse', 'HEAD^{tree}'], capture=True),
        'patches': applied,
    }


def validate_config(raw_config):
    values = piano_kbuild.config_values(raw_config)
    for key, expected in REQUIRED.items():
        actual = values.get('CONFIG_' + key, 'n')
        if actual != expected:
            raise ValueError(f'NP05J Kconfig mismatch for CONFIG_{key}: expected {expected!r}, got {actual!r}')
    return {key: values.get('CONFIG_' + key, 'n') for key in REQUIRED}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=default_jobs())
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--rebuild', action='store_true')
    parser.add_argument('--source-manifest', type=Path, default=DEFAULT_SOURCE_MANIFEST)
    parser.add_argument('--worktree', type=Path, default=DEFAULT_WORKTREE)
    parser.add_argument('--build-dir', type=Path, default=DEFAULT_BUILD)
    parser.add_argument('--artifacts', type=Path, default=DEFAULT_ARTIFACTS)
    args = parser.parse_args()
    if args.jobs < 1:
        raise SystemExit('Use at least one kernel build job')

    worktree = args.worktree.resolve()
    out = args.build_dir.resolve()
    artifacts = args.artifacts.resolve()
    if (not worktree.is_relative_to(ROOT / 'build/kernel-worktrees') or
            not out.is_relative_to(ROOT / 'build/kernels') or
            not artifacts.is_relative_to(ROOT / 'artifacts/kernels')):
        raise ValueError('NP05J kernel paths must stay inside build/ and artifacts/')

    out.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)

    lock_root = ROOT / 'build/locks'
    lock_root.mkdir(parents=True, exist_ok=True)
    locks = []
    for directory in sorted({worktree, out, artifacts}):
        handle = (lock_root / (hashlib.sha256(str(directory).encode()).hexdigest() + '.lock')).open('a')
        locks.append(handle)
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('NP05J kernel directory is in use: ' + str(directory))

    policy = piano_kbuild.kernel_policy_identity()
    abi_epoch = policy['policy']['abi_epoch']
    env, tools = piano_kbuild.toolchain()
    env.update(piano_kbuild.kernel_build_environment(abi_epoch))

    base_manifest = json.loads(args.source_manifest.read_text())
    source_state = prepare_worktree(base_manifest, worktree, abi_epoch)

    for frag in FRAGMENTS:
        if not frag.is_file():
            raise ValueError('Missing NP05J kernel config fragment: ' + str(frag))

    inline_frag = out / 'np05j-cmdline.frag'
    inline_frag.write_text('CONFIG_CMDLINE=""\nCONFIG_CMDLINE_FORCE=n\nCONFIG_LOCALVERSION_AUTO=n\n')

    make_cmd = [
        'make', '-C', str(worktree), f'O={out}',
        'ARCH=arm64', 'LLVM=1', 'LLVM_IAS=1', 'LOCALVERSION=-np05j',
        'CC=' + ' '.join(tools['kernel_cc']) if len(tools['kernel_cc']) == 1 else
        'CC=' + __import__('shlex').join(tools['kernel_cc']),
    ]
    if 'aarch64-linux-gnu-gcc' in tools['paths']:
        make_cmd.append('CROSS_COMPILE=aarch64-linux-gnu-')

    marker = artifacts / 'manifest.json'
    if not args.configure_only and marker.exists():
        if not args.rebuild:
            raise ValueError('NP05J kernel artifacts exist; pass --rebuild to regenerate')
        marker.unlink()

    run(make_cmd + ['defconfig'], env=env)
    run(['bash', str(worktree / 'scripts/kconfig/merge_config.sh'), '-m', '-O', str(out),
         str(out / '.config'), *[str(f) for f in FRAGMENTS], str(inline_frag)],
        cwd=worktree, env=env)
    run(make_cmd + ['olddefconfig'], env=env)

    config_raw = (out / '.config').read_text()
    checked = validate_config(config_raw)
    config_hash = sha(out / '.config')
    shutil.copyfile(out / '.config', artifacts / 'config')

    state = {
        'device': 'np05j',
        'status': 'CONFIGURED_NOT_BUILT' if args.configure_only else 'BUILDING_NOT_BOOTABLE',
        'source': source_state,
        'config_sha256': config_hash,
        'checked_kconfig': checked,
        'toolchain': tools,
        'fragments': [str(f.relative_to(ROOT)) for f in FRAGMENTS],
        'hardware_verified': False,
    }
    if args.configure_only:
        print(json.dumps(state, indent=2))
        return 0

    run(make_cmd + [f'-j{args.jobs}', 'Image', 'modules', *DTB_TARGETS], env=env)

    built_image = out / 'arch/arm64/boot/Image'
    img_meta = image_info(built_image, kernel=True, simpleinit_limit=False)
    shutil.copyfile(built_image, artifacts / 'Image')
    shutil.copyfile(out / 'System.map', artifacts / 'System.map')

    # ติดตั้งโมดูลเคอร์เนลลง artifacts/kernels/np05j/modules เพื่อคัดลอกเข้า rootfs ของ Ubuntu
    modules_dir = artifacts / 'modules'
    if modules_dir.exists():
        shutil.rmtree(modules_dir)
    modules_dir.mkdir(parents=True, exist_ok=True)
    run(make_cmd + [f'-j{args.jobs}', f'INSTALL_MOD_PATH={modules_dir}', 'INSTALL_MOD_STRIP=1', 'modules_install'], env=env)
    kernel_release = (out / 'include/config/kernel.release').read_text().strip()
    mod_rel = modules_dir / 'lib/modules' / kernel_release
    for link_name in ('build', 'source'):
        link_path = mod_rel / link_name
        if link_path.is_symlink() or link_path.exists():
            link_path.unlink()
    module_count = sum(1 for _ in mod_rel.rglob('*.ko*')) if mod_rel.is_dir() else 0

    dtb_meta = {}
    for rel_dtb in DTB_TARGETS:
        src_dtb = out / 'arch/arm64/boot/dts' / rel_dtb
        dst_dtb = artifacts / Path(rel_dtb).name
        info = fdt_info(src_dtb)
        shutil.copyfile(src_dtb, dst_dtb)
        dtb_meta[dst_dtb.name] = {
            'bytes': dst_dtb.stat().st_size,
            'sha256': sha(dst_dtb),
            'compatible': info.get('compatible'),
        }

    state.update({
        'status': 'HOST_BUILT_NP05J_CANDIDATE_NOT_HARDWARE_VERIFIED',
        'kernel_release': kernel_release,
        'image': img_meta,
        'dtbs': dtb_meta,
        'modules_dir': str(modules_dir),
        'module_count': module_count,
    })
    tmp = marker.with_suffix('.json.tmp')
    tmp.write_text(json.dumps(state, indent=2) + '\n')
    os.replace(tmp, marker)
    print(json.dumps({
        'status': state['status'],
        'kernel_release': state['kernel_release'],
        'image': img_meta,
        'dtbs': dtb_meta,
    }, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
