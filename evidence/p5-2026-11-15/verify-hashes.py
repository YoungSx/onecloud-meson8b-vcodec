#!/usr/bin/env python3
"""Compare actual frame hashes; empty or partial output never passes."""
import argparse
from pathlib import Path


def hashes(path):
    return [line.rsplit(',', 1)[-1].strip() for line in path.read_text().splitlines()
            if line and not line.startswith('#')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('software', type=Path)
    parser.add_argument('hardware', type=Path)
    args = parser.parse_args()
    reference, output = hashes(args.software), hashes(args.hardware)
    if not reference or len(reference) != len(output) or reference != output:
        raise SystemExit(f'FAIL: software {len(reference)} frames, hardware {len(output)} frames')
    print(f'PASS: {len(output)}/{len(reference)} frame hashes match')


if __name__ == '__main__':
    main()
