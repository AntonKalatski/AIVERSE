import test from 'node:test';
import assert from 'node:assert/strict';
import { filterLogs, levelCounts, logParts, logSources } from '../src/panels/consoleLogic.ts';

const logs = [
  { level: 'info', text: 'opened maps/valley.json', time: 'studio', source: 'studio' },
  { level: 'error', text: 'bad value on entity:@hero7', time: 'mcp', source: 'mcp' },
  { level: 'info', text: 'saved', time: 'mcp', source: 'mcp' },
];
test('filters by level, source and text', () => {
  assert.equal(filterLogs(logs, { level: 'error', source: '', query: '' }).length, 1);
  assert.equal(filterLogs(logs, { level: 'all', source: 'mcp', query: '' }).length, 2);
  assert.equal(filterLogs(logs, { level: 'all', source: '', query: 'VALLEY' }).length, 1);
  assert.deepEqual(levelCounts(logs), { info: 2, warning: 0, error: 1 });
  assert.deepEqual(logSources(logs), ['mcp', 'studio']);
});
test('entity ids and project files become links', () => {
  assert.deepEqual(logParts('bad value on entity:@hero7'), [{ kind: 'text', text: 'bad value on ' }, { kind: 'entity', text: 'entity:@hero7', id: 'entity:@hero7' }]);
  const parts = logParts('opened maps/valley.json, then content/trees/oak.glb');
  assert.deepEqual(parts.filter(p => p.kind === 'file').map(p => p.path), ['maps/valley.json', 'content/trees/oak.glb']);
  assert.deepEqual(logParts('plain'), [{ kind: 'text', text: 'plain' }]);
});
