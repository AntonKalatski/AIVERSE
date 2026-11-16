#!/usr/bin/env python3
"""Unit tests for tools/bots/sweep.py (output parsing and classification; no engine binary needed)."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('sweep', Path(__file__).with_name('sweep.py'))
sweep = importlib.util.module_from_spec(spec); spec.loader.exec_module(sweep)

OUT = '''Result: winner=Red  612 game-s  commands=340  sim=900 ms (680x realtime)
Player: slot=1 name=Red team=1 alive=1 commands=200
Player: slot=2 name=Blue team=2 alive=0 commands=140
Events: spawned=1 died=1 attacks=1 trained=1 built=1 gathered=1 captured=0
Checksum: 0123456789abcdef
'''


class SweepTest(unittest.TestCase):
    def test_parse_and_win(self):
        r = sweep.parse(OUT)
        self.assertEqual((r['winner'], r['seconds'], r['checksum']), ('Red', 612, '0123456789abcdef'))
        self.assertEqual([p['commands'] for p in r['players']], [200, 140])
        self.assertEqual(sweep.classify(r, 0, set()), 'win')

    def test_idle_timeout_error(self):
        r = sweep.parse(OUT.replace('commands=140', 'commands=0'))
        self.assertEqual(sweep.classify(r, 0, set()), 'idle')
        self.assertEqual(sweep.classify(r, 0, {2}), 'win')   # a waves / monsters slot may be silent
        r = sweep.parse(OUT.replace('winner=Red', 'winner=none (time limit)'))
        self.assertEqual(sweep.classify(r, 0, set()), 'timeout')
        self.assertEqual(sweep.classify(sweep.parse('error: boom'), 2, set()), 'error')

    def test_maps_and_scripted_slots(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / 'maps').mkdir()
            (p / 'game.json').write_text('// comment\n{"maps": [{"file": "maps/a.json"}, "maps/b.json"]}')
            (p / 'maps/a.json').write_text(json.dumps({'players': [{}, {'waves': True}, {'monsters': True}]}))
            self.assertEqual(sweep.game_maps(p), ['maps/a.json', 'maps/b.json'])
            self.assertEqual(sweep.scripted_slots(p, 'maps/a.json'), {2, 3})

    def test_table(self):
        r = sweep.parse(OUT); r.update(map='maps/a.json', seed=1, code=0, **{'class': 'win'})
        self.assertIn('| maps/a.json | 1 | win | Red | 612 | 1:200 2:140 |', sweep.table([r]))


if __name__ == '__main__':
    unittest.main()
