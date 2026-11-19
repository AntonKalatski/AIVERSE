import type { StudioMethod, StudioParams, StudioResult, StudioSnapshot } from './types';

let snapshot: StudioSnapshot = {
  project: { name: 'AIVERSE Demo', path: 'DEMO / no native project', dirty: false },
  scene: {
    name: 'Sample scene',
    entities: [
      { id: 'root', name: 'Environment', kind: 'Group', visible: true },
      { id: 'sun', name: 'Directional Light', kind: 'Light', parentId: 'root', visible: true },
      { id: 'terrain', name: 'Terrain', kind: 'Mesh', parentId: 'root', visible: true },
      { id: 'camera', name: 'Main Camera', kind: 'Camera', visible: true },
      { id: 'player', name: 'Player', kind: 'Entity', visible: true }
    ]
  },
  selectedId: 'camera',
  properties: [
    { key: 'name', label: 'Name', type: 'text', value: 'Main Camera' },
    { key: 'x', label: 'Position X', type: 'number', value: 0 },
    { key: 'y', label: 'Position Y', type: 'number', value: 3 },
    { key: 'z', label: 'Position Z', type: 'number', value: -6 },
    { key: 'enabled', label: 'Enabled', type: 'boolean', value: true }
  ],
  logs: [{ level: 'info', text: 'HTML5 Studio preview loaded — native engine is not connected.', time: 'preview' }],
  mode: 'edit'
};
const clone = (): StudioSnapshot => structuredClone(snapshot);

export async function mockInvoke<M extends StudioMethod>(method: M, params: StudioParams[M]): Promise<StudioResult[M]> {
  if (method === 'studio.select') {
    const id = (params as StudioParams['studio.select']).id;
    if (id !== null && !snapshot.scene.entities.some(entity => entity.id === id)) throw Error('Unknown entity: ' + id);
    snapshot.selectedId = id;
    snapshot.properties = id === null ? [] : [
      { key: 'name', label: 'Name', type: 'text', value: snapshot.scene.entities.find(e => e.id === id)?.name ?? '' },
      { key: 'x', label: 'Position X', type: 'number', value: 0 },
      { key: 'y', label: 'Position Y', type: 'number', value: 0 },
      { key: 'z', label: 'Position Z', type: 'number', value: 0 },
      { key: 'enabled', label: 'Enabled', type: 'boolean', value: true }
    ];
  } else if (method === 'studio.setProperty') {
    const { entityId, key, value } = params as StudioParams['studio.setProperty'];
    if (entityId !== snapshot.selectedId) throw Error('Selection changed while editing');
    const property = snapshot.properties.find(p => p.key === key);
    if (!property) throw Error('Unknown property: ' + key);
    if (typeof value !== property.type.replace('text', 'string').replace('number', 'number').replace('boolean', 'boolean')) {
      throw Error('Property type mismatch');
    }
    property.value = value;
    if (key === 'name') {
      const entity = snapshot.scene.entities.find(e => e.id === entityId);
      if (entity) entity.name = String(value);
    }
    snapshot.project.dirty = true;
  } else if (method !== 'studio.snapshot') {
    throw Error('Unsupported operation');
  }
  return clone() as StudioResult[M];
}
