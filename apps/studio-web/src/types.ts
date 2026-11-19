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
  history: { canUndo: boolean; canRedo: boolean };
}
export type StudioMethod = 'studio.snapshot' | 'studio.select' | 'studio.setProperty' | 'studio.save' | 'studio.undo' | 'studio.redo' | 'studio.viewportRect' | 'studio.openProject';
export interface StudioParams {
  'studio.snapshot': Record<string, never>;
  'studio.save': Record<string, never>;
  'studio.undo': Record<string, never>;
  'studio.redo': Record<string, never>;
  'studio.openProject': Record<string, never>;
  'studio.select': { id: string | null };
  'studio.setProperty': { entityId: string; key: string; value: string | number | boolean };
  'studio.viewportRect': { id: 'scene'; x: number; y: number; width: number; height: number; dpr: number; visible: boolean };
}
export interface StudioResult {
  'studio.snapshot': StudioSnapshot;
  'studio.save': StudioSnapshot;
  'studio.undo': StudioSnapshot;
  'studio.redo': StudioSnapshot;
  'studio.openProject': { cancelled: boolean; snapshot: StudioSnapshot | null };
  'studio.select': StudioSnapshot;
  'studio.setProperty': StudioSnapshot;
  'studio.viewportRect': { ok: boolean };
}
