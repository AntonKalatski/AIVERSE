/** Named editor layouts. One factory set per workspace, plus layouts the user saves in this browser. */
import { normalizeDockTree, type DockTree } from './nestedDock.ts';
import { WORKSPACE_INFO, isWorkspace, workspaceDockTree, type StudioWorkspace } from './workspaces.ts';

export interface LayoutPreset {
  id: string;
  title: string;
  workspace: StudioWorkspace;
  factory: boolean;
  tree: DockTree;
}

/** Scene with Assets as a full-height column on the left. The bottom strip no longer owns Assets. */
export function sceneAssetsLeft(): DockTree {
  return normalizeDockTree({
    version: 3,
    root: {
      kind: 'split', id: 'root', axis: 'horizontal', ratio: .22,
      first: { kind: 'stack', id: 'dock-assets', tabs: ['assets'], active: 'assets' },
      second: {
        kind: 'split', id: 'split-main', axis: 'vertical', ratio: .74,
        first: {
          kind: 'split', id: 'split-body-left', axis: 'horizontal', ratio: .22,
          first: { kind: 'stack', id: 'dock-left', tabs: ['hierarchy', 'project'], active: 'hierarchy' },
          second: {
            kind: 'split', id: 'split-body-right', axis: 'horizontal', ratio: .74,
            first: { kind: 'stack', id: 'dock-center', tabs: ['scene', 'game'], active: 'scene' },
            second: { kind: 'stack', id: 'dock-right', tabs: ['inspector', 'history'], active: 'inspector' },
          },
        },
        second: { kind: 'stack', id: 'dock-bottom', tabs: ['console', 'agent'], active: 'console' },
      },
    },
  });
}

export function factoryPresets(workspace: StudioWorkspace): LayoutPreset[] {
  const presets: LayoutPreset[] = [{
    id: workspace + ':default', title: WORKSPACE_INFO[workspace].title + ' default',
    workspace, factory: true, tree: workspaceDockTree(workspace),
  }];
  if (workspace === 'scene') presets.push({
    id: 'scene:assets-left', title: 'Assets on the left', workspace, factory: true, tree: sceneAssetsLeft(),
  });
  return presets;
}

const KEY = 'aiverse.studio.presets.v1';
type Store = Pick<Storage, 'getItem' | 'setItem'>;
function browserStore(): Store | null {
  try { return typeof window === 'undefined' ? null : window.localStorage; } catch { return null; }
}

export function readSavedPresets(workspace: StudioWorkspace | null = null, store: Store | null = browserStore()): LayoutPreset[] {
  try {
    const raw = store?.getItem(KEY);
    const parsed: unknown = raw ? JSON.parse(raw) : [];
    if (!Array.isArray(parsed)) return [];
    const out: LayoutPreset[] = [];
    for (const item of parsed) {
      if (!item || typeof item !== 'object') continue;
      const row = item as { id?: unknown; title?: unknown; workspace?: unknown; tree?: unknown };
      if (typeof row.id !== 'string' || typeof row.title !== 'string' || !isWorkspace(row.workspace)) continue;
      if (workspace && row.workspace !== workspace) continue;
      out.push({ id: row.id, title: row.title.slice(0, 40), workspace: row.workspace, factory: false, tree: normalizeDockTree(row.tree) });
    }
    return out.slice(0, 24);
  } catch { return []; }
}

function writeAll(list: LayoutPreset[], store: Store | null): void {
  const saved = list.filter(preset => !preset.factory).map(preset => ({
    id: preset.id, title: preset.title, workspace: preset.workspace, tree: preset.tree,
  }));
  try { store?.setItem(KEY, JSON.stringify(saved)); } catch { /* private mode or quota */ }
}

export function saveLayoutPreset(title: string, workspace: StudioWorkspace, tree: DockTree, store: Store | null = browserStore()): LayoutPreset[] {
  const name = title.trim().slice(0, 40) || 'Layout';
  const preset: LayoutPreset = {
    id: workspace + ':' + Date.now().toString(36), title: name, workspace, factory: false, tree: normalizeDockTree(tree),
  };
  const all = readSavedPresets(null, store);
  const next = [...all, preset].slice(-24);
  writeAll(next, store);
  return next;
}

export function deleteLayoutPreset(id: string, store: Store | null = browserStore()): LayoutPreset[] {
  const next = readSavedPresets(null, store).filter(preset => preset.id !== id);
  writeAll(next, store);
  return next;
}
