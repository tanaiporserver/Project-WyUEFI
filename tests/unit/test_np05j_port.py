"""ชุดทดสอบโฮสต์แบบ Portable สำหรับพอร์ต nubia NP05J (REDMAGIC Astra)"""
import ast
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))

import np05j_display_mapping
import np05j_inherited_clock
import np05j_vendor_inputs
import prepare_product
import prepare_product_console
import product_contract


class Np05jPortTests(unittest.TestCase):
    def test_product_contract_accepts_np05j_and_piano(self):
        np05j = json.loads((ROOT / 'config/np05j-product.json').read_text())
        validated = product_contract.validate(np05j)
        self.assertEqual(validated['artifact'], 'Np05jUEFI-product.img')
        self.assertEqual(validated['device']['codename'], 'nubia_np05j')
        self.assertIs(validated['features']['pogo_keyboard_touchpad'], False)

        piano = json.loads((ROOT / 'config/piano-product.json').read_text())
        self.assertEqual(product_contract.validate(piano)['artifact'], 'PianoUEFI-product.img')

    def test_vendor_inputs_and_clock_derivation(self):
        bundle = np05j_vendor_inputs.load(ROOT)
        self.assertEqual(len(bundle['native_drivers']), 16)
        self.assertEqual(len(bundle['paths']), 34)
        record = np05j_vendor_inputs.record(ROOT, bundle)
        self.assertEqual(
            record['manifest_sha256'],
            '39e36ce24f7ca1a98dc5b305c62f13e419bc29593d6407ed7dff6988a44ae70c',
        )
        self.assertIs(record['runtime_write_authorized'], False)

        clock_raw = bundle['native_drivers']['ClockDxe']['pe_path'].read_bytes()
        derived, meta = np05j_inherited_clock.derive(clock_raw)
        self.assertEqual(
            meta['derived_sha256'],
            '3b76bda865353e35fdaad3b29f5948bd2c7d74a98ca13bcd01b5ab1f130bb7a3',
        )
        self.assertEqual(meta['patch_rva'], '0xc270')
        self.assertEqual(meta['dcd_startup_control_rva'], '0x3a4e0')
        self.assertIs(meta['dcd_startup_dependencies_deferred'], True)
        self.assertEqual(meta['pe_checksum'], '0x0004e97a')
        self.assertNotEqual(clock_raw, derived)

    def test_low_heap_and_display_mmio_mapping_round_trip(self):
        bundle = np05j_vendor_inputs.load(ROOT)
        base_map, low_contract = prepare_product.fix_np05j_low_heap(bundle['board_dtb'])
        self.assertEqual(low_contract['named_hob_arena'], {'base': 0xB90C0000, 'end': 0xD4E23000})
        self.assertEqual(low_contract['upper_resource'], {'base': 0xD5100000, 'end': 0xD8000000})
        self.assertEqual(low_contract['ramoops'], {'base': 0xE4A00000, 'end': 0xE4C00000})

        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            (tmp / 'tools').mkdir(parents=True)
            shutil.copyfile(ROOT / 'tools/np05j_display_mapping.py', tmp / 'tools/np05j_display_mapping.py')
            shutil.copyfile(ROOT / 'tools/np05j_vendor_inputs.py', tmp / 'tools/np05j_vendor_inputs.py')
            shutil.copytree(ROOT / 'vendor/np05j', tmp / 'vendor/np05j')

            derived_clock, _ = np05j_inherited_clock.derive(
                bundle['native_drivers']['ClockDxe']['pe_path'].read_bytes()
            )
            clock_dst = tmp / np05j_display_mapping.CLOCK_PE
            clock_dst.parent.mkdir(parents=True, exist_ok=True)
            clock_dst.write_bytes(derived_clock)

            expanded, record = np05j_display_mapping.prepare(tmp, base_map)
            self.assertEqual(record['original_rows'], 48)
            self.assertEqual(record['candidate_rows'], 52)
            self.assertEqual(len(record['windows']), 4)
            self.assertTrue(np05j_display_mapping.verify(tmp, expanded, record))
            self.assertEqual(np05j_display_mapping.recover_original(expanded), base_map)

    def test_console_profile_and_product_components(self):
        self.assertEqual(
            prepare_product_console.CONSOLE_PROFILES['np05j'],
            (3, [2400, 1504], [800, 501]),
        )
        components = prepare_product.product_components('np05j')
        self.assertIn('touch-np05j', components)
        self.assertNotIn('pogo-product', components)
        self.assertNotIn('touch-product', components)

    def test_patch_series_manifests_exist_and_match_sha256(self):
        fw_dir = ROOT / 'patches/firmware/np05j'
        fw = json.loads((fw_dir / 'series.json').read_text())
        self.assertEqual(fw.get('targets'), ['uefi'])
        for row in fw['patches']:
            p = fw_dir / row['patch']
            self.assertTrue(p.is_file(), row['patch'])
            self.assertEqual(np05j_vendor_inputs.sha(p.read_bytes()), row['sha256'])

        lx_dir = ROOT / 'patches/linux/7.3/np05j'
        lx = json.loads((lx_dir / 'series.json').read_text())
        self.assertEqual(lx.get('targets'), ['linux'])
        for row in lx['patches']:
            p = lx_dir / row['file']
            self.assertTrue(p.is_file(), row['file'])
            self.assertEqual(np05j_vendor_inputs.sha(p.read_bytes()), row['sha256'])

    def test_product_packaging_and_np05j_guards(self):
        # ตรวจสอบว่า Phase 2 ใช้ Boot Header v3 เป็นค่าเริ่มต้นเพื่อให้ ABL โหลด initrd/app-payload.bin
        pkg_src = (ROOT / 'tools/package_product.py').read_text()
        self.assertNotIn('4 if is_np05j else 3', pkg_src)
        tree = ast.parse(pkg_src)
        header_defaults = [
            kw.value.value
            for node in ast.walk(tree)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr == 'add_argument'
            and any(isinstance(a, ast.Constant) and a.value == '--header-version' for a in node.args)
            for kw in node.keywords
            if kw.arg == 'default' and isinstance(kw.value, ast.Constant)
        ]
        self.assertEqual(header_defaults, [3])

        # ตรวจสอบว่า PianoRamPartition.c ผูก SHA-256 ของ EnvDxeEnhanced.efi ประจำ NP05J ตรงกับไบนารีจริง
        ram_src = (ROOT / 'uefi/core/memory/PianoRamPartition.c').read_text()
        match = re.search(
            r'#ifdef SUNUEFI_NP05J.*?STATIC CONST UINT8 mHash\[\]\s*=\s*\{([^}]+)\};',
            ram_src,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        declared_sha = ''.join(f'{int(tok, 16):02x}' for tok in re.findall(r'0x[0-9a-fA-F]+', match.group(1)))
        actual_sha = np05j_vendor_inputs.sha(
            (ROOT / 'vendor/np05j/early-dxe/EnvDxeEnhanced/EnvDxeEnhanced.efi').read_bytes()
        )
        self.assertEqual(declared_sha, actual_sha)

        # ตรวจสอบว่า PianoProductCore.c ข้าม legacy Piano LUN4 volume เฉพาะบน NP05J และลงทะเบียน PianoExt4Register
        core_src = (ROOT / 'uefi/core/product/PianoProductCore.c').read_text()
        self.assertRegex(
            core_src,
            r'#ifndef SUNUEFI_NP05J\s+STATIC PIANO_UFS_PRODUCT_VOLUME mProductVolume;\s+#endif',
        )
        self.assertRegex(
            core_src,
            r'#ifndef SUNUEFI_NP05J[\s\S]*?PianoUfsProductVolumeOpen[\s\S]*?#else[\s\S]*?#endif',
        )
        self.assertIn('PianoExt4Register(Image,SystemTable)', core_src)
        # ตรวจสอบการนำ upstream UEFI hardening มาปรับใช้ครบ (PianoExt4Lib, BootNext non-looping, ResetSystem guard)
        ext4_prep = (ROOT / 'tools/firmware/prepare_ext4.py').read_text()
        self.assertIn('Library/PianoExt4Lib/PianoExt4Lib.inf', ext4_prep)
        self.assertNotIn('PianoExt4Dxe', ext4_prep)
        nv_src = (ROOT / 'uefi/components/nv/PianoNvFile.c').read_text()
        self.assertIn('PianoNvFileCanConsumeBootNext', nv_src)
        boot_opt_src = (ROOT / 'uefi/core/boot/PianoBootOptions.c').read_text()
        self.assertIn('PianoNvFileCanConsumeBootNext', boot_opt_src)
        basecore_patch = (ROOT / 'patches/firmware/mu-basecore.patch').read_text()
        self.assertIn('Piano reset refused before ExitBootServices', basecore_patch)

    def test_np05j_english_ui_touch_resync_and_ramlog_init(self):
        # ตรวจสอบคอนฟิกเมนูภาษาอังกฤษสำหรับ NP05J (ไม่มีอักษรจีนในค่าคอนฟิกที่ใช้งาน)
        cfg_path = ROOT / 'config/simpleinit.np05j.uefi.cfg'
        self.assertTrue(cfg_path.is_file())
        cfg_lines = [
            line for line in cfg_path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith('#')
        ]
        cfg_body = '\n'.join(cfg_lines)
        self.assertIn('language = "en_US.UTF-8"', cfg_body)
        self.assertIn('gui.dark = true', cfg_body)
        self.assertIn('boot.timeout = -1', cfg_body)
        for icon_ref in (
            'icon = "@app-boot-linux"',
            'icon = "@app-bootmgr"',
            'icon = "@app-config-manager"',
            'icon = "@app-uefi-shell"',
            'icon = "@app-file-manager"',
        ):
            self.assertIn(icon_ref, cfg_body)
        self.assertFalse(any('\u4e00' <= ch <= '\u9fff' for ch in cfg_body))

        # ตรวจสอบว่า Np05jTouchService.c รอ turnaround และซิงก์ TCM2_STATUS_INVALID เพื่อกันทัชหลุด
        touch_src = (ROOT / 'uefi/components/touch-np05j/Np05jTouchService.c').read_text()
        self.assertIn('Tcm2GetReportRuntime', touch_src)
        self.assertIn('TCM2_STATUS_INVALID', touch_src)
        self.assertIn('MicroSecondDelay (Poll == 0U ? 400U : 600U)', touch_src)

        # ตรวจสอบว่า RamLog.c ตั้งค่า header DBGC บน NP05J เมื่อยังไม่เคยถูกสร้างมาก่อน
        for rel in (
            'uefi/components/product-support/Library/RamOnlySerialPortLib/RamLog.c',
            'uefi/handoff/bootshim/RamLog.c',
        ):
            ramlog_src = (ROOT / rel).read_text()
            self.assertRegex(
                ramlog_src,
                r'#ifdef SUNUEFI_NP05J[\s\S]*?Ring->Signature\s*=\s*0x43474244;',
                msg=rel,
            )

    def test_np05j_performance_optimizations(self):
        # ตรวจสอบ flag คอมไพล์ -O3 Oryon, BaseMemoryLibOptDxe และการ subset ฟอนต์สำหรับ NP05J
        prep_si = (ROOT / 'tools/prepare_simpleinit.py').read_text()
        self.assertIn('-DSUNUEFI_NP05J=1 -O3 -march=armv8.7-a+crypto -mtune=oryon-1 -fno-strict-aliasing', prep_si)
        self.assertIn('BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf', prep_si)
        self.assertIn('NotoSansNP05J-Subset.otf', prep_si)

        # ตรวจสอบ BaseMemoryLibOptDxe และ Oryon -O2 ใน prepare_product.py เฉพาะ NP05J
        prep_prod = (ROOT / 'tools/prepare_product.py').read_text()
        self.assertIn('BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf', prep_prod)
        self.assertIn('-O2 -march=armv8.7-a+crypto -mtune=oryon-1', prep_prod)

        # ตรวจสอบจูน LVGL/FreeType/Animation/Assert/Trace สำหรับ NP05J ใน prepare_product_ui.py
        prep_ui = (ROOT / 'tools/prepare_product_ui.py').read_text()
        for snippet in (
            'lv_freetype_init(64,16,4*1024*1024);',
            '#define LV_INDEV_DEF_READ_PERIOD            8',
            'static time_t cache_time=3600;',
            'if(t-c->last>cache_time){',
            '#define LV_CIRCLE_CACHE_SIZE                16',
            '#define LV_MEMCPY_MEMSET_STD                1',
            '#define LV_COLOR_SCREEN_TRANSP              0',
            '#define LV_USE_ASSERT_NULL                  0',
            'lv_anim_set_time(&bm->anim_exec,180);',
            'lv_anim_set_path_cb(&bm->anim_exec,lv_anim_path_ease_out);',
            'lv_anim_set_time(&color_change,80);',
            'lv_anim_set_time(&move_out_img,100);',
        ):
            self.assertIn(snippet, prep_ui)
        self.assertNotIn('#define LV_IMG_CACHE_DEF_SIZE               32', prep_ui)

        # ตรวจสอบ deadline-accurate wait 4ms + slice 500us ใน piano_product_runtime.c ภายใต้ SUNUEFI_NP05J
        runtime_c = (ROOT / 'uefi/components/product-pump/simple-init/src/gui/piano_product_runtime.c').read_text()
        self.assertRegex(runtime_c, r'#ifdef SUNUEFI_NP05J[\s\S]*?if\(milliseconds>4\)milliseconds=4;')
        self.assertIn('gBS->Stall(500)', runtime_c)

        # ตรวจสอบ fast-path GOP Blt ครบทั้ง 4 โหมด และ single-DSB cache clean ใน SimpleFb.c
        simple_fb = (ROOT / 'uefi/components/product-support/Drivers/PianoGopDxe/SimpleFb.c').read_text()
        for snippet in (
            'dc cvac, %0',
            'dsb sy',
            'BltOperation==EfiBltVideoToVideo && mFramePixelBytes==sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL)',
            'BltOperation==EfiBltVideoFill',
            'BltOperation==EfiBltBufferToVideo',
            'BltOperation==EfiBltVideoToBltBuffer',
        ):
            self.assertIn(snippet, simple_fb)

        # ตรวจสอบ RAM-staged single-Blt splash ใน ProductSplash.c สำหรับ NP05J
        splash_c = (ROOT / 'uefi/components/product-support/Library/ProductBootManagerLib/ProductSplash.c').read_text()
        self.assertIn('C->Surface!=NULL', splash_c)
        self.assertIn('EfiBltBufferToVideo', splash_c)

        # ตรวจสอบ reusable scratch buffer และ unrolled 3x/4x scale ใน PianoConsoleScale.h
        console_scale = (ROOT / 'uefi/components/product-support/Library/ConsoleScale/PianoConsoleScale.h').read_text()
        self.assertIn('ScratchPixels', console_scale)
        self.assertIn('if(N==3)', console_scale)

        # ตรวจสอบ SWAR 8-byte scan ใน PianoProductPayload.c และ ARMv8 CRC32 ใน Fastboot Screen/DWC3
        payload_c = (ROOT / 'uefi/core/product/PianoProductPayload.c').read_text()
        self.assertIn('0x8080808080808080ULL', payload_c)
        screen_c = (ROOT / 'uefi/core/usb/PianoFastbootScreen.c').read_text()
        self.assertIn('crc32x', screen_c)
        dwc3_c = (ROOT / 'uefi/core/usb/PianoDwc3Device.c').read_text()
        self.assertIn('crc32x', dwc3_c)
        self.assertIn('UINTN First=MIN(Bytes,Capacity-Offset);', dwc3_c)

        # ตรวจสอบ SPI fast path, header-first read, IDENTIFY offset และคง CS setup 12us ตาม DTS ใน Np05jTouchService.c
        touch_src = (ROOT / 'uefi/components/touch-np05j/Np05jTouchService.c').read_text()
        for snippet in (
            'SpiReadBytes',
            'SpiBitDelay',
            'SpiReadBytes (mT.RxBuf, TCM2_HDR_SIZE)',
            'MicroSecondDelay (12)',
            'mT.PayBuf[2U + K]',
            'Tcm2GetLe32 (mT.PayBuf + 18U)',
        ):
            self.assertIn(snippet, touch_src)

        # ตรวจสอบ Phase 1 Hybrid UEFI (np05jPkg) + Pre-UEFI BootSelect optimizations
        touch_dxe = (ROOT / 'uefi/platforms/np05jPkg/Drivers/Np05jTouchDxe/Np05jTouchDxe.c').read_text()
        self.assertIn('Tcm2ReadReportFast', touch_dxe)
        self.assertIn('SpiBitDelay', touch_dxe)
        np05j_scale = (ROOT / 'uefi/platforms/np05jPkg/Drivers/Np05jGraphicsConsoleDxe/Np05jConsoleScale.h').read_text()
        self.assertIn('ScratchPixels', np05j_scale)
        np05j_screen = (ROOT / 'uefi/platforms/np05jPkg/Drivers/Np05jUsbSerialDxe/Np05jFastbootScreen.c').read_text()
        self.assertIn('crc32x', np05j_screen)
        np05j_dsc = (ROOT / 'uefi/platforms/np05jPkg/nubia_np05j.dsc').read_text()
        self.assertIn('BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf', np05j_dsc)
        self.assertIn('-march=armv8.7-a+crypto -mtune=oryon-1', np05j_dsc)
        early_touch = (ROOT / 'uefi/handoff/bootselect-np05j/EarlyTouch.c').read_text()
        self.assertIn('for (uint32_t K = 0U; K < TCM2_HDR_SIZE; K++)', early_touch)
        early_splash = (ROOT / 'uefi/handoff/bootselect-np05j/EarlySplash.c').read_text()
        self.assertIn('uint64_t ColorPair', early_splash)

        # ตรวจสอบ StartupKeysMs = 200ms ภายใต้ SUNUEFI_NP05J
        core_src = (ROOT / 'uefi/core/product/PianoProductCore.c').read_text()
        self.assertRegex(core_src, r'#ifdef SUNUEFI_NP05J\s+CONST UINTN StartupKeysMs=200;')

        # ตรวจสอบ Linux 7.3 performance config สำหรับ NP05J (Oryon O3 + ThinLTO + BORE + MGLRU + HZ_1000 + ZSTD ZRAM + BBR/FQ)
        np05j_frag = (ROOT / 'linux/configs/np05j/np05j.frag').read_text()
        for opt in (
            'CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE_O3=y',
            'CONFIG_ARM64_PIANO_OPTIMIZE=y',
            'CONFIG_LTO_CLANG_THIN=y',
            'CONFIG_SCHED_BORE=y',
            'CONFIG_LRU_GEN_ENABLED=y',
            'CONFIG_HZ_1000=y',
            'CONFIG_IO_URING=y',
            'CONFIG_ZRAM=y',
            'CONFIG_ZRAM_BACKEND_ZSTD=y',
            'CONFIG_ZRAM_DEF_COMP_ZSTD=y',
        ):
            self.assertIn(opt, np05j_frag)
        phase2_frag = (ROOT / 'linux/configs/np05j/phase2.frag').read_text()
        for opt in ('CONFIG_TCP_CONG_BBR=y', 'CONFIG_DEFAULT_TCP_CONG="bbr"', 'CONFIG_DEFAULT_FQ=y'):
            self.assertIn(opt, phase2_frag)
        desktop_frag = (ROOT / 'linux/configs/np05j/desktop.frag').read_text()
        for opt in ('CONFIG_DRM_SIMPLEDRM=y', 'CONFIG_SYSFB_SIMPLEFB=y', 'CONFIG_DRM_FBDEV_EMULATION=y', 'CONFIG_IPV6=y'):
            self.assertIn(opt, desktop_frag)
        common_dtsi = (ROOT / 'linux/dts/np05j/sm8750-nubia-np05j-common.dtsi').read_text()
        self.assertIn('chosen { };', common_dtsi)
        self.assertTrue((ROOT / 'tools/build/np05j_ubuntu.py').is_file())

    def test_native_np05j_port_c_suite(self):
        res = subprocess.run(
            ['bash', str(ROOT / 'tests/native/test_np05j_port.sh')],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self.assertEqual(res.returncode, 0, msg=res.stdout)


if __name__ == '__main__':
    unittest.main()
