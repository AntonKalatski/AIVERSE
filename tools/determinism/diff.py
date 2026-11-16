#!/usr/bin/env python3
"""Find where two AIVERSE simulation runs stop being identical.

Compare two checksum logs (aiverse_headless --checksum-log F):
  python3 tools/determinism/diff.py logs A.txt B.txt
Run a match twice (same build, or two builds with --headless-b) and narrow the divergence:
  python3 tools/determinism/diff.py run --headless build/linux-release/aiverse_headless --project templates/skirmish \
      [--headless-b other/aiverse_headless] [--map maps/valley.json] [--seed 3] [--max-seconds 600] [--out /tmp/det]

'run' writes both checksum logs, reports the last equal and first different checksum tick, then saves the state of
both runs at the last equal whole second before the divergence (--save/--save-at) and one second after it, and
reports the first differing byte of the serialized states. Exit 0 = identical, 1 = diverged, 2 = runner error.
Field-level comparison of native state: skills/verification/aiverse-state-diff.
"""
import argparse
import subprocess
import sys
from pathlib import Path


def read_log(path):
    rate, rows = 20, []
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if line.startswith('# tickRate '):
            rate = int(line.split()[2])
        elif line.strip():
            t, h = line.split()
            rows.append((int(t), h))
    return rate, rows


def first_divergence(a, b):
    """(last equal tick or None, first different tick or None, reason)."""
    hb = dict(b)
    last = None
    for t, h in a:
        if t not in hb:
            continue
        if hb[t] != h:
            return last, t, 'checksum'
        last = t
    ea, eb = a[-1] if a else None, b[-1] if b else None
    if ea and eb and ea[0] != eb[0]:
        return last, min(ea[0], eb[0]), 'length'
    return last, None, None


def first_byte(pa, pb):
    da, db = Path(pa).read_bytes(), Path(pb).read_bytes()
    for i, (x, y) in enumerate(zip(da, db)):
        if x != y:
            return i, len(da), len(db)
    return (None if len(da) == len(db) else min(len(da), len(db))), len(da), len(db)


def headless(binary, args):
    p = subprocess.run([str(binary)] + args, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout[-2000:] + p.stderr[-2000:])
        raise RuntimeError(f'{binary} exited with {p.returncode}')
    return p.stdout


def run(a):
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    bins = [Path(a.headless), Path(a.headless_b or a.headless)]
    base = ['--project', a.project, '--seed', str(a.seed), '--max-seconds', str(a.max_seconds), '--quiet']
    if a.map:
        base += ['--map', a.map]
    logs = []
    for i, b in enumerate(bins):
        log = out / f'run{"ab"[i]}.checksums'
        headless(b, base + ['--checksum-log', str(log)])
        logs.append(log)
    rate, la = read_log(logs[0]); _, lb = read_log(logs[1])
    last, first, why = first_divergence(la, lb)
    if first is None:
        print(f'identical: {len(la)} checksums, end tick {la[-1][0] if la else 0}')
        return 0
    print(f'diverged ({why}): last equal tick {last}, first different tick {first} '
          f'(t={first / rate:.2f}s, checksum interval {la[1][0] - la[0][0] if len(la) > 1 else "?"} ticks)')
    for label, sec in (('before', (last or 0) // rate), ('after', first // rate + 1)):
        if sec <= 0:
            continue
        saves = []
        for i, b in enumerate(bins):
            f = out / f'{label}{"ab"[i]}.state'
            headless(b, base[:-1] + ['--save', str(f), '--save-at', str(sec), '--quiet'])
            saves.append(f)
        if not all(s.exists() for s in saves):
            print(f'{label}: no state at {sec}s (match ended earlier)')
            continue
        off, na, nb = first_byte(*saves)
        state = 'identical' if off is None else f'first differing byte {off}'
        print(f'{label}: states at t={sec}s ({na} / {nb} bytes): {state}  [{saves[0]} {saves[1]}]')
    return 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    lg = sub.add_parser('logs'); lg.add_argument('a'); lg.add_argument('b')
    r = sub.add_parser('run')
    r.add_argument('--headless', required=True); r.add_argument('--headless-b')
    r.add_argument('--project', required=True); r.add_argument('--map')
    r.add_argument('--seed', type=int, default=3); r.add_argument('--max-seconds', type=int, default=600)
    r.add_argument('--out', default='determinism-diff')
    a = ap.parse_args(argv)
    try:
        if a.cmd == 'logs':
            rate, la = read_log(a.a); _, lb = read_log(a.b)
            last, first, why = first_divergence(la, lb)
            if first is None:
                print(f'identical: {len(la)} checksums'); return 0
            print(f'diverged ({why}): last equal tick {last}, first different tick {first} (t={first / rate:.2f}s)')
            return 1
        return run(a)
    except (RuntimeError, OSError, ValueError) as e:
        print(f'error: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
