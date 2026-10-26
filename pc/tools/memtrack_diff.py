#!/usr/bin/env python3
"""What grew between two pc_memtrack.c dumps (a -DPC_MEMTRACK=ON build run with PC_MEMTRACK=N).
  memtrack_diff.py FIRST.txt LAST.txt AnimalCrossing.exe [N]   the N stacks that grew most (default 15)
  memtrack_diff.py --unix FIRST.txt LAST.txt [N]               the same for umemtrack.so dumps (umt_*.txt)
"""
import re, subprocess, sys

def load(path):
    head = open(path).readline()
    if head.startswith('# time'):  # umemtrack.so: seconds, frames already named
        base, tick = None, int(re.search(r'time (\d+)', head).group(1)) * 1000
    else:
        base = int(re.search(r'base (\w+)', head).group(1), 16)
        tick = int(re.search(r'tick (\d+)', head).group(1))
    sites = {}
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split()
        sites[tuple(f[3:])] = (int(f[0]), int(f[1]), int(f[2]))
    return base, tick, sites

def symbolize(exe, base, addrs):
    # i686 MinGW exes link at 0x400000; the dump's base is where Windows put it
    vas = ['%x' % (int(a, 16) - base + 0x400000) for a in addrs]
    out = subprocess.run(['i686-w64-mingw32-addr2line', '-f', '-s', '-e', exe] + vas,
                         capture_output=True, text=True).stdout.splitlines()
    return ['%s (%s)' % (out[i * 2], out[i * 2 + 1]) for i in range(len(vas))]

def main():
    if sys.argv[1] == '--unix':
        a, b = sys.argv[2:4]
        exe = None
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 15
    else:
        a, b, exe = sys.argv[1:4]
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 15
    base, t0, s0 = load(a)
    _, t1, s1 = load(b)
    hours = (t1 - t0) / 3600000.0
    rows = []
    for k, (bytes1, blocks1, total1) in s1.items():
        bytes0, blocks0, total0 = s0.get(k, (0, 0, 0))
        rows.append((bytes1 - bytes0, blocks1 - blocks0, total1 - total0, bytes1, k))
    rows.sort(reverse=True)
    grew = sum(r[0] for r in rows)
    print('%.1f min, live heap %+d bytes (%.1f MB/h)' % (hours * 60, grew, grew / 1048576.0 / max(hours, 1e-9)))
    for d, dblk, dtot, live, k in rows[:n]:
        if d <= 0:
            break
        print('\n%+d bytes, %+d blocks (%d allocs since, %d live bytes)' % (d, dblk, dtot, live))
        frames = list(k) if exe is None else symbolize(exe, base, [x for x in k if int(x, 16)])
        for s in frames:
            print('   ', s)

main()
