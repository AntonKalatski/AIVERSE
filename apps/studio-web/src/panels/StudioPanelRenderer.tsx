// AI-friendly rendering surface: keep App.tsx as the application shell.
// For a new editor panel, register a typed panel ID in nestedDock.ts,
// then insert one render case below. All data mutations still use Studio Bridge.
import type { ReactNode } from 'react';
import {
  Activity, Box, ChevronDown, FolderTree, Lightbulb, MonitorPlay,
  MoreHorizontal, Plus, Search, SlidersHorizontal
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

export interface StudioPanelContext {
  snapshot: StudioSnapshot | null;
  busy: boolean;
  search: string;
  setSearch: (value: string) => void;
  rows: Array<{node: StudioEntity; depth: number}>;
  selected: StudioEntity | undefined;
  select: (id: string | null) => Promise<void>;
  changeProperty: (key: string, value: string | number | boolean) => Promise<void>;
}

export function renderStudioPanel(panel: StudioPanel, ctx: StudioPanelContext): ReactNode {
  const {snapshot,busy,search,setSearch,rows,selected,select,changeProperty}=ctx;
    switch (panel) {
      case 'hierarchy':
        return <>
          <PanelTitle title="HIERARCHY" right={<>
            <button className="icon-button" disabled title="Create entity (pending)"><Plus size={16}/></button>
            <button className="icon-button" disabled title="Hierarchy menu (pending)"><MoreHorizontal size={16}/></button>
          </>}/>
          <label className="search-box"><Search size={15}/>
            <input aria-label="Search hierarchy" placeholder="Search objects..."
              value={search} onChange={e => setSearch(e.target.value)}/>
            <kbd>⌘ K</kbd>
          </label>
          <div className="tree-label"><ChevronDown size={14}/><FolderTree size={15}/>
            <span>{snapshot?.scene.name ?? 'Scene'}</span>
          </div>
          <div className="tree-list">
            {rows.filter(({ node }) => node.name.toLowerCase().includes(search.toLowerCase()))
              .map(({ node, depth }) =>
                <button key={node.id}
                  className={'tree-row ' + (snapshot?.selectedId === node.id ? 'active' : '')}
                  style={{ paddingLeft: 14 + depth * 16 }} onClick={() => void select(node.id)}
                  disabled={busy}>
                  {node.kind === 'Light' ? <Lightbulb size={15}/> :
                    node.kind === 'Camera' ? <MonitorPlay size={15}/> : <Box size={15}/>}
                  <span>{node.name}</span>
                </button>)}
          </div>
          <div className="panel-footer">{snapshot?.scene.entities.length ?? 0} objects</div>
        </>;
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
            <span className="render-badge"><Activity size={13}/> Native Renderer3D (experimental)</span>
          </div>
          <ViewportPlaceholder title="Scene viewport" active/>
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
