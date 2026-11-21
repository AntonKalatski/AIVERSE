import { useCallback, useEffect, useMemo, useState } from 'react';
import { shortcutKey } from './keyboard';
import { ChevronDown, Search, Settings2 } from 'lucide-react';
import { invoke, nativeConnected, nativeWindowAction, onNativeEvent } from './bridge';
import {
  STUDIO_PANELS, STUDIO_PANEL_TITLES, STUDIO_PANEL_HOME, closeTab, panelStack, reopenTab
} from './nestedDock';
import {
  WORKSPACES, WORKSPACE_INFO, legacyWorkspace, readActiveWorkspace, readWorkspaceDock, saveActiveWorkspace,
  saveWorkspaceDock, workspaceDockTree, type StudioWorkspace
} from './workspaces';
import type { CommandEntry } from './commandSearch';
import CommandPalette from './components/CommandPalette';
import StudioStatusBar from './components/StudioStatusBar';
import WorkspaceTabs from './components/WorkspaceTabs';
import type { StudioPanel } from './nestedDock';
import type { StudioAsset, StudioEntity, StudioGameState, StudioInspector, StudioSnapshot } from './types';
import HeaderGameControls from './components/HeaderGameControls';
import engineLogo from '../../../engine/assets/branding/mark.png';
import NestedDockWorkspace from './components/NestedDockWorkspace';
import StudioMenuBar, { type StudioMenu } from './components/StudioMenuBar';
import { LEGACY_WINDOWS_NOT_MIGRATED } from './legacyWindows';
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
  const [workspace, setWorkspace] = useState<StudioWorkspace>(readActiveWorkspace);
  const [dock, setDock] = useState(() => readWorkspaceDock(readActiveWorkspace()));
  const [palette, setPalette] = useState<string | null>(null); // null = closed, else initial query
  const [paletteAssets, setPaletteAssets] = useState<StudioAsset[]>([]);
  const [game, setGame] = useState<StudioGameState | null>(null);
  const [sceneAction, setSceneAction] = useState('');
  const [hoverId, setHoverId] = useState('');
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
    const handle = window.setTimeout(() => saveWorkspaceDock(workspace, dock), 180);
    return () => window.clearTimeout(handle);
  }, [dock, workspace]);
  // Each workspace keeps its own arrangement; switching never loses the current one.
  const switchWorkspace = useCallback((next: StudioWorkspace) => {
    if (next === workspace) return;
    saveWorkspaceDock(workspace, dock);
    saveActiveWorkspace(next);
    setWorkspace(next);
    setDock(readWorkspaceDock(next));
  }, [workspace, dock]);

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
      } else if (event === 'viewport.hover') {
        setHoverId(payload && typeof payload === 'object' && 'message' in payload ? String(payload.message) : '');
      } else if (event === 'viewport.action') {
        setSceneAction(payload && typeof payload === 'object' && 'message' in payload ? String(payload.message) : '');
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
    // Editor chrome: no browser context menu (Reload/Inspect…) except in text fields.
    const menu = (event: MouseEvent) => {
      if (!(event.target instanceof HTMLElement && event.target.closest('input,textarea,[contenteditable="true"]')))
        event.preventDefault();
    };
    window.addEventListener('contextmenu', menu);
    return () => window.removeEventListener('contextmenu', menu);
  }, []);
  useEffect(() => {
    const shortcut = (event: KeyboardEvent) => {
      if (!(event.metaKey || event.ctrlKey)) return;
      const key = shortcutKey(event);
      // Global navigation works even while a request is in flight.
      if (key === 'k' && !event.altKey && !event.shiftKey) { event.preventDefault(); setPalette(current => current === null ? '' : null); return; }
      if (key === 'p') {
        event.preventDefault();
        window.dispatchEvent(new CustomEvent('studio:play-control', { detail: event.altKey ? 'step' : event.shiftKey ? 'pause' : 'toggle' }));
        return;
      }
      const ws = WORKSPACES.find(id => WORKSPACE_INFO[id].key === key);
      if (ws && !event.altKey && !event.shiftKey) { event.preventDefault(); switchWorkspace(ws); return; }
      if (event.altKey || busy) return;
      const target = event.target;
      const editable = target instanceof HTMLElement &&
        !!target.closest('input,textarea,[contenteditable="true"]');
      if (key === 'o' && connected) { event.preventDefault(); void openProject(); }
      else if (key === 's' && event.shiftKey && connected && snapshot?.project.path) {
        event.preventDefault();
        void invoke('studio.saveAs', {}).then(setSnapshot).catch(e => setError(e instanceof Error ? e.message : String(e)));
      } else if (key === 's' && connected && snapshot?.project.path) {
        event.preventDefault();
        if (snapshot.project.dirty) void historyAction('studio.save');
      } else if (key === 'd' && !editable && !event.shiftKey) {
        event.preventDefault();
        window.dispatchEvent(new CustomEvent('studio:entity-action', { detail: 'duplicate' }));
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
  }, [busy, connected, snapshot, historyAction, switchWorkspace]);

  const select = async (id: string | null, mode: 'replace' | 'add' | 'toggle' = 'replace') => {
    setBusy(true);
    try { setSnapshot(await invoke('studio.select', { id, mode })); setError(''); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const selectIds = async (ids: string[], mode: 'replace' | 'add' | 'toggle' = 'replace') => {
    setBusy(true);
    try { setSnapshot(await invoke('studio.select', { ids, mode })); setError(''); }
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
  const renderPanel = (panel: StudioPanel) => renderStudioPanel(panel, {
    snapshot, busy, connected, search, setSearch, rows, selected,
    inspector, inspectorPinned: !!inspectorPin, toggleInspectorPin,
    select, selectIds, hoverId, changeProperty, openProject, updateSnapshot: value => {
      setInspectorPin(null); // create/delete/duplicate or map replacement invalidates positional pins
      setSnapshot(value);
    }, setBusy, commitSnapshot: setSnapshot, showPanel: panel => showPanel(panel), reportError: setError
  });

  const project = connected && !!snapshot?.project.path;
  const selectedEntity = snapshot?.selectedId ?? null;
  const selection = (snapshot?.selectedIds?.length ? snapshot.selectedIds : selectedEntity ? [selectedEntity] : [])
    .filter(id => id.startsWith('entity:'));
  const entityAction = (method: 'studio.entityDuplicate' | 'studio.entityDelete') => {
    if (!selection.length || busy) return;
    setBusy(true);
    void invoke(method === 'studio.entityDelete' ? 'studio.entitiesDelete' : 'studio.entitiesDuplicate', { entityIds: selection })
      .then(next => { setInspectorPin(null); setSnapshot(next); setError(''); })
      .catch(e => setError(e instanceof Error ? e.message : String(e))).finally(() => setBusy(false));
  };
  useEffect(() => {
    const run = (event: Event) => entityAction((event as CustomEvent<string>).detail === 'delete' ? 'studio.entityDelete' : 'studio.entityDuplicate');
    window.addEventListener('studio:entity-action', run);
    return () => window.removeEventListener('studio:entity-action', run);
  });
  const showPanel = (panel: StudioPanel) => setDock(t => reopenTab(t, panel, STUDIO_PANEL_HOME[panel]));
  const menus: StudioMenu[] = [
    { id: 'file', label: 'File', items: () => [
      { id: 'open', label: 'Open Project…', shortcut: '⌘O', disabled: !connected || busy, onSelect: () => void openProject() },
      { id: 'maps', label: 'New / Open Map…', separator: true, disabled: !project, onSelect: () => showPanel('project') },
      { id: 'save', label: 'Save', shortcut: '⌘S', disabled: !project || busy || !snapshot?.project.dirty,
        onSelect: () => void historyAction('studio.save') },
      { id: 'saveas', label: 'Save As…', shortcut: '⇧⌘S', disabled: !project || busy,
        onSelect: () => { setBusy(true); void invoke('studio.saveAs', {}).then(next => { if (next && 'project' in next) setSnapshot(next); })
          .catch(e => setError(e instanceof Error ? e.message : String(e))).finally(() => setBusy(false)); } },
    ] },
    { id: 'edit', label: 'Edit', items: () => [
      { id: 'undo', label: 'Undo', shortcut: '⌘Z', disabled: busy || !snapshot?.history.canUndo, onSelect: () => void historyAction('studio.undo') },
      { id: 'redo', label: 'Redo', shortcut: '⇧⌘Z', disabled: busy || !snapshot?.history.canRedo, onSelect: () => void historyAction('studio.redo') },
      { id: 'dup', label: selection.length > 1 ? 'Duplicate ' + selection.length + ' Objects' : 'Duplicate', shortcut: '⌘D', separator: true, disabled: busy || !selection.length,
        onSelect: () => entityAction('studio.entityDuplicate') },
      { id: 'del', label: selection.length > 1 ? 'Delete ' + selection.length + ' Objects' : 'Delete', shortcut: '⌫', disabled: busy || !selection.length,
        onSelect: () => entityAction('studio.entityDelete') },
      { id: 'frame', label: 'Frame Selected', shortcut: 'F', disabled: !selectedEntity || !connected,
        onSelect: () => void invoke('studio.viewportInput', { kind: 'focus' }).catch(console.warn) },
      { id: 'prefs', label: 'Preferences…', separator: true, onSelect: () => setSettingsOpen(true) },
    ] },
    { id: 'window', label: 'Window', items: () => [
      ...STUDIO_PANELS.map(panel => ({ id: 'panel-' + panel, label: STUDIO_PANEL_TITLES[panel],
        checked: panelStack(dock, panel) !== null,
        onSelect: () => setDock(t => panelStack(t, panel) !== null ? closeTab(t, panel) : reopenTab(t, panel, STUDIO_PANEL_HOME[panel])) })),
      { id: 'ws', label: 'Workspaces', caption: true, separator: true, onSelect: () => {} },
      ...WORKSPACES.map(id => ({ id: 'ws-' + id, label: WORKSPACE_INFO[id].title, shortcut: '⌘' + WORKSPACE_INFO[id].key,
        checked: workspace === id, onSelect: () => switchWorkspace(id) })),
      { id: 'reset', label: 'Reset ' + WORKSPACE_INFO[workspace].title + ' Layout', separator: true, onSelect: () => setDock(workspaceDockTree(workspace)) },
      { id: 'palette', label: 'Command Palette…', shortcut: '⌘K', onSelect: () => setPalette('') },
      { id: 'legacy', label: 'Legacy editor — not migrated yet', caption: true, separator: true, onSelect: () => {} },
      ...LEGACY_WINDOWS_NOT_MIGRATED.map(w => {
        const area = legacyWorkspace(w.id);
        return { id: 'legacy-' + w.id, label: w.title + (area ? '  ·  ' + WORKSPACE_INFO[area].title : ''), disabled: true, onSelect: () => {} };
      }),
    ] },
  ];

  useEffect(() => {
    if (palette === null || !project) return;
    let live = true;
    void invoke('studio.assets', {}).then(list => { if (live) setPaletteAssets(list.files); }).catch(() => {});
    return () => { live = false; };
  }, [palette === null, project]);
  const paletteEntries = useMemo<CommandEntry[]>(() => {
    const out: CommandEntry[] = [];
    for (const menu of menus) for (const item of menu.items()) {
      if (item.caption || item.id.startsWith('legacy-') || item.id.startsWith('panel-') || item.id.startsWith('ws-') || item.id === 'palette') continue;
      out.push({ id: 'menu:' + menu.id + ':' + item.id, kind: 'command', title: item.label, hint: menu.label,
        shortcut: item.shortcut, disabled: item.disabled, run: item.onSelect });
    }
    out.push({ id: 'cmd:refresh', kind: 'command', title: 'Refresh from engine', hint: 'Re-read the native document', run: () => void refresh() });
    out.push({ id: 'cmd:console', kind: 'command', title: 'Show Console', run: () => setDock(t => reopenTab(t, 'console', 'dock-bottom')) });
    for (const id of WORKSPACES) out.push({ id: 'ws:' + id, kind: 'workspace', title: WORKSPACE_INFO[id].title + ' workspace',
      hint: WORKSPACE_INFO[id].purpose, shortcut: '⌘' + WORKSPACE_INFO[id].key, run: () => switchWorkspace(id) });
    for (const panel of STUDIO_PANELS) out.push({ id: 'panel:' + panel, kind: 'window', title: STUDIO_PANEL_TITLES[panel],
      hint: panelStack(dock, panel) ? 'Open · focus' : 'Open in ' + WORKSPACE_INFO[workspace].title, run: () => showPanel(panel) });
    for (const w of LEGACY_WINDOWS_NOT_MIGRATED) {
      const area = legacyWorkspace(w.id);
      out.push({ id: 'legacy:' + w.id, kind: 'window', title: w.title, legacy: true, disabled: true,
        hint: (area ? WORKSPACE_INFO[area].title + ' workspace · ' : '') + 'not migrated to HTML5 yet — use the legacy editor', run: () => {} });
    }
    for (const entity of snapshot?.scene.entities ?? []) out.push({ id: 'entity:' + entity.id, kind: 'entity', title: entity.name,
      hint: entity.kind + ' · select and frame (F)', run: () => { void select(entity.id).then(() => invoke('studio.viewportInput', { kind: 'focus' })).catch(console.warn); } });
    for (const asset of paletteAssets) out.push({ id: 'asset:' + asset.path, kind: 'asset', title: asset.path.split('/').pop() ?? asset.path,
      hint: asset.path, keywords: asset.path, run: () => {
        setDock(t => reopenTab(t, 'assets', 'dock-bottom'));
        window.setTimeout(() => window.dispatchEvent(new CustomEvent('studio:reveal-asset', { detail: { path: asset.path } })), 0);
      } });
    return out;
    // menus are rebuilt each render from the same state listed here
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [snapshot, paletteAssets, dock, workspace, busy, connected, switchWorkspace]);

  return <div className={'studio compact-shell'+(connected?' compact-shell--native':'')}>
    <header className="titlebar"
      onMouseDown={event => {
        // Empty title-bar areas move the native window (like the macOS title bar).
        if (event.button !== 0 || (event.target as HTMLElement).closest('button,input,select,textarea,a,[role=button],[role=menu]')) return;
        if (event.detail === 2) nativeWindowAction('studio.windowZoom');
        else nativeWindowAction('studio.windowDrag');
      }}>
      <div className="wordmark"><img className="engine-mark" src={engineLogo} alt=""/>
        <strong>AIVERSE</strong></div>
      <StudioMenuBar menus={menus}/>
      <button className="project-breadcrumb tool-button" onClick={() => void openProject()}
        disabled={!connected || busy} title={snapshot?.project.dirty ? 'Open project · unsaved changes' : 'Open project'}>
        <b>{snapshot?.project.name ?? 'Loading…'}</b>{snapshot?.project.dirty && <span className="dirty-dot active" aria-label="Unsaved changes"/>}<ChevronDown size={13}/>
      </button>
      <div className="header-drag-space"/>
      <HeaderGameControls enabled={connected && !!snapshot?.project.path} onState={setGame}
        onPlay={()=>setDock(current=>reopenTab(current,'game','dock-center'))} onError={setError}/>
      <div className="header-drag-space"/>
      <WorkspaceTabs value={workspace} onChange={switchWorkspace}/>
      <button type="button" className="command-search-button" onClick={() => setPalette('')} aria-label="Search or run a command (⌘K)">
        <Search size={13}/><span>Search or run…</span><kbd>⌘K</kbd>
      </button>
      <div className="title-actions">
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
      <button type="button" onClick={() => setDock(workspaceDockTree(workspace))}>Reset panel layout</button>
    </section>}
    {!connected && <div className="demo-banner"><span>Preview mode:</span> interface and edits
      are demonstrated locally. Native C++ Workspace and GPU renderer are not connected yet.</div>}
    {error && <div role="alert" className="error-banner">{error}
      <button onClick={() => setError('')} aria-label="Dismiss">×</button></div>}
    <NestedDockWorkspace tree={dock} setTree={setDock} renderPanel={renderPanel}/>
    <StudioStatusBar snapshot={snapshot} connected={connected} game={game} action={sceneAction}
      onOpenConsole={() => setDock(current => reopenTab(current, 'console', 'dock-bottom'))}/>
    {palette !== null && <CommandPalette entries={paletteEntries} initial={palette} onClose={() => setPalette(null)}/>}
  </div>;
}
