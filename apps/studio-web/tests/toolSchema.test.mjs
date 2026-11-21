import test from 'node:test';
import assert from 'node:assert/strict';
import { buildParams, fieldDraft, fieldLabel, opLabel, resultLine, schemaFields, stateOps } from '../src/panels/toolSchema.ts';
import { TOOL_PANELS, TOOL_PANEL_IDS } from '../src/panels/toolPanels.ts';
import { STUDIO_PANELS } from '../src/nestedDock.ts';

const schema = { properties: { type: { type: 'string' }, minQuality: { type: 'string', enum: ['low', 'medium', 'high'] },
  enabled: { type: 'boolean' }, radius: { type: 'number', minimum: 0, maximum: 10 }, count: { type: 'integer' }, params: { type: 'object' } },
  required: ['type'] };

test('schema fields: required first, kinds from the JSON schema', () => {
  const f = schemaFields(schema);
  assert.equal(f[0].key, 'type'); assert.ok(f[0].required);
  assert.deepEqual(f.map(x => x.kind), ['string', 'enum', 'boolean', 'number', 'integer', 'json']);
  assert.deepEqual(f[1].options, ['low', 'medium', 'high']);
  assert.deepEqual(schemaFields('{}'), []);
});
test('params: empty optional fields are left out, values typed and checked', () => {
  const f = schemaFields(schema);
  assert.deepEqual(buildParams(f, {}).errors, { type: 'Required' });
  const ok = buildParams(f, { type: 'bloom', enabled: 'true', radius: '2.5', count: '3', params: '{"a":1}', minQuality: '' });
  assert.deepEqual(ok.errors, {});
  assert.deepEqual(ok.params, { type: 'bloom', enabled: true, radius: 2.5, count: 3, params: { a: 1 } });
  const bad = buildParams(f, { type: 'x', radius: '11', count: '1.5', minQuality: 'ultra' });
  assert.deepEqual(Object.keys(bad.errors).sort(), ['count', 'minQuality', 'radius']);
});
test('labels', () => {
  assert.equal(fieldLabel('roadType'), 'Road type');
  assert.equal(opLabel('terrain_paint_material', 'terrain'), 'Paint material');
  assert.equal(opLabel('road_add', 'road'), 'Add');
  assert.equal(opLabel('map_info', 'terrain'), 'Map info');
  assert.equal(resultLine({ ok: true, message: 'Готово' }), 'Готово');
  assert.equal(resultLine({ ok: true, warnings: ['a'], chain: [] }), '1 warning · warnings, chain');
});
test('state ops: read-only and runnable without parameters', () => {
  const ops = [{ name: 'a_get', description: '', schema: {}, readOnly: true },
    { name: 'a_info', description: '', schema: { properties: { path: { type: 'string' } }, required: ['path'] }, readOnly: true },
    { name: 'a_set', description: '', schema: {}, readOnly: false }];
  assert.deepEqual(stateOps(ops, ['a_get', 'a_info', 'a_set', 'missing']).map(o => o.name), ['a_get']);
});
test('every tool panel is a registered dock panel', () => {
  for (const id of TOOL_PANEL_IDS) { assert.ok(STUDIO_PANELS.includes(id), id); assert.ok(TOOL_PANELS[id].state.length); }
});
test('choice fields start on their first option', () => {
  const [op] = schemaFields({ properties: { op: { type: 'string', enum: ['raise', 'lower'] } } });
  assert.equal(fieldDraft(op), 'raise');
  const [q] = schemaFields({ properties: { minQuality: { type: 'string', enum: ['low', 'high'] } } });
  assert.equal(fieldDraft(q), '');
});
