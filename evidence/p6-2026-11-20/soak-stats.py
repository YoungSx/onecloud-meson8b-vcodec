#!/usr/bin/env python3
"""Summarize a soak.log metric line into the min/p50/p95/max table used in
the deploy records. Reads the box's soak.log (or a copy) on stdin."""
import re
import statistics
import sys

rows = []
pat = re.compile(r'temp=(\d+)C MemFree:=(\d+) MemAvailable:=(\d+) '
                 r'CmaTotal:=(\d+) CmaFree:=(\d+)')
for line in sys.stdin:
    m = pat.search(line)
    if m:
        rows.append([int(x) for x in m.groups()])

if not rows:
    raise SystemExit('no metric lines')

names = ['SoC 温度 (°C)', 'MemFree (MB)', 'MemAvailable (MB)',
         'CmaTotal (MB)', 'CmaFree (MB)']
print('| 指标 | min | p50 | p95 | max | 前 50 轮均值 | 后 50 轮均值 |')
print('| --- | --- | --- | --- | --- | --- | --- |')


def pct(sorted_vals, q):
    return sorted_vals[min(len(sorted_vals) - 1, int(len(sorted_vals) * q))]


for i, name in enumerate(names):
    col = sorted(r[i] for r in rows)
    head = statistics.mean(r[i] for r in rows[:50])
    tail = statistics.mean(r[i] for r in rows[-50:])
    print(f'| {name} | {col[0]} | {pct(col, 0.5)} | {pct(col, 0.95)} | '
          f'{col[-1]} | {head:.1f} | {tail:.1f} |')

print()
print(f'iterations with metrics: {len(rows)}')