// AI-friendly rendering surface: keep App.tsx as the application shell.
// For a new editor panel, register a typed panel ID in nestedDock.ts,
// then insert one render case below. All data mutations still use Studio Bridge.
import type { ReactNode } from 'react';
import { StudioSection } from '../ui';
import {
  Activity, Box, ChevronDown, FolderOpen,
  SlidersHorizontal, Focus, RotateCcw, Lock, LockOpen
} from 'lucide-react';
import type { StudioEntity, StudioProperty, StudioSnapshot } from '../types';
import { invoke } from '../bridge';
import type { StudioPanel } from '../nestedDock';
import PanelTitle from '../components/PanelTitle';
import PropertyField from '../components/PropertyField';
import ViewportPlaceholder from '../components/ViewportPlaceholder';
import AssetsPanel from '../components/AssetsPanel';
import LightingPanel from './LightingPanel';
import MaterialsPanel from './MaterialsPanel';
import ShaderGraphPanel from './ShaderGraphPanel';
import FacialRigPanel from './FacialRigPanel';
import { studioSceneMode } from './sceneState';
import HierarchyPanel from './HierarchyPanel';
import GamePanel from './GamePanel';

import ProjectPanel from './generated/ProjectPanel';

import RuntimePanel from './generated/RuntimePanel';

import NereidFlowPanel from './generated/NereidFlowPanel';

import WindPanel from './generated/WindPanel';

export interface StudioPanelContext {
  snapshot: StudioSnapshot | null;
  busy: boolean;
  setBusy: (value: boolean) => void;
  updateSnapshot: (value: StudioSnapshot) => void;
  connected: boolean;
  openProject: () => Promise<void>;
  search: string;
  setSearch: (value: string) => void;
  rows: Array<{node: StudioEntity; depth: number}>;
  selected: StudioEntity | undefined;
  inspectorProperties: StudioProperty[];
  inspectorPinned: boolean;
  toggleInspectorPin: () => void;
  select: (id: string | null) => Promise<void>;
  changeProperty: (key: string, value: string | number | boolean, componentId?: string) => Promise<void>;
}

export function renderStudioPanel(panel: StudioPanel, ctx: StudioPanelContext): ReactNode {
  const {snapshot,busy,connected,openProject,selected,inspectorProperties,inspectorPinned,toggleInspectorPin,changeProperty,select}=ctx;
  const sceneMode=studioSceneMode(connected,snapshot?.project.path);
    switch (panel) {
      case 'hierarchy':
        return <HierarchyPanel {...ctx}/>;
      case 'assets':
        return <AssetsPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'inspector':
        return <>
          <PanelTitle title="INSPECTOR" right={
            <button className="icon-button" onClick={toggleInspectorPin}
              disabled={!selected} aria-pressed={inspectorPinned}
              title={inspectorPinned ? 'Unpin Inspector from selected object' : 'Lock Inspector to current object'}
              aria-label={inspectorPinned ? 'Unlock Inspector' : 'Lock Inspector'}>
              {inspectorPinned ? <Lock size={16}/> : <LockOpen size={16}/>}
            </button>}/>
          {selected ? <>
            <div className="selected-object compact-object-header">
              <input type="checkbox" className="au-checkbox__input" aria-label="Object active"
                checked={selected.visible !== false} disabled
                title="Object activation is not yet exposed by the native document API"/>
              <input className="au-input" aria-label="Object name" value={selected.name} readOnly
                title="Object naming is not yet exposed by the native document API"/>
            </div>
            {(!inspectorPinned ? snapshot?.entityProperties : [])?.map(property =>
              <PropertyField key={'entity:' + selected.id + ':' + property.key} property={property}
                disabled={busy || property.editable === false}
                onChange={value => void changeProperty(property.key, value, 'Entity')}/>)}
            {(!inspectorPinned ? snapshot?.components : [])?.map(component =>
              <StudioSection key={selected.id + ':' + component.id} title={component.title}>
                {component.properties.map(property =>
                  <PropertyField key={component.id + ':' + property.key} property={property}
                    disabled={busy || property.editable === false}
                    onChange={value => void changeProperty(property.key, value, component.id)}/>)}
              </StudioSection>)}
            {!!inspectorProperties.length && <div className="inspector-section">
              <div className="section-heading"><ChevronDown size={13}/> Properties</div>
              {inspectorProperties.map(property =>
                <PropertyField key={selected.id + ':' + property.key}
                  property={property} disabled={busy || property.editable === false}
                  onChange={value => void changeProperty(property.key, value)}/>)}
            </div>}
          </> : <div className="empty-panel">Select an entity to inspect its properties.</div>}
        </>;
      case 'scene':
        return <>
          <div className="viewport-toolbar">
            <div className="viewport-tools">
              <button className="viewport-control selected" title="Select tool">
                <SlidersHorizontal size={15}/></button>
              <span className="toolbar-divider"/>
              <span className="viewport-label">Perspective</span>
              <button type="button" className="viewport-control" title="Focus selected object (F)"
                aria-label="Focus selected Scene object" disabled={!connected || !selected}
                onClick={()=>void invoke('studio.viewportInput',{kind:'focus'}).catch(console.warn)}>
                <Focus size={15}/></button>
              <button type="button" className="viewport-control" title="Reset Scene camera (Home)"
                aria-label="Reset Scene camera" disabled={!connected}
                onClick={()=>void invoke('studio.viewportInput',{kind:'reset'}).catch(console.warn)}>
                <RotateCcw size={15}/></button>
            </div>
            <span className="render-badge"><Activity size={13}/>
              {sceneMode === 'welcome' ? 'Open project to render' : 'Native Renderer3D (experimental)'}
            </span>
          </div>
          {sceneMode === 'welcome' ?
            <div className="studio-no-project" role="status">
              <div className="studio-no-project-symbol"><FolderOpen size={32} strokeWidth={1.25}/></div>
              <h2>No project open</h2>
              <p>Open an AIVERSE project to inspect its scene, edit objects,
                and render the world using the native engine.</p>
              <button className="studio-no-project-action"
                disabled={busy}
                onClick={() => void openProject()}><FolderOpen size={15}/> Open Project</button>
            </div> :
            <ViewportPlaceholder title="Scene viewport" active/>}
        </>;
      case 'console':
        return <div className="log-list">
          {snapshot?.logs.map((line, i) =>
            <div className={'log-row log-' + line.level} key={i}>
              <span>{line.time}</span>{line.text}
            </div>)}
        </div>;
      case 'lighting':
        return <LightingPanel projectPath={snapshot?.project.path ?? ''} selectedId={snapshot?.selectedId ?? null} onSelect={select}/>;
      case 'materials':
        return <MaterialsPanel projectPath={snapshot?.project.path ?? ''}
          previewMaterial={snapshot?.previewMaterial}/>;
      case 'shader_graph':
        return <ShaderGraphPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'project': return <ProjectPanel {...ctx}/>;
      case 'facial_rig':
        return <FacialRigPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'runtime': return <RuntimePanel snapshot={snapshot}/>;
      case 'nereid_flow': return <NereidFlowPanel snapshot={snapshot}/>;
      case 'wind': return <WindPanel snapshot={snapshot}/>;
      // AI_PANEL_CASES: add new panel renderers above this line.
      case 'game': return <GamePanel {...ctx}/>;
      case 'animation':
        return <div className="dock-unported" role="status">
          <Box size={28}/>
          <strong>{panel.charAt(0).toUpperCase() + panel.slice(1)} editor</strong>
          <p>HTML5 panel migration is pending. The legacy native editor remains available.</p>
        </div>;
    }
}
