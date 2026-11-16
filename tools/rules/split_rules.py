#!/usr/bin/env python3
"""Split a game's rules.json into topic files with "include" (docs/game-format.md § Splitting rules.json).

    python3 tools/rules/split_rules.py --project <game folder> [--check] [--plan <plan.json>]

Chosen top-level blocks get their own file under rules/; types are cut into files of whole runs of one kind in
their original order (the order of types is part of a game). Running it again on split rules re-cuts the types
from the composed document, so a file that grew past the size limit is split again. --check only reports whether
the composed rules equal what the split would write. The composed document never changes (asserted).

The plan (default: <project>/rules-split.json when it exists) tunes the cut:
    {"ownFile": {"driving": "rules/driving.json", "ai": "rules/ai.json"},   // top-level blocks with own files
     "typeLimitKB": 90,                                                       // compact JSON per types file
     "prefixes": ["port_", "metro_", "city_"],                                // kinds by id prefix, first match
     "vehicleKind": true}                                                     // types in driving.vehicles: "vehicles"
Without prefixes the kind of a type is the part of its id before the first "_" (or "misc").
"""
from pathlib import Path
import argparse, itertools, json, sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import rules_io

DEFAULT_PLAN = {
    'ownFile': {'driving': 'rules/driving.json', 'traffic': 'rules/traffic.json', 'nativeModules': 'rules/native-modules.json',
                'ai': 'rules/ai.json', 'abilities': 'rules/abilities.json', 'upgrades': 'rules/upgrades.json'},
    'typeLimitKB': 90,
    'prefixes': [],
    'vehicleKind': True,
}


class Plan:
    def __init__(self, data=None):
        d = dict(DEFAULT_PLAN, **(data or {}))
        self.own = dict(d['ownFile'])
        self.limit = int(d['typeLimitKB']) * 1024
        self.prefixes = list(d['prefixes'])
        self.vehicle_kind = bool(d['vehicleKind'])
        self.vehicles = set()

    def kind(self, name):
        if name in self.vehicles:
            return 'vehicles'
        for p in self.prefixes:
            if name.startswith(p):
                return p.rstrip('_') or 'misc'
        if self.prefixes:
            return 'misc'
        head = name.split('_', 1)[0]
        return head if head and head != name else 'misc'


def plan_types(types, plan):
    """[(file name, [type names])]: runs of one kind in order, packed into files up to the limit."""
    files, cur, size = [], [], 0
    def flush():
        nonlocal cur, size
        if cur:
            weight = {}
            for n in cur:
                weight[plan.kind(n)] = weight.get(plan.kind(n), 0) + len(json.dumps(types[n]))
            files.append((max(weight, key=weight.get), cur))
        cur, size = [], 0
    for _, run in itertools.groupby(types, key=plan.kind):
        run = list(run)
        rsize = sum(len(json.dumps(types[n])) for n in run)
        if cur and size + rsize > plan.limit:
            flush()
        for n in run:   # a single run larger than the limit is cut inside the run
            s = len(json.dumps(types[n]))
            if cur and size + s > plan.limit and size > plan.limit // 2:
                flush()
            cur.append(n); size += s
    flush()
    return [('rules/types/%02d-%s.json' % (i + 1, k.replace('_', '-')), names) for i, (k, names) in enumerate(files)]


def build(doc, plan):
    """{relative path: content} with the root file as 'rules.json'."""
    root = {k: v for k, v in doc.items() if k not in plan.own and k not in ('types', 'include')}
    plan.vehicles.clear()
    if plan.vehicle_kind:
        vehicles = doc.get('driving', {}).get('vehicles', {}) if isinstance(doc.get('driving'), dict) else {}
        plan.vehicles.update(vehicles if isinstance(vehicles, dict) else (v.get('type') for v in vehicles if isinstance(v, dict)))
    files, includes = {}, []
    for k, rel in plan.own.items():
        if k in doc:
            files[rel] = {k: doc[k]}
            includes.append(rel)
    if isinstance(doc.get('types'), dict) and doc['types']:
        for rel, names in plan_types(doc['types'], plan):
            files[rel] = {'types': {n: doc['types'][n] for n in names}}
            includes.append(rel)
    files['rules.json'] = dict(include=includes, **root)
    return files


def split(project, plan, check=False, out=print):
    doc = rules_io.load(project)
    files = build(doc, plan)
    if check:
        same = all((project / rel).exists() and json.loads((project / rel).read_text()) == c for rel, c in files.items())
        out('split rules are current' if same else 'split rules differ from the plan')
        return 0 if same else 1
    old = set(rules_io.files(project))
    for rel, content in files.items():
        p = project / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(rules_io.dumps(content), encoding='utf-8')
    new = {project / rel for rel in files}
    stale = sorted(str(p) for p in old if p not in new)
    again = rules_io.load(project)
    want = {k: v for k, v in doc.items() if k != 'include'}
    assert set(again) == set(want) and all(rules_io._ordered_equal(again[k], want[k]) for k in want), 'composed rules changed'
    sizes = sorted(((project / rel).stat().st_size, rel) for rel in files)
    out('%d files, largest %s (%d KB), rules.json %d KB' % (len(files), sizes[-1][1], sizes[-1][0] // 1024, (project / 'rules.json').stat().st_size // 1024))
    if stale:
        out('no longer included (remove them):\n  ' + '\n  '.join(stale))
    return 0


def main():
    a = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    a.add_argument('--project', type=Path, required=True, help='game folder with rules.json')
    a.add_argument('--plan', type=Path, help='plan JSON (default: <project>/rules-split.json if present)')
    a.add_argument('--check', action='store_true')
    args = a.parse_args()
    plan_path = args.plan or (args.project / 'rules-split.json')
    plan = Plan(json.loads(plan_path.read_text()) if plan_path.is_file() else None)
    return split(args.project, plan, args.check)


if __name__ == '__main__':
    sys.exit(main())
