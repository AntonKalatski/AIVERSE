import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  ChevronDown, CheckCircle2, FileCode2, FolderOpen,
  PanelBottom, Redo2, RefreshCw, Save, Settings2, Terminal, Undo2
} from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from './bridge';
import {
  STUDIO_PANELS, STUDIO_PANEL_TITLES, STUDIO_PANEL_HOME, closeTab, defaultDockTree, characterDockTree, DOCK_PRESET_EVENT, panelStack, reopenTab, readNestedDock, saveNestedDock
} from './nestedDock';
import type { StudioPanel } from './nestedDock';
import type { StudioEntity, StudioInspector, StudioSnapshot } from './types';
import HeaderGameControls from './components/HeaderGameControls';
import engineLogo from '../../../engine/assets/branding/mark.png';
import NestedDockWorkspace from './components/NestedDockWorkspace';
import { renderStudioPanel } from './panels/StudioPanelRenderer';

function hierarchyRows(nodes: StudioEntity[], parentId?: string, depth = 0):
  Array<{ node: StudioEntity; depth: number }> {
  return nodes.filter(node => node.parentId === parentId)
    .flatMap(node => [{ node, depth }, ...hierarchyRows(nodes, node.id, depth + 1)]);
}

export default function App() {
  const [snapshot, setSnapshot] = useState<StudioSnapshot | null>(null);
  const [search, setSearch] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [dock, setDock] = useState(readNestedDock);
  useEffect(() => {
    const preset = (event: Event) => {
      const which = (event as CustomEvent<string>).detail;
      setDock(which === 'character' ? characterDockTree() : defaultDockTree());
    };
    window.addEventListener(DOCK_PRESET_EVENT, preset);
    return () => window.removeEventListener(DOCK_PRESET_EVENT, preset);
  }, []);
  const [connected] = useState(nativeConnected);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [uiScale, setUiScale] = useState<number>(() => {
    const stored = Number(window.localStorage.getItem('aiverse.studio.uiScale'));
    return Number.isFinite(stored) && stored >= 0.75 && stored <= 2 ? stored : 1;
  });
  const [inspectorPin, setInspectorPin] = useState<{
    entity: StudioEntity; inspector: StudioInspector | null;
  } | null>(null);
  useEffect(() => {
    // CSS zoom scales the entire web layout, including dialogs and dock tabs.
    // The native viewport continues using getBoundingClientRect() screen pixels.
    document.documentElement.style.zoom = String(uiScale);
    window.dispatchEvent(new Event('resize'));
    window.localStorage.setItem('aiverse.studio.uiScale', String(uiScale));
    return () => { document.documentElement.style.zoom = ''; };
  }, [uiScale]);

  // Store browser-only layout (not engine state) with bounded write frequency.
  useEffect(() => {
    const handle = window.setTimeout(() => saveNestedDock(dock), 180);
    return () => window.clearTimeout(handle);
  }, [dock]);

  const refresh = useCallback(async () => {
    try {
      setSnapshot(await invoke('studio.snapshot', {}));
      setError('');
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  }, []);
  useEffect(() => {
    void refresh();
    return onNativeEvent((event,payload) => {
      if (event === 'material.preview') {
        if (typeof payload === 'object' && payload !== null &&
            'message' in payload && String(payload.message)) {
          setDock(current => reopenTab(current,'scene','dock-center'));
        }
        void refresh();
      } else if (event === 'workspace.changed' || event === 'selection.changed' ||
                 event === 'document.changed') void refresh();
    });
  }, [refresh]);

  const historyAction = useCallback(async (method: 'studio.save' | 'studio.undo' | 'studio.redo') => {
    if (method !== 'studio.save') setInspectorPin(null); // positional entity IDs may be reassigned by history
    setBusy(true);
    try { setSnapshot(await invoke(method, {})); setError(''); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  }, []);

  const openProject = async () => {
    if (!connected || busy) return;
    setBusy(true);
    try {
      const result = await invoke('studio.openProject', {});
      if (result.snapshot) { setInspectorPin(null); setSnapshot(result.snapshot); }
      setError('');
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };

  useEffect(() => {
    const shortcut = (event: KeyboardEvent) => {
      if (!(event.metaKey || event.ctrlKey) || event.altKey || busy) return;
      const key = event.key.toLowerCase();
      const target = event.target;
      const editable = target instanceof HTMLElement &&
        !!target.closest('input,textarea,[contenteditable="true"]');
      if (key === 's' && connected && snapshot?.project.path) {
        event.preventDefault();
        if (snapshot.project.dirty) void historyAction('studio.save');
      } else if (key === 'z' && !editable && !event.shiftKey && snapshot?.history.canUndo) {
        event.preventDefault();
        void historyAction('studio.undo');
      } else if (((key === 'z' && event.shiftKey) || key === 'y') &&
                 !editable && snapshot?.history.canRedo) {
        event.preventDefault();
        void historyAction('studio.redo');
      }
    };
    window.addEventListener('keydown', shortcut);
    return () => window.removeEventListener('keydown', shortcut);
  }, [busy, connected, snapshot, historyAction]);

  const select = async (id: string | null) => {
    setBusy(true);
    try { setSnapshot(await invoke('studio.select', { id })); setError(''); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const changeProperty = async (key: string, value: string | number | boolean, componentId?: string) => {
    const entityId = inspectorPin?.entity.id ?? snapshot?.selectedId;
    if (!entityId) return;
    setBusy(true);
    try {
      // The pinned Inspector re-reads its entity after every snapshot change.
      setSnapshot(await invoke('studio.setProperty', { entityId, componentId, key, value }));
      setError('');
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  // Refresh the pinned object from native by ID without changing global selection.
  // This also reflects undo/redo and edits made from other panels.
  useEffect(() => {
    const entityId = inspectorPin?.entity.id;
    if (!entityId || !snapshot?.project.path) return;
    let active = true;
    void invoke('studio.inspect', {entityId}).then(inspector => {
      if (active) setInspectorPin(current => current?.entity.id === entityId
        ? {...current, inspector} : current);
    }).catch(() => {
      // The pinned object may have been removed by undo or another editor.
      if (active) setInspectorPin(current => current?.entity.id === entityId ? null : current);
    });
    return () => { active = false; };
  }, [inspectorPin?.entity.id, snapshot]);

  // Pins are project-local; never retain a target after switching projects.
  useEffect(() => { setInspectorPin(null); }, [snapshot?.project.path]);
  const rows = useMemo(() => hierarchyRows(snapshot?.scene.entities ?? []), [snapshot]);
  const selected = inspectorPin?.entity ??
    snapshot?.scene.entities.find(entity => entity.id === snapshot.selectedId);
  const inspector = inspectorPin ? inspectorPin.inspector : snapshot?.inspector ?? null;
  const toggleInspectorPin = () => setInspectorPin(current => current ? null :
    selected ? {entity: {...selected}, inspector: snapshot?.inspector ? structuredClone(snapshot.inspector) : null} : null);
  const consoleOpen = panelStack(dock, 'console') !== null;
  const renderPanel = (panel: StudioPanel) => renderStudioPanel(panel, {
    snapshot, busy, connected, search, setSearch, rows, selected,
    inspector, inspectorPinned: !!inspectorPin, toggleInspectorPin,
    select, changeProperty, openProject, updateSnapshot: value => {
      setInspectorPin(null); // create/delete/duplicate or map replacement invalidates positional pins
      setSnapshot(value);
    }, setBusy
  });

  return <div className={'studio compact-shell'+(connected?' compact-shell--native':'')}>
    <header className="titlebar">
      <div className="wordmark"><img className="engine-mark" src={engineLogo} alt=""/>
        <strong>AIVERSE</strong></div>
      <button className="project-breadcrumb tool-button" onClick={() => void openProject()}
        disabled={!connected || busy} title="Open project">
        <b>{snapshot?.project.name ?? 'Loading…'}</b><ChevronDown size={13}/>
      </button>
      <span className="save-indicator" title={snapshot?.project.dirty ? 'Unsaved changes' : 'Saved'}>
        <CheckCircle2 size={14}/><span>{snapshot?.project.dirty ? 'Unsaved' : 'Saved'}</span>
      </span>
      <div className="header-history">
        <button className="icon-button" aria-label="Undo" onClick={() => void historyAction('studio.undo')}
          disabled={busy || !snapshot?.history.canUndo}><Undo2 size={16}/></button>
        <button className="icon-button" aria-label="Redo" onClick={() => void historyAction('studio.redo')}
          disabled={busy || !snapshot?.history.canRedo}><Redo2 size={16}/></button>
        <button className="icon-button" aria-label="Save current map" onClick={() => void historyAction('studio.save')}
          disabled={busy || !connected || !snapshot?.project.path || !snapshot.project.dirty}><Save size={15}/></button>
      </div>
      <HeaderGameControls enabled={connected && !!snapshot?.project.path}
        onPlay={()=>setDock(current=>reopenTab(current,'game','dock-center'))} onError={setError}/>
      <div className="header-drag-space"/>
      <div className="title-actions">
        <button className="icon-button" onClick={() => void openProject()} disabled={!connected || busy}
          aria-label="Open Project" title="Open Project"><FolderOpen size={17}/></button>
        <details className="nested-panels-list">
          <summary>Panels <ChevronDown size={12}/></summary>
          <div className="nested-panels-popover">
            {STUDIO_PANELS.map(panel=><label key={panel}>
              <input type="checkbox" checked={panelStack(dock,panel)!==null}
                onChange={event=>setDock(t=>event.target.checked
                  ?reopenTab(t,panel,STUDIO_PANEL_HOME[panel]):closeTab(t,panel))}/>
              {STUDIO_PANEL_TITLES[panel]}</label>)}
          </div>
        </details>
        <button className="icon-button" title="Refresh from engine" aria-label="Refresh from engine" onClick={() => void refresh()}>
          <RefreshCw size={15}/></button>
        <button className="icon-button" aria-label="Editor settings" title="Editor settings"
          aria-expanded={settingsOpen} onClick={() => setSettingsOpen(v => !v)}><Settings2 size={17}/></button>
      </div>
    </header>
    {settingsOpen && <section className="studio-editor-settings" aria-label="Editor settings">
      <strong>Editor settings</strong>
      <label>UI scale <output>{Math.round(uiScale * 100)}%</output>
        <input type="range" min="75" max="200" step="5" value={Math.round(uiScale * 100)}
          onChange={e => setUiScale(Number(e.target.value) / 100)}/>
      </label>
      <button type="button" onClick={() => setUiScale(1)}>Reset scale to 100%</button>
      <button type="button" onClick={() => setDock(defaultDockTree())}>Reset panel layout</button>
    </section>}
    {!connected && <div className="demo-banner"><span>Preview mode:</span> interface and edits
      are demonstrated locally. Native C++ Workspace and GPU renderer are not connected yet.</div>}
    {error && <div role="alert" className="error-banner">{error}
      <button onClick={() => setError('')} aria-label="Dismiss">×</button></div>}
    <NestedDockWorkspace tree={dock} setTree={setDock} renderPanel={renderPanel}/>
    <footer className="statusbar">
      <div><span className={connected ? 'status-dot connected' : 'status-dot'}/>
        {connected ? 'Native transport connected' : 'Demo transport'}
      </div>
      <div className="statusbar-right">
        <button onClick={() => setDock(current => consoleOpen
          ? closeTab(current, 'console') : reopenTab(current, 'console', 'dock-bottom'))}>
          <PanelBottom size={14}/> {consoleOpen ? 'Hide console' : 'Show console'}
        </button>
        <span><Terminal size={13}/> {snapshot?.logs.length ?? 0} messages</span>
        <span><FileCode2 size={13}/> HTML5 + TypeScript</span>
      </div>
    </footer>
  </div>;
}
