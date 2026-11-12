#!/usr/bin/env python3
"""Build a volatile overlay using provider phandles from the live device tree."""
import argparse
import pathlib
import struct


def strings(path):
    return path.read_bytes().rstrip(b'\0').split(b'\0')


def provider(tree, compatible):
    nodes = [p.parent for p in tree.rglob('compatible') if compatible.encode() in strings(p)]
    if len(nodes) != 1:
        raise ValueError(f'Expected one {compatible} provider, found {len(nodes)}')
    value = (nodes[0] / 'phandle').read_bytes()
    if len(value) != 4:
        raise ValueError(f'Invalid phandle for {compatible}')
    handle = struct.unpack('>I', value)[0]
    if handle == 0 or handle == 0xffffffff:
        raise ValueError('Invalid provider phandle')
    return handle


def render(tree):
    for compat in [b'xunlei,onecloud', b'amlogic,meson8b']:
        if compat not in strings(tree / 'compatible'):
            raise ValueError('This overlay requires an OneCloud Meson8b board')
    for prop in ['#address-cells', '#size-cells']:
        if (tree / 'soc' / prop).read_bytes() != struct.pack('>I', 1):
            raise ValueError('Unexpected SoC address layout')
    if any(b'amlogic,meson8b-vdec' in strings(p) for p in tree.rglob('compatible')):
        raise ValueError('A decoder node already exists')
    clkc = provider(tree, 'amlogic,meson8b-clkc')
    canvas = provider(tree, 'amlogic,meson8b-canvas')
    pmu = provider(tree, 'amlogic,meson8b-pmu')
    reset = provider(tree, 'amlogic,meson8b-reset')
    return f'''/dts-v1/;
/plugin/;
/ {{
    fragment@0 {{
        target-path = "/soc";
        __overlay__ {{
            video-decoder@d0050000 {{
                compatible = "amlogic,meson8b-vdec";
                reg = <0xd0050000 0x10000>, <0xc110a580 0xe4>;
                reg-names = "dos", "esparser";
                interrupts = <0 44 1>, <0 32 1>;
                interrupt-names = "vdec", "esparser";
                clocks = <{clkc} 54>, <{clkc} 17>, <{clkc} 196>;
                clock-names = "dos_parser", "dos", "vdec_1";
                resets = <{reset} 40>;
                reset-names = "esparser";
                amlogic,ao-sysctrl = <{pmu}>;
                amlogic,canvas = <{canvas}>;
                status = "okay";
            }};
        }};
    }};
}};
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=pathlib.Path, default=pathlib.Path('/sys/firmware/devicetree/base'))
    parser.add_argument('--output', type=pathlib.Path, required=True, help='New .dts file; compile externally with dtc -@')
    args = parser.parse_args()
    text = render(args.tree)
    with args.output.open('x') as stream:
        stream.write(text)


if __name__ == '__main__':
    main()
