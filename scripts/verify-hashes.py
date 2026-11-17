#!/usr/bin/env python3
"""Compare actual frame hashes; empty or partial output never passes.

On mismatch the message says where the outputs diverge: a hardware output
that is a prefix of the reference is a lost tail (the EOS/drain family),
while a first differing frame points inside the stream. Without this, a
300→299 short output discards the only datum that separates the two.
"""
import argparse
from pathlib import Path


def hashes(path):
    return [line.rsplit(',', 1)[-1].strip() for line in path.read_text().splitlines()
            if line and not line.startswith('#')]


def divergence(reference, output):
    for i, (a, b) in enumerate(zip(reference, output)):
        if a != b:
            return f', first differing frame {i}'
    if len(output) < len(reference):
        return ', hardware output is a prefix of the reference'
    return ', reference is a prefix of the hardware output'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('software', type=Path)
    parser.add_argument('hardware', type=Path)
    args = parser.parse_args()
    reference, output = hashes(args.software), hashes(args.hardware)
    if not reference or len(reference) != len(output) or reference != output:
        detail = divergence(reference, output) if reference and output else ''
        raise SystemExit(f'FAIL: software {len(reference)} frames, '
                         f'hardware {len(output)} frames{detail}')
    print(f'PASS: {len(output)}/{len(reference)} frame hashes match')


if __name__ == '__main__':
    main()
