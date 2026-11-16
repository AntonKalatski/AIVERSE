#!/usr/bin/env python3
"""Total Annihilation data -> an AIVERSE game folder (rules, maps, models), from the user's own copy of the game.

    python3 tools/importers/ta/import.py --source <TA folder | extracted folder> --out <game folder> [--json]

--source: the game folder with its .hpi / .ufo / .ccx / .gp3 archives (read with tools/unpackers/hpi), or a folder
already extracted (units/, weapons/, guis/, download/, features/, maps/, objects3d/, textures/). Later archives
override earlier ones, as in the game. Nothing is written into the engine repository: --out is required.

What comes over (docs/overview.md § Importing games):
  units/*.fbi      -> types: cost, build time / worker time, health, speed, sight, radar / sonar / jammers, stealth,
                      layers (air, naval, hover = amphibious, underwater), footprints, income and storage, extractors,
                      wind / tidal generators, metal makers, air repair pads, commanders, side (faction)
  guis/*.gui, download/*.tdf -> build menus (structures for builders, units for factories)
  weapons/*.tdf    -> weapons (range, reload, damage, area, velocity, anti-air, torpedoes), stockpiled weapons as
                      abilities + "stockpile", interceptors as "intercept", explosions as death payloads
  features/**.tdf  -> reclaimable features (metal / energy nodes) and wrecks
  maps/*.tnt + .ota -> heightfield maps (sea level -> water), start positions, features, metal spots
  objects3d/*.3do  -> copied to models/ (the engine reads .3do + .gaf textures itself, docs/models.md)
Units: 1 cell = 16 TA pixels; speeds px/frame x 30 / 16; build time / worker time divided by 100.
The engine's TA mechanics (radar, stockpiles, reclaim, wind) are documented in docs/game-format.md."""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..', 'unpackers', 'hpi'))
import tdf  # noqa: E402
import tnt  # noqa: E402

PX = 16.0          # TA pixels per cell
FPS = 30.0         # TA game frames per second
WORK = 100.0       # worker time units per build power 1
ARCHIVES = ('.hpi', '.ufo', '.ccx', '.gp3', '.gp4', '.swx')


def r2(v):
    v = round(float(v), 2)
    return int(v) if v == int(v) else v


def ident(name):
    return re.sub(r'[^a-z0-9_]', '_', name.strip().lower())


# ---------------------------------------------------------------- source files

class Source:
    """Files of the game by lower-case relative path (archives unpacked in name order, loose files on top)."""
    def __init__(self, root):
        self.files = {}
        self.tmp = None
        archives = []
        for dp, _, fs in os.walk(root):
            for f in fs:
                p = os.path.join(dp, f)
                if f.lower().endswith(ARCHIVES):
                    archives.append(p)
        if archives:
            from unpack import Hpi  # tools/unpackers/hpi
            self.tmp = tempfile.mkdtemp(prefix='ta_import_')
            for a in sorted(archives, key=lambda p: os.path.basename(p).lower()):
                with open(a, 'rb') as fh:
                    h = Hpi(fh.read())
                for name, doff, size, comp in h.files():
                    rel = name.replace('\\', '/').lower()
                    out = os.path.join(self.tmp, *rel.split('/'))
                    os.makedirs(os.path.dirname(out), exist_ok=True)
                    with open(out, 'wb') as o:
                        o.write(h.extract(doff, size, comp))
                    self.files[rel] = out
        for dp, _, fs in os.walk(root):
            for f in fs:
                if f.lower().endswith(ARCHIVES):
                    continue
                p = os.path.join(dp, f)
                self.files[os.path.relpath(p, root).replace(os.sep, '/').lower()] = p

    def glob(self, folder, ext):
        folder = folder.lower().rstrip('/') + '/'
        return sorted(p for p in self.files if p.startswith(folder) and p.endswith(ext))

    def path(self, rel):
        return self.files.get(rel.lower())

    def close(self):
        if self.tmp:
            shutil.rmtree(self.tmp, ignore_errors=True)


# ---------------------------------------------------------------- weapons

def read_weapons(src):
    weapons = {}
    for rel in src.glob('weapons', '.tdf'):
        for wid, w in tdf.load(src.path(rel)).items():
            if isinstance(w, dict):
                weapons[wid.split('#')[0]] = w
    return weapons


def damage_of(w):
    d = w.get('damage')
    if isinstance(d, dict):
        return tdf.num(d, 'default', 0)
    return tdf.num(w, 'damage', 0)


def weapon_block(w):
    out = {'range': r2(tdf.num(w, 'range', 160) / PX), 'damage': r2(damage_of(w)),
           'cooldown': r2(max(0.1, tdf.num(w, 'reloadtime', 1)))}
    v = tdf.num(w, 'weaponvelocity', 0)
    if v > 0 and not tdf.num(w, 'beamweapon', 0):
        out['projectileSpeed'] = r2(v / PX)
    aoe = tdf.num(w, 'areaofeffect', 0)
    if aoe > PX:
        out['splash'] = r2(aoe / PX / 2)
    if tdf.num(w, 'toairweapon', 0):
        out['targets'] = ['air']
    elif tdf.num(w, 'waterweapon', 0):
        out['targets'] = ['naval']
        out['underwater'] = True
    else:
        out['targets'] = ['ground', 'naval']
    burst = tdf.num(w, 'burst', 1)
    if burst > 1:
        out['damage'] = r2(damage_of(w) * burst)
    return out


def explosion(w):
    """A weapon used as a death blast -> payload."""
    return [{'type': 'damage', 'amount': r2(damage_of(w)), 'radius': r2(max(1.0, tdf.num(w, 'areaofeffect', 48) / PX / 2)),
             'falloff': 0.7, 'affects': 'any'}]


# ---------------------------------------------------------------- build menus

def build_menus(src, units):
    menus = {}
    for rel in src.glob('guis', '.gui'):
        m = re.match(r'guis/([a-z0-9_]+?)(\d+)\.gui$', rel)
        if not m or m.group(1) not in units:
            continue
        for g in tdf.load(src.path(rel)).values():
            if not isinstance(g, dict):
                continue
            for sec in g.values() if any(isinstance(v, dict) for v in g.values()) else [g]:
                if isinstance(sec, dict):
                    name = sec.get('name', '').lower()
                    if name in units and name not in menus.setdefault(m.group(1), []):
                        menus[m.group(1)].append(name)
    for rel in src.glob('download', '.tdf'):
        for e in tdf.load(src.path(rel)).values():
            if isinstance(e, dict):
                owner, item = e.get('unitmenu', '').lower(), e.get('unitname', '').lower()
                if owner in units and item in units and item not in menus.setdefault(owner, []):
                    menus[owner].append(item)
    return menus


# ---------------------------------------------------------------- units

def convert_unit(u, weapons, extra_types, warnings):
    uid = u['unitname']
    structure = tdf.num(u, 'bmcode', 1) == 0
    t = {'name': u.get('name', uid)}
    side = u.get('side', '').lower()
    if side:
        t['factions'] = [ident(side)]
    t['health'] = {'max': r2(max(1, tdf.num(u, 'maxdamage', 100)))}
    cost = {}
    if tdf.num(u, 'buildcostmetal'):
        cost['metal'] = r2(tdf.num(u, 'buildcostmetal'))
    if tdf.num(u, 'buildcostenergy'):
        cost['energy'] = r2(tdf.num(u, 'buildcostenergy'))
    cost['time'] = r2(max(0.5, tdf.num(u, 'buildtime', 1000) / WORK))
    t['cost'] = cost
    fx, fz = int(tdf.num(u, 'footprintx', 2)), int(tdf.num(u, 'footprintz', 2))
    cat = u.get('category', '').upper().split()
    if structure:
        t['structure'] = {'footprint': [max(1, fx), max(1, fz)]}
        if tdf.num(u, 'extractsmetal'):
            t['structure']['on'] = 'metal_spot'
        if u.get('yardmap', '').strip().lower().startswith('g'):
            t['structure']['on'] = 'geo_vent'
    else:
        move = {'speed': r2(max(0.1, tdf.num(u, 'maxvelocity', 1) * FPS / PX)), 'radius': r2(max(0.3, min(fx, fz) * 0.45))}
        if tdf.num(u, 'canfly'):
            move['layer'] = 'air'
        elif tdf.num(u, 'canhover') or (tdf.num(u, 'minwaterdepth', -1) < 0 and tdf.num(u, 'maxwaterdepth', 0) > 255):
            move['layer'] = 'amphibious'
        elif tdf.num(u, 'minwaterdepth', -1) > 0 or tdf.num(u, 'floater') or 'SHIP' in cat or 'UNDERWATER' in cat:
            move['layer'] = 'naval'
        t['move'] = move
    t['sight'] = r2(max(2, tdf.num(u, 'sightdistance', 200) / PX))
    for key, out in (('radardistance', 'radar'), ('sonardistance', 'sonar'), ('radardistancejam', 'jammer')):
        if tdf.num(u, key) > 0:
            t[out] = r2(tdf.num(u, key) / PX)
    if tdf.num(u, 'sonardistancejam') > 0 and 'jammer' not in t:
        t['jammer'] = r2(tdf.num(u, 'sonardistancejam') / PX)
    if tdf.num(u, 'stealth'):
        t['stealth'] = True
    if 'UNDERWATER' in cat or tdf.num(u, 'waterline') > 10:
        t['underwater'] = True
    if tdf.num(u, 'commander'):
        t['key'] = True
    # economy
    income, storage = {}, {}
    if tdf.num(u, 'metalmake'):
        income['metal'] = r2(tdf.num(u, 'metalmake'))
    if tdf.num(u, 'energymake'):
        income['energy'] = r2(tdf.num(u, 'energymake'))
    if tdf.num(u, 'extractsmetal'):
        income['metal'] = r2(income.get('metal', 0) + tdf.num(u, 'extractsmetal') * 1000)   # TA: 0.001 x spot metal
    if tdf.num(u, 'makesmetal'):
        income['metal'] = r2(income.get('metal', 0) + tdf.num(u, 'makesmetal'))
        t['tags'] = t.get('tags', []) + ['converter']
    if tdf.num(u, 'energyuse') and structure:
        income['energy'] = r2(income.get('energy', 0) - tdf.num(u, 'energyuse'))
    if tdf.num(u, 'metalstorage'):
        storage['metal'] = r2(tdf.num(u, 'metalstorage'))
    if tdf.num(u, 'energystorage'):
        storage['energy'] = r2(tdf.num(u, 'energystorage'))
    amb = {}
    if tdf.num(u, 'windgenerator'):
        amb['wind'] = {'energy': 1}
        income['energy'] = r2(income.get('energy', 0) + 1)   # a little fixed output: the bot counts it as income
    if tdf.num(u, 'tidalgenerator'):
        amb['tidal'] = {'energy': r2(tdf.num(u, 'tidalgenerator'))}
        income.setdefault('energy', 1)
    if income:
        t['income'] = income
    if storage:
        t['storage'] = storage
    if amb:
        t['ambientIncome'] = amb
    if tdf.num(u, 'isairbase'):
        t['repairPad'] = {'heal': r2(max(10, tdf.num(u, 'workertime', 100) / 5)), 'radius': 1.5}
    if tdf.num(u, 'canfly'):
        t['seekRepair'] = 0.3
    # building
    if tdf.num(u, 'builder'):
        t['buildPower'] = r2(max(0.1, tdf.num(u, 'workertime', 100) / WORK))
        if not structure:
            t['assist'] = True
            t['patrolWork'] = True
            t['gather'] = {'metal': {'carry': 10, 'time': 1}, 'energy': {'carry': 10, 'time': 1}}
    # weapons
    ws, abilities = [], []
    for k in ('weapon1', 'weapon2', 'weapon3'):
        wid = u.get(k, '').lower()
        if not wid:
            continue
        w = weapons.get(wid)
        if w is None:
            warnings.append('%s: unknown weapon %s' % (uid, wid))
            continue
        if tdf.num(w, 'interceptor'):
            t['intercept'] = {'radius': r2(max(1, tdf.num(w, 'coverage', 1000) / PX)), 'tags': ['targetable']}
            t['stockpile'] = {'max': 5, 'time': r2(max(1, tdf.num(w, 'reloadtime', 30))),
                              'cost': {'metal': r2(tdf.num(w, 'metalpershot')), 'energy': r2(tdf.num(w, 'energypershot'))}}
            continue
        if tdf.num(w, 'stockpile') or tdf.num(w, 'commandfire'):
            payload = explosion(w)
            ab = {'id': ident(wid), 'name': w.get('name', wid), 'target': 'point', 'range': r2(tdf.num(w, 'range', 1000) / PX),
                  'cooldown': r2(max(1, tdf.num(w, 'reloadtime', 1))),
                  'effects': [{'type': 'projectile', 'speed': r2(max(2, tdf.num(w, 'weaponvelocity', 400) / PX)),
                               'delivery': {'arc': True}, 'effects': payload}]}
            if tdf.num(w, 'energypershot') and not tdf.num(w, 'stockpile'):
                ab['cost'] = {'energy': r2(tdf.num(w, 'energypershot'))}   # the commander's D-gun
            abilities.append(ab)
            if tdf.num(w, 'stockpile'):
                cost = {}
                if tdf.num(w, 'metalpershot'):
                    cost['metal'] = r2(tdf.num(w, 'metalpershot'))
                if tdf.num(w, 'energypershot'):
                    cost['energy'] = r2(tdf.num(w, 'energypershot'))
                t['stockpile'] = {'max': 5, 'time': r2(max(1, tdf.num(w, 'reloadtime', 60))), 'cost': cost, 'abilities': [ident(wid)]}
                if tdf.num(w, 'targetable'):
                    t['tags'] = t.get('tags', []) + ['targetable']
                ab['autocast'] = True
                ab['smart'] = 'strongestEnemyStructure'
            continue
        ws.append(weapon_block(w))
    if ws:
        t['weapons'] = ws
    if abilities:
        t['abilities'] = abilities
    blast = weapons.get(u.get('explodeas', '').lower())
    if blast and damage_of(blast) > 0:
        t['onDeath'] = explosion(blast)
    if not structure and tdf.num(u, 'buildcostmetal') > 0:
        t['wreck'] = {'metal': r2(tdf.num(u, 'buildcostmetal') * 0.6)}
    model = u.get('objectname', '').lower()
    if model:
        t['view'] = {'model': 'models/%s.3do' % model}
    return t


# ---------------------------------------------------------------- features

def read_features(src):
    feats = {}
    for rel in src.glob('features', '.tdf'):
        for fid, f in tdf.load(src.path(rel)).items():
            if isinstance(f, dict):
                feats[fid.split('#')[0]] = f
    return feats


def feature_type(fid, f):
    t = {'name': f.get('description', fid), 'invulnerable': True}
    metal, energy = tdf.num(f, 'metal'), tdf.num(f, 'energy')
    if tdf.num(f, 'reclaimable', 1) and (metal > 0 or energy > 0):
        res = 'metal' if metal >= energy else 'energy'
        t['resourceNode'] = {'resource': res, 'amount': r2(metal if res == 'metal' else energy)}
    if tdf.num(f, 'blocking', 1):
        t['structure'] = {'footprint': [max(1, int(tdf.num(f, 'footprintx', 1))), max(1, int(tdf.num(f, 'footprintz', 1)))]}
    return t


# ---------------------------------------------------------------- maps

def convert_map(src, rel, feats, types, warnings):
    with open(src.path(rel), 'rb') as fh:
        m = tnt.Tnt(fh.read())
    base = os.path.splitext(os.path.basename(rel))[0]
    ota = src.path(rel[:-4] + '.ota')
    info = tdf.load(ota) if ota else {}
    header = info.get('globalheader', {})
    w, h = m.w, m.h
    corners = [m.height(x, y) for y in range(h + 1) for x in range(w + 1)]
    rows = []
    for y in range(h):
        rows.append(''.join('~' if m.height(x, y) < m.sea else '.' for x in range(w)))
    players, entities = [], []
    schema = next((v for k, v in header.items() if k.startswith('schema') and isinstance(v, dict)), {})
    specials = schema.get('specials', {})
    starts = []
    for s in specials.values():
        if isinstance(s, dict) and s.get('specialwhat', '').lower().startswith('startpos'):
            starts.append((int(re.sub(r'\D', '', s['specialwhat']) or 0), tdf.num(s, 'xpos') / PX, tdf.num(s, 'zpos') / PX))
    starts.sort()
    for i, (_, x, z) in enumerate(starts):
        players.append({'name': 'Player %d' % (i + 1), 'team': i + 1, 'start': [r2(x), r2(z)]})
    if len(players) < 2:
        warnings.append('%s: fewer than 2 start positions in the .ota, corners used' % base)
        players = [{'name': 'Player 1', 'team': 1, 'start': [4, 4]}, {'name': 'Player 2', 'team': 2, 'start': [w - 5, h - 5]}]
    for x, y, fi in m.features:
        name = m.names.get(fi, '').lower()
        if not name:
            continue
        if 'metal' in name and name not in feats:
            entities.append({'type': 'metal_spot', 'owner': 0, 'pos': [x, y]})
            continue
        if 'geo' in name and name not in feats:
            entities.append({'type': 'geo_vent', 'owner': 0, 'pos': [x, y]})
            continue
        tid = 'feature_' + ident(name)
        if tid not in types:
            if name not in feats:
                warnings.append('%s: unknown feature %s' % (base, name))
                continue
            types[tid] = feature_type(name, feats[name])
        entities.append({'type': tid, 'owner': 0, 'pos': [x, y]})
    out = {'name': header.get('missionname', base), 'size': [w, h], 'players': players,
           'terrain': {'corners': corners, 'rows': rows}, 'entities': entities}
    if tdf.num(header, 'tidalstrength'):
        out['ambient'] = {'tidal': r2(tdf.num(header, 'tidalstrength'))}
    wmin, wmax = tdf.num(header, 'minwindspeed', -1), tdf.num(header, 'maxwindspeed', -1)
    if wmax >= 0:
        out.setdefault('ambient', {})['wind'] = {'min': r2(max(0, wmin) / 100), 'max': r2(max(wmin, wmax) / 100), 'every': [15, 45]}
    return base, out


# ---------------------------------------------------------------- game

def import_game(source, out, maps_only=False):
    src = Source(source)
    warnings = []
    try:
        weapons = read_weapons(src)
        feats = read_features(src)
        fbi = {}
        for rel in src.glob('units', '.fbi'):
            for sec in tdf.load(src.path(rel)).values():
                if isinstance(sec, dict) and 'unitname' in sec:
                    fbi[sec['unitname'].lower()] = sec
        types = {
            'metal_spot': {'name': 'Metal deposit', 'invulnerable': True, 'structure': {'footprint': [2, 2]}, 'view': {'shape': 'spot'}},
            'geo_vent': {'name': 'Geothermal vent', 'invulnerable': True, 'structure': {'footprint': [2, 2]}, 'view': {'shape': 'spot'}},
            'wreck': {'name': 'Wreck', 'invulnerable': True, 'resourceNode': {'resource': 'metal', 'amount': 50}, 'view': {'shape': 'wreck'}},
        }
        units = set(fbi)
        menus = build_menus(src, units)
        factions = {}
        if not maps_only:
            for uid, u in sorted(fbi.items()):
                t = convert_unit(u, weapons, types, warnings)
                buildable = menus.get(uid, [])
                if buildable:
                    struct_items = [b for b in buildable if tdf.num(fbi[b], 'bmcode', 1) == 0]
                    mobile_items = [b for b in buildable if tdf.num(fbi[b], 'bmcode', 1) != 0]
                    if 'structure' in t:
                        if mobile_items:
                            t['produces'] = mobile_items
                    elif struct_items:
                        t['builds'] = struct_items
                types[uid] = t
                if tdf.num(u, 'commander') and t.get('factions'):
                    factions[t['factions'][0]] = {'name': u.get('side', uid), 'start': [{'type': uid, 'offset': [0, 0]}]}
        maps = []
        for rel in src.glob('maps', '.tnt'):
            try:
                maps.append(convert_map(src, rel, feats, types, warnings))
            except ValueError as e:
                warnings.append('%s: %s' % (rel, e))
        os.makedirs(os.path.join(out, 'maps'), exist_ok=True)
        os.makedirs(os.path.join(out, 'rules'), exist_ok=True)
        sides = sorted(factions)
        for name, m in maps:
            if sides:
                for i, pl in enumerate(m['players']):
                    pl['faction'] = sides[i % len(sides)]   # alternate the sides over the start positions
            with open(os.path.join(out, 'maps', ident(name) + '.json'), 'w') as f:
                json.dump(m, f, ensure_ascii=False, separators=(',', ':'))
        rules = {'include': ['rules/units.json'], 'costModel': 'progressive', 'victory': 'keyUnits' if factions else 'noStructures',
                 'supplyCap': 500,
                 'resources': {'metal': {'name': 'Metal', 'kind': 'flow', 'start': 1000, 'storage': 1000, 'deficitProduction': 0.3},
                               'energy': {'name': 'Energy', 'kind': 'flow', 'start': 1000, 'storage': 1000, 'deficitProduction': 0.3,
                                          'deficitDisables': ['converter']}},
                 'terrain': {'model': 'heightfield'},
                 'radar': {'targeting': True}, 'repair': {'cost': 0, 'units': True}, 'reclaim': {'units': True, 'refund': 1},
                 'selfDestruct': {'time': 5}, 'ambient': {'wind': {'min': 0, 'max': 20, 'every': [15, 45]}}}
        if factions:
            rules['factions'] = factions
        with open(os.path.join(out, 'rules.json'), 'w') as f:
            json.dump(rules, f, ensure_ascii=False, indent=1)
        with open(os.path.join(out, 'rules', 'units.json'), 'w') as f:
            json.dump({'types': types}, f, ensure_ascii=False, indent=1)
        game = {'name': 'Imported TA', 'id': 'local.imported.ta', 'version': '0.1.0', 'module': 'rts', 'rules': 'rules.json',
                'maps': [{'file': 'maps/%s.json' % ident(n), 'title': m['name']} for n, m in maps], 'terrain': {'style': 'smooth'},
                'external': True}
        gpath = os.path.join(out, 'game.json')
        if not os.path.exists(gpath):
            with open(gpath, 'w') as f:
                json.dump(game, f, ensure_ascii=False, indent=1)
        models = 0
        if not maps_only:
            os.makedirs(os.path.join(out, 'models'), exist_ok=True)
            for rel in src.glob('objects3d', '.3do'):
                shutil.copyfile(src.path(rel), os.path.join(out, 'models', os.path.basename(rel)))
                models += 1
            tex = src.glob('textures', '.gaf')
            if tex:
                os.makedirs(os.path.join(out, 'textures'), exist_ok=True)
                for rel in tex:
                    shutil.copyfile(src.path(rel), os.path.join(out, 'textures', os.path.basename(rel)))
            for pal in ('palettes/palette.pal',):
                if src.path(pal):
                    os.makedirs(os.path.join(out, 'palettes'), exist_ok=True)
                    shutil.copyfile(src.path(pal), os.path.join(out, 'palettes', 'PALETTE.PAL'))
        return {'units': len(fbi), 'weapons': len(weapons), 'features': len(feats), 'maps': [n for n, _ in maps],
                'models': models, 'factions': sorted(factions), 'warnings': warnings}
    finally:
        src.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--source', required=True, help='TA folder with .hpi/.ufo archives, or an extracted folder')
    ap.add_argument('--out', required=True, help='game folder to write (outside the engine repository)')
    ap.add_argument('--maps-only', action='store_true')
    ap.add_argument('--json', action='store_true', help='print the report as JSON')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    engine = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
    if os.path.commonpath([out, os.path.join(engine, 'templates')]) == os.path.join(engine, 'templates'):
        sys.exit('refusing to write into the engine templates/ folder: games live outside the engine repository')
    rep = import_game(a.source, out, a.maps_only)
    if a.json:
        print(json.dumps(rep))
    else:
        print('%d units, %d weapons, %d features, maps: %s, models: %d' % (rep['units'], rep['weapons'], rep['features'],
                                                                        ', '.join(rep['maps']) or '-', rep['models']))
        for w in rep['warnings']:
            print('warning:', w)


if __name__ == '__main__':
    main()
