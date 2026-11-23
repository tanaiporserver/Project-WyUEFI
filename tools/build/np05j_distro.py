#!/usr/bin/env python3
"""สร้าง Native Overlays, Rootfs, Shared ESP และ Sparse Images สำหรับ nubia NP05J (REDMAGIC Astra)

รองรับครบทั้ง 6 ดิสโทรปลายทางของ CI:
  - debian-gnome, debian-kde, debian-denial
  - nyarch-gnome, arch-denial
  - steamos
โดยทุกดิสโทรใช้เคอร์เนล 7.3.3-np05j, เฟิร์มแวร์สต็อก NP05J (vendor/np05j-firmware),
AudioReach topology (TFA9874 + WCD9395 AMIC1/AMIC3), udev touch/protect-android,
และ Shared ESP พร้อม early-firmware initramfs ที่ยืนยันบนฮาร์ดแวร์จริงแล้ว
"""
import argparse
import copy
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))

import assemble_rootfs
import build_arch_package
import build_nyarch_apps
import build_nyarch_rootfs
import build_release_rootfs
import build_steamos_gamescope
import build_steamos_manager
import build_steamos_rootfs
import package_sparse_images
import prepare_debian_desktop
import prepare_nyarch_material
import prepare_steam_client
import prepare_steam_runtime
import prepare_steamos_rootfs
import stage_nyarch_desktop
import stage_steamos_desktop
from build import arch_mesa, denial, denial_manifest, mesa, overlay_packages
from build.np05j_ubuntu import (
    DEFAULT_FIRMWARE,
    DEFAULT_KERNEL,
    build_early_firmware_cpio_gz,
    ensure_qemu_binfmt,
    install_np05j_audio_userspace,
    install_np05j_firmware,
    sha,
    write_np05j_firmware_service,
)
from build_chroot import arch_mirror_url, guest_command, stop_helpers
from build_piano_disk_bootstrap import APPLETS as BOOTSTRAP_APPLETS, runtime_files as bootstrap_runtime_files
from build_piano_ram_bootstrap import guest_resolve
from make_kernel_initramfs import make_newc, validate_static_arm64_elf
from package.release import shared_stub
from stage_piano_full_userspace import stage_gnome_desktop

MIB = 1024 * 1024
DISTRO_TARGETS = (
    'debian-gnome',
    'debian-kde',
    'debian-denial',
    'nyarch-gnome',
    'arch-denial',
    'steamos',
)
OVERLAY_TARGETS = ('debian-gnome', 'nyarch-gnome', 'steamos')
ALL_OVERLAY_TARGETS = (*OVERLAY_TARGETS, 'debian-denial', 'arch-denial')
DEBIAN_MIRROR = 'https://deb.debian.org/debian'
DEBIAN_KEYRING = '/usr/share/keyrings/debian-archive-keyring.gpg'

DEBIAN_BASE_PACKAGES = (
    'systemd-sysv', 'dbus', 'sudo', 'locales', 'network-manager',
    'openssh-server', 'kmod', 'util-linux', 'e2fsprogs', 'dosfstools',
    'bluez', 'wpasupplicant', 'rfkill', 'wireless-regdb',
    'pipewire', 'pipewire-pulse', 'wireplumber', 'alsa-utils', 'alsa-ucm-conf',
    'rtkit', 'libcanberra-pulse', 'polkitd', 'pkexec',
    'chromium', 'chromium-sandbox', 'chromium-l10n',
    'fonts-noto-core', 'fonts-noto-cjk', 'fonts-noto-color-emoji',
    'zram-tools', 'mesa-utils', 'vulkan-tools', 'libgl1-mesa-dri',
    'mesa-vulkan-drivers', 'curl', 'usbutils', 'pciutils', 'systemd-coredump',
)
DEBIAN_GNOME_PACKAGES = (
    'task-gnome-desktop', 'gnome-core', 'gdm3', 'gnome-control-center',
    'gnome-console', 'gnome-text-editor', 'gnome-system-monitor', 'nautilus',
    'dconf-cli', 'xdg-desktop-portal-gnome', 'ibus', 'ibus-libpinyin',
    'ibus-gtk3', 'ibus-gtk4', 'task-chinese-s-gnome-desktop',
)
DEBIAN_KDE_PACKAGES = tuple(dict.fromkeys((
    'plasma-desktop', 'plasma-workspace', *prepare_debian_desktop.KDE_PACKAGES,
)))
DEBIAN_DENIAL_BASE_PACKAGES = ('sddm', 'zenity', 'dconf-cli')

NP05J_INIT_SCRIPT = '''#!/bin/sh
# สคริปต์เริ่มต้นใน initramfs กลางสำหรับ nubia NP05J (REDMAGIC Astra)
# บังคับตรวจทั้ง PARTNAME=sunuefi_root, LABEL=PIANOROOT และ TYPE=ext4 เพื่อปกป้องพาร์ติชัน Android เสมอ
export PATH=/usr/sbin:/usr/bin:/bin:/sbin

log() {
    printf 'np05j-init: %s\\n' "$*"
    kmsg=/dev/kmsg
    [ -c "$kmsg" ] || kmsg=/sysroot/dev/kmsg
    if [ -c "$kmsg" ]; then printf '<5>np05j-init: %s\\n' "$*" >"$kmsg"; fi
}

fail() {
    log "ROOT_MOUNT_FAILED: $*; entering initramfs rescue shell (hold power button to return to Android)"
    for virtual in dev proc sys run; do
        if ! mountpoint -q "/$virtual" && mountpoint -q "/sysroot/$virtual"; then
            mount --move "/sysroot/$virtual" "/$virtual" 2>/dev/null || true
        fi
    done
    console=/dev/console
    [ -c /dev/tty1 ] && console=/dev/tty1
    [ -c "$console" ] || console=/sysroot/dev/console
    while :; do
        setsid /bin/sh -c 'exec /bin/sh <"$1" >"$1" 2>&1' -- "$console"
        sleep 5
    done
}

mkdir -p /dev /proc /sys /run /sysroot || fail 'Cannot create mountpoints'
mountpoint -q /dev || mount -t devtmpfs devtmpfs /dev || fail 'devtmpfs failed'
mountpoint -q /proc || mount -t proc proc /proc || fail 'proc failed'
mountpoint -q /sys || mount -t sysfs sysfs /sys || fail 'sysfs failed'
mountpoint -q /run || mount -t tmpfs -o mode=0755 tmpfs /run || fail 'run failed'

expected=$(cat /etc/np05j/root-label 2>/dev/null || printf 'PIANOROOT')
partname=$(cat /etc/np05j/root-partname 2>/dev/null || printf 'sunuefi_root')
release=$(cat /etc/np05j/kernel-release 2>/dev/null || uname -r)
log "BEGIN kernel=$release expected=LABEL=$expected partname=$partname"

rootdev=
attempt=0
while [ "$attempt" -lt 30 ]; do
    for part in /sys/class/block/*; do
        [ -f "$part/partition" ] || continue
        grep -qx "PARTNAME=$partname" "$part/uevent" || continue
        candidate=/dev/${part##*/}
        [ -b "$candidate" ] || continue
        label=$(blkid -p -s LABEL -o value "$candidate" 2>/dev/null) || continue
        [ "$label" = "$expected" ] || continue
        [ -z "$rootdev" ] || fail 'Duplicate Linux root partition found'
        rootdev=$candidate
    done
    [ -z "$rootdev" ] || break
    attempt=$((attempt + 1))
    sleep 1
done
[ -n "$rootdev" ] || fail "Partition PARTNAME=$partname LABEL=$expected did not appear"

verify_device() {
    [ "$(blkid -p -s LABEL -o value "$rootdev")" = "$expected" ] &&
    [ "$(blkid -p -s PART_ENTRY_NAME -o value "$rootdev")" = "$partname" ] &&
    [ "$(blkid -p -s TYPE -o value "$rootdev")" = ext4 ]
}
verify_device || fail "Root partition $rootdev failed LABEL/PARTNAME/ext4 verification"

mount -t ext4 -o ro "$rootdev" /sysroot || fail "Read-only mount of $rootdev failed"
init_bin=/usr/lib/systemd/systemd
[ -x "/sysroot$init_bin" ] || init_bin=/sbin/init
[ -x "/sysroot$init_bin" ] || fail 'Missing systemd /sbin/init in rootfs'
[ -d "/sysroot/usr/lib/modules/$release" ] || [ -d "/sysroot/lib/modules/$release" ] || fail "Missing kernel modules for $release"
verify_device || fail 'Root identity changed before rw remount'
mount -o remount,rw /sysroot || fail 'Remounting rootfs read-write failed'

mkdir -p /sysroot/dev /sysroot/proc /sysroot/sys /sysroot/run /sysroot/tmp || fail 'Root mountpoints failed'
chmod 1777 /sysroot/tmp || fail 'Root tmp permissions failed'
for virtual in dev proc sys run; do
    mount --move "/$virtual" "/sysroot/$virtual" || fail "Cannot move $virtual"
done

log "READY: switching root to $rootdev ($init_bin)"
exec switch_root -c /dev/console /sysroot "$init_bin" --log-target=kmsg --log-level=info >/sysroot/dev/kmsg 2>&1
fail 'switch_root failed'
'''

NP05J_STEAMOS_DEVICE_TOML = '''[[device]]
dt.compatible = "nubia,np05j"
device = "nubia_np05j"
variant = "Astra"
friendly_name = "REDMAGIC Astra"

[gpu_performance]
driver = "devfreq"
sysfs_path = "/sys/devices/platform/soc/3d00000.gpu/devfreq/3d00000.gpu"
'''


def run(cmd, **kwargs):
    return subprocess.run([str(x) for x in cmd], check=True, **kwargs)


def sha_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_np05j_tool_portable(dest: Path):
    """คอมไพล์ np05j-tool แบบ static AArch64 รองรับทั้งโฮสต์ x86_64 และ ARM64"""
    src = ROOT / 'linux/userspace/np05j/np05j-tool.c'
    dest.parent.mkdir(parents=True, exist_ok=True)
    if platform.machine() in ('aarch64', 'arm64'):
        compiler = shutil.which('gcc') or shutil.which('aarch64-linux-gnu-gcc')
    else:
        compiler = shutil.which('aarch64-linux-gnu-gcc')
    if not compiler:
        raise ValueError('ไม่พบคอมไพเลอร์ AArch64 (gcc / aarch64-linux-gnu-gcc) สำหรับ np05j-tool')
    run([compiler, '-static', '-O2', '-Wall', '-Wextra', '-Werror', src, '-o', dest])
    dest.chmod(0o755)


def np05j_kwin_output_config() -> str:
    """สร้าง kwinoutputconfig.json ที่ตรงกับจอ OLED 2400x1504 ของ REDMAGIC Astra (NP05J)"""
    source = ROOT / 'linux/desktops/kde/config/kwinoutputconfig.json'
    config = json.loads(source.read_text())
    outputs = next(row['data'] for row in config if row['name'] == 'outputs')
    outputs[0]['mode'] = {'width': 2400, 'height': 1504, 'refreshRate': 144000}
    outputs[0]['scale'] = 2
    outputs[0]['edidIdentifier'] = ''
    return json.dumps(config, indent=2) + '\n'


def overlay_input_identity(target: str) -> dict:
    """คำนวณแฮชของอินพุตที่ใช้บิลด์ overlay เพื่อใช้แคชข้ามรอบ CI อย่างปลอดภัย"""
    files = ['config/mesa.json', 'tools/build/mesa.py', 'tools/build/np05j_distro.py']
    if target == 'nyarch-gnome':
        files += ['config/nyarch.json', 'tools/build/arch_mesa.py', 'tools/build_nyarch_apps.py']
    elif target == 'steamos':
        files += [
            'config/steamos.json', 'tools/build/arch_mesa.py', 'tools/build_steamos_rootfs.py',
            'tools/build_steamos_gamescope.py', 'tools/build_steamos_manager.py',
            'patches/mesa/27.0/series.json', 'patches/gamescope/series.json',
            'patches/gtk/3.24.52/series.json', 'patches/ibus/1.5.34/series.json',
            'patches/kwin/6.6.5/series.json', 'patches/kscreen/6.6.5/series.json',
        ]
    digests = {}
    for rel in sorted(set(files)):
        path = ROOT / rel
        if path.is_file():
            digests[rel] = sha(path)
    return {
        'schema_version': 1,
        'device': 'np05j',
        'target': target,
        'environment_id': os.environ.get('SUNUEFI_BUILD_ENVIRONMENT_ID', 'local'),
        'files': digests,
    }


def verify_overlay_directory(directory: Path, target: str) -> dict:
    """ตรวจสอบความสมบูรณ์และ SHA-256 ของชุดแพ็กเกจ overlay ก่อนนำไปประกอบ rootfs"""
    directory = Path(directory).resolve()
    if target in denial_manifest.TARGETS:
        archives = denial_manifest.verify_archives(directory, target)
        return {'target': target, 'packages': len(archives)}
    if target not in OVERLAY_TARGETS:
        raise ValueError('Unknown NP05J overlay target: ' + target)
    if directory.is_symlink() or (directory / '.incomplete').exists():
        raise ValueError('NP05J overlay build did not complete: ' + str(directory))
    manifest_path = directory / 'manifest.json'
    if not manifest_path.is_file():
        raise ValueError('Missing NP05J overlay manifest: ' + str(manifest_path))
    record = json.loads(manifest_path.read_text())
    if (record.get('schema_version') != 1 or record.get('device') != 'np05j' or
            record.get('target') != target or not record.get('packages')):
        raise ValueError('NP05J overlay manifest metadata differs: ' + str(manifest_path))
    for filename, row in record['packages'].items():
        if Path(filename).name != filename:
            raise ValueError('Unsafe overlay package filename: ' + filename)
        archive = directory / filename
        if archive.is_symlink() or not archive.is_file() or sha(archive) != row['sha256']:
            raise ValueError('NP05J overlay package missing or SHA-256 mismatch: ' + filename)
    return record


def try_restore_overlay_cache(target: str, output: Path, inputs: dict):
    """นำชุดแพ็กเกจ overlay จากแคชกลับมาใช้ถ้า build_inputs และ SHA-256 ตรงกันทั้งหมด"""
    cache_dir = ROOT / 'build/overlay-cache' / f'np05j-{target}'
    if not (cache_dir / 'manifest.json').is_file():
        return None
    try:
        record = verify_overlay_directory(cache_dir, target)
    except (OSError, ValueError, json.JSONDecodeError):
        return None
    if record.get('build_inputs') != inputs:
        return None
    shutil.copytree(cache_dir, output)
    return verify_overlay_directory(output, target)


def export_np05j_overlay(output: Path, target: str, repo_kind: str,
                         files_map: dict, inputs: dict) -> dict:
    """ส่งออกชุดแพ็กเกจ overlay แบบ atomic พร้อม manifest.json และตรวจ SHA-256 ซ้ำ"""
    if output.exists() or output.is_symlink():
        raise ValueError('Use a fresh overlay output directory: ' + str(output))
    if not files_map:
        raise ValueError('NP05J overlay produced no packages for ' + target)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f'np05j-overlay-{target}-', dir=output.parent) as tmp_str:
        stage = Path(tmp_str) / 'packages'
        stage.mkdir()
        packages = {}
        for filename, (src_path, meta) in sorted(files_map.items()):
            dst = stage / filename
            shutil.copyfile(src_path, dst)
            dst.chmod(0o644)
            digest = sha(dst)
            if meta.get('sha256') and digest != meta['sha256']:
                raise ValueError('Overlay package changed while exporting: ' + filename)
            packages[filename] = {**meta, 'sha256': digest}
        manifest = {
            'schema_version': 1,
            'device': 'np05j',
            'target': target,
            'format': repo_kind,
            'build_inputs': inputs,
            'packages': packages,
        }
        (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        stage.chmod(0o755)
        stage.rename(output)
    return verify_overlay_directory(output, target)


def build_debian_overlay(output: Path, inputs: dict) -> dict:
    """บิลด์ Mesa 27.0 สำหรับ Debian sid ARM64 (ใช้ร่วมกันทั้ง debian-gnome, debian-kde, debian-denial)"""
    work = ROOT / 'build/distros/overlays/np05j-debian-gnome'
    if work.exists():
        shutil.rmtree(work)
    work.parent.mkdir(parents=True, exist_ok=True)
    build_release_rootfs.prepare_package_build_root(work, 'gnome', run)
    mesa_out = work / 'mesa'
    run([ROOT / 'build.sh', 'mesa', mesa_out, '--sysroot', work / 'rootfs'])
    source_meta = json.loads((mesa_out / 'SOURCE.json').read_text())
    files_map = {}
    for pkg_name, info in source_meta['packages'].items():
        deb = mesa_out / 'runtime' / info['file']
        files_map[info['file']] = (deb, {
            'name': pkg_name,
            'version': info['version'],
            'architecture': info['architecture'],
            'sha256': info['sha256'],
            'component': 'mesa',
        })
    return export_np05j_overlay(output, 'debian-gnome', 'debian', files_map, inputs)


def _fetch_arch_base_tarball(inputs_dir: Path) -> tuple[Path, str, str, str]:
    cfg = json.loads((ROOT / 'config/nyarch.json').read_text())
    mirror = arch_mirror_url(cfg['pacman_build_mirror'])
    base_url = mirror + '/os/ArchLinuxARM-aarch64-latest.tar.gz'
    inputs_dir.mkdir(parents=True, exist_ok=True)
    archive = inputs_dir / 'ArchLinuxARM-aarch64.tar.gz'
    if not archive.exists():
        partial = archive.with_suffix('.partial')
        with urllib.request.urlopen(base_url, timeout=90) as source, partial.open('wb') as dst:
            shutil.copyfileobj(source, dst)
        partial.replace(archive)
    return archive, mirror, base_url, sha(archive)


def build_nyarch_overlay(output: Path, inputs: dict) -> dict:
    """บิลด์ Mesa 27.0 + แอป Nyarch สำหรับ Arch Linux ARM (ใช้ร่วมกันทั้ง nyarch-gnome และ arch-denial)"""
    work = ROOT / 'build/distros/overlays/np05j-nyarch-gnome'
    if work.exists():
        shutil.rmtree(work)
    inputs_dir = ROOT / 'build/nyarch-inputs'
    archive, mirror, base_url, base_hash = _fetch_arch_base_tarball(inputs_dir)
    options = assemble_rootfs.parser().parse_args([
        '--distro', 'arch', '--desktop', 'gnome', '--execute',
        '--output', str(work), '--base-file', str(archive),
        '--base-url', base_url, '--base-sha256', base_hash,
    ])
    plan = assemble_rootfs.plan(options)
    plan['pacman_build_mirror'] = mirror
    assemble_rootfs.execute(plan, defer_denial=False)
    root = work / 'rootfs'

    apps_src = work / 'applications'
    build_nyarch_apps.prepare_sources(apps_src)
    with build_release_rootfs.build_resolver(root), build_nyarch_apps.guest_environment(root) as guest:
        guest(['pacman', '-Syu', '--needed', '--noconfirm', *build_nyarch_apps.PACKAGES])

    files_map = {}
    # 1. บิลด์แอป Nyarch ทั้ง 4 ตัว
    nyarch_dir = work / 'packages/nyarch'
    with build_release_rootfs.build_resolver(root):
        app_records = build_nyarch_apps.build(root, apps_src, nyarch_dir, refresh=False)
    for row in app_records:
        pkg_path = nyarch_dir / row['package']
        name, version, arch = overlay_packages.pacman_identity(pkg_path)
        files_map[pkg_path.name] = (pkg_path, {
            'name': name, 'version': version, 'architecture': arch,
            'sha256': row['sha256'], 'component': 'nyarch',
        })

    # 2. บิลด์ Mesa 27.0 สำหรับ Arch Linux ARM (Adreno 830)
    mesa_recipe_dir = work / 'recipes/mesa'
    arch_mesa.recipe(mesa_recipe_dir, steamos=False)
    mesa_out = work / 'packages/mesa'
    build_arch_package.build(root, mesa_recipe_dir, mesa_out, validate=arch_mesa.validate)
    mesa_manifest = json.loads((mesa_out / 'manifest.json').read_text())
    for filename, row in mesa_manifest['packages'].items():
        pkg_path = mesa_out / filename
        name, version, arch = overlay_packages.pacman_identity(pkg_path)
        files_map[filename] = (pkg_path, {
            'name': name, 'version': version, 'architecture': arch,
            'sha256': row['sha256'], 'component': 'mesa',
        })
    return export_np05j_overlay(output, 'nyarch-gnome', 'pacman', files_map, inputs)


def _prepare_steamos_base_root(work: Path) -> tuple[Path, dict, dict]:
    steamos_cfg = json.loads((ROOT / 'config/steamos.json').read_text())
    base_cfg = steamos_cfg['base']
    base = ROOT / 'build' / ('steamos-base-' + base_cfg['build'])
    base_record = prepare_steamos_rootfs.prepare(base, workers=32)
    work.mkdir(parents=True, exist_ok=True)
    root = work / 'rootfs'
    if root.exists():
        shutil.rmtree(root)
    mount = work / 'base-mount'
    mount.mkdir(exist_ok=True)
    run(['mount', '-o', 'loop,ro', base / 'rootfs.img', mount])
    try:
        root.mkdir()
        run(['rsync', '-aHAX', '--numeric-ids', '--filter=-x btrfs.*', str(mount) + '/', root])
    finally:
        run(['umount', mount])
    (work / 'BASE.json').write_text(json.dumps(base_record, indent=2) + '\n')

    build_steamos_rootfs.configure_repositories(root)
    with build_release_rootfs.build_resolver(root), build_nyarch_apps.guest_environment(root) as guest:
        installed = subprocess.check_output(['chroot', root, 'pacman', '-Qq'], text=True).splitlines()
        frame_boot = [p for p in installed if p == 'deckard-boot-firmware' or
                      re.fullmatch(r'linux-[0-9]+-(?:v4l2loopback-)?deckard', p)]
        if frame_boot:
            guest(['pacman', '-R', '--noconfirm', *frame_boot])
        (root / 'boot/initrd.uImage').unlink(missing_ok=True)
        if 'deckard-chromium-integration' in installed:
            guest(['pacman', '-R', '--noconfirm', 'deckard-chromium-integration'])
        for obsolete in ('clang-libs', 'deckard-steamvr-session'):
            if subprocess.run(['chroot', root, 'pacman', '-Qq', obsolete],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                guest(['pacman', '-Rdd', '--noconfirm', obsolete])
        if subprocess.run(['chroot', root, 'pacman', '-Qq', 'holo-glibc-locales'],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            guest(['pacman', '-R', '--noconfirm', 'holo-glibc-locales'])
        build_steamos_rootfs.upgrade_valve_packages(guest)
        packages = [
            'base-devel', 'sddm', 'openssh', 'bluez', 'bluez-utils',
            'networkmanager', 'wpa_supplicant', 'alsa-utils',
            'pipewire', 'pipewire-pulse', 'wireplumber', 'python', 'python-gobject',
            'polkit', 'erofs-utils', 'rsync', 'chromium', 'protobuf-c', 'libglvnd',
            'bluez-qt', 'networkmanager-qt', 'modemmanager-qt', 'bluedevil',
            'plasma-nm', 'kinfocenter', 'powerdevil', 'discover', 'packagekit',
            'packagekit-qt6', 'pacman-contrib', 'dconf', 'noto-fonts-cjk',
            'mesa-utils', 'vulkan-tools',
        ]
        missing = subprocess.run(['chroot', root, 'pacman', '-T', *packages],
                                 capture_output=True, text=True)
        if missing.returncode not in (0, 127):
            raise RuntimeError(missing.stderr)
        if missing.returncode:
            guest(['pacman', '-S', '--needed', '--noconfirm', *missing.stdout.splitlines()])
        for pkg in ('qt6-base', 'qt6-declarative'):
            if subprocess.run(['chroot', root, 'pacman', '-Qk', pkg]).returncode:
                guest(['pacman', '-S', '--noconfirm', pkg])
        opengl_check = ['bash', '-euc', '''
for module in opengl glx; do
    directory=$(pkg-config --variable=libdir "$module")
    case "$module" in opengl) library=libOpenGL.so ;; glx) library=libGLX.so ;; esac
    test -r "$directory/$library"
done
''']
        if subprocess.run(['chroot', root, *opengl_check]).returncode:
            guest(['pacman', '-S', '--noconfirm', 'libglvnd'])
        guest(opengl_check)
        if subprocess.run(['chroot', root, 'id', 'piano'], capture_output=True).returncode:
            guest(['usermod', '-l', 'piano', '-d', '/home/piano', '-m', 'steamos'])
            guest(['groupmod', '-n', 'piano', 'steamos'])
        guest(['usermod', '-aG', 'wheel', 'piano'])
        guest(['groupadd', '-r', '-f', 'deck'])
        guest(['usermod', '-aG', 'deck', 'piano'])
        (root / 'etc/locale.gen').write_text('en_US.UTF-8 UTF-8\nzh_CN.UTF-8 UTF-8\n')
        guest(['locale-gen'])
    return root, base_record, steamos_cfg


def build_steamos_overlay(output: Path, inputs: dict) -> dict:
    """บิลด์ Mesa 27.0, Gamescope, SteamOS Manager, KDE Qt6 fixes, GTK3, IBus และ TuneD สำหรับ SteamOS NP05J"""
    work = ROOT / 'build/distros/overlays/np05j-steamos'
    if work.exists():
        shutil.rmtree(work)
    root, _, steamos_cfg = _prepare_steamos_base_root(work)
    files_map = {}

    def record_dir(comp: str, pkg_dir: Path):
        manifest = json.loads((pkg_dir / 'manifest.json').read_text())
        for filename, row in manifest['packages'].items():
            pkg_path = pkg_dir / filename
            name, version, arch = overlay_packages.pacman_identity(pkg_path)
            files_map[filename] = (pkg_path, {
                'name': name, 'version': version, 'architecture': arch,
                'sha256': row['sha256'], 'component': comp,
            })

    # 1. KDE Qt6 ABI-matched packages (เฉพาะตัวที่แพ็กเกจของ Valve ไม่ผ่าน ldd/Qt check)
    for name in build_steamos_rootfs.kde_recipes(work / 'kde-recipes', root):
        if build_steamos_rootfs.kde_repository_package_works(root, name):
            continue
        pkg_dir = work / 'packages' / name
        build_arch_package.build(root, work / 'kde-recipes' / name, pkg_dir)
        record_dir(name, pkg_dir)

    # 2. GTK3 และ IBus
    gtk_dir = work / 'packages/gtk3'
    build_arch_package.build(root, build_steamos_rootfs.gtk3_recipe(work / 'gtk3-recipe'), gtk_dir)
    record_dir('gtk3', gtk_dir)

    ibus_recipe = work / 'ibus-recipe'
    ibus_recipe.mkdir(exist_ok=True)
    from build_piano_runtime_helpers import source_patches
    ibus_patch, = source_patches(ROOT / 'patches/ibus/1.5.34/series.json')
    shutil.copy2(ibus_patch, ibus_recipe / 'keep-x11-candidate-focus.patch')
    (ibus_recipe / 'PKGBUILD').write_text(
        (ROOT / 'linux/bsp/steamos/ibus/PKGBUILD').read_text().replace(
            '@CANDIDATE_PATCH_SHA256@', sha(ibus_patch)))
    ibus_dir = work / 'packages/ibus'
    build_arch_package.build(root, ibus_recipe, ibus_dir)
    record_dir('ibus', ibus_dir)

    # 3. TuneD + tuned-ppd (แทน power-profiles-daemon ของ Frame)
    with build_nyarch_apps.guest_environment(root) as guest:
        overlay_packages.remove_steamos_power_profiles_daemon(root, guest)
    for name in overlay_packages.STEAMOS_TUNED_RECIPES:
        pkg_dir = work / 'packages' / name
        build_arch_package.build(root, ROOT / 'linux/bsp/steamos' / name, pkg_dir)
        record_dir(name, pkg_dir)

    # 4. Mesa 27.0 (deckard-mesa-linux-aarch64 สำหรับ Adreno 830)
    mesa_recipe_dir = build_steamos_rootfs.mesa_recipe(work / 'mesa-recipe', steamos_cfg['mesa'])
    mesa_dir = work / 'packages/mesa'
    build_arch_package.build(root, mesa_recipe_dir, mesa_dir, validate=arch_mesa.validate)
    record_dir('mesa', mesa_dir)

    # 5. Power profiles (dependency ของ steamos-manager), SteamOS Manager และ Gamescope
    power_dir = work / 'packages/power-profiles'
    power_archive, power_meta = overlay_packages.build_bsp(power_dir, 'pacman', component='power-profiles')
    with build_release_rootfs.build_resolver(root), build_nyarch_apps.guest_environment(root) as guest:
        staged_power = root / 'var/tmp' / power_archive.name
        shutil.copyfile(power_archive, staged_power)
        try:
            guest(['pacman', '-U', '--noconfirm',
                   *overlay_packages.overwrite_options(overlay_packages.matching_power_files(root)),
                   '/var/tmp/' + power_archive.name])
        finally:
            staged_power.unlink(missing_ok=True)
    files_map[power_archive.name] = (power_archive, {
        'name': power_meta['name'], 'version': power_meta['version'],
        'architecture': power_meta['architecture'], 'sha256': power_meta['sha256'],
        'component': 'power-profiles',
    })

    manager_dir = work / 'packages/steamos-manager'
    build_steamos_manager.build(root, manager_dir)
    record_dir('steamos-manager', manager_dir)

    gamescope_dir = work / 'packages/gamescope'
    build_steamos_gamescope.build(root, gamescope_dir)
    record_dir('gamescope', gamescope_dir)

    return export_np05j_overlay(output, 'steamos', 'pacman', files_map, inputs)


def build_np05j_shared_initrd(kernel_meta: dict, work: Path,
                              fw_source: Path = DEFAULT_FIRMWARE) -> Path:
    """สร้าง Shared Bootstrap Initramfs สำหรับ NP05J (Stream 1: BusyBox+blkid+usrmerge, Stream 2: early-fw)"""
    release = kernel_meta['kernel_release']
    _, archives = build_release_rootfs.bootstrap_tools()
    with tempfile.TemporaryDirectory(prefix='np05j-initrd-extract-', dir=work) as tmp_str:
        stage = Path(tmp_str)
        for archive in archives.values():
            run(['dpkg-deb', '-x', archive, stage])
        for name in ('bin', 'sbin', 'lib'):
            if not (stage / name).exists():
                (stage / name).symlink_to('usr/' + name)
        busybox = guest_resolve(stage, '/usr/bin/busybox')
        validate_static_arm64_elf(busybox.read_bytes())
        runtime_map = bootstrap_runtime_files(stage, ('/usr/sbin/blkid', '/usr/bin/mountpoint'))

        # ย้ายไลบรารีทั้งหมดให้อยู่ใต้ usr/ เพื่อให้ Stream 1 ใช้ usrmerge symlink lib -> usr/lib
        # ตรงกับ Stream 2 (np05j-early-fw.cpio.gz) ที่วางเฟิร์มแวร์ไว้ใน usr/lib/firmware/
        files: dict[str, Path] = {}
        for rel, src in runtime_map.items():
            norm = 'usr/' + rel if rel.startswith(('lib/', 'bin/', 'sbin/')) else rel
            files[norm] = src
        files['usr/bin/busybox'] = busybox

        generated = {
            'init': NP05J_INIT_SCRIPT,
            'etc/np05j/root-label': 'PIANOROOT\n',
            'etc/np05j/root-partname': 'sunuefi_root\n',
            'etc/np05j/kernel-release': release + '\n',
        }
        dirs = {'dev', 'proc', 'sys', 'run', 'tmp', 'sysroot', 'usr', 'usr/bin', 'usr/sbin', 'usr/lib', 'etc', 'etc/np05j'}
        for name in files.keys() | generated.keys():
            dirs.update(p.as_posix() for p in Path(name).parents if p.as_posix() != '.')
        rows = [{'name': n, 'mode': stat.S_IFDIR | (0o1777 if n == 'tmp' else 0o755)}
                for n in sorted(dirs, key=lambda item: (item.count('/'), item))]
        rows += [
            {'name': 'bin', 'mode': stat.S_IFLNK | 0o777, 'data': b'usr/bin'},
            {'name': 'sbin', 'mode': stat.S_IFLNK | 0o777, 'data': b'usr/sbin'},
            {'name': 'lib', 'mode': stat.S_IFLNK | 0o777, 'data': b'usr/lib'},
        ]
        rows += [
            {'name': 'dev/console', 'mode': stat.S_IFCHR | 0o600, 'major': 5, 'minor': 1},
            {'name': 'dev/null', 'mode': stat.S_IFCHR | 0o666, 'major': 1, 'minor': 3},
        ]
        rows += [{'name': n, 'mode': stat.S_IFREG | 0o755, 'data': p.read_bytes()}
                 for n, p in sorted(files.items())]
        rows += [{'name': n, 'mode': stat.S_IFREG | (0o755 if n == 'init' else 0o644), 'data': v.encode()}
                 for n, v in sorted(generated.items())]
        bootstrap_applets = tuple(name for name in BOOTSTRAP_APPLETS if name != 'mountpoint')
        rows += [{'name': 'usr/bin/' + name, 'mode': stat.S_IFLNK | 0o777, 'data': b'busybox'}
                 for name in bootstrap_applets]

        stream1 = gzip.compress(make_newc(rows), mtime=0)

    early_fw_gz = work / 'np05j-early-fw.cpio.gz'
    build_early_firmware_cpio_gz(early_fw_gz, fw_source)
    pad_len = (-(len(stream1) % 4)) % 4
    initrd = work / f'initrd-np05j-{release}.img'
    initrd.write_bytes(stream1 + (b'\x00' * pad_len) + early_fw_gz.read_bytes())
    return initrd


def build_np05j_esp(esp_path: Path, esp_size_mib: int, kernel_dir: Path,
                    kernel_meta: dict, work: Path) -> list[str]:
    """สร้าง Shared ESP (FAT32 4 KiB sector, LABEL=SUNUEFI_ESP) สำหรับทุกดิสโทรของ NP05J"""
    release = kernel_meta['kernel_release']
    record = json.loads((ROOT / 'config/release.json').read_text())
    systemd_boot = shared_stub(record['efi_stub'], boot=True)
    initrd = build_np05j_shared_initrd(kernel_meta, work)

    loader_conf = work / 'loader.conf'
    loader_conf.write_text(
        'default 01-np05j-oled.conf\n'
        'timeout 5\n'
        'console-mode keep\n'
    )

    base = f'/linux/{release}'
    bl_keep_sync_state = ','.join((
        '__initstub__kmod_core__802_1299_sync_state_resume_initcall7',
        '__initstub__kmod_platform__675_611_of_platform_sync_state_init7s',
    ))
    common_opts = (
        'console=tty0 loglevel=4 consoleblank=0 fbcon=font:TER16x32 '
        'root=LABEL=PIANOROOT rootfstype=ext4 rootwait ro '
        'cma=128M pd_ignore_unused clk_ignore_unused cpuidle.off=1 '
        'panic=0 iommu.passthrough=1 pcie_port_pm=off '
        'hci_uart.piano_retries=0 hci_uart.piano_peri=106 '
        'efi=noruntime'
    )
    entries = {
        '/loader/entries/01-np05j-oled.conf': (
            'title Boot Linux\n'
            f'version {release}\n'
            f'linux {base}/Image\n'
            f'initrd {base}/initrd.img\n'
            f'devicetree {base}/sm8750-nubia-np05j-display.dtb\n'
            f'options {common_opts}\n'
        ),
        '/loader/entries/02-np05j-gpu.conf': (
            'title Boot Linux (Safe 60Hz)\n'
            f'version {release}\n'
            f'linux {base}/Image\n'
            f'initrd {base}/initrd.img\n'
            f'devicetree {base}/sm8750-nubia-np05j-gpu.dtb\n'
            f'options {common_opts} msm.separate_gpu_kms=1 initcall_blacklist={bl_keep_sync_state}\n'
        ),
        '/loader/entries/03-np05j-safe.conf': (
            'title Boot Linux (Safe - No GPU)\n'
            f'version {release}\n'
            f'linux {base}/Image\n'
            f'initrd {base}/initrd.img\n'
            f'devicetree {base}/sm8750-nubia-np05j-otg.dtb\n'
            f'options {common_opts} initcall_blacklist={bl_keep_sync_state}\n'
        ),
        '/loader/entries/04-np05j-serial.conf': (
            'title Boot Linux (USB Serial)\n'
            f'version {release}\n'
            f'linux {base}/Image\n'
            f'initrd {base}/initrd.img\n'
            f'devicetree {base}/sm8750-nubia-np05j.dtb\n'
            f'options console=ttyGS0,115200 {common_opts} initcall_blacklist={bl_keep_sync_state}\n'
        ),
    }

    esp_files = {
        '/EFI/BOOT/BOOTAA64.EFI': systemd_boot,
        '/EFI/systemd/systemd-bootaa64.efi': systemd_boot,
        '/loader/loader.conf': loader_conf,
        f'{base}/Image': kernel_dir / 'Image',
        f'{base}/initrd.img': initrd,
        f'{base}/sm8750-nubia-np05j-display.dtb': kernel_dir / 'sm8750-nubia-np05j-display.dtb',
        f'{base}/sm8750-nubia-np05j-gpu.dtb': kernel_dir / 'sm8750-nubia-np05j-gpu.dtb',
        f'{base}/sm8750-nubia-np05j-otg.dtb': kernel_dir / 'sm8750-nubia-np05j-otg.dtb',
        f'{base}/sm8750-nubia-np05j.dtb': kernel_dir / 'sm8750-nubia-np05j.dtb',
    }
    for dest, body in entries.items():
        tmp = work / Path(dest).name
        tmp.write_text(body)
        esp_files[dest] = tmp

    total_payload = sum(p.stat().st_size for p in esp_files.values())
    if total_payload + 8 * MIB >= esp_size_mib * MIB:
        raise ValueError(f'ESP payload ({total_payload // MIB} MiB) exceeds {esp_size_mib} MiB')

    with esp_path.open('xb') as stream:
        stream.truncate(esp_size_mib * MIB)
    run(['mkfs.vfat', '--invariant', '-S', '4096', '-F', '32', '-n', 'SUNUEFI_ESP', esp_path])

    esp_tree = work / 'esp-tree'
    for dest, src in esp_files.items():
        dst = esp_tree / dest.lstrip('/')
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
    for entry in sorted(esp_tree.iterdir()):
        run(['mcopy', '-s', '-m', '-i', esp_path, entry, '::/'])

    for dest, src in esp_files.items():
        verify = work / ('verify-' + dest.lstrip('/').replace('/', '-'))
        run(['mcopy', '-i', esp_path, '::' + dest, verify])
        if sha(verify) != sha(src):
            raise ValueError('ESP readback mismatch: ' + dest)

    return sorted(esp_files.keys())


def apply_np05j_hardware_layer(rootfs: Path, kernel_dir: Path, kernel_meta: dict,
                               *, target: str, pacman: bool) -> dict:
    """ติดตั้งชั้นฮาร์ดแวร์ NP05J ที่ยืนยันแล้วลงทุกดิสโทร (เคอร์เนลโมดูล, เฟิร์มแวร์, เสียง, ทัช, udev, polkit)"""
    release = kernel_meta['kernel_release']
    mod_src = kernel_dir / 'modules/lib/modules' / release
    if not mod_src.is_dir():
        raise ValueError('Missing NP05J kernel modules directory: ' + str(mod_src))

    # 1. โมดูลเคอร์เนล 7.3.3-np05j
    mod_dst_parent = guest_resolve(rootfs, 'lib/modules')
    mod_dst_parent.mkdir(parents=True, exist_ok=True)
    mod_dst = mod_dst_parent / release
    if mod_dst.exists():
        shutil.rmtree(mod_dst)
    shutil.copytree(mod_src, mod_dst, symlinks=True)

    # 2. เฟิร์มแวร์สต็อก NP05J ครบทั้ง 7 ชุด + AudioReach topology + UCM2
    fw_installed = install_np05j_firmware(rootfs, DEFAULT_FIRMWARE)
    install_np05j_audio_userspace(rootfs)

    # 3. np05j-tool + np05j-fw-load + systemd oneshot service
    bin_dir = guest_resolve(rootfs, 'usr/bin')
    build_np05j_tool_portable(bin_dir / 'np05j-tool')
    libexec_dir = guest_resolve(rootfs, 'usr/libexec')
    libexec_dir.mkdir(parents=True, exist_ok=True)
    fw_script = libexec_dir / 'np05j-fw-load'
    shutil.copyfile(ROOT / 'linux/userspace/np05j/np05j-fw-load', fw_script)
    fw_script.chmod(0o755)
    write_np05j_firmware_service(rootfs)

    # 4. udev rules: แมทริกซ์ทัช Synaptics S3930 + ล็อกพาร์ติชัน Android read-only
    udev_dir = rootfs / 'etc/udev/rules.d'
    udev_dir.mkdir(parents=True, exist_ok=True)
    (udev_dir / '79-np05j-touch.rules').write_text(
        '# แมทริกซ์ปรับแกน Synaptics S3930 (portrait 15039x23999) ให้ตรงจอ OLED แนวนอน 2400x1504\n'
        'ACTION=="add|change", SUBSYSTEM=="input", ATTRS{name}=="Synaptics S3930 TCMv2 Touchscreen", '
        'ENV{LIBINPUT_CALIBRATION_MATRIX}="0 1 0 -1 0 1"\n'
    )
    (udev_dir / '01-np05j-protect-android.rules').write_text(
        '# ป้องกันพาร์ติชันสต็อก Android บน UFS ยกเว้น sunuefi_esp และ sunuefi_root\n'
        'SUBSYSTEM=="block", KERNELS=="1d84000.*", ENV{DEVTYPE}=="partition", '
        'ENV{PARTNAME}!="sunuefi_esp", ENV{PARTNAME}!="sunuefi_root", '
        'ENV{UDISKS_IGNORE}="1", RUN+="/usr/sbin/blockdev --setro /dev/%k"\n'
    )

    # 5. Polkit สำหรับ Wi-Fi, Bluetooth และพลังงาน + ตัวตนเครื่องและ fstab
    polkit_dir = rootfs / 'etc/polkit-1/rules.d'
    polkit_dir.mkdir(parents=True, exist_ok=True)
    polkit_rule = polkit_dir / '50-np05j-user-hardware.rules'
    polkit_rule.write_text(
        '// อนุญาตให้ผู้ใช้ในกลุ่มระบบจัดการ Wi-Fi, Bluetooth และพลังงานบน REDMAGIC Astra (NP05J)\n'
        'polkit.addRule(function(action, subject) {\n'
        '    if (subject.local && subject.active && (\n'
        '        subject.isInGroup("sudo") ||\n'
        '        subject.isInGroup("wheel") ||\n'
        '        subject.isInGroup("netdev") ||\n'
        '        subject.isInGroup("bluetooth")\n'
        '    )) {\n'
        '        if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 ||\n'
        '            action.id.indexOf("org.freedesktop.login1.") === 0 ||\n'
        '            action.id.indexOf("org.freedesktop.UPower.") === 0 ||\n'
        '            action.id.indexOf("org.blueman.") === 0) {\n'
        '            return polkit.Result.YES;\n'
        '        }\n'
        '    }\n'
        '});\n'
    )
    polkit_rule.chmod(0o644)

    # ใช้ cairo สำหรับ widget 2D ของ GTK 4 บน GNOME เพื่อป้องกันบั๊ก dma-buf ของ Mesa 27.0-devel Adreno 830
    if target in ('debian-gnome', 'nyarch-gnome'):
        env_dir = rootfs / 'etc/environment.d'
        env_dir.mkdir(parents=True, exist_ok=True)
        gtk4_env = env_dir / '60-np05j-gtk4.conf'
        gtk4_env.write_text('GSK_RENDERER=cairo\n')
        gtk4_env.chmod(0o644)

    (rootfs / 'etc/hostname').write_text('redmagic-astra\n')
    (rootfs / 'etc/hosts').write_text(
        '127.0.0.1 localhost\n'
        '127.0.1.1 redmagic-astra\n'
        '::1 localhost ip6-localhost ip6-loopback\n'
    )
    (rootfs / 'etc/machine-info').write_text(
        'CHASSIS=tablet\n'
        'PRETTY_HOSTNAME="REDMAGIC Astra"\n'
    )
    (rootfs / 'etc/fstab').write_text(assemble_rootfs.product_fstab(pacman=pacman))
    growfs_mask = rootfs / 'etc/systemd/system/systemd-growfs-root.service'
    if growfs_mask.is_symlink() and growfs_mask.readlink() == Path('/dev/null'):
        growfs_mask.unlink()

    # ลบ machine-id และ SSH host keys เพื่อสร้างใหม่ตอนบูตแรก
    for path in (rootfs / 'etc/ssh').glob('ssh_host_*') if (rootfs / 'etc/ssh').is_dir() else ():
        if path.is_file() or path.is_symlink():
            path.unlink()
    (rootfs / 'etc/machine-id').write_text('')
    dbus_id = rootfs / 'var/lib/dbus/machine-id'
    if dbus_id.exists() or dbus_id.is_symlink():
        dbus_id.unlink()
    dbus_id.parent.mkdir(parents=True, exist_ok=True)
    dbus_id.symlink_to('/etc/machine-id')

    run(['chroot', rootfs, 'depmod', '-a', release])
    clock = build_release_rootfs.clock_epoch(rootfs)
    return {'firmware_components': fw_installed, 'clock_epoch': clock}


def _install_local_debs(rootfs: Path, overlay_dir: Path, *, exclude_names: set[str] = frozenset()):
    manifest = json.loads((overlay_dir / 'manifest.json').read_text())
    tmp_dir = rootfs / 'var/tmp/np05j-local-debs'
    if tmp_dir.exists():
        shutil.rmtree(tmp_dir)
    tmp_dir.mkdir(parents=True)
    try:
        deb_paths = []
        for filename, row in sorted(manifest['packages'].items()):
            if row.get('name') in exclude_names:
                continue
            src = overlay_dir / filename
            dst = tmp_dir / filename
            shutil.copyfile(src, dst)
            deb_paths.append('/var/tmp/np05j-local-debs/' + filename)
        if deb_paths:
            run([
                'chroot', rootfs, 'env', 'DEBIAN_FRONTEND=noninteractive',
                'SYSTEMD_OFFLINE=1', 'SYSTEMD_IN_CHROOT=1',
                'apt-get', 'install', '-y', '--allow-downgrades',
                '--no-install-recommends', *deb_paths,
            ])
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def _install_local_pacman_packages(rootfs: Path, overlay_dir: Path, guest, *,
                                   components: set[str] | None = None,
                                   exclude_names: set[str] = frozenset(),
                                   overwrite: list[str] | tuple[str, ...] = ()):
    manifest = json.loads((overlay_dir / 'manifest.json').read_text())
    tmp_dir = rootfs / 'var/tmp/np05j-local-pacman'
    if tmp_dir.exists():
        shutil.rmtree(tmp_dir)
    tmp_dir.mkdir(parents=True)
    try:
        pkg_paths = []
        for filename, row in sorted(manifest['packages'].items()):
            if components is not None and row.get('component') not in components:
                continue
            if row.get('name') in exclude_names:
                continue
            src = overlay_dir / filename
            dst = tmp_dir / filename
            shutil.copyfile(src, dst)
            pkg_paths.append('/var/tmp/np05j-local-pacman/' + filename)
        if pkg_paths:
            guest(['pacman', '-U', '--noconfirm',
                   *overlay_packages.overwrite_options(overwrite), *pkg_paths])
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def build_debian_rootfs(target: str, output: Path, overlay_dir: Path,
                        denial_dir: Path | None) -> Path:
    """ประกอบ rootfs Debian sid (GNOME, KDE หรือ Denial) พร้อม Mesa 27.0 สำหรับ NP05J"""
    desktop = target.removeprefix('debian-')
    rootfs = output / 'rootfs'
    output.mkdir(parents=True, exist_ok=True)
    login = json.loads((ROOT / 'config/release.json').read_text())['login']

    cache_dir = ROOT / 'build/source-cache/debootstrap-sid'
    cache_dir.mkdir(parents=True, exist_ok=True)
    run([
        'debootstrap', '--arch=arm64', '--components=main',
        '--include=ca-certificates', '--cache-dir=' + str(cache_dir),
        '--force-check-sig', '--keyring=' + DEBIAN_KEYRING,
        '--foreign', 'sid', rootfs, DEBIAN_MIRROR,
    ])
    run(['chroot', rootfs, '/debootstrap/debootstrap', '--second-stage'])

    (rootfs / 'etc/apt/sources.list').write_text(
        f'deb [signed-by={DEBIAN_KEYRING}] {DEBIAN_MIRROR} sid main contrib\n'
    )
    policy = rootfs / 'usr/sbin/policy-rc.d'
    policy.write_text('#!/bin/sh\nexit 101\n')
    policy.chmod(0o755)

    def apt(*args):
        run([
            'chroot', rootfs, 'env', 'DEBIAN_FRONTEND=noninteractive',
            'SYSTEMD_OFFLINE=1', 'SYSTEMD_IN_CHROOT=1',
            'apt-get', *args,
        ])

    try:
        with build_release_rootfs.build_devices(rootfs), build_release_rootfs.build_resolver(rootfs):
            apt('-o', 'APT::Update::Error-Mode=any', 'update')
            apt('install', '-y', '--no-install-recommends', *DEBIAN_BASE_PACKAGES)
            if desktop == 'gnome':
                apt('install', '-y', '--install-recommends', *DEBIAN_GNOME_PACKAGES)
                if subprocess.run(['chroot', rootfs, 'dpkg-query', '-W', 'firefox-esr'],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                    apt('purge', '-y', 'firefox-esr')
            elif desktop == 'kde':
                apt('install', '-y', '--install-recommends', *DEBIAN_KDE_PACKAGES)
            elif desktop == 'denial':
                apt('install', '-y', '--no-install-recommends', *DEBIAN_DENIAL_BASE_PACKAGES)

            # ติดตั้ง Mesa 27.0 (Adreno 830) จาก overlay ที่บิลด์ใน run เดียวกัน
            _install_local_debs(rootfs, overlay_dir, exclude_names={'libgbm-dev'})

            # ตั้งค่า wireless-regdb ให้ใช้ upstream signature
            if (rootfs / 'lib/firmware/regulatory.db-upstream').is_file():
                run(['chroot', rootfs, 'update-alternatives', '--set',
                     'regulatory.db', '/lib/firmware/regulatory.db-upstream'])

            # บัญชีผู้ใช้เริ่มต้น + กลุ่มฮาร์ดแวร์ + sudoers
            if subprocess.run(['chroot', rootfs, 'getent', 'passwd', login['user']],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
                run(['chroot', rootfs, 'useradd', '-m', '-s', '/bin/bash', login['user']])
            run(['chroot', rootfs, 'usermod', '-aG',
                 'video,render,audio,input,bluetooth,sudo,netdev,plugdev', login['user']])
            subprocess.run(['chroot', rootfs, '/usr/sbin/chpasswd'],
                           input=f"{login['user']}:{login['password']}\n", text=True, check=True)
            sudoers = rootfs / 'etc/sudoers.d' / ('10-' + login['user'])
            sudoers.parent.mkdir(parents=True, exist_ok=True)
            sudoers.write_text(f"{login['user']} ALL=(ALL:ALL) ALL\n")
            sudoers.chmod(0o440)

            (rootfs / 'etc/locale.gen').write_text('en_US.UTF-8 UTF-8\nzh_CN.UTF-8 UTF-8\n')
            run(['chroot', rootfs, 'locale-gen'])
            (rootfs / 'etc/locale.conf').write_text('LANG=zh_CN.UTF-8\nLANGUAGE=zh_CN:zh\n')
            (rootfs / 'etc/default/zramswap').write_text('ALGO=zstd\nPERCENT=33\nPRIORITY=100\n')

            if desktop == 'gnome':
                stage_gnome_desktop(rootfs)
                dconf_dir = rootfs / 'etc/dconf/db/local.d'
                dconf_dir.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / 'linux/desktops/gnome/defaults.ini', dconf_dir / '00-sunuefi')
                profile = rootfs / 'etc/dconf/profile/user'
                profile.parent.mkdir(parents=True, exist_ok=True)
                profile.write_text('user-db:user\nsystem-db:local\n')
                run(['chroot', rootfs, 'dconf', 'update'])
                for conf_name in ('etc/gdm3/daemon.conf', 'etc/gdm3/custom.conf'):
                    conf = rootfs / conf_name
                    conf.parent.mkdir(parents=True, exist_ok=True)
                    conf.write_text(
                        '[daemon]\nAutomaticLoginEnable=true\n'
                        f"AutomaticLogin={login['user']}\n"
                    )
                run(['chroot', rootfs, 'systemctl', 'enable', 'gdm3.service'])
            elif desktop == 'kde':
                prepare_debian_desktop.configure(rootfs, subprocess.run)
                # แทนที่ความละเอียดจอ Piano (3200x2136) ด้วยจอ OLED 2400x1504 @ 144Hz ของ NP05J
                kwin_out = rootfs / 'home' / login['user'] / '.config/kwinoutputconfig.json'
                kwin_out.write_text(np05j_kwin_output_config())
                run(['chroot', rootfs, 'chown', '-R', f"{login['user']}:{login['user']}",
                     f"/home/{login['user']}/.config"])
                run(['chroot', rootfs, 'systemctl', 'enable', 'sddm.service'])
            elif desktop == 'denial':
                if denial_dir is None:
                    raise ValueError('debian-denial requires --denial-dir')
                denial.configure_apt(rootfs, build_release_rootfs.put)
                apt('-o', 'APT::Update::Error-Mode=any',
                    '-o', 'Dir::Etc::sourcelist=' + denial.APT_SOURCE,
                    '-o', 'Dir::Etc::sourceparts=-',
                    '-o', 'APT::Get::List-Cleanup=false', 'update')
                # ติดตั้ง denial-flutter-engine จาก signed Denial APT + denial จาก local overlay ที่บิลด์ใน run เดียวกัน
                apt('install', '-y', '--no-install-recommends',
                    denial.ENGINE, *denial.DEBIAN_DESKTOP_PACKAGES)
                _install_local_debs(rootfs, denial_dir, exclude_names={denial.PLUGINS})
                denial.configure_session(rootfs, build_release_rootfs.put)
                run(['chroot', rootfs, 'systemctl', 'enable', 'sddm.service'])

            run(['chroot', rootfs, 'systemctl', 'enable',
                 'NetworkManager.service', 'ssh.service', 'bluetooth.service'])
            run(['chroot', rootfs, 'systemctl', 'set-default', 'graphical.target'])
            apt('clean')
    finally:
        policy.unlink(missing_ok=True)

    # ยืนยันว่าไลบรารี Mesa 27.0 ใน rootfs รองรับ Adreno 830 จริง
    gallium_libs = sorted((rootfs / 'usr/lib/aarch64-linux-gnu').glob('libgallium-*.so'))
    if not gallium_libs or not any(b'Adreno (TM) 830' in lib.read_bytes() for lib in gallium_libs):
        raise RuntimeError('Mesa ใน Debian rootfs ไม่พบการรองรับ Adreno (TM) 830')

    return rootfs


def build_arch_rootfs(target: str, output: Path, overlay_dir: Path,
                      denial_dir: Path | None) -> Path:
    """ประกอบ rootfs Arch Linux ARM (nyarch-gnome หรือ arch-denial) พร้อม Mesa 27.0 สำหรับ NP05J"""
    denial_mode = target == 'arch-denial'
    inputs_dir = ROOT / 'build/nyarch-inputs'
    archive, mirror, base_url, base_hash = _fetch_arch_base_tarball(inputs_dir)
    options = assemble_rootfs.parser().parse_args([
        '--distro', 'arch', '--desktop', 'denial' if denial_mode else 'gnome', '--execute',
        '--output', str(output), '--base-file', str(archive),
        '--base-url', base_url, '--base-sha256', base_hash,
    ])
    plan = assemble_rootfs.plan(options)
    plan['pacman_build_mirror'] = mirror
    assemble_rootfs.execute(plan, defer_denial=denial_mode)
    rootfs = output / 'rootfs'
    login = json.loads((ROOT / 'config/release.json').read_text())['login']

    with build_release_rootfs.build_resolver(rootfs), build_nyarch_apps.guest_environment(rootfs) as guest:
        base_pkgs = [
            'bluez', 'bluez-utils', 'alsa-utils', 'alsa-ucm-conf',
            'pipewire-audio', 'pipewire-pulse', 'wireplumber',
            'mesa-utils', 'vulkan-tools', 'erofs-utils', 'dosfstools', 'e2fsprogs', 'rsync',
        ]
        if not denial_mode:
            base_pkgs += list(build_nyarch_apps.PACKAGES)
        guest(['pacman', '-Syu', '--needed', '--noconfirm', *dict.fromkeys(base_pkgs)])

        if denial_mode:
            if denial_dir is None:
                raise ValueError('arch-denial requires --denial-dir')
            _install_local_pacman_packages(rootfs, overlay_dir, guest, components={'mesa'})
            _install_local_pacman_packages(rootfs, denial_dir, guest, exclude_names={denial.PLUGINS})
            guest(['pacman', '-S', '--needed', '--noconfirm', *denial.ARCH_DESKTOP_PACKAGES])
        else:
            _install_local_pacman_packages(rootfs, overlay_dir, guest)

        if subprocess.run(['chroot', rootfs, 'id', 'alarm'], capture_output=True).returncode == 0:
            guest(['userdel', '-r', 'alarm'])
        if subprocess.check_output(['chroot', rootfs, 'id', '-u', login['user']], text=True).strip() != '1000':
            guest(['usermod', '-u', '1000', login['user']])
        guest(['chown', '-R', f"{login['user']}:{login['user']}", f"/home/{login['user']}"])
        guest(['usermod', '-aG', 'wheel,video,render,audio,input', login['user']])
        guest(['chpasswd'], input=f"{login['user']}:{login['password']}\n", text=True)
        guest(['systemctl', 'enable', 'NetworkManager', 'sshd', 'bluetooth'])
        guest(['systemctl', 'enable', 'sddm' if denial_mode else 'gdm'])
        guest(['systemctl', 'set-default', 'graphical.target'])

    if denial_mode:
        denial.configure_session(rootfs, build_release_rootfs.put)
        fonts = rootfs / 'etc/fonts/conf.d/10-nerd-font-symbols.conf'
        if not fonts.exists() and not fonts.is_symlink():
            fonts.parent.mkdir(parents=True, exist_ok=True)
            fonts.symlink_to('/usr/share/fontconfig/conf.avail/10-nerd-font-symbols.conf')
        (rootfs / 'etc/locale.gen').write_text('en_US.UTF-8 UTF-8\nzh_CN.UTF-8 UTF-8\n')
        (rootfs / 'etc/locale.conf').write_text('LANG=zh_CN.UTF-8\nLANGUAGE=zh_CN:zh\n')
        with build_nyarch_apps.guest_environment(rootfs) as guest:
            guest(['locale-gen'])
    else:
        cfg = json.loads((ROOT / 'config/nyarch.json').read_text())
        desktop_src, extensions_src = build_nyarch_rootfs.nyarch_material(cfg, inputs_dir)
        ext_bundle = output / 'nyarch-shell-extensions'
        if ext_bundle.exists():
            shutil.rmtree(ext_bundle)
        shutil.copytree(extensions_src, ext_bundle)
        prepare_nyarch_material.prepare(desktop_src.resolve(), rootfs, ext_bundle / prepare_nyarch_material.UUID)
        stage_nyarch_desktop.stage(rootfs, desktop_src, ext_bundle)

    for cache_rel in ('var/cache/pacman/pkg', 'var/tmp', 'tmp'):
        folder = rootfs / cache_rel
        if folder.is_dir():
            for child in folder.iterdir():
                if child.is_dir() and not child.is_symlink():
                    shutil.rmtree(child)
                else:
                    child.unlink()
    return rootfs


def build_steamos_rootfs_np05j(output: Path, overlay_dir: Path) -> Path:
    """ประกอบ rootfs SteamOS ARM64 สำหรับ NP05J (OLED 2400x1504 @ 165Hz, Adreno 830)"""
    rootfs, _, _ = _prepare_steamos_base_root(output)
    login = json.loads((ROOT / 'config/release.json').read_text())['login']

    with build_release_rootfs.build_resolver(rootfs), build_nyarch_apps.guest_environment(rootfs) as guest:
        overlay_packages.remove_steamos_power_profiles_daemon(rootfs, guest)
        _install_local_pacman_packages(
            rootfs, overlay_dir, guest,
            overwrite=overlay_packages.matching_power_files(rootfs),
        )
        guest(['usermod', '-aG', 'wheel,deck,video,render,audio,input', login['user']])
        guest(['chpasswd'], input=f"{login['user']}:{login['password']}\n", text=True)
        guest(['systemctl', 'enable', 'NetworkManager', 'sshd', 'bluetooth',
               'tuned-ppd.service', 'sddm'])
        guest(['systemctl', 'set-default', 'graphical.target'])

    client_dir = output / 'steam-client'
    prepare_steam_client.prepare(rootfs, client_dir)
    prepare_steam_client.enable_first_launch(rootfs)
    runtime_dir = output / 'steam-runtime'
    prepare_steam_runtime.prepare(rootfs, runtime_dir)

    stage_steamos_desktop.configure(rootfs, published_packages=False)

    # ปรับแต่งเฉพาะฮาร์ดแวร์ NP05J (แทนที่ค่าเฉพาะของ Xiaomi Pad 8 Pro)
    (rootfs / 'usr/share/steamos-manager/devices/xiaomi-piano.toml').unlink(missing_ok=True)
    dev_toml = rootfs / 'usr/share/steamos-manager/devices/nubia-np05j.toml'
    dev_toml.parent.mkdir(parents=True, exist_ok=True)
    dev_toml.write_text(NP05J_STEAMOS_DEVICE_TOML)

    gs_run = rootfs / 'usr/local/bin/piano-gamescope-run'
    if gs_run.is_file():
        text = gs_run.read_text()
        text = text.replace('STEAM_DISPLAY_REFRESH_LIMITS=30,120', 'STEAM_DISPLAY_REFRESH_LIMITS=60,165')
        text = text.replace('-W 3200 -H 2136 -r 120', '-W 2400 -H 1504 -r 165')
        gs_run.write_text(text)

    kwin_out = rootfs / 'home' / login['user'] / '.config/kwinoutputconfig.json'
    kwin_out.write_text(np05j_kwin_output_config())
    os.chown(kwin_out, 1000, 1000)

    os_release = rootfs / 'usr/lib/os-release'
    if os_release.is_file():
        os_release.write_text(os_release.read_text().replace('VERSION_CODENAME="piano"', 'VERSION_CODENAME="np05j"'))

    for cache_rel in ('var/cache/pacman/pkg', 'var/tmp', 'tmp'):
        folder = rootfs / cache_rel
        if folder.is_dir():
            for child in folder.iterdir():
                if child.is_dir() and not child.is_symlink():
                    shutil.rmtree(child)
                else:
                    child.unlink()
    return rootfs


def dir_size_bytes(path: Path) -> int:
    total = 0
    for walk_root, _, files in os.walk(path, followlinks=False):
        for name in files:
            try:
                total += (Path(walk_root) / name).lstat().st_size
            except OSError:
                pass
    return total


def build_root_ext4(rootfs: Path, root_img: Path, root_size_mib: int | None) -> int:
    """สร้าง root.ext4.img (LABEL=PIANOROOT, 4 KiB block) และตรวจสอบด้วย e2fsck"""
    used_bytes = dir_size_bytes(rootfs)
    min_mib = ((int((used_bytes * 1.25) // MIB) + 1024 + 63) // 64) * 64
    size_mib = max(root_size_mib or min_mib, min_mib)
    with root_img.open('xb') as stream:
        stream.truncate(size_mib * MIB)
    run(['mke2fs', '-q', '-t', 'ext4', '-b', '4096', '-F', '-L', 'PIANOROOT', '-d', rootfs, root_img])
    run(['e2fsck', '-fn', root_img])
    return size_mib


def build_distro_image(args) -> dict:
    """ประกอบ rootfs + NP05J hardware layer + Shared ESP + root.ext4 + Android sparse images"""
    target = args.target
    kernel_dir = args.kernel.resolve()
    overlay_dir = args.overlay_dir.resolve()
    denial_dir = args.denial_dir.resolve() if args.denial_dir else None
    bundle_out = args.output_bundle.resolve()
    sparse_out = args.output_sparse.resolve()
    rootfs_out = (args.output_rootfs or (ROOT / 'build/distros' / f'{target}-np05j')).resolve()

    manifest_path = kernel_dir / 'manifest.json'
    if not manifest_path.is_file():
        raise ValueError('Missing NP05J kernel manifest: ' + str(manifest_path))
    kernel_meta = json.loads(manifest_path.read_text())
    if kernel_meta.get('device') != 'np05j':
        raise ValueError('Kernel manifest does not belong to np05j')

    overlay_target = ('debian-gnome' if target.startswith('debian-')
                      else 'nyarch-gnome' if target == 'arch-denial'
                      else target)
    verify_overlay_directory(overlay_dir, overlay_target)
    if target in denial_manifest.TARGETS:
        if denial_dir is None:
            raise ValueError(f'{target} requires --denial-dir')
        verify_overlay_directory(denial_dir, target)

    plan_summary = {
        'schema_version': 1,
        'status': 'PLAN_ONLY',
        'device': 'np05j',
        'target': target,
        'kernel_release': kernel_meta['kernel_release'],
        'overlay_dir': str(overlay_dir),
        'denial_dir': str(denial_dir) if denial_dir else None,
        'rootfs_output': str(rootfs_out),
        'bundle_output': str(bundle_out),
        'sparse_output': str(sparse_out),
        'esp_size_mib': args.esp_size_mib,
        'root_size_mib': args.root_size_mib,
    }
    if not args.execute:
        return plan_summary

    if os.geteuid() != 0:
        raise ValueError('--execute requires root privileges inside sunuefi-builder')
    ensure_qemu_binfmt()

    for path in (rootfs_out, bundle_out, sparse_out):
        if path.exists():
            shutil.rmtree(path)

    pacman = not target.startswith('debian-')
    if target.startswith('debian-'):
        rootfs = build_debian_rootfs(target, rootfs_out, overlay_dir, denial_dir)
    elif target in ('nyarch-gnome', 'arch-denial'):
        rootfs = build_arch_rootfs(target, rootfs_out, overlay_dir, denial_dir)
    else:
        rootfs = build_steamos_rootfs_np05j(rootfs_out, overlay_dir)

    hw_record = apply_np05j_hardware_layer(rootfs, kernel_dir, kernel_meta, target=target, pacman=pacman)

    # ล้างโฟลเดอร์ runtime mountpoints ก่อนแพ็ก ext4
    for name in ('dev', 'proc', 'sys', 'run'):
        folder = rootfs / name
        folder.mkdir(exist_ok=True)
        for child in folder.iterdir():
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()

    # บันทึกรายการแพ็กเกจทั้งหมดลง packages.tsv
    pkg_cmd = (['chroot', rootfs, 'pacman', '-Q'] if pacman else
               ['chroot', rootfs, 'dpkg-query', '-W', '-f=${binary:Package}\t${Version}\t${Architecture}\n'])
    (rootfs_out / 'packages.tsv').write_text(subprocess.check_output(pkg_cmd, text=True))

    bundle_out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='np05j-bundle-', dir=bundle_out) as work_str:
        work = Path(work_str)
        esp_img = bundle_out / 'esp.img'
        root_img = bundle_out / 'root.ext4.img'
        esp_files = build_np05j_esp(esp_img, args.esp_size_mib, kernel_dir, kernel_meta, work)
        actual_root_mib = build_root_ext4(rootfs, root_img, args.root_size_mib)

    bundle_manifest = {
        'schema_version': 1,
        'device': 'np05j',
        'target': target,
        'status': f'HOST_BUILT_NP05J_{target.upper().replace("-", "_")}_NOT_BOOT_VERIFIED',
        'kernel_release': kernel_meta['kernel_release'],
        'root_policy': 'LABEL=PIANOROOT',
        'esp_files': esp_files,
        'hardware': hw_record,
        'partitions': {
            'esp': {'partlabel': 'sunuefi_esp', 'label': 'SUNUEFI_ESP', 'sector_bytes': 4096,
                    'capacity_bytes': esp_img.stat().st_size},
            'root': {'partlabel': 'sunuefi_root', 'label': 'PIANOROOT',
                     'capacity_bytes': actual_root_mib * MIB},
        },
        'files': {
            'esp.img': {'bytes': esp_img.stat().st_size, 'sha256': sha(esp_img), 'format': 'raw-fat32'},
            'root.ext4.img': {'bytes': root_img.stat().st_size, 'sha256': sha(root_img), 'format': 'raw-ext4'},
        },
    }
    (bundle_out / 'manifest.json').write_text(json.dumps(bundle_manifest, indent=2) + '\n')
    (bundle_out / 'SHA256SUMS').write_text(
        f"{bundle_manifest['files']['esp.img']['sha256']}  esp.img\n"
        f"{bundle_manifest['files']['root.ext4.img']['sha256']}  root.ext4.img\n"
    )

    sparse_meta = package_sparse_images.package(bundle_out, sparse_out)
    return {
        'device': 'np05j',
        'target': target,
        'status': bundle_manifest['status'],
        'kernel_release': kernel_meta['kernel_release'],
        'raw': bundle_manifest['files'],
        'sparse': sparse_meta['files'],
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)

    p_ov = sub.add_parser('overlays', help='Build native Mesa/desktop overlay packages for NP05J')
    p_ov.add_argument('--target', choices=OVERLAY_TARGETS, required=True)
    p_ov.add_argument('--output', type=Path, required=True)
    p_ov.add_argument('--worker', action='store_true', help=argparse.SUPPRESS)

    p_ver = sub.add_parser('verify-overlay', help='Verify a sealed NP05J overlay directory')
    p_ver.add_argument('--target', choices=ALL_OVERLAY_TARGETS, required=True)
    p_ver.add_argument('--directory', type=Path, required=True)

    p_build = sub.add_parser('build', help='Assemble one NP05J distribution rootfs, ESP and sparse images')
    p_build.add_argument('--target', choices=DISTRO_TARGETS, required=True)
    p_build.add_argument('--kernel', type=Path, default=DEFAULT_KERNEL)
    p_build.add_argument('--overlay-dir', type=Path, required=True)
    p_build.add_argument('--denial-dir', type=Path)
    p_build.add_argument('--output-rootfs', type=Path)
    p_build.add_argument('--output-bundle', type=Path, default=ROOT / 'artifacts/release-np05j')
    p_build.add_argument('--output-sparse', type=Path, default=ROOT / 'artifacts/fastboot-sparse-np05j')
    p_build.add_argument('--esp-size-mib', type=int, default=512)
    p_build.add_argument('--root-size-mib', type=int)
    mode = p_build.add_mutually_exclusive_group(required=True)
    mode.add_argument('--plan', action='store_true')
    mode.add_argument('--execute', action='store_true')
    p_build.add_argument('--worker', action='store_true', help=argparse.SUPPRESS)

    args = parser.parse_args(argv)
    try:
        if args.command == 'verify-overlay':
            print(json.dumps(verify_overlay_directory(args.directory, args.target), indent=2))
            return 0

        if args.command == 'overlays':
            if not args.worker:
                raw_args = list(sys.argv[1:] if argv is None else argv)
                return subprocess.run(['unshare', '--mount', '--propagation', 'private',
                                       sys.executable, __file__, *raw_args, '--worker']).returncode
            if os.geteuid() != 0:
                raise ValueError('overlays requires root inside sunuefi-builder')
            ensure_qemu_binfmt()
            inputs = overlay_input_identity(args.target)
            output = args.output.resolve()
            if reused := try_restore_overlay_cache(args.target, output, inputs):
                print(json.dumps({'reused': True, 'target': args.target,
                                  'packages': len(reused['packages'])}, indent=2))
                return 0
            if args.target == 'debian-gnome':
                result = build_debian_overlay(output, inputs)
            elif args.target == 'nyarch-gnome':
                result = build_nyarch_overlay(output, inputs)
            else:
                result = build_steamos_overlay(output, inputs)
            print(json.dumps({'reused': False, 'target': args.target,
                              'packages': len(result['packages'])}, indent=2))
            return 0

        if args.command == 'build':
            if args.execute and not args.worker:
                raw_args = list(sys.argv[1:] if argv is None else argv)
                return subprocess.run(['unshare', '--mount', '--propagation', 'private',
                                       sys.executable, __file__, *raw_args, '--worker']).returncode
            print(json.dumps(build_distro_image(args), indent=2))
            return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(2, str(error) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
