#!/usr/bin/env python3
"""Apply a checked runtime package or restore its firmware and unload modules.

apply PACKAGE NEW_STATE_DIRECTORY
rollback STATE_DIRECTORY
check PACKAGE
No boot writes, reboot, automatic service registration or forced module unload.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

MODULES = ['meson8b_runtime', 'meson_vdec', 'meson_venc']
MODULE_FILES = ['meson8b-runtime.ko', 'meson-vdec.ko', 'meson-venc.ko']
PROVIDERS = ['clkc', 'canvas', 'pmu', 'reset']
FIRMWARE_ROOT = Path('/lib/firmware')
OVERLAY_FILE = 'meson/vdec/meson8b-runtime.dtbo'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_package(package):
    """Manifest covers modules, firmware, scripts, tools and provenance."""
    files = {}
    for line in (package / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        relative = Path(name)
        if (relative.is_absolute() or '..' in relative.parts or
                relative.parts[0] not in ('modules', 'firmware', 'scripts', 'tools', 'patches', 'config', 'systemd', 'docs', 'README.md', 'README.zh-CN.md', 'CHANGELOG.md', 'LICENSE', 'KERNEL_RELEASE', 'SOURCE_COMMIT', 'FFMPEG_BUILD_VARIANT', 'TARGET_PROVIDERS.json') or name in files):
            raise ValueError('invalid package path: ' + name)
        path = package / relative
        if any(p.is_symlink() for p in [path, *path.parents]) or not path.is_file():
            raise ValueError('package payload must be regular files: ' + name)
        if sha(path) != digest:
            raise ValueError('package checksum mismatch: ' + name)
        files[name] = digest
    actual = {str(p.relative_to(package)) for p in package.rglob('*')
              if p.is_file() and p.name != 'SHA256SUMS'}
    if actual != files.keys() or {n for n in files if n.startswith('modules/')} != {
            'modules/' + name for name in MODULE_FILES}:
        raise ValueError('package manifest does not cover exactly its payload')
    expected_fw = {
        'firmware/meson/vdec/meson8b-runtime.dtbo',
        'firmware/meson/vdec/meson8b_h264.bin',
        'firmware/meson/vdec/meson8b_hevc.bin',
        'firmware/meson/vdec/meson8b_mpeg12.bin',
        'firmware/meson/vdec/meson8b_mpeg4.bin',
        'firmware/meson/vdec/meson8b_vc1.bin',
        'firmware/meson/vdec/meson8b_mjpeg.bin',
        'firmware/meson/venc/meson8b_avc_enc.bin',
        'firmware/meson/venc/meson8b_jpeg_enc.bin',
    }
    firmware_files = {n for n in files if n.startswith('firmware/')}
    # Older packages predate H.263; both inventories remain valid for rollback.
    if firmware_files not in (expected_fw, expected_fw | {
            'firmware/meson/vdec/meson8b_h263.bin'}):
        raise ValueError('unexpected firmware payload')
    return files


def check_target(package):
    if os.geteuid():
        raise ValueError('run as root on the target')
    if os.uname().release != (package / 'KERNEL_RELEASE').read_text().strip():
        raise ValueError('kernel version does not match the package')
    tree = Path('/sys/firmware/devicetree/base')
    board = (tree / 'compatible').read_bytes().split(b'\0')
    if b'xunlei,onecloud' not in board or b'amlogic,meson8b' not in board:
        raise ValueError('package requires OneCloud Meson8b')
    expected = json.loads((package / 'TARGET_PROVIDERS.json').read_text())
    if set(expected) != set(PROVIDERS):
        raise ValueError('invalid overlay provider manifest')
    for provider in PROVIDERS:
        nodes = [p.parent for p in tree.rglob('compatible') if
                 ('amlogic,meson8b-' + provider).encode() in p.read_bytes().split(b'\0')]
        if len(nodes) != 1 or int.from_bytes((nodes[0] / 'phandle').read_bytes(), 'big') != expected[provider]:
            raise ValueError('overlay is for different provider phandles: ' + provider)


def loaded():
    return {line.split()[0] for line in Path('/proc/modules').read_text().splitlines()}


def save_state(state, data):
    temporary = state / 'state.json.tmp'
    temporary.write_text(json.dumps(data, indent=2) + '\n')
    temporary.replace(state / 'state.json')


def stage_firmware(package, state, files, firmware_root=Path('/lib/firmware')):
    firmware_root = firmware_root.resolve()
    data = {'version': 1, 'firmware_root': str(firmware_root), 'files': [], 'status': 'staging'}
    save_state(state, data)
    for name, digest in files.items():
        if not name.startswith('firmware/'):
            continue
        relative = Path(name).relative_to('firmware')
        target = firmware_root / relative
        backup = state / 'original' / relative
        if any(p.is_symlink() for p in [target, *target.parents]):
            raise ValueError('refusing symlink in firmware destination: ' + str(target))
        if target.exists() and not target.is_file():
            raise ValueError('firmware destination is not a file: ' + str(target))
        original = None
        if target.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, backup)
            original = sha(backup)
        # Journal before each copy permits recovery from an interrupted apply.
        data['files'].append({'path': str(relative), 'original': original, 'installed': digest})
        save_state(state, data)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(package / name, target)
    data['status'] = 'staged'
    save_state(state, data)
    return data


def restore_firmware(state, firmware_root=Path('/lib/firmware')):
    firmware_root = firmware_root.resolve()
    data = json.loads((state / 'state.json').read_text())
    if data.get('version') != 1 or data.get('firmware_root') != str(firmware_root):
        raise ValueError('state directory does not match firmware root')
    for entry in data['files']:
        relative = Path(entry['path'])
        if relative.is_absolute() or '..' in relative.parts or relative.parts[:1] != ('meson',):
            raise ValueError('invalid rollback path')
        target = firmware_root / relative
        if any(p.is_symlink() for p in [target, *target.parents]):
            raise ValueError('refusing symlink during rollback')
        # Refuse to overwrite a later legitimate firmware update.
        if target.exists() and sha(target) not in (entry['installed'], entry['original']):
            raise ValueError('firmware changed since apply: ' + str(target))
        if entry['original'] is not None:
            backup = state / 'original' / relative
            if backup.is_symlink() or sha(backup) != entry['original']:
                raise ValueError('original backup checksum mismatch: ' + str(backup))
    for entry in reversed(data['files']):
        relative = Path(entry['path'])
        target = firmware_root / relative
        if entry['original'] is not None:
            shutil.copy2(state / 'original' / relative, target)
        elif target.exists():
            target.unlink()
    data['status'] = 'rolled_back'
    save_state(state, data)


def unload():
    for module in reversed(MODULES):
        if module in loaded():
            subprocess.run(['rmmod', module], check=True)


def load_package(package, temporary_overlay=False):
    files = checked_package(package)
    check_target(package)
    for name, digest in files.items():
        if name.startswith('firmware/'):
            if temporary_overlay and name == 'firmware/' + OVERLAY_FILE:
                continue
            deployed = FIRMWARE_ROOT / Path(name).relative_to('firmware')
            if not deployed.is_file() or sha(deployed) != digest:
                raise ValueError('deployed firmware differs from checked package: ' + name)
    if set(MODULES) & loaded():
        raise ValueError('codec modules already loaded')
    started = []
    overlay_directory = None
    try:
        overlay_arguments = []
        if temporary_overlay:
            # A reboot must still load the installed overlay after a failed test.
            overlay_directory = tempfile.TemporaryDirectory(
                prefix='meson8b-runtime-', dir=FIRMWARE_ROOT / 'meson/vdec')
            overlay = Path(overlay_directory.name) / 'overlay.dtbo'
            shutil.copyfile(package / 'firmware' / OVERLAY_FILE, overlay)
            overlay_arguments = ['overlay_file=' + str(overlay.relative_to(FIRMWARE_ROOT))]
        for dependency in ('v4l2_mem2mem', 'videobuf2_dma_contig', 'videobuf2_vmalloc'):
            subprocess.run(['modprobe', dependency], check=True)
        for name, module in zip(MODULE_FILES, MODULES):
            arguments = overlay_arguments if module == 'meson8b_runtime' else []
            subprocess.run(['insmod', str(package / 'modules' / name), *arguments], check=True)
            started.append(module)
        # insmod may succeed even when a platform probe failed.
        names = {p.read_text().strip() for p in Path('/sys/class/video4linux').glob('*/name')}
        if not {'meson-video-decoder', 'meson-video-encoder'} <= names:
            raise ValueError('both codec devices must probe successfully')
    except Exception:
        for module in reversed(started):
            if module in loaded():
                subprocess.run(['rmmod', module], check=True)
        raise
    finally:
        if overlay_directory is not None:
            overlay_directory.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for command in ('apply', 'check', 'load'):
        p = sub.add_parser(command)
        p.add_argument('package', type=Path)
        if command == 'load':
            p.add_argument('--temporary-overlay', action='store_true',
                           help='load the checked overlay without replacing the installed file; '
                                'requires a runtime module with overlay_file support')
        if command == 'apply':
            p.add_argument('state', type=Path)
    sub.add_parser('rollback').add_argument('state', type=Path)
    sub.add_parser('unload')
    args = parser.parse_args()
    if args.command == 'unload':
        if os.geteuid():
            raise ValueError('run as root')
        unload()
        return
    if args.command == 'load':
        load_package(args.package.resolve(), args.temporary_overlay)
        return
    if args.command == 'rollback':
        if os.geteuid():
            raise ValueError('run as root')
        unload()
        restore_firmware(args.state.resolve())
        print('Modules removed, original firmware restored; state backup retained.')
        return
    package = args.package.resolve()
    files = checked_package(package)
    check_target(package)
    if args.command == 'check':
        print('Package checksums, kernel, board and overlay providers match.')
        return
    if set(MODULES) & loaded():
        raise ValueError('codec modules already loaded; unload them before apply')
    args.state.mkdir(parents=False, exist_ok=False)
    state = args.state.resolve()
    try:
        stage_firmware(package, state, files)
        load_package(package)
        data = json.loads((state / 'state.json').read_text())
        data['status'] = 'loaded'
        save_state(state, data)
    except Exception:
        unload()
        if (state / 'state.json').exists():
            restore_firmware(state)
        raise
    print('Codec devices loaded. Roll back with:', Path(__file__).resolve(), 'rollback', state)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
