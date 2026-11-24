import importlib.util
import pathlib
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('runtime_device', ROOT / 'scripts/runtime-device.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class RuntimeDeviceTests(unittest.TestCase):
    def test_temporary_overlay_preserves_installed_file_and_cleans_up_on_failure(self):
        for fail in (False, True):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                package, firmware = root / 'package', root / 'firmware'
                relative = pathlib.Path(runtime.OVERLAY_FILE)
                candidate = package / 'firmware' / relative
                deployed = firmware / relative
                candidate.parent.mkdir(parents=True)
                deployed.parent.mkdir(parents=True)
                candidate.write_bytes(b'candidate overlay')
                deployed.write_bytes(b'installed overlay')
                codec = 'meson/vdec/meson8b_h264.bin'
                (firmware / codec).write_bytes(b'installed microcode')
                files = {'firmware/' + str(relative): runtime.sha(candidate),
                         'firmware/' + codec: runtime.sha(firmware / codec)}
                modules = set()
                overlay_paths = []

                def run(args, check):
                    self.assertTrue(check)
                    self.assertEqual(deployed.read_bytes(), b'installed overlay')
                    if args[0] == 'insmod':
                        name = runtime.MODULES[runtime.MODULE_FILES.index(pathlib.Path(args[1]).name)]
                        if name == 'meson8b_runtime':
                            self.assertEqual(len(args), 3)
                            self.assertTrue(args[2].startswith('overlay_file='))
                            overlay = firmware / args[2].split('=', 1)[1]
                            overlay_paths.append(overlay)
                            self.assertEqual(overlay.read_bytes(), b'candidate overlay')
                        elif fail and name == 'meson_venc':
                            raise runtime.subprocess.CalledProcessError(1, args)
                        modules.add(name)
                    elif args[0] == 'rmmod':
                        modules.remove(args[1])

                nodes = [Mock(), Mock()]
                nodes[0].read_text.return_value = 'meson-video-decoder'
                nodes[1].read_text.return_value = 'meson-video-encoder'
                with patch.object(runtime, 'FIRMWARE_ROOT', firmware), \
                        patch.object(runtime, 'checked_package', return_value=files), \
                        patch.object(runtime, 'check_target'), \
                        patch.object(runtime, 'loaded', side_effect=lambda: modules.copy()), \
                        patch.object(runtime.subprocess, 'run', side_effect=run), \
                        patch.object(runtime.Path, 'glob', return_value=nodes):
                    if fail:
                        with self.assertRaises(runtime.subprocess.CalledProcessError):
                            runtime.load_package(package, temporary_overlay=True)
                        self.assertFalse(modules)
                    else:
                        runtime.load_package(package, temporary_overlay=True)
                        self.assertEqual(modules, set(runtime.MODULES))
                self.assertEqual(len(overlay_paths), 1)
                self.assertFalse(overlay_paths[0].exists())
                self.assertFalse(overlay_paths[0].parent.exists())
                self.assertEqual(deployed.read_bytes(), b'installed overlay')
                self.assertEqual((firmware / codec).read_bytes(), b'installed microcode')

    def test_bilingual_docs_are_checked_and_omissions_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            names = ['modules/' + name for name in runtime.MODULE_FILES]
            names += ['README.md', 'README.zh-CN.md', 'CHANGELOG.md', 'FFMPEG_BUILD_VARIANT', 'docs/installation.md']
            names += ['firmware/meson/vdec/' + name for name in (
                'meson8b-runtime.dtbo', 'meson8b_h264.bin', 'meson8b_hevc.bin',
                'meson8b_mpeg12.bin', 'meson8b_mpeg4.bin', 'meson8b_vc1.bin', 'meson8b_mjpeg.bin')]
            names += ['firmware/meson/venc/meson8b_avc_enc.bin',
                      'firmware/meson/venc/meson8b_jpeg_enc.bin']
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'test payload')
            (root / 'SHA256SUMS').write_text(''.join(
                f'{runtime.sha(root / name)}  {name}\n' for name in names))
            self.assertEqual(len(runtime.checked_package(root)), len(names))
            h263 = 'firmware/meson/vdec/meson8b_h263.bin'
            (root / h263).write_bytes(b'H.263 firmware')
            with (root / 'SHA256SUMS').open('a') as manifest:
                manifest.write(runtime.sha(root / h263) + '  ' + h263 + '\n')
            self.assertEqual(len(runtime.checked_package(root)), len(names) + 1)
            extra = 'firmware/meson/vdec/unexpected.bin'
            (root / extra).write_bytes(b'unexpected firmware')
            with (root / 'SHA256SUMS').open('a') as manifest:
                manifest.write(runtime.sha(root / extra) + '  ' + extra + '\n')
            with self.assertRaisesRegex(ValueError, 'unexpected firmware'):
                runtime.checked_package(root)
            (root / 'docs/installation.md').write_bytes(b'tampered guide')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                runtime.checked_package(root)

    def test_restore_replaced_and_new_firmware_after_interrupted_apply(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            package, state, firmware = [root / name for name in ('package', 'state', 'firmware')]
            state.mkdir()
            firmware.mkdir()
            entries = {}
            for name in ('meson/vdec/old.bin', 'meson/venc/new.bin'):
                path = package / 'firmware' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'new firmware')
                entries['firmware/' + name] = runtime.sha(path)
            original = firmware / 'meson/vdec/old.bin'
            original.parent.mkdir(parents=True)
            original.write_bytes(b'original firmware')
            runtime.stage_firmware(package, state, entries, firmware)
            self.assertEqual(original.read_bytes(), b'new firmware')
            runtime.restore_firmware(state, firmware)
            self.assertEqual(original.read_bytes(), b'original firmware')
            self.assertFalse((firmware / 'meson/venc/new.bin').exists())
            runtime.restore_firmware(state, firmware)  # rollback is repeatable
            self.assertEqual(original.read_bytes(), b'original firmware')

    def test_refuse_later_change_or_symlink_without_overwriting_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            package, state, firmware = [root / name for name in ('package', 'state', 'firmware')]
            state.mkdir()
            firmware.mkdir()
            relative = 'meson/vdec/old.bin'
            payload = package / 'firmware' / relative
            payload.parent.mkdir(parents=True)
            payload.write_bytes(b'candidate')
            target = firmware / relative
            target.parent.mkdir(parents=True)
            target.write_bytes(b'original')
            runtime.stage_firmware(package, state, {'firmware/' + relative: runtime.sha(payload)}, firmware)
            target.write_bytes(b'legitimate later update')
            with self.assertRaises(ValueError):
                runtime.restore_firmware(state, firmware)
            self.assertEqual(target.read_bytes(), b'legitimate later update')
            target.unlink()
            target.symlink_to(payload)
            with self.assertRaises(ValueError):
                runtime.restore_firmware(state, firmware)
            self.assertEqual(payload.read_bytes(), b'candidate')

    def test_reject_manifest_path_escape_and_corrupt_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            manifest = root / 'SHA256SUMS'
            manifest.write_text('00  ../escape\n')
            with self.assertRaises(ValueError):
                runtime.checked_package(root)
            payload = root / 'modules/example.ko'
            payload.parent.mkdir()
            payload.write_bytes(b'broken module')
            manifest.write_text('00  modules/example.ko\n')
            with self.assertRaises(ValueError):
                runtime.checked_package(root)
