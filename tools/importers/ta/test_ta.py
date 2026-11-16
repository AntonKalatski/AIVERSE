#!/usr/bin/env python3
"""Self-test of the Total Annihilation importer and the HPI unpacker on made-up data (no game files).

Builds a small game in the TA layouts (FBI units, TDF weapons / features, GUI build menus, a TNT map + OTA), packs
part of it into an HPI archive with the header cipher, zlib and LZ77 chunks, imports it and checks the rules and the
map. With AIVERSE_PORT_HEADLESS (the aiverse_headless binary) the imported game also plays a bot match."""
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..', 'unpackers', 'hpi'))
import tdf  # noqa: E402
import tnt  # noqa: E402
from unpack import Hpi, lz77  # noqa: E402

FAILS = []


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print('FAIL:', what)


# ---------------------------------------------------------------- an HPI writer (test only)

def lz77_literals(data, repeat_at=None):
    """Cavedog LZ77 stream: literals, optionally one back reference (window offset 1 = the first literal)."""
    items = [('lit', b) for b in data]
    if repeat_at:
        items.insert(repeat_at[0], ('ref', repeat_at[1]))
    items.append(('end', 0))
    out = bytearray()
    for i in range(0, len(items), 8):
        group = items[i:i + 8]
        flags = 0
        body = bytearray()
        for k, (kind, v) in enumerate(group):
            if kind == 'lit':
                body.append(v)
            else:
                flags |= 1 << k
                word = 0 if kind == 'end' else (1 << 4) | (v - 2)
                body += struct.pack('<H', word)
        out.append(flags)
        out += body
    return bytes(out)


def make_hpi(files, key=0x7D):
    """files: {path: (bytes, compression 0/1/2)} -> HPI bytes (one directory level per path part)."""
    tree = {}
    for path, v in files.items():
        node = tree
        parts = path.split('/')
        for p in parts[:-1]:
            node = node.setdefault(p, {})
        node[parts[-1]] = v
    directory = bytearray(b'\0' * 20)

    def emit_dir(node):
        here = len(directory)
        directory.extend(b'\0' * 8)
        entries_at = len(directory)
        names = sorted(node)
        directory.extend(b'\0' * (9 * len(names)))
        struct.pack_into('<2I', directory, here, len(names), entries_at)
        for i, name in enumerate(names):
            name_off = len(directory)
            directory.extend(name.encode() + b'\0')
            if isinstance(node[name], dict):
                sub = emit_dir(node[name])
                struct.pack_into('<2IB', directory, entries_at + 9 * i, name_off, sub, 1)
            else:
                rec = len(directory)
                directory.extend(b'\0' * 9)
                pending.append((rec, node[name]))
                struct.pack_into('<2IB', directory, entries_at + 9 * i, name_off, rec, 0)
        return here

    pending = []
    emit_dir(tree)
    dsize = len(directory)
    body = bytearray()
    for rec, (data, comp) in pending:
        doff = dsize + len(body)
        if comp == 0:
            body += data
        else:
            chunks = [data[i:i + 65536] for i in range(0, len(data), 65536)] or [b'']
            blobs = []
            for c in chunks:
                packed = zlib.compress(c) if comp == 2 else lz77_literals(c)
                enc = bytearray((((b ^ i) + i) & 0xFF) for i, b in enumerate(packed))   # scrambled (inverse of (b - i) ^ i)
                head = struct.pack('<IBBBIII', 0x48535153, 2, comp, 1, len(enc), len(c), sum(enc) & 0xFFFFFFFF)
                blobs.append(head + bytes(enc))
            body += struct.pack('<%dI' % len(blobs), *[len(b) for b in blobs])
            for b in blobs:
                body += b
        struct.pack_into('<2IB', directory, rec, doff, len(data), comp)
    struct.pack_into('<5I', directory, 0, 0x49504148, 0x00010000, dsize, key, 20)
    plain = bytes(directory) + bytes(body)
    if not key:
        return plain
    k = (~((key * 4) | (key >> 6))) & 0xFF
    out = bytearray(plain[:20])
    for pos in range(20, len(plain)):
        tkey = (pos ^ k) & 0xFF
        out.append(~(plain[pos] ^ tkey) & 0xFF)   # inverse of plain = tkey ^ ~byte
    return bytes(out)


# ---------------------------------------------------------------- made-up game data

FBI = {
    'armcom': '''[UNITINFO]{ UnitName=ARMCOM; Name=Commander; Side=ARM; BMcode=1; BuildCostMetal=2500; BuildCostEnergy=25000;
        BuildTime=60000; MaxDamage=3000; MaxVelocity=1.2; SightDistance=300; RadarDistance=700; Builder=1; WorkerTime=300;
        Commander=1; FootprintX=2; FootprintZ=2; EnergyMake=25; MetalMake=1.5; EnergyStorage=500; MetalStorage=500;
        Weapon1=ARM_LASER; Weapon2=ARM_DISINTEGRATOR; ExplodeAs=COMMANDER_BLAST; ObjectName=ARMCOM; Category=ARM COMMANDER NOTAIR; }''',
    'armmex': '''[UNITINFO]{ UnitName=ARMMEX; Name=Metal Extractor; Side=ARM; BMcode=0; BuildCostMetal=50; BuildCostEnergy=500;
        BuildTime=1800; MaxDamage=200; FootprintX=2; FootprintZ=2; ExtractsMetal=0.001; EnergyUse=3; }''',
    'armwin': '''[UNITINFO]{ UnitName=ARMWIN; Name=Wind Generator; Side=ARM; BMcode=0; BuildCostMetal=40; BuildCostEnergy=175;
        BuildTime=1600; MaxDamage=200; FootprintX=2; FootprintZ=2; WindGenerator=120; }''',
    'armrad': '''[UNITINFO]{ UnitName=ARMRAD; Name=Radar Tower; Side=ARM; BMcode=0; BuildCostMetal=50; BuildCostEnergy=600;
        BuildTime=1000; MaxDamage=100; FootprintX=1; FootprintZ=1; RadarDistance=2100; EnergyUse=20; }''',
    'armsilo': '''[UNITINFO]{ UnitName=ARMSILO; Name=Retaliator; Side=ARM; BMcode=0; BuildCostMetal=3000; BuildCostEnergy=60000;
        BuildTime=180000; MaxDamage=4000; FootprintX=4; FootprintZ=4; Weapon1=NUCLEAR_MISSILE; }''',
    'armlab': '''[UNITINFO]{ UnitName=ARMLAB; Name=Kbot Lab; Side=ARM; BMcode=0; BuildCostMetal=600; BuildCostEnergy=1100;
        BuildTime=6500; MaxDamage=2000; FootprintX=4; FootprintZ=4; Builder=1; WorkerTime=100; }''',
    'armpw': '''[UNITINFO]{ UnitName=ARMPW; Name=Peewee; Side=ARM; BMcode=1; BuildCostMetal=50; BuildCostEnergy=800;
        BuildTime=1400; MaxDamage=250; MaxVelocity=1.7; SightDistance=350; FootprintX=2; FootprintZ=2; Weapon1=EMG; }''',
    'armsub': '''[UNITINFO]{ UnitName=ARMSUB; Name=Lurker; Side=ARM; BMcode=1; BuildCostMetal=700; BuildCostEnergy=6000;
        BuildTime=9000; MaxDamage=700; MaxVelocity=1.5; SightDistance=400; SonarDistance=500; MinWaterDepth=20;
        FootprintX=3; FootprintZ=3; Category=ARM UNDERWATER; Weapon1=ARM_TORPEDO; }''',
    'corcom': '''[UNITINFO]{ UnitName=CORCOM; Name=Commander; Side=CORE; BMcode=1; BuildCostMetal=2500; BuildCostEnergy=25000;
        BuildTime=60000; MaxDamage=3200; MaxVelocity=1.1; SightDistance=300; Builder=1; WorkerTime=300; Commander=1;
        FootprintX=2; FootprintZ=2; EnergyMake=25; MetalMake=1.5; Weapon1=ARM_LASER; ExplodeAs=COMMANDER_BLAST; }''',
}
WEAPONS = '''
[ARM_LASER] { name=Laser; range=180; reloadtime=1; weaponvelocity=800; beamweapon=1; [DAMAGE] { default=25; } }
[ARM_DISINTEGRATOR] { name=D-Gun; range=250; reloadtime=1; weaponvelocity=350; commandfire=1; energypershot=500;
                      areaofeffect=36; [DAMAGE] { default=6000; } }
[EMG] { range=180; reloadtime=0.4; weaponvelocity=500; [DAMAGE] { default=10; } }
[ARM_TORPEDO] { range=500; reloadtime=6; weaponvelocity=100; waterweapon=1; [DAMAGE] { default=300; } }
[NUCLEAR_MISSILE] { range=72000; reloadtime=180; weaponvelocity=550; stockpile=1; targetable=1; metalpershot=1000;
                    energypershot=60000; areaofeffect=1000; [DAMAGE] { default=5500; } }
[COMMANDER_BLAST] { areaofeffect=700; [DAMAGE] { default=2500; } }
'''
FEATURES = '''[ROCK1] { description=Rock; footprintx=1; footprintz=1; reclaimable=1; metal=30; blocking=1; }
[TREE1] { description=Tree; footprintx=1; footprintz=1; reclaimable=1; energy=250; blocking=1; }'''
GUI_COM = '''[GADGET0] { [COMMON] { id=0; name=ARMCOM1.GUI; } }
[GADGET1] { [COMMON] { id=12; name=ARMMEX; } }
[GADGET2] { [COMMON] { id=12; name=ARMWIN; } }
[GADGET3] { [COMMON] { id=12; name=ARMLAB; } }
[GADGET4] { [COMMON] { id=12; name=NEXT; } }'''
GUI_LAB = '''[GADGET1] { [COMMON] { id=12; name=ARMPW; } }'''
DOWNLOAD = '''[MENUENTRY1] { UNITMENU=ARMCOM; MENU=2; BUTTON=4; UNITNAME=ARMRAD; }
[MENUENTRY2] { UNITMENU=ARMCOM; MENU=2; BUTTON=5; UNITNAME=ARMSILO; }'''
OTA = '''[GlobalHeader] { missionname=Test Lakes; tidalstrength=18; minwindspeed=500; maxwindspeed=2500;
  [Schema 0] { Type=Network 1; [specials] {
     [special0] { specialwhat=StartPos1; XPos=96; ZPos=128; }
     [special1] { specialwhat=StartPos2; XPos=896; ZPos=640; } } } }'''


def tnt_map():
    w, h = 64, 48
    heights = []
    for y in range(h):
        for x in range(w):
            heights.append(10 if 28 <= x <= 36 else 60 + (x + y) % 7)   # a lake across the middle
    feats = [(10, 10, 0), (12, 30, 1), (40, 20, 2), (11, 12, 0)]
    return tnt.write(w, h, heights, feats, ['Metal1', 'ROCK1', 'TREE1'], sea=20)


def main():
    # ---- tdf parser
    t = tdf.parse('[A] { x=1; // c\n [B] { y = two words ; } } [A] { x=2; }')
    check(t['a']['x'] == '1' and t['a']['b']['y'] == 'two words' and t['a#2']['x'] == '2', 'tdf parse: nesting, comments, repeats')
    # ---- lz77
    check(lz77(lz77_literals(b'ABCD', repeat_at=(4, 4))) == b'ABCDABCD', 'lz77 back reference')
    data = bytes(range(256)) * 300
    check(lz77(lz77_literals(data)) == data, 'lz77 literals over many flag bytes')
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 'ta')
        os.makedirs(src)
        files = {}
        for uid, text in FBI.items():
            files['units/%s.fbi' % uid] = (text.encode(), 2)
        files['weapons/weapons.tdf'] = (WEAPONS.encode(), 1)
        files['features/all/rocks.tdf'] = (FEATURES.encode(), 0)
        files['guis/armcom1.gui'] = (GUI_COM.encode(), 2)
        files['guis/armlab1.gui'] = (GUI_LAB.encode(), 1)
        files['download/menus.tdf'] = (DOWNLOAD.encode(), 0)
        files['objects3d/armcom.3do'] = (b'\x01\x00\x00\x00' + b'\0' * 48, 0)
        big = os.urandom(70000)
        files['sounds/big.wav'] = (big, 2)   # two zlib chunks
        hpi = make_hpi(files)
        with open(os.path.join(src, 'totala1.hpi'), 'wb') as f:
            f.write(hpi)
        h = Hpi(hpi)
        listed = {p.lower(): (o, s, c) for p, o, s, c in h.files()}
        check(set(listed) == set(files), 'hpi lists every file')
        check(h.extract(*listed['sounds/big.wav']) == big, 'hpi multi-chunk zlib file')
        check(h.extract(*listed['weapons/weapons.tdf']) == WEAPONS.encode(), 'hpi lz77 file through the cipher')
        # loose map files (as the game keeps maps of a .ufo next to it, already extracted)
        os.makedirs(os.path.join(src, 'maps'))
        with open(os.path.join(src, 'maps', 'lakes.tnt'), 'wb') as f:
            f.write(tnt_map())
        with open(os.path.join(src, 'maps', 'lakes.ota'), 'w') as f:
            f.write(OTA)
        out = os.path.join(d, 'game')
        r = subprocess.run([sys.executable, os.path.join(HERE, 'import.py'), '--source', src, '--out', out, '--json'],
                           capture_output=True, text=True)
        check(r.returncode == 0, 'importer ran: ' + r.stderr[-400:])
        if r.returncode != 0:
            return
        rep = json.loads(r.stdout.strip().splitlines()[-1])
        check(rep['units'] == len(FBI) and rep['maps'] == ['lakes'] and rep['models'] == 1, 'report: %s' % rep)
        check(rep['factions'] == ['arm', 'core'], 'factions from the commanders')
        with open(os.path.join(out, 'rules', 'units.json')) as f:
            types = json.load(f)['types']
        com = types['armcom']
        check(com['key'] and com['buildPower'] == 3 and com['cost']['time'] == 600, 'commander: key, worker time, build time')
        check(set(com['builds']) == {'armmex', 'armwin', 'armlab', 'armrad', 'armsilo'}, 'commander build menu (gui + download): %s' % com.get('builds'))
        check(com['radar'] == 43.75 and com['abilities'][0]['cost'] == {'energy': 500}, 'commander radar and D-gun')
        check(com['onDeath'][0]['amount'] == 2500, 'commander death blast')
        check(types['armlab']['produces'] == ['armpw'] and 'builds' not in types['armlab'], 'factory produces mobile units')
        check(types['armmex']['structure']['on'] == 'metal_spot' and types['armmex']['income']['metal'] == 1, 'extractor')
        check(types['armwin']['ambientIncome'] == {'wind': {'energy': 1}}, 'wind generator')
        silo = types['armsilo']
        check(silo['stockpile']['abilities'] == ['nuclear_missile'] and silo['stockpile']['cost'] == {'metal': 1000, 'energy': 60000},
              'nuke silo stockpile')
        check('targetable' in silo['tags'] and silo['abilities'][0]['autocast'], 'nuke: interceptable, bot fires it')
        sub = types['armsub']
        check(sub['underwater'] and sub['move']['layer'] == 'naval' and sub['sonar'] == 31.25, 'submarine')
        check(sub['weapons'][0]['underwater'] and sub['weapons'][0]['targets'] == ['naval'], 'torpedo')
        check(types['armpw']['move']['speed'] == 3.19 and types['armpw']['weapons'][0]['range'] == 11.25, 'peewee speed / range')
        check(types['feature_rock1']['resourceNode'] == {'resource': 'metal', 'amount': 30}, 'rock feature')
        check(types['feature_tree1']['resourceNode'] == {'resource': 'energy', 'amount': 250}, 'tree feature')
        with open(os.path.join(out, 'maps', 'lakes.json')) as f:
            m = json.load(f)
        check(m['size'] == [64, 48] and len(m['terrain']['corners']) == 65 * 49, 'map size and corners')
        check(m['terrain']['rows'][5][30] == '~' and m['terrain']['rows'][5][5] == '.', 'sea level -> water')
        check([p['start'] for p in m['players']] == [[6, 8], [56, 40]], 'start positions from the .ota')
        spots = [e for e in m['entities'] if e['type'] == 'metal_spot']
        check(len(spots) == 2, 'metal spots from metal features')
        check(m['ambient']['tidal'] == 18 and m['ambient']['wind']['max'] == 25, 'tide and wind from the .ota')
        check(os.path.exists(os.path.join(out, 'models', 'armcom.3do')), '3do copied')
        # refuses to write into the engine templates
        r2 = subprocess.run([sys.executable, os.path.join(HERE, 'import.py'), '--source', src, '--out',
                             os.path.join(HERE, '..', '..', '..', 'templates', 'ta_import_test')], capture_output=True, text=True)
        check(r2.returncode != 0, 'refuses templates/')
        # the imported game plays (a bot match on the imported map)
        headless = os.environ.get('AIVERSE_PORT_HEADLESS')
        if headless:
            r3 = subprocess.run([headless, '--project', out, '--seed', '1', '--quiet'], capture_output=True, text=True)
            check(r3.returncode == 0 and 'Checksum:' in r3.stdout + r3.stderr, 'headless match on the imported game: ' + (r3.stdout + r3.stderr)[-600:])
    print('ta importer: %d failed' % len(FAILS))
    sys.exit(1 if FAILS else 0)


if __name__ == '__main__':
    main()
