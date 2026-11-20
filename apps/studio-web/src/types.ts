export type { PropertyType } from './propertyLogic';
export type { FacialRigDocument, FacialRigList, FacialRigResult, FacialRigValidation } from './panels/facialRigSchema';
export type StudioProperty = import('./propertyLogic').FieldDescriptor;
export interface StudioEntity { id: string; name: string; kind: string; parentId?: string; visible?: boolean }
export interface StudioLog { level: 'info' | 'warning' | 'error'; text: string; time: string }
export interface StudioAsset {
  path: string;
  bytes: number;
  guid?: string;
  importer?: string;
}
export interface StudioAssetList {
  files: StudioAsset[];
  folders?: StudioAsset[];
  root?: 'content';
}
export interface StudioAssetTransferItem {
  id: number; name: string; from: string; to: string; directory: boolean;
  conflict: string; guid: string | null;
}
export interface StudioAssetTransferPlan {
  token: string; source: 'internal' | 'external'; items: StudioAssetTransferItem[];
  references?: Array<{ id: number; document: string; owner: string; field: string;
    from: string; to: string; writable: boolean }>;
}
export type StudioAssetConflictAction =
  'import' | 'skip' | 'keep-both' | 'replace' | 'new-guid';
export interface StudioAssetTransferResult {
  files: string[]; issues: Array<{name: string; message: string}>;
  repairedMaterialReferences?: number;
}
export interface StudioAssetReference {
  document: string; owner: string; field: string; path: string;
  guid: string; writable: boolean;
}
export interface StudioAssetDetail extends StudioAsset {
  importer: string; guid: string; directory: boolean; settings: Record<string,unknown>;
}
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
export interface StudioEntityCatalog { types: Array<{id: string; name: string}>; mapEntities: boolean }
export interface StudioProjectState { maps: Array<{file: string; title: string}>; currentMap: string; canCreateMap: boolean; maxMapSize: number }
export type StudioMethod = 'studio.project' | 'studio.newMap' | 'studio.openMap' | 'studio.saveAs' | 'studio.entityCatalog' | 'studio.entityCreate' | 'studio.entityDelete' | 'studio.entityDuplicate' | 'studio.snapshot' | 'studio.propertiesFor' | 'studio.select' | 'studio.setProperty' | 'studio.save' | 'studio.undo' | 'studio.redo' | 'studio.viewportRect' | 'studio.viewportInput' | 'studio.openProject' | 'studio.assets' | 'studio.assetByGuid' | 'studio.assetReferences' | 'studio.assetMetaPatch' | 'studio.assetFolderCreate' |
  'studio.assetMovePreview' | 'studio.assetImportPreview' | 'studio.assetTransferCommit' | 'studio.lighting' | 'studio.lightingSet' | 'studio.lightAdd' | 'studio.lightUpdate' | 'studio.lightDelete' | 'studio.materials' | 'studio.materialPatch' | 'studio.materialCreate' | 'studio.materialPreview' | 'studio.shaderGraphs' | 'studio.shaderGraphGet' | 'studio.shaderGraphValidate' | 'studio.shaderGraphSave' | 'studio.shaderGraphCatalog' | 'studio.facialRigList' | 'studio.facialRigGet' | 'studio.facialRigValidate' | 'studio.facialRigSave';
export interface StudioParams {
  'studio.project': Record<string, never>;
  'studio.newMap': {width: number; height: number; name: string};
  'studio.openMap': {file: string};
  'studio.saveAs': Record<string, never>;
  'studio.entityCatalog': Record<string, never>;
  'studio.entityCreate': {type: string; x: number; z: number; owner: number};
  'studio.entityDelete': {entityId: string};
  'studio.entityDuplicate': {entityId: string};
  'studio.snapshot': Record<string, never>;
  'studio.propertiesFor': { entityId: string };
  'studio.save': Record<string, never>;
  'studio.undo': Record<string, never>;
  'studio.redo': Record<string, never>;
  'studio.openProject': Record<string, never>;
  'studio.assets': Record<string, never>;
  'studio.assetByGuid': { guid: string };
  'studio.assetReferences': Record<string, never>;
  'studio.assetMetaPatch': {guid:string;key:string;value:string|number|boolean};
  'studio.assetFolderCreate': { parent: string; name: string };
  'studio.assetMovePreview': { paths: string[]; destination: string };
  'studio.assetImportPreview': { destination: string };
  'studio.assetTransferCommit': { token: string; decisions: Array<{id:number;action:StudioAssetConflictAction}> };
  'studio.materials': Record<string, never>;
  'studio.materialPatch': { name: string; patch: import('./panels/materialSchema').MaterialPatch };
  'studio.materialCreate': { name: string; preset: import('./panels/materialSchema').MaterialPreset };
  'studio.materialPreview': { name: string };
  'studio.shaderGraphs': Record<string, never>;
  'studio.shaderGraphGet': { name: string };
  'studio.shaderGraphCatalog': Record<string, never>;
  'studio.facialRigList': Record<string, never>;
  'studio.facialRigGet': { name: string };
  'studio.facialRigValidate': { rig: import('./panels/facialRigSchema').FacialRigDocument };
  'studio.facialRigSave': { name: string; rig: import('./panels/facialRigSchema').FacialRigDocument };
  'studio.shaderGraphValidate': { graph: StudioShaderGraphDocument['graph'] };
  'studio.shaderGraphSave': { name: string; graph: StudioShaderGraphDocument['graph'] };
  'studio.lighting': Record<string, never>;
  'studio.lightingSet': { key: import('./panels/lightingSchema').LightingScalarKey; value: number };
  'studio.lightAdd': Record<string, never>;
  'studio.lightUpdate': { index: number; key: import('./panels/lightingSchema').LightEditableKey; value: string | number | boolean };
  'studio.lightDelete': { index: number };
  'studio.select': { id: string | null };
  'studio.setProperty': { entityId: string; key: string; value: string | number | boolean };
  'studio.viewportRect': { id: 'scene'; x: number; y: number; width: number; height: number; dpr: number; visible: boolean };
  'studio.viewportInput': { kind: 'orbit' | 'pan' | 'look'; dx: number; dy: number } | { kind: 'move'; right: number; up: number; forward: number } | { kind: 'focus' | 'reset' } | { kind: 'zoom'; amount: number } | { kind: 'pick'; x: number; y: number; width: number; height: number };
}
export interface StudioResult {
  'studio.project': StudioProjectState;
  'studio.newMap': StudioSnapshot;
  'studio.openMap': StudioSnapshot;
  'studio.saveAs': StudioSnapshot;
  'studio.entityCatalog': StudioEntityCatalog;
  'studio.entityCreate': StudioSnapshot;
  'studio.entityDelete': StudioSnapshot;
  'studio.entityDuplicate': StudioSnapshot;
  'studio.snapshot': StudioSnapshot;
  'studio.propertiesFor': StudioProperty[];
  'studio.save': StudioSnapshot;
  'studio.undo': StudioSnapshot;
  'studio.redo': StudioSnapshot;
  'studio.openProject': { cancelled: boolean; snapshot: StudioSnapshot | null };
  'studio.assets': StudioAssetList;
  'studio.assetByGuid': StudioAssetDetail;
  'studio.assetReferences': StudioAssetReference[];
  'studio.assetMetaPatch': StudioAssetDetail;
  'studio.assetFolderCreate': { path: string };
  'studio.assetMovePreview': StudioAssetTransferPlan;
  'studio.assetImportPreview': StudioAssetTransferPlan | { cancelled: true };
  'studio.assetTransferCommit': StudioAssetTransferResult;
  'studio.materials': StudioMaterialLibrary;
  'studio.materialPatch': StudioMaterialLibrary;
  'studio.materialCreate': StudioMaterialLibrary;
  'studio.materialPreview': { active: boolean; name: string };
  'studio.shaderGraphs': StudioShaderGraphList;
  'studio.shaderGraphGet': StudioShaderGraphDocument;
  'studio.shaderGraphCatalog': StudioShaderGraphCatalog;
  'studio.facialRigList': import('./panels/facialRigSchema').FacialRigList;
  'studio.facialRigGet': import('./panels/facialRigSchema').FacialRigResult;
  'studio.facialRigValidate': import('./panels/facialRigSchema').FacialRigValidation;
  'studio.facialRigSave': import('./panels/facialRigSchema').FacialRigResult;
  'studio.shaderGraphValidate': StudioShaderGraphDocument;
  'studio.shaderGraphSave': StudioShaderGraphDocument;
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
