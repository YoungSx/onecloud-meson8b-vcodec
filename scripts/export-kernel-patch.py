#!/usr/bin/env python3
"""Export runtime codecs as one additive Linux 5.15.79 Kbuild patch.

The source must be the matching OneCloud baseline, without the old research
staging-VDEC patch. Runtime overlays remain the deployment mechanism.
"""
import argparse
import difflib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGET = Path('drivers/media/platform/meson/meson8b-vcodec')


def diff(path, before, after):
    return ''.join(difflib.unified_diff(
        before.splitlines(keepends=True), after.splitlines(keepends=True),
        fromfile='a/' + str(path) if before else '/dev/null',
        tofile='b/' + str(path), n=3))


def export(source):
    if (source / 'Makefile').read_text().split('SUBLEVEL = ', 1)[1].splitlines()[0] != '79':
        raise ValueError('expected Linux 5.15.79 source')
    if (source / TARGET).exists():
        raise ValueError('driver directory already exists')
    result = []
    kconfig = Path('drivers/media/platform/Kconfig')
    before = (source / kconfig).read_text()
    result.append(diff(kconfig, before, before + '''
config VIDEO_MESON8B_VCODEC
	tristate "OneCloud Meson8b runtime video codecs"
	depends on ARCH_MESON && OF_OVERLAY && VIDEO_V4L2 && m
	select MESON_CANVAS
	select V4L2_MEM2MEM_DEV
	select VIDEOBUF2_DMA_CONTIG
	select VIDEOBUF2_VMALLOC
	help
	  Runtime overlay, H.264/JPEG encoder and stateful video decoders for
	  OneCloud Meson8b. This target-specific port is module-only and needs
	  vendor firmware plus an overlay generated from the live device tree.
'''))
    makefile = Path('drivers/media/platform/Makefile')
    before = (source / makefile).read_text()
    result.append(diff(makefile, before, before + '\nobj-$(CONFIG_VIDEO_MESON8B_VCODEC) += meson/meson8b-vcodec/\n'))
    result.append(diff(TARGET / 'Makefile', '', '# SPDX-License-Identifier: GPL-2.0\nobj-m += runtime/ vdec/ venc/ ge2d/\n'))
    for directory in ('runtime', 'vdec', 'venc', 'ge2d'):
        for path in sorted((ROOT / directory).iterdir()):
            if path.name == 'Makefile' or path.suffix in ('.c', '.h') and '.mod.' not in path.name:
                result.append(diff(TARGET / directory / path.name, '', path.read_text()))
    return ''.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with args.output.open('x') as output:
        output.write(export(args.source))
    print(args.output)


if __name__ == '__main__':
    main()
