#!/usr/bin/env python3
"""Tests of tools/rules: rules_io (read / write split rules) and split_rules (cut rules.json into files)."""
from pathlib import Path
import json, sys, tempfile, unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import rules_io, split_rules


def sample():
    types = {}
    for i in range(30):
        types['port_crane_%d' % i] = {'health': 100 + i, 'note': 'x' * 400}
    for i in range(30):
        types['metro_car_%d' % i] = {'health': 50, 'note': 'y' * 400}
    types['sedan'] = {'health': 300}
    return {'name': 'Sample', 'resources': {'gold': {}}, 'types': types,
            'driving': {'vehicles': {'sedan': {'mass': 1200}}}, 'ai': {'difficulty': 2}}


class Rules(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.project = Path(self.tmp.name)
        (self.project / 'rules.json').write_text(json.dumps(sample(), indent=2))

    def tearDown(self):
        self.tmp.cleanup()

    def plan(self, **kw):
        return split_rules.Plan(dict({'typeLimitKB': 8}, **kw))

    def test_plain_file_reads_and_writes_like_json(self):
        doc = rules_io.load(self.project)
        self.assertEqual(doc, sample())
        doc['ai']['difficulty'] = 3
        rules_io.save(self.project, doc)
        self.assertEqual(json.loads((self.project / 'rules.json').read_text())['ai']['difficulty'], 3)

    def test_split_keeps_the_composed_document_and_order(self):
        self.assertEqual(split_rules.split(self.project, self.plan(), out=lambda *_: None), 0)
        root = json.loads((self.project / 'rules.json').read_text())
        self.assertIn('rules/driving.json', root['include'])
        self.assertIn('rules/ai.json', root['include'])
        self.assertNotIn('types', root)
        doc = rules_io.load(self.project)
        self.assertEqual(list(doc['types']), list(sample()['types']))   # type order is part of the game
        self.assertEqual(doc['types'], sample()['types'])
        names = sorted(p.name for p in (self.project / 'rules/types').iterdir())
        self.assertTrue(any('port' in n for n in names) and any('metro' in n for n in names), names)
        for p in (self.project / 'rules/types').iterdir():
            self.assertLessEqual(len(json.dumps(json.loads(p.read_text()))), 16 * 1024)
        self.assertEqual(split_rules.split(self.project, self.plan(), check=True, out=lambda *_: None), 0)

    def test_edits_go_back_to_the_file_they_came_from(self):
        split_rules.split(self.project, self.plan(), out=lambda *_: None)
        doc = rules_io.load(self.project)
        doc['types']['metro_car_3']['health'] = 77
        doc['types']['metro_car_new'] = {'health': 1}
        rules_io.save(self.project, doc)
        holder = [p for p in (self.project / 'rules/types').iterdir() if 'metro_car_3' in json.loads(p.read_text())['types']]
        self.assertEqual(len(holder), 1)
        held = json.loads(holder[0].read_text())['types']
        self.assertEqual(held['metro_car_3']['health'], 77)
        self.assertNotIn('types', json.loads((self.project / 'rules.json').read_text()))
        again = rules_io.load(self.project)
        self.assertEqual(again['types']['metro_car_new'], {'health': 1})
        order = list(again['types'])
        self.assertEqual(order.index('metro_car_new'), len(order) - 1)   # next to the member before it

    def test_kinds_by_prefix_or_first_word(self):
        p = split_rules.Plan()
        self.assertEqual(p.kind('port_crane'), 'port')
        self.assertEqual(p.kind('sedan'), 'misc')
        q = split_rules.Plan({'prefixes': ['port_crane_']})
        self.assertEqual(q.kind('port_crane_1'), 'port_crane')
        self.assertEqual(q.kind('metro_car'), 'misc')

    def test_rules_file_is_path_like(self):
        split_rules.split(self.project, self.plan(), out=lambda *_: None)
        f = rules_io.rules_file(self.project)
        doc = json.loads(f.read_text())
        self.assertEqual(doc['ai'], {'difficulty': 2})
        doc['ai']['difficulty'] = 9
        f.write_text(json.dumps(doc))
        self.assertEqual(json.loads((self.project / 'rules/ai.json').read_text())['ai']['difficulty'], 9)


if __name__ == '__main__':
    unittest.main()
