#!/usr/bin/env python3
"""สร้าง Ubuntu 24.04 LTS (noble) ARM64 GNOME rootfs, ESP และ sparse images สำหรับ nubia NP05J"""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))

import assemble_rootfs
import package_sparse_images
from build_chroot import guest_command, stop_helpers
from build_piano_ram_bootstrap import guest_resolve
from package.release import shared_stub

MIB = 1024 * 1024
DEFAULT_KERNEL = ROOT / 'artifacts/kernels/np05j'
DEFAULT_FIRMWARE = ROOT / 'vendor/np05j-firmware'
DEFAULT_ROOTFS_OUT = ROOT / 'build/distros/ubuntu-gnome-np05j'
DEFAULT_BUNDLE_OUT = ROOT / 'artifacts/ubuntu-gnome-np05j'
DEFAULT_USER = 'porserver'
DEFAULT_PASSWORD = ''
UBUNTU_KEYRING_URL = 'https://ports.ubuntu.com/ubuntu-ports/pool/main/u/ubuntu-keyring/ubuntu-keyring_2023.11.28.1build1_all.deb'
UBUNTU_KEYRING_MEMBER = './usr/share/keyrings/ubuntu-archive-keyring.gpg'
QEMU_AARCH64_BINFMT = (
    ':qemu-aarch64:M::'
    r'\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7\x00:'
    r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:'
    '/usr/bin/qemu-aarch64-static:OCF'
)

EXTRA_UBUNTU_PACKAGES = [
    'initramfs-tools',
    'e2fsprogs',
    'dosfstools',
    'bluez',
    'wpasupplicant',
    'rfkill',
    'wireless-regdb',
    'libgl1-mesa-dri',
    'mesa-vulkan-drivers',
    'mesa-utils',
    'gnome-terminal',
    'gnome-text-editor',
    'gnome-system-monitor',
    'xdg-desktop-portal-gnome',
    'epiphany-browser',
    'fonts-noto-core',
    'fonts-noto-cjk',
    'fonts-noto-color-emoji',
    'systemd-timesyncd',
    'zram-tools',
    'curl',
    'wget',
    'vim',
    'htop',
    'usbutils',
    'pciutils',
    'i2c-tools',
    'evtest',
    'alsa-utils',
    'alsa-ucm-conf',
]


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(MIB), b''):
            h.update(chunk)
    return h.hexdigest()


def run(cmd, **kwargs):
    return subprocess.run([str(x) for x in cmd], check=True, **kwargs)


def ensure_qemu_binfmt():
    """ลงทะเบียน qemu-aarch64-static ใน binfmt_misc เมื่อบิลด์ข้ามสถาปัตยกรรมบน x86_64"""
    if platform.machine() in ('aarch64', 'arm64'):
        return
    binfmt = Path('/proc/sys/fs/binfmt_misc')
    entry = binfmt / 'qemu-aarch64'
    if not (binfmt / 'register').exists():
        run(['mount', '-t', 'binfmt_misc', 'binfmt_misc', binfmt])
    if entry.is_file() and 'enabled' in entry.read_text():
        return
    (binfmt / 'register').write_text(QEMU_AARCH64_BINFMT)
    if not entry.is_file() or 'enabled' not in entry.read_text():
        raise RuntimeError('ลงทะเบียน binfmt_misc สำหรับ qemu-aarch64 ไม่สำเร็จ')


def ensure_ubuntu_keyring() -> Path:
    """จัดหา ubuntu-archive-keyring.gpg จากแพ็กเกจทางการของ Ubuntu"""
    system_key = Path('/usr/share/keyrings/ubuntu-archive-keyring.gpg')
    if system_key.is_file() and system_key.stat().st_size > 0:
        return system_key
    cache_dir = ROOT / 'build/source-cache/ubuntu-keyring'
    cache_dir.mkdir(parents=True, exist_ok=True)
    keyring = cache_dir / 'ubuntu-archive-keyring.gpg'
    if keyring.is_file() and keyring.stat().st_size > 0:
        return keyring
    deb = cache_dir / 'ubuntu-keyring.deb'
    run(['curl', '-fL', '--proto', '=https', '--output', deb, UBUNTU_KEYRING_URL])
    raw = subprocess.check_output(['dpkg-deb', '--fsys-tarfile', deb])
    with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
        member = tar.getmember(UBUNTU_KEYRING_MEMBER)
        data = tar.extractfile(member).read()
    if len(data) < 1024:
        raise ValueError('ไฟล์ ubuntu-archive-keyring.gpg ไม่สมบูรณ์')
    tmp = keyring.with_suffix('.gpg.tmp')
    tmp.write_bytes(data)
    tmp.replace(keyring)
    return keyring


def build_np05j_tool(dest: Path):
    """คอมไพล์ np05j-tool แบบ static AArch64 เพื่อใช้ mount vendor_b แบบ read-only และตรวจอุปกรณ์"""
    src = ROOT / 'linux/userspace/np05j/np05j-tool.c'
    dest.parent.mkdir(parents=True, exist_ok=True)
    run([
        'aarch64-linux-gnu-gcc', '-static', '-O2', '-Wall', '-Wextra', '-Werror',
        src, '-o', dest,
    ])
    dest.chmod(0o755)


def install_np05j_firmware(rootfs: Path, fw_source: Path = DEFAULT_FIRMWARE) -> dict:
    """ตรวจสอบ SHA-256 และติดตั้งเฟิร์มแวร์สต็อก NP05J ทุกชุดลง /usr/lib/firmware"""
    if not fw_source.is_dir():
        raise ValueError(f'ไม่พบโฟลเดอร์เฟิร์มแวร์ NP05J: {fw_source}')
    fw_root = guest_resolve(rootfs, 'usr/lib/firmware')
    installed = {}
    for manifest_file in sorted(fw_source.glob('*/manifest.json')):
        meta = json.loads(manifest_file.read_text())
        sub = meta.get('directory', '.')
        dst_dir = fw_root if sub in ('', '.') else fw_root / sub
        dst_dir.mkdir(parents=True, exist_ok=True)
        count = 0
        for fname, finfo in meta['files'].items():
            src_file = manifest_file.parent / fname
            if sha(src_file) != finfo['sha256'] or src_file.stat().st_size != finfo['bytes']:
                raise ValueError(f'SHA-256 ของเฟิร์มแวร์ไม่ตรง manifest: {src_file}')
            dst_file = dst_dir / fname
            if dst_file.is_symlink():
                dst_file.unlink()
            shutil.copyfile(src_file, dst_file)
            dst_file.chmod(0o644)
            if sha(dst_file) != finfo['sha256']:
                raise ValueError(f'ตรวจอ่านกลับเฟิร์มแวร์หลังคัดลอกไม่ตรง: {dst_file}')
            count += 1
        aliases = 0
        for alias_name, target_name in meta.get('aliases', {}).items():
            if target_name not in meta['files']:
                raise ValueError(f'Alias เฟิร์มแวร์ชี้ไปไฟล์ที่ไม่มีใน manifest: {alias_name} -> {target_name}')
            alias_path = dst_dir / alias_name
            if alias_path.exists() or alias_path.is_symlink():
                alias_path.unlink()
            alias_path.symlink_to(target_name)
            aliases += 1
        installed[meta['component']] = {'directory': sub, 'files': count, 'aliases': aliases}
    return installed


def install_np05j_audio_userspace(rootfs: Path):
    """คอมไพล์ AudioReach topology และติดตั้ง ALSA UCM2 สำหรับลำโพงคู่ NXP TFA9874 ของ NP05J"""
    audio_src = ROOT / 'linux/userspace/np05j/audio'
    tplg_m4 = audio_src / 'nubia-REDMAGIC-Astra.m4'
    ar_macros = ROOT / 'upstream/audioreach-topology'
    if tplg_m4.is_file() and ar_macros.is_dir() and shutil.which('m4') and shutil.which('alsatplg'):
        fw_dst = guest_resolve(rootfs, 'usr/lib/firmware/qcom/sm8750')
        fw_dst.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='np05j-tplg-') as tmpdir:
            conf = Path(tmpdir) / 'nubia-REDMAGIC-Astra.conf'
            raw = subprocess.check_output(
                ['m4', '-I', str(audio_src), '-I', str(ar_macros), str(tplg_m4)]
            ).replace(b'stream0.vol_ctrl0', b'stream0.vol0')
            conf.write_bytes(raw)
            run(['alsatplg', '-c', conf, '-o', fw_dst / 'nubia-REDMAGIC-Astra-tplg.bin'])

    ucm_src = audio_src / 'ucm2/Qualcomm/sm8750/nubia-REDMAGIC-Astra'
    if ucm_src.is_dir():
        ucm_dst = rootfs / 'usr/share/alsa/ucm2/Qualcomm/sm8750/nubia-REDMAGIC-Astra'
        ucm_dst.mkdir(parents=True, exist_ok=True)
        for name in ('nubia-REDMAGIC-Astra.conf', 'HiFi.conf'):
            src_f = ucm_src / name
            if src_f.is_file():
                shutil.copyfile(src_f, ucm_dst / name)
                (ucm_dst / name).chmod(0o644)
        confd = rootfs / 'usr/share/alsa/ucm2/conf.d/sm8750'
        confd.mkdir(parents=True, exist_ok=True)
        link = confd / 'nubia-REDMAGIC-Astra.conf'
        if link.exists() or link.is_symlink():
            link.unlink()
        link.symlink_to('../../Qualcomm/sm8750/nubia-REDMAGIC-Astra/nubia-REDMAGIC-Astra.conf')


def configure_ubuntu_gdm_autologin(rootfs: Path, username: str = DEFAULT_USER):
    """ตั้งค่า autologin ผู้ใช้บน /etc/gdm3/custom.conf และ daemon.conf สำหรับ Ubuntu GNOME"""
    custom_conf = rootfs / 'etc/gdm3/custom.conf'
    if custom_conf.parent.is_dir():
        custom_conf.write_text(
            '[daemon]\n'
            'AutomaticLoginEnable=true\n'
            f'AutomaticLogin={username}\n'
            '\n'
            '[security]\n'
            '\n'
            '[xdmcp]\n'
            '\n'
            '[chooser]\n'
            '\n'
            '[debug]\n'
        )
        daemon_conf = rootfs / 'etc/gdm3/daemon.conf'
        daemon_conf.write_text(
            '[daemon]\n'
            'AutomaticLoginEnable=true\n'
            f'AutomaticLogin={username}\n'
        )


def configure_np05j_user(rootfs: Path, username: str, password: str | None, guest):
    """เปลี่ยนชื่อบัญชีเริ่มต้น (จาก piano เป็น username) ตั้งรหัสผ่าน sudoers และ autologin"""
    if not username or any(c in username for c in ':/ \t\n'):
        raise ValueError(f'ชื่อผู้ใช้ไม่ถูกต้อง: {username!r}')
    has_piano = subprocess.run(
        guest_command(rootfs, ['/usr/bin/getent', 'passwd', 'piano']),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0
    has_target = subprocess.run(
        guest_command(rootfs, ['/usr/bin/getent', 'passwd', username]),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0
    if username != 'piano' and has_piano and not has_target:
        guest(['usermod', '-l', username, '-d', f'/home/{username}', '-m', 'piano'])
        guest(['groupmod', '-n', username, 'piano'])
        for sub_name in ('etc/subuid', 'etc/subgid'):
            sub_path = rootfs / sub_name
            if sub_path.is_file():
                lines = [
                    (f'{username}:' + line.split(':', 1)[1]) if line.startswith('piano:') else line
                    for line in sub_path.read_text().splitlines(keepends=True)
                ]
                sub_path.write_text(''.join(lines))
    elif not has_target:
        guest(['useradd', '-m', '-s', '/bin/bash', username])

    # ให้บัญชีหลักมีสิทธิ์ใช้งาน GPU (/dev/dri/renderD128), เสียง และ Bluetooth โดยตรง
    guest(['usermod', '-aG', 'video,render,audio,bluetooth', username])

    if password:
        argv = guest_command(rootfs, [
            '/usr/bin/env', 'PATH=/usr/sbin:/usr/bin:/sbin:/bin', '/usr/sbin/chpasswd',
        ])
        subprocess.run(argv, input=f'{username}:{password}\n', text=True, check=True)

    # อัปเดต sudoers และลบไฟล์เก่าของ piano
    sudoers_dir = rootfs / 'etc/sudoers.d'
    sudoers_dir.mkdir(parents=True, exist_ok=True)
    if username != 'piano':
        (sudoers_dir / '10-piano').unlink(missing_ok=True)
    sudoers = sudoers_dir / f'10-{username}'
    sudoers.write_text(f'{username} ALL=(ALL:ALL) ALL\n')
    sudoers.chmod(0o440)

    # อัปเดต autologin และบันทึกคู่มือเบื้องต้นในโฮมของผู้ใช้
    configure_ubuntu_gdm_autologin(rootfs, username)
    setup_note = rootfs / 'home' / username / 'FIRST-SETUP.txt'
    setup_note.parent.mkdir(parents=True, exist_ok=True)
    setup_note.write_text(
        'REDMAGIC Astra (NP05J) — Ubuntu 24.04 LTS GNOME\n'
        f'Default user: {username}\n'
        'Change your password in Settings -> System -> Users.\n'
    )
    guest(['chown', f'{username}:{username}', f'/home/{username}/FIRST-SETUP.txt'])


def write_np05j_firmware_service(rootfs: Path):
    """ติดตั้ง systemd unit np05j-firmware.service (TimeoutStartSec=15s) และเปิดใช้งาน"""
    unit_dir = rootfs / 'etc/systemd/system'
    unit_dir.mkdir(parents=True, exist_ok=True)
    (unit_dir / 'np05j-firmware.service').write_text(
        '[Unit]\n'
        'Description=Load stock read-only firmware for REDMAGIC Astra (NP05J)\n'
        'DefaultDependencies=no\n'
        'After=local-fs.target systemd-remount-fs.service systemd-udevd.service\n'
        'Before=NetworkManager.service bluetooth.service multi-user.target\n'
        '\n'
        '[Service]\n'
        'Type=oneshot\n'
        'RemainAfterExit=yes\n'
        'TimeoutStartSec=15s\n'
        'ExecStart=/usr/libexec/np05j-fw-load\n'
        '\n'
        '[Install]\n'
        'WantedBy=multi-user.target\n'
    )
    wants = unit_dir / 'multi-user.target.wants'
    wants.mkdir(parents=True, exist_ok=True)
    link = wants / 'np05j-firmware.service'
    if link.exists() or link.is_symlink():
        link.unlink()
    link.symlink_to('/etc/systemd/system/np05j-firmware.service')


KISAK_MESA_FPR = 'EB8B81E14DA65431D7504EA8F63F0F2B90935439'
KISAK_MESA_KEY_URL = f'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x{KISAK_MESA_FPR}'


def ensure_kisak_mesa_keyring() -> Path:
    """จัดหาและตรวจ fingerprint ของ GPG key สำหรับ ppa:kisak/kisak-mesa ผ่าน HTTPS"""
    cache_dir = ROOT / 'build/source-cache/kisak-mesa'
    cache_dir.mkdir(parents=True, exist_ok=True)
    keyring = cache_dir / 'kisak-mesa.gpg'
    if keyring.is_file() and keyring.stat().st_size > 256:
        return keyring
    armored = subprocess.check_output(['curl', '-fsSL', '--proto', '=https', KISAK_MESA_KEY_URL])
    info = subprocess.check_output(['gpg', '--show-keys', '--with-colons'], input=armored).decode('utf-8', 'replace')
    if KISAK_MESA_FPR not in info:
        raise ValueError(f'GPG fingerprint ของ kisak-mesa ไม่ตรงกับ {KISAK_MESA_FPR}')
    dearmored = subprocess.check_output(['gpg', '--dearmor'], input=armored)
    tmp = keyring.with_suffix('.gpg.tmp')
    tmp.write_bytes(dearmored)
    tmp.replace(keyring)
    return keyring


def upgrade_mesa_a830_and_ssh(rootfs: Path, guest) -> str:
    """อัปเกรด Mesa จาก ppa:kisak/kisak-mesa ให้รองรับ Adreno 830 และสร้าง SSH host keys"""
    keyring_src = ensure_kisak_mesa_keyring()
    keyring_dst = rootfs / 'usr/share/keyrings/kisak-mesa.gpg'
    keyring_dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(keyring_src, keyring_dst)
    keyring_dst.chmod(0o644)

    sources_dst = rootfs / 'etc/apt/sources.list.d/kisak-mesa.sources'
    sources_dst.parent.mkdir(parents=True, exist_ok=True)
    sources_dst.write_text(
        'Types: deb\n'
        'URIs: https://ppa.launchpadcontent.net/kisak/kisak-mesa/ubuntu\n'
        'Suites: noble\n'
        'Components: main\n'
        'Signed-By: /usr/share/keyrings/kisak-mesa.gpg\n'
    )

    guest(['apt-get', 'update'])
    guest(['apt-get', 'upgrade', '-y'])
    guest([
        'apt-get', 'install', '-y', '--no-install-recommends',
        'libgl1-mesa-dri', 'mesa-vulkan-drivers', 'mesa-utils', 'vulkan-tools', 'alsa-utils',
    ])

    # หากมีแพ็กเกจ Mesa 27.0 (รองรับ Adreno 830v1 / speedbin wildcard 0xffff44050001) ให้ติดตั้งทับ kisak-mesa
    mesa27_dir = ROOT / 'artifacts/mesa-np05j-noble'
    mesa27_debs = sorted(mesa27_dir.glob('*.deb')) if mesa27_dir.is_dir() else []
    if mesa27_debs:
        stage_dir = rootfs / 'tmp/mesa-27-np05j'
        stage_dir.mkdir(parents=True, exist_ok=True)
        try:
            deb_args = []
            for deb in mesa27_debs:
                shutil.copyfile(deb, stage_dir / deb.name)
                deb_args.append(f'/tmp/mesa-27-np05j/{deb.name}')
            guest(['apt-get', 'install', '-y', '--no-install-recommends', *deb_args])
        finally:
            shutil.rmtree(stage_dir, ignore_errors=True)

    # ยืนยันว่าไลบรารี Gallium หลังอัปเกรดรองรับ Adreno (TM) 830 จริง
    gallium_libs = sorted((rootfs / 'usr/lib/aarch64-linux-gnu').glob('libgallium-*.so'))
    if not gallium_libs or not any(b'Adreno (TM) 830' in lib.read_bytes() for lib in gallium_libs):
        raise RuntimeError('อัปเกรด Mesa ไม่พบการรองรับ Adreno (TM) 830 ใน libgallium')
    mesa_ver = subprocess.check_output(
        guest_command(rootfs, ['/usr/bin/dpkg-query', '-W', '-f=${Version}', 'mesa-libgallium']),
        text=True,
    ).strip()

    # สร้าง SSH host keys, เปิด PasswordAuthentication และ enable ssh.service ให้พร้อมใช้งานทันที
    if (rootfs / 'usr/sbin/sshd').is_file():
        if not (rootfs / 'etc/ssh/ssh_host_ed25519_key').is_file():
            guest(['ssh-keygen', '-A'])
        sshd_dropin = rootfs / 'etc/ssh/sshd_config.d/60-np05j-password-auth.conf'
        sshd_dropin.parent.mkdir(parents=True, exist_ok=True)
        sshd_dropin.write_text(
            '# เปิดให้เข้าสู่ระบบผ่าน SSH ด้วยรหัสผ่านสำหรับ REDMAGIC Astra (NP05J)\n'
            'PasswordAuthentication yes\n'
            'KbdInteractiveAuthentication yes\n'
        )
        (rootfs / 'run/sshd').mkdir(parents=True, exist_ok=True)
        guest(['/usr/sbin/sshd', '-t'])
        guest(['systemctl', 'enable', 'ssh.service'])

    guest(['apt-get', 'clean'])
    return mesa_ver


def install_np05j_hardware_layer(rootfs: Path, kernel_dir: Path, kernel_meta: dict,
                                 username: str = DEFAULT_USER, password: str | None = None) -> Path:
    """ติดตั้งโมดูลเคอร์เนล, เฟิร์มแวร์สต็อก NP05J, ไดรเวอร์ firmware oneshot, udev rules, ภาษา EN และสร้าง initrd.img"""
    release = kernel_meta['kernel_release']
    mod_src = kernel_dir / 'modules/lib/modules' / release
    if not mod_src.is_dir():
        raise ValueError(f'ไม่พบโฟลเดอร์โมดูลเคอร์เนล: {mod_src}')

    # 1. คัดลอกโมดูลเคอร์เนลเข้า rootfs (/usr/lib/modules/<release>)
    mod_dst_parent = guest_resolve(rootfs, 'lib/modules')
    mod_dst_parent.mkdir(parents=True, exist_ok=True)
    mod_dst = mod_dst_parent / release
    if mod_dst.exists():
        shutil.rmtree(mod_dst)
    shutil.copytree(mod_src, mod_dst, symlinks=True)

    # 1.5 คัดลอกเฟิร์มแวร์สต็อก NP05J ที่ตรวจ SHA-256 แล้ว (ADSP, CDSP, Wi-Fi 7, BT, GPU, Audio, Touch) เข้า /usr/lib/firmware
    install_np05j_firmware(rootfs, DEFAULT_FIRMWARE)
    install_np05j_audio_userspace(rootfs)
    configure_ubuntu_gdm_autologin(rootfs, username)

    # 2. ติดตั้ง np05j-tool และ np05j-fw-load + systemd oneshot service
    bin_dir = guest_resolve(rootfs, 'usr/bin')
    build_np05j_tool(bin_dir / 'np05j-tool')

    libexec_dir = guest_resolve(rootfs, 'usr/libexec')
    libexec_dir.mkdir(parents=True, exist_ok=True)
    fw_script = libexec_dir / 'np05j-fw-load'
    shutil.copyfile(ROOT / 'linux/userspace/np05j/np05j-fw-load', fw_script)
    fw_script.chmod(0o755)
    write_np05j_firmware_service(rootfs)

    # 3. ติดตั้ง udev rules: แมทริกซ์ทัชแนวนอน 2400x1504 และล็อกพาร์ติชันสต็อก Android แบบ read-only
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

    # 4. ตั้งค่าตัวตนเครื่อง, locale ภาษาอังกฤษ และ dconf เดสก์ท็อปสำหรับแท็บเล็ต
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
    for rel in ('etc/locale.conf', 'etc/default/locale'):
        p = guest_resolve(rootfs, rel)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('LANG=en_US.UTF-8\n')

    # เปิดคีย์บอร์ดบนหน้าจอ + ธีมมืด + สเกล 200% + แอนิเมชันลื่นไหล และปิด auto-suspend ชั่วคราวระหว่างใช้ simpledrm
    dconf_dir = rootfs / 'etc/dconf/db/local.d'
    dconf_dir.mkdir(parents=True, exist_ok=True)
    (dconf_dir / '01-np05j').write_text(
        '[org/gnome/desktop/interface]\n'
        'scaling-factor=uint32 2\n'
        'enable-animations=true\n'
        "color-scheme='prefer-dark'\n"
        '\n'
        '[org/gnome/desktop/a11y/applications]\n'
        'screen-keyboard-enabled=true\n'
        '\n'
        '[org/gnome/desktop/input-sources]\n'
        "sources=[('xkb', 'us')]\n"
        '\n'
        '[org/gnome/settings-daemon/plugins/power]\n'
        "sleep-inactive-ac-type='nothing'\n"
        "sleep-inactive-battery-type='nothing'\n"
        '\n'
        '[org/gnome/desktop/session]\n'
        'idle-delay=uint32 0\n'
        '\n'
        '[org/gnome/desktop/screensaver]\n'
        'lock-enabled=false\n'
    )
    mimeapps = rootfs / 'etc/xdg/mimeapps.list'
    mimeapps.parent.mkdir(parents=True, exist_ok=True)
    mimeapps.write_text(
        '[Default Applications]\n'
        'x-scheme-handler/http=org.gnome.Epiphany.desktop\n'
        'x-scheme-handler/https=org.gnome.Epiphany.desktop\n'
        'text/html=org.gnome.Epiphany.desktop\n'
        'application/xhtml+xml=org.gnome.Epiphany.desktop\n'
    )

    # 5. รัน chroot เพื่อติดตั้งแพ็กเกจเสริม, ตั้งค่าบัญชีผู้ใช้, gen locale, depmod และสร้าง initrd.img
    cross = platform.machine() not in ('aarch64', 'arm64')
    qemu_dst = rootfs / 'usr/bin/qemu-aarch64-static'
    if cross:
        shutil.copy2('/usr/bin/qemu-aarch64-static', qemu_dst)

    mounted = []
    policy = rootfs / 'usr/sbin/policy-rc.d'
    policy.write_text('#!/bin/sh\nexit 101\n')
    policy.chmod(0o755)
    resolv_dst = rootfs / 'etc/resolv.conf'
    resolv_was_symlink = resolv_dst.is_symlink()
    resolv_link_target = os.readlink(resolv_dst) if resolv_was_symlink else '../run/systemd/resolve/stub-resolv.conf'
    if resolv_was_symlink or resolv_dst.exists():
        resolv_dst.unlink()
    shutil.copyfile('/etc/resolv.conf', resolv_dst)

    def guest(cmd):
        argv = guest_command(rootfs, [
            '/usr/bin/env', 'PATH=/usr/sbin:/usr/bin:/sbin:/bin',
            'SYSTEMD_OFFLINE=1', 'SYSTEMD_IN_CHROOT=1',
            'DEBIAN_FRONTEND=noninteractive', *cmd,
        ])
        run(argv)

    try:
        for name, kind, opts in (('proc', 'proc', ''), ('sys', 'sysfs', 'ro'), ('dev', 'tmpfs', ''), ('run', 'tmpfs', '')):
            path = rootfs / name
            path.mkdir(exist_ok=True)
            run(['mount', '-t', kind, *(['-o', opts] if opts else []), kind, path])
            mounted.append(path)
        for name, major, minor in (('null', 1, 3), ('zero', 1, 5), ('random', 1, 8), ('urandom', 1, 9), ('tty', 5, 0)):
            devnode = rootfs / 'dev' / name
            if not devnode.exists():
                run(['mknod', '-m', '666', devnode, 'c', str(major), str(minor)])
        for name, target in (('fd', '/proc/self/fd'), ('stdin', '/proc/self/fd/0'),
                             ('stdout', '/proc/self/fd/1'), ('stderr', '/proc/self/fd/2')):
            sym = rootfs / 'dev' / name
            if not sym.exists() and not sym.is_symlink():
                sym.symlink_to(target)

        guest(['apt-get', 'update'])
        guest(['apt-get', 'install', '-y', '--no-install-recommends', *EXTRA_UBUNTU_PACKAGES])
        upgrade_mesa_a830_and_ssh(rootfs, guest)
        configure_np05j_user(rootfs, username, password, guest)

        # ตั้งค่า zram 8 GiB (zstd) และเปิดบริการหลัก
        (rootfs / 'etc/default/zramswap').write_text('ALGO=zstd\nPERCENT=33\nPRIORITY=100\n')
        guest(['sed', '-i', 's/^# *en_US.UTF-8/en_US.UTF-8/', '/etc/locale.gen'])
        guest(['locale-gen', 'en_US.UTF-8'])
        guest(['dconf', 'update'])
        guest(['systemctl', 'enable', 'bluetooth.service', 'systemd-timesyncd.service'])
        guest(['depmod', '-a', release])
        guest(['update-initramfs', '-c', '-k', release])
        guest(['apt-get', 'clean'])
    finally:
        stop_helpers(rootfs)
        for path in reversed(mounted):
            run(['umount', '--recursive', path])
        policy.unlink(missing_ok=True)
        # คืนค่า symlink /etc/resolv.conf -> ../run/systemd/resolve/stub-resolv.conf เพื่อไม่ให้ค้าง DNS ของ Docker (127.0.0.11)
        if resolv_dst.exists() or resolv_dst.is_symlink():
            resolv_dst.unlink()
        resolv_dst.symlink_to(resolv_link_target)
        if cross:
            qemu_dst.unlink(missing_ok=True)
        for name in ('dev', 'proc', 'sys', 'run'):
            for child in (rootfs / name).iterdir():
                if child.is_dir() and not child.is_symlink():
                    shutil.rmtree(child)
                else:
                    child.unlink()

    # ยืนยันว่าสร้าง initrd.img สำเร็จและรีเซ็ต machine-id ให้ว่างเพื่อสร้างใหม่ตอนบูตครั้งแรก
    initrd = rootfs / 'boot' / f'initrd.img-{release}'
    if not initrd.is_file() or initrd.stat().st_size < 1024 * 1024:
        raise ValueError(f'ไม่พบไฟล์ initrd.img ที่สมบูรณ์: {initrd}')
    (rootfs / 'etc/machine-id').write_text('')
    return initrd


EARLY_FIRMWARE_COMPONENTS = ('np05j-ath12k', 'np05j-bluetooth', 'np05j-gpu')
EARLY_FIRMWARE_MARKER = 'usr/lib/firmware/.np05j-early-fw'


def _cpio_newc_entry(ino: int, mode: int, name: str, payload: bytes) -> bytes:
    """สร้าง 1 รายการในฟอร์แมต cpio newc (070701) พร้อม 4-byte NUL alignment"""
    name_bytes = name.encode('utf-8') + b'\x00'
    header = (
        f'070701'
        f'{ino:08x}'
        f'{mode:08x}'
        f'{0:08x}{0:08x}'
        f'{1:08x}{0:08x}'
        f'{len(payload):08x}'
        f'{0:08x}{0:08x}{0:08x}{0:08x}'
        f'{len(name_bytes):08x}'
        f'{0:08x}'
    ).encode('ascii')
    head_pad = (-((len(header) + len(name_bytes)) % 4)) % 4
    body_pad = (-(len(payload) % 4)) % 4
    return header + name_bytes + (b'\x00' * head_pad) + payload + (b'\x00' * body_pad)


def strip_trailing_early_fw(initrd_bytes: bytes) -> bytes:
    """ตัด gzip cpio stream ที่ฝังเฟิร์มแวร์ต่อท้ายเดิมออก เพื่อให้รัน --esp-only ซ้ำได้โดยไฟล์ไม่บวม"""
    pos = 0
    streams = []
    marker_bytes = EARLY_FIRMWARE_MARKER.encode('ascii')
    while pos < len(initrd_bytes):
        while pos < len(initrd_bytes) and initrd_bytes[pos] == 0:
            pos += 1
        if pos >= len(initrd_bytes):
            break
        if initrd_bytes[pos:pos + 2] != b'\x1f\x8b':
            return initrd_bytes
        dec = zlib.decompressobj(16 + zlib.MAX_WBITS)
        data = dec.decompress(initrd_bytes[pos:])
        consumed = len(initrd_bytes) - pos - len(dec.unused_data)
        streams.append((pos, pos + consumed, data))
        pos += consumed
    if len(streams) >= 2 and marker_bytes in streams[-1][2]:
        return initrd_bytes[:streams[-1][0]]
    return initrd_bytes


def build_early_firmware_cpio_gz(dest: Path, fw_source: Path = DEFAULT_FIRMWARE,
                                 components: tuple[str, ...] = EARLY_FIRMWARE_COMPONENTS) -> dict:
    """สร้าง gzip-compressed cpio overlay ฝังเฟิร์มแวร์ Wi-Fi 7, Bluetooth และ Adreno 830 GPU ลง usr/lib/firmware"""
    if not fw_source.is_dir():
        raise ValueError(f'ไม่พบโฟลเดอร์เฟิร์มแวร์ NP05J: {fw_source}')

    entries: list[bytes] = []
    seen_dirs: set[str] = set()
    ino = 900000

    def ensure_dir(rel_dir: str):
        nonlocal ino
        parts = [p for p in rel_dir.strip('/').split('/') if p]
        cur = ''
        for part in parts:
            cur = f'{cur}/{part}' if cur else part
            # ห้ามสร้างไดเรกทอรี lib ทับ symlink lib -> usr/lib ใน initramfs ของ Ubuntu
            if cur == 'lib':
                raise ValueError('ห้ามสร้างไดเรกทอรี lib ใน early initramfs; ต้องใช้ usr/lib เสมอ')
            if cur not in seen_dirs:
                seen_dirs.add(cur)
                ino += 1
                entries.append(_cpio_newc_entry(ino, stat.S_IFDIR | 0o755, cur, b''))

    ensure_dir('usr/lib/firmware')
    summary = {}
    for comp in components:
        manifest_file = fw_source / comp / 'manifest.json'
        if not manifest_file.is_file():
            raise ValueError(f'ไม่พบ manifest ของเฟิร์มแวร์ early initramfs: {manifest_file}')
        meta = json.loads(manifest_file.read_text())
        sub = meta.get('directory', '.').strip('/')
        target_dir = 'usr/lib/firmware' if sub in ('', '.') else f'usr/lib/firmware/{sub}'
        ensure_dir(target_dir)

        fcount = 0
        for fname in sorted(meta['files'].keys()):
            finfo = meta['files'][fname]
            src_file = manifest_file.parent / fname
            raw = src_file.read_bytes()
            if len(raw) != finfo['bytes'] or hashlib.sha256(raw).hexdigest() != finfo['sha256']:
                raise ValueError(f'SHA-256 ของเฟิร์มแวร์ไม่ตรง manifest: {src_file}')
            ino += 1
            entries.append(_cpio_newc_entry(ino, stat.S_IFREG | 0o644, f'{target_dir}/{fname}', raw))
            fcount += 1

        acount = 0
        for alias_name in sorted(meta.get('aliases', {}).keys()):
            target_name = meta['aliases'][alias_name]
            if target_name not in meta['files']:
                raise ValueError(f'Alias เฟิร์มแวร์ชี้ไปไฟล์ที่ไม่มีใน manifest: {alias_name} -> {target_name}')
            ino += 1
            entries.append(_cpio_newc_entry(ino, stat.S_IFLNK | 0o777, f'{target_dir}/{alias_name}',
                                           target_name.encode('utf-8')))
            acount += 1

        summary[comp] = {'directory': target_dir, 'files': fcount, 'aliases': acount}

    ino += 1
    entries.append(_cpio_newc_entry(ino, stat.S_IFREG | 0o644, EARLY_FIRMWARE_MARKER, b'np05j-early-fw\n'))
    entries.append(_cpio_newc_entry(0, 0, 'TRAILER!!!', b''))

    raw_cpio = b''.join(entries)
    with dest.open('wb') as raw_out:
        with gzip.GzipFile(filename='', mode='wb', compresslevel=6, fileobj=raw_out, mtime=0) as gz:
            gz.write(raw_cpio)
    return summary


def build_esp_image(esp_path: Path, esp_size_mib: int, kernel_dir: Path, kernel_meta: dict, initrd: Path, work: Path):
    """สร้าง esp.img (FAT32 4 KiB sector, LABEL=SUNUEFI_ESP) พร้อม systemd-boot, early firmware และรายการบูต NP05J"""
    release = kernel_meta['kernel_release']
    record = json.loads((ROOT / 'config/release.json').read_text())
    systemd_boot = shared_stub(record['efi_stub'], boot=True)

    # รวมเฟิร์มแวร์ built-in drivers (Wi-Fi 7 ath12k, Bluetooth BRAHMA 2.0, Adreno 830v2 GPU)
    # ต่อท้าย initrd.img เพื่อให้ไดรเวอร์โหลดได้ทันทีตั้งแต่ช่วง boot โดยไม่ fail -2 ก่อน mount rootfs
    early_fw_gz = work / 'np05j-early-fw.cpio.gz'
    build_early_firmware_cpio_gz(early_fw_gz, DEFAULT_FIRMWARE)
    combined_initrd = work / f'initrd-np05j-{release}.img'
    base_bytes = strip_trailing_early_fw(initrd.read_bytes())
    pad_len = (-(len(base_bytes) % 4)) % 4
    combined_initrd.write_bytes(base_bytes + (b'\x00' * pad_len) + early_fw_gz.read_bytes())

    loader_conf = work / 'loader.conf'
    loader_conf.write_text(
        'default 01-ubuntu-np05j-gpu.conf\n'
        'timeout 5\n'
        'console-mode keep\n'
    )

    base = f'/linux/{release}'
    # คง boot-up votes ของ rpmhpd (MMCX/CX/MX) และ sm8750 interconnect (mmss_noc/mc_virt)
    # ไว้ระหว่างใช้ UEFI GOP / simpledrm เพื่อไม่ให้ of_platform_sync_state_init ตัดไฟ/แบนด์วิดท์จอ OLED
    bl_keep_sync_state = ','.join((
        '__initstub__kmod_core__802_1299_sync_state_resume_initcall7',
        '__initstub__kmod_platform__675_611_of_platform_sync_state_init7s',
    ))
    common_opts = (
        'console=tty0 loglevel=7 consoleblank=0 fbcon=font:TER16x32 '
        'root=LABEL=PIANOROOT rootfstype=ext4 rootwait ro '
        'cma=128M pd_ignore_unused clk_ignore_unused cpuidle.off=1 '
        'panic=0 iommu.passthrough=1 pcie_port_pm=off '
        'hci_uart.piano_retries=0 hci_uart.piano_peri=106 '
        'efi=noruntime systemd.show_status=true systemd.log_target=kmsg'
    )
    entries = {
        '/loader/entries/01-ubuntu-np05j-gpu.conf': (
            'title Boot Linux\n'
            f'version {release}\n'
            f'linux {base}/Image\n'
            f'initrd {base}/initrd.img\n'
            f'devicetree {base}/sm8750-nubia-np05j-gpu.dtb\n'
            f'options {common_opts} msm.separate_gpu_kms=1 initcall_blacklist={bl_keep_sync_state}\n'
        ),
        '/loader/entries/02-ubuntu-np05j-oled.conf': (
            'title Boot Linux (OLED 144Hz/165Hz)\n'
            f'version {release}\n'
            f'linux {base}/Image\n'
            f'initrd {base}/initrd.img\n'
            f'devicetree {base}/sm8750-nubia-np05j-display.dtb\n'
            f'options {common_opts}\n'
        ),
        '/loader/entries/03-ubuntu-np05j-safe.conf': (
            'title Boot Linux (Safe - No GPU)\n'
            f'version {release}\n'
            f'linux {base}/Image\n'
            f'initrd {base}/initrd.img\n'
            f'devicetree {base}/sm8750-nubia-np05j-otg.dtb\n'
            f'options {common_opts} initcall_blacklist={bl_keep_sync_state}\n'
        ),
        '/loader/entries/04-ubuntu-np05j-serial.conf': (
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
        f'{base}/initrd.img': combined_initrd,
        f'{base}/sm8750-nubia-np05j-otg.dtb': kernel_dir / 'sm8750-nubia-np05j-otg.dtb',
        f'{base}/sm8750-nubia-np05j.dtb': kernel_dir / 'sm8750-nubia-np05j.dtb',
        f'{base}/sm8750-nubia-np05j-gpu.dtb': kernel_dir / 'sm8750-nubia-np05j-gpu.dtb',
        f'{base}/sm8750-nubia-np05j-display.dtb': kernel_dir / 'sm8750-nubia-np05j-display.dtb',
    }
    for dest, body in entries.items():
        tmp = work / Path(dest).name
        tmp.write_text(body)
        esp_files[dest] = tmp

    total_payload = sum(p.stat().st_size for p in esp_files.values())
    if total_payload + 8 * MIB >= esp_size_mib * MIB:
        raise ValueError(f'ไฟล์ ESP รวม {total_payload // MIB} MiB เกินความจุ {esp_size_mib} MiB')

    with esp_path.open('xb') as stream:
        stream.truncate(esp_size_mib * MIB)
    run(['mkfs.vfat', '--invariant', '-S', '4096', '-F', '32', '-n', 'SUNUEFI_ESP', esp_path])

    esp_tree = work / 'esp-tree'
    for dest, src in esp_files.items():
        dst = esp_tree / dest.lstrip('/')
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
    for directory in sorted(esp_tree.iterdir()):
        run(['mcopy', '-s', '-m', '-i', esp_path, directory, '::/'])

    # ตรวจอ่านกลับและเทียบ SHA-256 ทุกไฟล์ใน esp.img
    for dest, src in esp_files.items():
        verify = work / ('verify-' + dest.lstrip('/').replace('/', '-'))
        run(['mcopy', '-i', esp_path, '::' + dest, verify])
        if sha(verify) != sha(src):
            raise ValueError(f'ตรวจอ่านกลับไฟล์ใน ESP ไม่ตรง: {dest}')

    return sorted(esp_files.keys())


def dir_size_bytes(path: Path) -> int:
    total = 0
    for root, _, files in os.walk(path, followlinks=False):
        for name in files:
            p = Path(root) / name
            try:
                st = p.lstat()
                total += st.st_size
            except OSError:
                pass
    return total


def build_root_image(rootfs: Path, root_img: Path, root_size_mib: int | None):
    """สร้าง root.ext4.img (LABEL=PIANOROOT, 4 KiB block) และตรวจสอบความสมบูรณ์ด้วย e2fsck"""
    used_bytes = dir_size_bytes(rootfs)
    if root_size_mib is None:
        # เผื่อ overhead ของ ext4 inode/journal และ headroom ~1.5 GiB แล้วให้ systemd-growfs ขยายเต็ม 676 GiB ตอนบูตแรก
        estimated_mib = int((used_bytes * 1.25) // MIB) + 1024
        root_size_mib = ((estimated_mib + 63) // 64) * 64
    with root_img.open('xb') as stream:
        stream.truncate(root_size_mib * MIB)
    run(['mke2fs', '-q', '-t', 'ext4', '-b', '4096', '-F', '-L', 'PIANOROOT', '-d', rootfs, root_img])
    run(['e2fsck', '-fn', root_img])
    return root_size_mib


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--kernel', type=Path, default=DEFAULT_KERNEL)
    ap.add_argument('--output-rootfs', type=Path, default=DEFAULT_ROOTFS_OUT)
    ap.add_argument('--output-bundle', type=Path, default=DEFAULT_BUNDLE_OUT)
    ap.add_argument('--esp-size-mib', type=int, default=512)
    ap.add_argument('--root-size-mib', type=int)
    ap.add_argument('--user', default=DEFAULT_USER, help='ชื่อบัญชีผู้ใช้เริ่มต้นใน Ubuntu')
    ap.add_argument('--password', default=None, help='รหัสผ่านสำหรับบัญชีผู้ใช้เริ่มต้น')
    ap.add_argument('--esp-only', action='store_true',
                    help='สร้างใหม่เฉพาะ esp.img และ sparse/esp.img โดยคง root.ext4.img เดิมไว้')
    ap.add_argument('--update-rootfs-firmware', action='store_true',
                    help='ติดตั้งเฟิร์มแวร์ NP05J (vendor/np05j-firmware) + np05j-fw-load ลง root.ext4.img เดิมแล้วสร้าง sparse ใหม่')
    ap.add_argument('--initrd', type=Path,
                    help='พาธไฟล์ initrd.img สำหรับ --esp-only (ถ้าไม่ระบุจะดึงจาก esp.img เดิม)')
    ap.add_argument('--rebuild', action='store_true')
    ap.add_argument('--plan', action='store_true')
    ap.add_argument('--execute', action='store_true')
    args = ap.parse_args()
    if not (args.plan ^ args.execute):
        raise SystemExit('ต้องระบุ --plan หรือ --execute อย่างใดอย่างหนึ่ง')

    kernel_dir = args.kernel.resolve()
    rootfs_out = args.output_rootfs.resolve()
    bundle_out = args.output_bundle.resolve()
    manifest_path = kernel_dir / 'manifest.json'
    if not manifest_path.is_file():
        raise SystemExit(f'ไม่พบ kernel manifest: {manifest_path} (ให้รัน ./build.sh linux --device np05j ก่อน)')
    kernel_meta = json.loads(manifest_path.read_text())

    summary = {
        'device': 'np05j',
        'distro': 'ubuntu',
        'suite': 'noble',
        'desktop': 'gnome',
        'kernel_release': kernel_meta['kernel_release'],
        'rootfs_output': str(rootfs_out),
        'bundle_output': str(bundle_out),
        'esp_size_mib': args.esp_size_mib,
        'root_size_mib': args.root_size_mib,
        'esp_only': args.esp_only,
        'status': 'PLAN_ONLY',
    }
    if args.plan:
        print(json.dumps(summary, indent=2))
        return 0

    if args.esp_only:
        bundle_manifest_path = bundle_out / 'manifest.json'
        esp_img = bundle_out / 'esp.img'
        root_img = bundle_out / 'root.ext4.img'
        if not bundle_manifest_path.is_file() or not esp_img.is_file() or not root_img.is_file():
            raise SystemExit(f'--esp-only ต้องมี bundle เดิมครบอยู่ใน {bundle_out}')
        bundle_manifest = json.loads(bundle_manifest_path.read_text())
        release = kernel_meta['kernel_release']
        with tempfile.TemporaryDirectory(dir=bundle_out) as work_str:
            work = Path(work_str)
            if args.initrd:
                initrd = args.initrd.resolve()
            else:
                initrd = work / f'initrd.img-{release}'
                run(['mcopy', '-i', esp_img, f'::/linux/{release}/initrd.img', initrd])
            new_esp = work / 'esp.img.new'
            esp_files = build_esp_image(new_esp, args.esp_size_mib, kernel_dir, kernel_meta, initrd, work)
            os.replace(new_esp, esp_img)

        bundle_manifest['kernel_release'] = release
        bundle_manifest['esp_files'] = esp_files
        bundle_manifest['partitions']['esp']['capacity_bytes'] = esp_img.stat().st_size
        bundle_manifest['files']['esp.img'] = {
            'bytes': esp_img.stat().st_size,
            'sha256': sha(esp_img),
            'format': 'raw-fat32',
        }
        bundle_manifest_path.write_text(json.dumps(bundle_manifest, indent=2) + '\n')
        (bundle_out / 'SHA256SUMS').write_text(
            f"{bundle_manifest['files']['esp.img']['sha256']}  esp.img\n"
            f"{bundle_manifest['files']['root.ext4.img']['sha256']}  root.ext4.img\n"
        )

        # แปลงเฉพาะ sparse/esp.img ใหม่ โดยคง sparse/root.ext4.img เดิมไว้เพื่อลดเวลา
        sparse_out = bundle_out / 'sparse'
        sparse_manifest_path = sparse_out / 'manifest.json'
        if sparse_manifest_path.is_file() and (sparse_out / 'root.ext4.img').is_file():
            sparse_meta = json.loads(sparse_manifest_path.read_text())
            new_sparse_esp = sparse_out / 'esp.img.new'
            new_sparse_esp.unlink(missing_ok=True)
            row = package_sparse_images.convert(esp_img, new_sparse_esp)
            if (row['expanded_sha256'] != bundle_manifest['files']['esp.img']['sha256'] or
                    row['expanded_bytes'] != bundle_manifest['files']['esp.img']['bytes']):
                raise ValueError('sparse esp.img ไม่ตรงกับ raw esp.img')
            os.replace(new_sparse_esp, sparse_out / 'esp.img')
            sparse_meta['kernel_release'] = release
            sparse_meta['source_manifest_sha256'] = package_sparse_images.digest(bundle_manifest_path)
            sparse_meta['files']['esp.img'] = row
            sparse_manifest_path.write_text(json.dumps(sparse_meta, indent=2) + '\n')
            (sparse_out / 'SHA256SUMS').write_text(''.join(
                info['sha256'] + '  ' + name + '\n' for name, info in sparse_meta['files'].items()
            ))
        else:
            if sparse_out.exists():
                shutil.rmtree(sparse_out)
            sparse_meta = package_sparse_images.package(bundle_out, sparse_out)

        print(json.dumps({
            'status': bundle_manifest['status'],
            'mode': 'esp-only',
            'kernel_release': bundle_manifest['kernel_release'],
            'raw': bundle_manifest['files'],
            'sparse': sparse_meta['files'],
            'sparse_dir': str(sparse_out),
        }, indent=2))
        return 0

    if args.update_rootfs_firmware:
        if os.geteuid() != 0:
            raise SystemExit('--update-rootfs-firmware ต้องรันด้วยสิทธิ์ root ภายใน container sunuefi-builder (--privileged)')
        ensure_qemu_binfmt()
        bundle_manifest_path = bundle_out / 'manifest.json'
        esp_img = bundle_out / 'esp.img'
        root_img = bundle_out / 'root.ext4.img'
        if not bundle_manifest_path.is_file() or not esp_img.is_file() or not root_img.is_file():
            raise SystemExit(f'--update-rootfs-firmware ต้องมี bundle เดิมครบอยู่ใน {bundle_out}')
        bundle_manifest = json.loads(bundle_manifest_path.read_text())
        cross = platform.machine() not in ('aarch64', 'arm64')
        with tempfile.TemporaryDirectory(prefix='np05j-fw-mnt-') as mnt_str:
            mnt = Path(mnt_str)
            run(['mount', '-o', 'loop', root_img, mnt])
            qemu_dst = mnt / 'usr/bin/qemu-aarch64-static'
            policy = mnt / 'usr/sbin/policy-rc.d'
            resolv_dst = mnt / 'etc/resolv.conf'
            resolv_was_symlink = False
            resolv_link_target = None
            bound = []
            try:
                fw_installed = install_np05j_firmware(mnt, DEFAULT_FIRMWARE)
                fw_script = guest_resolve(mnt, 'usr/libexec') / 'np05j-fw-load'
                shutil.copyfile(ROOT / 'linux/userspace/np05j/np05j-fw-load', fw_script)
                fw_script.chmod(0o755)
                write_np05j_firmware_service(mnt)
                if cross:
                    shutil.copy2('/usr/bin/qemu-aarch64-static', qemu_dst)
                policy.write_text('#!/bin/sh\nexit 101\n')
                policy.chmod(0o755)
                resolv_was_symlink = resolv_dst.is_symlink()
                resolv_link_target = os.readlink(resolv_dst) if resolv_was_symlink else None
                if resolv_was_symlink:
                    resolv_dst.unlink()
                shutil.copyfile('/etc/resolv.conf', resolv_dst)
                for name, kind, opts in (('proc', 'proc', ''), ('sys', 'sysfs', 'ro'), ('dev', 'tmpfs', ''), ('run', 'tmpfs', '')):
                    target = mnt / name
                    target.mkdir(exist_ok=True)
                    run(['mount', '-t', kind, *(['-o', opts] if opts else []), kind, target])
                    bound.append(target)
                for dname, maj, mino in (('null', 1, 3), ('zero', 1, 5), ('random', 1, 8), ('urandom', 1, 9), ('tty', 5, 0)):
                    run(['mknod', '-m', '666', mnt / 'dev' / dname, 'c', str(maj), str(mino)])
                for sym_name, sym_target in (('fd', '/proc/self/fd'), ('stdin', '/proc/self/fd/0'),
                                             ('stdout', '/proc/self/fd/1'), ('stderr', '/proc/self/fd/2')):
                    sym = mnt / 'dev' / sym_name
                    if not sym.exists() and not sym.is_symlink():
                        sym.symlink_to(sym_target)
                def guest(cmd):
                    run(guest_command(mnt, [
                        '/usr/bin/env', 'PATH=/usr/sbin:/usr/bin:/sbin:/bin',
                        'SYSTEMD_OFFLINE=1', 'SYSTEMD_IN_CHROOT=1',
                        'DEBIAN_FRONTEND=noninteractive', *cmd,
                    ]))
                mesa_ver = upgrade_mesa_a830_and_ssh(mnt, guest)
                configure_np05j_user(mnt, args.user, args.password, guest)
            finally:
                stop_helpers(mnt)
                for target in reversed(bound):
                    run(['umount', '--recursive', target])
                policy.unlink(missing_ok=True)
                if resolv_was_symlink and resolv_link_target:
                    resolv_dst.unlink(missing_ok=True)
                    resolv_dst.symlink_to(resolv_link_target)
                if cross:
                    qemu_dst.unlink(missing_ok=True)
                for name in ('dev', 'proc', 'sys', 'run'):
                    for child in (mnt / name).iterdir():
                        if child.is_dir() and not child.is_symlink():
                            shutil.rmtree(child)
                        else:
                            child.unlink()
                # ล้างบล็อกว่างให้เป็นศูนย์เพื่อให้ sparse root.ext4.img มีขนาดเล็กที่สุด
                zero_file = mnt / '.np05j-zero-fill'
                subprocess.run(
                    ['dd', 'if=/dev/zero', f'of={zero_file}', 'bs=4M'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
                )
                zero_file.unlink(missing_ok=True)
                run(['sync'])
                run(['umount', mnt])
        run(['e2fsck', '-fn', root_img])

        bundle_manifest['firmware_components'] = fw_installed
        bundle_manifest['mesa_version'] = mesa_ver
        bundle_manifest['default_user'] = args.user
        bundle_manifest['files']['root.ext4.img'] = {
            'bytes': root_img.stat().st_size,
            'sha256': sha(root_img),
            'format': 'raw-ext4',
        }
        bundle_manifest_path.write_text(json.dumps(bundle_manifest, indent=2) + '\n')
        (bundle_out / 'SHA256SUMS').write_text(
            f"{bundle_manifest['files']['esp.img']['sha256']}  esp.img\n"
            f"{bundle_manifest['files']['root.ext4.img']['sha256']}  root.ext4.img\n"
        )

        # แปลงเฉพาะ sparse/root.ext4.img ใหม่ โดยคง sparse/esp.img เดิมไว้
        sparse_out = bundle_out / 'sparse'
        sparse_manifest_path = sparse_out / 'manifest.json'
        if sparse_manifest_path.is_file() and (sparse_out / 'esp.img').is_file():
            sparse_meta = json.loads(sparse_manifest_path.read_text())
            new_sparse_root = sparse_out / 'root.ext4.img.new'
            new_sparse_root.unlink(missing_ok=True)
            row = package_sparse_images.convert(root_img, new_sparse_root)
            if (row['expanded_sha256'] != bundle_manifest['files']['root.ext4.img']['sha256'] or
                    row['expanded_bytes'] != bundle_manifest['files']['root.ext4.img']['bytes']):
                raise ValueError('sparse root.ext4.img ไม่ตรงกับ raw root.ext4.img')
            os.replace(new_sparse_root, sparse_out / 'root.ext4.img')
            sparse_meta['source_manifest_sha256'] = package_sparse_images.digest(bundle_manifest_path)
            sparse_meta['files']['root.ext4.img'] = row
            sparse_manifest_path.write_text(json.dumps(sparse_meta, indent=2) + '\n')
            (sparse_out / 'SHA256SUMS').write_text(''.join(
                info['sha256'] + '  ' + name + '\n' for name, info in sparse_meta['files'].items()
            ))
        else:
            if sparse_out.exists():
                shutil.rmtree(sparse_out)
            sparse_meta = package_sparse_images.package(bundle_out, sparse_out)

        print(json.dumps({
            'status': bundle_manifest['status'],
            'mode': 'update-rootfs-firmware',
            'firmware_components': fw_installed,
            'raw': bundle_manifest['files'],
            'sparse': sparse_meta['files'],
            'sparse_dir': str(sparse_out),
        }, indent=2))
        return 0

    if os.geteuid() != 0:
        raise SystemExit('--execute ต้องรันด้วยสิทธิ์ root ภายใน container sunuefi-builder (--privileged)')

    ensure_qemu_binfmt()
    keyring = ensure_ubuntu_keyring()

    if args.rebuild:
        for path in (rootfs_out, bundle_out):
            if path.exists():
                shutil.rmtree(path)
    else:
        for path in (rootfs_out, bundle_out):
            if path.exists():
                raise SystemExit(f'โฟลเดอร์ปลายทางมีอยู่แล้ว: {path} (ใช้ --rebuild เพื่อสร้างใหม่)')

    # เตรียมพื้นที่ทำงานชั่วคราวแบบ tmpfs (dev,exec,suid) ใต้ build/distros/ เพื่อให้ debootstrap สร้าง device node ได้แม้โฮสต์เป็น nodev
    stage_mount = ROOT / 'build/distros/np05j-stage-tmpfs'
    stage_mount.mkdir(parents=True, exist_ok=True)
    run(['mount', '-t', 'tmpfs', '-o', 'size=16G,dev,exec,suid', 'tmpfs', stage_mount])
    stage_rootfs_out = stage_mount / 'ubuntu-gnome-np05j'

    try:
        # 1. สร้างฐาน Ubuntu 24.04 LTS + GNOME ผ่าน assemble_rootfs.py ของรีโป
        run([
            sys.executable, ROOT / 'tools/assemble_rootfs.py',
            '--distro', 'ubuntu',
            '--desktop', 'gnome',
            '--scale-percent', '200',
            '--keyring', keyring,
            '--output', stage_rootfs_out,
            '--execute',
        ])

        rootfs = stage_rootfs_out / 'rootfs'
        # 2. ติดตั้งโมดูล NP05J, เฟิร์มแวร์, บัญชีผู้ใช้, firmware oneshot, udev rules, initramfs และเดสก์ท็อปเสริม
        initrd = install_np05j_hardware_layer(rootfs, kernel_dir, kernel_meta, args.user, args.password)

        # 3. สร้าง bundle (esp.img, root.ext4.img) + sparse images สำหรับ Fastboot
        bundle_out.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=bundle_out) as work_str:
            work = Path(work_str)
            esp_img = bundle_out / 'esp.img'
            root_img = bundle_out / 'root.ext4.img'
            esp_files = build_esp_image(esp_img, args.esp_size_mib, kernel_dir, kernel_meta, initrd, work)
            actual_root_mib = build_root_image(rootfs, root_img, args.root_size_mib)

        # บันทึก manifest และรายการแพ็กเกจจาก stage rootfs ไว้ใน build/distros/ubuntu-gnome-np05j
        rootfs_out.mkdir(parents=True, exist_ok=True)
        for meta_name in ('manifest.json', 'packages.tsv', 'build.log'):
            src_meta = stage_rootfs_out / meta_name
            if src_meta.is_file():
                shutil.copy2(src_meta, rootfs_out / meta_name)
    finally:
        # สำเนา log สำคัญออกมาก่อน unmount tmpfs แม้เกิดข้อผิดพลาด
        rootfs_out.mkdir(parents=True, exist_ok=True)
        for meta_name in ('manifest.json', 'packages.tsv', 'build.log', 'FAILED.json'):
            src_meta = stage_rootfs_out / meta_name
            if src_meta.is_file():
                shutil.copy2(src_meta, rootfs_out / meta_name)
        run(['umount', '--recursive', stage_mount])
        try:
            stage_mount.rmdir()
        except OSError:
            pass

    bundle_manifest = {
        'schema_version': 1,
        'device': 'np05j',
        'distro': 'ubuntu-24.04-gnome',
        'status': 'HOST_BUILT_NP05J_UBUNTU_GNOME_NOT_HARDWARE_VERIFIED',
        'kernel_release': kernel_meta['kernel_release'],
        'root_policy': 'LABEL=PIANOROOT',
        'esp_files': esp_files,
        'partitions': {
            'esp': {'partlabel': 'sunuefi_esp', 'label': 'SUNUEFI_ESP', 'sector_bytes': 4096, 'capacity_bytes': esp_img.stat().st_size},
            'root': {'partlabel': 'sunuefi_root', 'label': 'PIANOROOT', 'capacity_bytes': actual_root_mib * MIB},
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

    sparse_out = bundle_out / 'sparse'
    sparse_meta = package_sparse_images.package(bundle_out, sparse_out)
    print(json.dumps({
        'status': bundle_manifest['status'],
        'kernel_release': bundle_manifest['kernel_release'],
        'raw': bundle_manifest['files'],
        'sparse': sparse_meta['files'],
        'sparse_dir': str(sparse_out),
    }, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
