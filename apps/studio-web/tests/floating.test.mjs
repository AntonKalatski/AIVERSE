import test from 'node:test';
import assert from 'node:assert/strict';
import { addFloating, floatingPanelParam, isFloating, readFloating, removeFloating, saveFloating, updateFloatingFrame, FLOATING_KEY } from '../src/floating.ts';

function memory() { const m = new Map(); return { getItem: k => m.has(k) ? m.get(k) : null, setItem: (k, v) => m.set(k, String(v)), m }; }

test('panel query selects a single known panel', () => {
  assert.equal(floatingPanelParam('?panel=inspector'), 'inspector');
  assert.equal(floatingPanelParam('?panel=scene&x=1'), 'scene');
  assert.equal(floatingPanelParam('?panel=nope'), null);
  assert.equal(floatingPanelParam(''), null);
});

test('floating list round-trips and drops junk and duplicates', () => {
  const store = memory();
  saveFloating([{ panel: 'inspector', x: 10, y: 20, width: 400, height: 500 }, { panel: 'assets' }], store);
  assert.deepEqual(readFloating(store), [{ panel: 'inspector', x: 10, y: 20, width: 400, height: 500 }, { panel: 'assets' }]);
  store.setItem(FLOATING_KEY, JSON.stringify([{ panel: 'inspector' }, { panel: 'inspector' }, { panel: 'x' }, 5, { panel: 'game', width: -1, x: 0, y: 0, height: 3 }]));
  assert.deepEqual(readFloating(store), [{ panel: 'inspector' }, { panel: 'game' }]);
  store.setItem(FLOATING_KEY, '{bad');
  assert.deepEqual(readFloating(store), []);
});

test('add, remove and frame updates', () => {
  let list = addFloating([], 'console');
  assert.equal(addFloating(list, 'console'), list);
  assert.ok(isFloating(list, 'console'));
  list = updateFloatingFrame(list, { panel: 'console', x: -1800, y: 100, width: 600, height: 400 });
  assert.deepEqual(list, [{ panel: 'console', x: -1800, y: 100, width: 600, height: 400 }]);
  assert.equal(updateFloatingFrame(list, { panel: 'assets', x: 0, y: 0, width: 1, height: 1 }), list);
  assert.equal(updateFloatingFrame(list, { panel: 'console', x: 'a' }), list);
  assert.deepEqual(removeFloating(list, 'console'), []);
});
