#!/usr/bin/env python3
"""Bot sweep: play every map of a game with bots in all slots and classify the outcomes.

  python3 tools/bots/sweep.py --headless build/linux-release/aiverse_headless --project templates/skirmish \
      [--seeds 1,2,3] [--max-seconds 1800] [--maps valley,forest] [--json out.json] [--jobs 2]

Runs aiverse_headless once per map and seed (the headless runner automates every slot with RtsAI) and reports,
per run: winner, game seconds, commands per slot, checksum, and a class:
  win      - a winner before the limit
  draw     - the simulation ended in a draw
  timeout  - no winner by --max-seconds (stalemate, broken objectives, ...)
  idle     - a slot that is not a wave/monster side issued 0 commands (the bot never played)
  error    - the runner failed (load error, crash, script errors)
With two or more seeds the first seed is also run twice: different checksums = 'nondeterministic' (stop and use
tools/determinism/diff.py). Exit code 1 when any run is idle, error or nondeterministic. Markdown table on stdout.
"""
import argparse
import concurrent.futures
import json
import re
import subprocess
import sys
from pathlib import Path

RESULT = re.compile(r'^Result: winner=(.+?)\s{2}(\d+) game-s')
PLAYER = re.compile(r'^Player: slot=(\d+) name=(\S*) team=(\d+) alive=(\d) commands=(\d+)')
CHECKSUM = re.compile(r'^Checksum: ([0-9a-f]{16})')


def strip_comments(text):
    out, i, n, in_str = [], 0, len(text), False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(text[i + 1]); i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True; out.append(c)
        elif text.startswith('//', i):
            while i < n and text[i] != '\n':
                i += 1
            continue
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        else:
            out.append(c)
        i += 1
    return ''.join(out)


def read_json(path):
    return json.loads(strip_comments(Path(path).read_text(encoding='utf-8')))


def game_maps(project):
    game = read_json(project / 'game.json')
    maps = [m['file'] if isinstance(m, dict) else m for m in game.get('maps', [])]
    if not maps:
        maps = sorted(p.relative_to(project).as_posix() for p in (project / 'maps').glob('*.json'))
    return maps


def scripted_slots(project, map_file):
    """Slots driven by scripts or encounters by design (waves / monsters): commands=0 is expected there."""
    try:
        players = read_json(project / map_file).get('players', [])
    except (OSError, ValueError):
        return set()
    return {i + 1 for i, p in enumerate(players) if isinstance(p, dict) and (p.get('waves') or p.get('monsters'))}


def parse(text):
    run = dict(winner=None, seconds=None, checksum=None, players=[])
    for line in text.splitlines():
        if m := RESULT.match(line):
            run['winner'], run['seconds'] = m.group(1), int(m.group(2))
        elif m := PLAYER.match(line):
            run['players'].append(dict(slot=int(m.group(1)), name=m.group(2), team=int(m.group(3)),
                                       alive=m.group(4) == '1', commands=int(m.group(5))))
        elif m := CHECKSUM.match(line):
            run['checksum'] = m.group(1)
    return run


def classify(run, code, skip):
    if code != 0 or run['winner'] is None or run['checksum'] is None:
        return 'error'
    if any(p['commands'] == 0 and p['slot'] not in skip for p in run['players']):
        return 'idle'
    if run['winner'].startswith('none'):
        return 'timeout'
    return 'draw' if run['winner'] == 'draw' else 'win'


def play(headless, project, map_file, seed, max_seconds):
    cmd = [str(headless), '--project', str(project), '--map', map_file, '--seed', str(seed),
           '--max-seconds', str(max_seconds), '--quiet']
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=max(600, max_seconds * 2))
        out, code = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired:
        out, code = 'runner timed out', -1
    run = parse(out)
    run.update(map=map_file, seed=seed, code=code)
    run['class'] = classify(run, code, scripted_slots(project, map_file))
    if run['class'] == 'error':
        run['log'] = '\n'.join(out.strip().splitlines()[-8:])
    return run


def sweep(headless, project, maps, seeds, max_seconds, jobs=1):
    tasks = [(m, s) for m in maps for s in seeds]
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        runs = list(pool.map(lambda t: play(headless, project, t[0], t[1], max_seconds), tasks))
    if len(seeds) > 1:   # determinism spot check: the first seed once more per map
        for m in maps:
            again = play(headless, project, m, seeds[0], max_seconds)
            first = next(r for r in runs if r['map'] == m and r['seed'] == seeds[0])
            if first['checksum'] and again['checksum'] and first['checksum'] != again['checksum']:
                first['class'] = 'nondeterministic'
                first['repeatChecksum'] = again['checksum']
    return runs


def table(runs):
    rows = ['| map | seed | class | winner | game s | commands per slot | checksum |', '|---|---|---|---|---|---|---|']
    for r in runs:
        cmds = ' '.join(f"{p['slot']}:{p['commands']}" for p in r['players']) or '-'
        rows.append(f"| {r['map']} | {r['seed']} | {r['class']} | {r['winner'] or '-'} | {r['seconds'] if r['seconds'] is not None else '-'} | {cmds} | {r['checksum'] or '-'} |")
    counts = {}
    for r in runs:
        counts[r['class']] = counts.get(r['class'], 0) + 1
    rows.append('')
    rows.append('Summary: ' + ', '.join(f'{k}={v}' for k, v in sorted(counts.items())))
    return '\n'.join(rows)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--headless', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--seeds', default='1,2,3')
    ap.add_argument('--max-seconds', type=int, default=1800)
    ap.add_argument('--maps', help='comma-separated map file names or stems (default: game.json maps)')
    ap.add_argument('--json', help='write the runs as JSON')
    ap.add_argument('--jobs', type=int, default=1)
    a = ap.parse_args(argv)
    project = Path(a.project).resolve()
    maps = game_maps(project)
    if a.maps:
        want = {w.strip() for w in a.maps.split(',') if w.strip()}
        maps = [m for m in maps if m in want or Path(m).stem in want]
        if not maps:
            ap.error('no matching maps')
    seeds = [int(s) for s in a.seeds.split(',') if s.strip()]
    runs = sweep(Path(a.headless).resolve(), project, maps, seeds, a.max_seconds, a.jobs)
    print(table(runs))
    for r in runs:
        if r.get('log'):
            print(f"\n{r['map']} seed {r['seed']}:\n{r['log']}")
    if a.json:
        Path(a.json).write_text(json.dumps(runs, indent=2) + '\n', encoding='utf-8')
    return 1 if any(r['class'] in ('idle', 'error', 'nondeterministic') for r in runs) else 0


if __name__ == '__main__':
    sys.exit(main())
