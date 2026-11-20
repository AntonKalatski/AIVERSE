export type { PropertyType } from './propertyLogic';
export type StudioProperty = import('./propertyLogic').FieldDescriptor;
export interface StudioEntity { id: string; name: string; kind: string; parentId?: string; visible?: boolean }
export interface StudioLog { level: 'info' | 'warning' | 'error'; text: string; time: string }
export interface StudioAsset { path: string; bytes: number }
export interface StudioAssetList { files: StudioAsset[] }
export type StudioLightingState = import('./panels/lightingSchema').LightingState;
export type StudioMaterialLibrary = import('./panels/materialSchema').MaterialLibrary;
export interface StudioShaderGraphList { graphs: string[] }
export interface StudioShaderGraphOp { id:string; inputs:number; domains:string[] }
export interface StudioShaderGraphCatalog { nodes:StudioShaderGraphOp[] }
export interface StudioShaderGraphDocument {
  graph: { version?: number; domain?: string; nodes?: Array<{id:string;op:string;inputs?:string[];position?:number[];value?:number[]}>; outputs?:Record<string,string> };
  nodes: number; samples: number;
}
export interface StudioSnapshot {
  project: { name: string; path: string; dirty: boolean };
  scene: { name: string; entities: StudioEntity[] };
  selectedId: string | null;
  properties: StudioProperty[];
  logs: StudioLog[];
  mode: 'edit' | 'play';
  previewMaterial?: string | null;
  history: { canUndo: boolean; canRedo: boolean };
}
export interface RenderLightingSettings { version: 1; indirect: 'off'|'cpu'|'auto'|'gpu'; direct: 'legacy'|'luma'; quality: 'balanced'|'high'|'ultra'; probes: number; layers: number; spacing: number; samples: number; distance: number; cpuBudgetMs: number }
export type StudioMethod = 'studio.renderLighting' | 'studio.renderLightingSet' | 'studio.snapshot' | 'studio.select' | 'studio.setProperty' | 'studio.save' | 'studio.undo' | 'studio.redo' | 'studio.viewportRect' | 'studio.viewportInput' | 'studio.openProject' | 'studio.assets' | 'studio.lighting' | 'studio.lightingSet' | 'studio.lightAdd' | 'studio.lightUpdate' | 'studio.lightDelete' | 'studio.materials' | 'studio.materialPatch' | 'studio.materialCreate' | 'studio.materialPreview' | 'studio.shaderGraphs' | 'studio.shaderGraphGet' | 'studio.shaderGraphValidate' | 'studio.shaderGraphSave' | 'studio.shaderGraphCatalog';
export interface StudioParams {
  'studio.snapshot': Record<string, never>;
  'studio.save': Record<string, never>;
  'studio.undo': Record<string, never>;
  'studio.redo': Record<string, never>;
  'studio.openProject': Record<string, never>;
  'studio.assets': Record<string, never>;
  'studio.materials': Record<string, never>;
  'studio.materialPatch': { name: string; patch: import('./panels/materialSchema').MaterialPatch };
  'studio.materialCreate': { name: string; preset: import('./panels/materialSchema').MaterialPreset };
  'studio.materialPreview': { name: string };
  'studio.shaderGraphs': Record<string, never>;
  'studio.shaderGraphGet': { name: string };
  'studio.shaderGraphCatalog': Record<string, never>;
  'studio.shaderGraphValidate': { graph: StudioShaderGraphDocument['graph'] };
  'studio.shaderGraphSave': { name: string; graph: StudioShaderGraphDocument['graph'] };
  'studio.renderLighting': Record<string, never>;
  'studio.renderLightingSet': { patch: Partial<RenderLightingSettings> };
  'studio.lighting': Record<string, never>;
  'studio.lightingSet': { key: import('./panels/lightingSchema').LightingScalarKey; value: number };
  'studio.lightAdd': Record<string, never>;
  'studio.lightUpdate': { index: number; key: import('./panels/lightingSchema').LightEditableKey; value: string | number | boolean };
  'studio.lightDelete': { index: number };
  'studio.select': { id: string | null };
  'studio.setProperty': { entityId: string; key: string; value: string | number | boolean };
  'studio.viewportRect': { id: 'scene'; x: number; y: number; width: number; height: number; dpr: number; visible: boolean };
  'studio.viewportInput': { kind: 'orbit'; dx: number; dy: number } | { kind: 'zoom'; amount: number } | { kind: 'pick'; x: number; y: number; width: number; height: number };
}
export interface StudioResult {
  'studio.snapshot': StudioSnapshot;
  'studio.save': StudioSnapshot;
  'studio.undo': StudioSnapshot;
  'studio.redo': StudioSnapshot;
  'studio.openProject': { cancelled: boolean; snapshot: StudioSnapshot | null };
  'studio.assets': StudioAssetList;
  'studio.materials': StudioMaterialLibrary;
  'studio.materialPatch': StudioMaterialLibrary;
  'studio.materialCreate': StudioMaterialLibrary;
  'studio.materialPreview': { active: boolean; name: string };
  'studio.shaderGraphs': StudioShaderGraphList;
  'studio.shaderGraphGet': StudioShaderGraphDocument;
  'studio.shaderGraphCatalog': StudioShaderGraphCatalog;
  'studio.shaderGraphValidate': StudioShaderGraphDocument;
  'studio.shaderGraphSave': StudioShaderGraphDocument;
  'studio.renderLighting': RenderLightingSettings;
  'studio.renderLightingSet': RenderLightingSettings;
  'studio.lighting': StudioLightingState;
  'studio.lightingSet': StudioLightingState;
  'studio.lightAdd': StudioLightingState;
  'studio.lightUpdate': StudioLightingState;
  'studio.lightDelete': StudioLightingState;
  'studio.select': StudioSnapshot;
  'studio.setProperty': StudioSnapshot;
  'studio.viewportRect': { ok: boolean };
  'studio.viewportInput': { ok: boolean };
}
