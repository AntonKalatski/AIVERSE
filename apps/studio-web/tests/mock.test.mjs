import test from 'node:test';
import assert from 'node:assert/strict';
import { mockInvoke } from '../src/mock.ts';

test('typed mock snapshots remain valid for browser demo', async () => {
  const result = await mockInvoke('studio.snapshot', {});
  assert.equal(result.project.name, 'AIVERSE Demo');
  assert.ok(Array.isArray(result.scene.entities));
  assert.ok(result.history && typeof result.history.canUndo === 'boolean');
});
test('mock Shader Graph endpoints return method-specific result shapes', async () => {
  assert.deepEqual(await mockInvoke('studio.shaderGraphs', {}), {graphs: []});
  assert.deepEqual(await mockInvoke('studio.shaderGraphCatalog', {}), {nodes: []});
  await assert.rejects(mockInvoke('studio.shaderGraphGet', {name: 'test'}), /native project/);
});
test('mock assets, materials and lighting use separate typed envelopes', async () => {
  assert.deepEqual(await mockInvoke('studio.assets', {}), {files: [], folders: [], root: 'content'});
  assert.deepEqual(await mockInvoke('studio.materials', {}), {library: {}});
  assert.deepEqual(await mockInvoke('studio.lighting', {}), {lighting: null, lights: []});
});
test('mock viewport acknowledges layout, not a synthetic GPU frame', async () => {
  assert.deepEqual(await mockInvoke('studio.viewportRect', {
    id:'scene', x:0, y:0, width:320, height:180, dpr:2, visible:true
  }), {ok: true});
});
test('native-only editing rejects in DEMO mode', async () => {
  await assert.rejects(mockInvoke('studio.materialCreate', {name:'steel',preset:'metal'}), /native AIVERSE host/);
  await assert.rejects(mockInvoke('studio.lightAdd', {}), /native AIVERSE host/);
  await assert.rejects(mockInvoke('studio.save', {}), /native AIVERSE host/);
});
