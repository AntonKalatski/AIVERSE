import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Box, ChevronRight, CirclePlay, Code2, FileCode2, FolderOpen, FolderTree,
  Layers3, PanelBottom, Redo2, RefreshCw, Save, Settings2, Terminal,
  Undo2, WandSparkles
} from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from './bridge';
import {
  closeTab, defaultDockTree, panelStack, reopenTab, readNestedDock, saveNestedDock
} from './nestedDock';
import type { StudioPanel } from './nestedDock';
import type { StudioEntity, StudioSnapshot } from './types';
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
  const [connected] = useState(nativeConnected);

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
      if (result.snapshot) setSnapshot(result.snapshot);
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
    if (!snapshot?.selectedId) return;
    setBusy(true);
    try {
      setSnapshot(await invoke('studio.setProperty', {
        entityId: snapshot.selectedId, componentId, key, value
      }));
      setError('');
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const rows = useMemo(() => hierarchyRows(snapshot?.scene.entities ?? []), [snapshot]);
  const selected = snapshot?.scene.entities.find(entity => entity.id === snapshot.selectedId);
  const consoleOpen = panelStack(dock, 'console') !== null;
  const renderPanel = (panel: StudioPanel) => renderStudioPanel(panel, {
    snapshot, busy, connected, search, setSearch, rows, selected,
    select, changeProperty, openProject
  });

  return <div className="studio">
    <header className="titlebar">
      <div className="wordmark"><div className="brand-logo"><Layers3 size={19}/></div>
        <strong>AIVERSE</strong><span>STUDIO</span></div>
      <div className="project-breadcrumb"><span>Projects</span><ChevronRight size={14}/>
        <b>{snapshot?.project.name ?? 'Loading...'}</b>
        <span className={snapshot?.project.dirty ? 'dirty-dot active' : 'dirty-dot'}/>
      </div>
      <div className="title-actions">
        <button className="tool-button" onClick={() => void openProject()}
          disabled={!connected || busy} title="Open AIVERSE project">
          <FolderOpen size={15}/> Open Project
        </button>
        <span className={connected ? 'mode-pill online' : 'mode-pill demo'}>
          {connected ? 'NATIVE' : 'DEMO · NO ENGINE'}
        </span>
        <button className="icon-button" title="Refresh from engine" onClick={() => void refresh()}>
          <RefreshCw size={15}/></button>
        <button className="icon-button" aria-label="Reset Studio layout" title="Reset panel layout"
          onClick={() => setDock(defaultDockTree())}><Settings2 size={17}/></button>
      </div>
    </header>
    <div className="toolbar">
      <div className="toolbar-left"><span className="toolbar-label"><Box size={16}/> Workspace</span>
        <span className="toolbar-divider"/>
        <button className="tool-button selected"><FolderTree size={15}/> Level Editor</button>
        <button className="tool-button" disabled><Code2 size={15}/> Scripting</button>
      </div>
      <div className="toolbar-right">
        <button className="tool-button" onClick={() => void historyAction('studio.undo')}
          disabled={busy || !snapshot?.history.canUndo} title="Undo"><Undo2 size={15}/> Undo</button>
        <button className="tool-button" onClick={() => void historyAction('studio.redo')}
          disabled={busy || !snapshot?.history.canRedo} title="Redo"><Redo2 size={15}/> Redo</button>
        <button className="tool-button" onClick={() => void historyAction('studio.save')}
          disabled={busy || !connected || !snapshot?.project.path || !snapshot.project.dirty}
          title="Save current map"><Save size={15}/> Save</button>
        <button className="tool-button" disabled title="Available after native engine integration">
          <WandSparkles size={15}/> AI Assist</button>
        <button className="play-button" disabled title="Play integration is pending">
          <CirclePlay size={16}/> Play</button>
      </div>
    </div>
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
