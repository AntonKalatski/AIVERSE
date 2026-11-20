// AI-friendly rendering surface: keep App.tsx as the application shell.
// For a new editor panel, register a typed panel ID in nestedDock.ts,
// then insert one render case below. All data mutations still use Studio Bridge.
import type { ReactNode } from 'react';
import {
  Activity, Box, ChevronDown, FolderOpen, MoreHorizontal, SlidersHorizontal
} from 'lucide-react';
import type { StudioEntity, StudioSnapshot } from '../types';
import type { StudioPanel } from '../nestedDock';
import PanelTitle from '../components/PanelTitle';
import PropertyField from '../components/PropertyField';
import ViewportPlaceholder from '../components/ViewportPlaceholder';
import AssetsPanel from '../components/AssetsPanel';
import LightingPanel from './LightingPanel';
import MaterialsPanel from './MaterialsPanel';
import ShaderGraphPanel from './ShaderGraphPanel';
import { studioSceneMode } from './sceneState';
import HierarchyPanel from './HierarchyPanel';

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
  select: (id: string | null) => Promise<void>;
  changeProperty: (key: string, value: string | number | boolean) => Promise<void>;
}

export function renderStudioPanel(panel: StudioPanel, ctx: StudioPanelContext): ReactNode {
  const {snapshot,busy,connected,openProject,selected,changeProperty}=ctx;
  const sceneMode=studioSceneMode(connected,snapshot?.project.path);
    switch (panel) {
      case 'hierarchy':
        return <HierarchyPanel {...ctx}/>;
      case 'assets':
        return <AssetsPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'inspector':
        return <>
          <PanelTitle title="INSPECTOR" right={
            <button className="icon-button" disabled aria-label="Inspector menu">
              <MoreHorizontal size={16}/>
            </button>}/>
          {selected ? <>
            <div className="selected-object">
              <div className="object-icon"><Box size={21}/></div>
              <div><h3>{selected.name}</h3><span>{selected.kind} · {selected.id}</span></div>
            </div>
            <div className="inspector-section">
              <div className="section-heading"><ChevronDown size={13}/> Properties</div>
              {snapshot?.properties.map(property =>
                <PropertyField key={selected.id + ':' + property.key}
                  property={property} disabled={busy || property.editable === false}
                  onChange={value => void changeProperty(property.key, value)}/>)}
            </div>
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
        return <LightingPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'materials':
        return <MaterialsPanel projectPath={snapshot?.project.path ?? ''}
          previewMaterial={snapshot?.previewMaterial}/>;
      case 'shader_graph':
        return <ShaderGraphPanel projectPath={snapshot?.project.path ?? ''}/>;
      // AI_PANEL_CASES: add new panel renderers above this line.
      case 'game':
      case 'animation':
        return <div className="dock-unported" role="status">
          <Box size={28}/>
          <strong>{panel.charAt(0).toUpperCase() + panel.slice(1)} editor</strong>
          <p>HTML5 panel migration is pending. The legacy native editor remains available.</p>
        </div>;
    }
}
