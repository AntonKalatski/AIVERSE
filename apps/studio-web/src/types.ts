export type PropertyType = 'text' | 'number' | 'boolean';
export interface StudioProperty { key: string; label: string; value: string | number | boolean; type: PropertyType }
export interface StudioEntity { id: string; name: string; kind: string; parentId?: string; visible?: boolean }
export interface StudioLog { level: 'info' | 'warning' | 'error'; text: string; time: string }
export interface StudioSnapshot {
  project: { name: string; path: string; dirty: boolean };
  scene: { name: string; entities: StudioEntity[] };
  selectedId: string | null;
  properties: StudioProperty[];
  logs: StudioLog[];
  mode: 'edit' | 'play';
}
export type StudioMethod = 'studio.snapshot' | 'studio.select' | 'studio.setProperty';
export interface StudioParams {
  'studio.snapshot': Record<string, never>;
  'studio.select': { id: string | null };
  'studio.setProperty': { entityId: string; key: string; value: string | number | boolean };
}
export interface StudioResult {
  'studio.snapshot': StudioSnapshot;
  'studio.select': StudioSnapshot;
  'studio.setProperty': StudioSnapshot;
}
