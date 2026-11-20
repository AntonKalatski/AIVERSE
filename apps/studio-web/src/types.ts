export type { PropertyType } from './propertyLogic';
export type { FacialRigDocument, FacialRigList, FacialRigResult, FacialRigValidation } from './panels/facialRigSchema';

// Native asset GUID and staged facial inputs; NOT a GPU preview result.
export interface StudioFacialPreviewBinding {
  bound: boolean;
  guid: string;
  path: string; // content-relative path, resolved by native AssetCatalog
  revision: number; // binding epoch, must match control updates
  values: Record<string,number>;
  vectors: Record<string,[number,number,number]>;
  runtimeReady: boolean; // set only after an actual native Metal Renderer3D frame
  runtimeVerified: false; // GPU pixels/backends have not passed native QA
  rendererChannelCapacity: number;
  reason: string;
}

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
}
export type StudioAssetConflictAction =
  'import' | 'skip' | 'keep-both' | 'replace' | 'new-guid';
export interface StudioAssetTransferResult {
  files: string[]; issues: Array<{name: string; message: string}>;
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
export type StudioMethod = 'studio.snapshot' | 'studio.propertiesFor' | 'studio.select' | 'studio.setProperty' | 'studio.save' | 'studio.undo' | 'studio.redo' | 'studio.viewportRect' | 'studio.viewportInput' | 'studio.openProject' | 'studio.assets' | 'studio.assetByGuid' | 'studio.assetMetaPatch' | 'studio.assetFolderCreate' |
  'studio.assetMovePreview' | 'studio.assetImportPreview' | 'studio.assetTransferCommit' | 'studio.lighting' | 'studio.lightingSet' | 'studio.lightAdd' | 'studio.lightUpdate' | 'studio.lightDelete' | 'studio.materials' | 'studio.materialPatch' | 'studio.materialCreate' | 'studio.materialPreview' | 'studio.shaderGraphs' | 'studio.shaderGraphGet' | 'studio.shaderGraphValidate' | 'studio.shaderGraphSave' | 'studio.shaderGraphCatalog' | 'studio.facialRigList' | 'studio.facialRigGet' | 'studio.facialRigValidate' | 'studio.facialRigSave' | 'studio.facialPreviewBind' | 'studio.facialPreviewControls' | 'studio.facialPreviewInfo' | 'studio.humanAppearanceInfo' | 'studio.humanAppearanceApply' | 'studio.humanAppearanceSave' | 'studio.humanAppearanceLoad' | 'studio.humanAppearanceList' | 'studio.humanAppearanceFromAsset' | 'studio.humanWardrobeFromAsset' | 'studio.humanPreviewAnimation' | 'studio.humanStudioLighting' | 'studio.humanFaceFitScan' | 'studio.humanHeadBodyImport' | 'studio.humanWardrobeInfo' | 'studio.humanCharacterBuild' | 'studio.humanCharacterBuildStatus';
export interface HumanLook { version: 1; values: Record<string,number>; materials: Record<string,string> }
export interface HumanLookField { key:string; label:string; min:number; max:number; initial:number }
export interface HumanPreviewAnimationSettings {pose:string;playing:boolean;faceIdle:boolean;speed:number}
export interface HumanStudioLightingSettings {preset:string;rotation:number;gain:number;backdrop:boolean}
export interface HumanStudioLightingInfo {settings:HumanStudioLightingSettings;presets:{id:string;label:string;sources:number}[]}
export interface HumanFaceShape {version:1;meshes:string[];values:Record<string,number>;landmarks:{from:[number,number,number];to:[number,number,number];radius:number;weight:number}[]}
export interface HumanLookInfo {face:HumanFaceShape|null;faceSchema:{key:string;label:string;min:number;max:number}[];schema:HumanLookField[];look:HumanLook;preview:StudioFacialPreviewBinding}
export interface StudioParams {
  'studio.humanWardrobeFromAsset': {guid:string};
  'studio.humanPreviewAnimation': {settings?:HumanPreviewAnimationSettings};
  'studio.humanStudioLighting': {settings?:HumanStudioLightingSettings};
  'studio.humanFaceFitScan': {guid:string;scanGuid:string;face:HumanFaceShape;distance:number;strength:number};
  'studio.humanHeadBodyImport': {guid:string;headGuid:string;bone:string;hiddenMeshes:string[];name:string};
  'studio.humanWardrobeInfo': {guid:string};
  'studio.humanCharacterBuild': {guid:string;look:HumanLook;wardrobe:HumanWardrobe;name:string;textureSize:number;face?:HumanFaceShape|null};
  'studio.humanCharacterBuildStatus': Record<string,never>;
  'studio.humanAppearanceInfo': Record<string,never>;
  'studio.humanAppearanceApply': {guid:string;revision:number;look:HumanLook;face?:HumanFaceShape|null};
  'studio.humanAppearanceSave': {name:string;look:HumanLook};
  'studio.humanAppearanceLoad': {name:string};
  'studio.humanAppearanceList': Record<string,never>;
  'studio.humanAppearanceFromAsset': {guid:string};
  'studio.snapshot': Record<string, never>;
  'studio.propertiesFor': { entityId: string };
  'studio.save': Record<string, never>;
  'studio.undo': Record<string, never>;
  'studio.redo': Record<string, never>;
  'studio.openProject': Record<string, never>;
  'studio.assets': Record<string, never>;
  'studio.assetByGuid': { guid: string };
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
  'studio.facialPreviewBind': { guid: string; rig: import('./panels/facialRigSchema').FacialRigDocument } | { guid: '' };
  'studio.facialPreviewControls': { revision: number; values: Record<string,number>; vectors?: Record<string,[number,number,number]> };
  'studio.facialPreviewInfo': Record<string,never>;
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
  'studio.viewportInput': { kind: 'orbit'; dx: number; dy: number } | { kind: 'zoom'; amount: number } | { kind: 'pick'; x: number; y: number; width: number; height: number };
}
export interface StudioResult {
  'studio.humanWardrobeFromAsset': HumanWardrobe;
  'studio.humanWardrobeInfo': {meshes:string[];bones:string[];materials:string[]};
  'studio.humanPreviewAnimation': HumanPreviewAnimationSettings;
  'studio.humanStudioLighting': HumanStudioLightingInfo;
  'studio.humanFaceFitScan': HumanBuildStatus;
  'studio.humanHeadBodyImport': HumanBuildStatus;
  'studio.humanCharacterBuild': HumanBuildStatus;
  'studio.humanCharacterBuildStatus': HumanBuildStatus;
  'studio.humanAppearanceInfo': HumanLookInfo;
  'studio.humanAppearanceApply': HumanLookInfo;
  'studio.humanAppearanceSave': {name:string;look:HumanLook};
  'studio.humanAppearanceLoad': {name:string;look:HumanLook};
  'studio.humanAppearanceList': {looks:string[]};
  'studio.humanAppearanceFromAsset': {name:string;look:HumanLook};
  'studio.snapshot': StudioSnapshot;
  'studio.propertiesFor': StudioProperty[];
  'studio.save': StudioSnapshot;
  'studio.undo': StudioSnapshot;
  'studio.redo': StudioSnapshot;
  'studio.openProject': { cancelled: boolean; snapshot: StudioSnapshot | null };
  'studio.assets': StudioAssetList;
  'studio.assetByGuid': StudioAssetDetail;
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
  'studio.facialPreviewBind': StudioFacialPreviewBinding;
  'studio.facialPreviewControls': StudioFacialPreviewBinding;
  'studio.facialPreviewInfo': StudioFacialPreviewBinding;
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

export interface HumanWardrobeItem {id:string;asset:string;slot:string;enabled:boolean;bone:string;translation:number[];rotation:number[];scale:number[];finish:'cloth'|'source';hideMeshes:string[];masks:{material:string;texture:string}[]}
export interface HumanWardrobe {version:1;hiddenMeshes:string[];items:HumanWardrobeItem[]}
export interface HumanBuildStatus {face?:HumanFaceShape;running:boolean;error?:string;path?:string;guid?:string;vertices?:number;meshes?:number;materials?:number;bytes?:number}
