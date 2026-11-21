/**
 * Floating panel windows: a docked tab can be torn out into its own OS window
 * (for example on a second monitor). The main window remembers which panels
 * float and where, so they come back on the next launch.
 */
import { STUDIO_PANELS, type StudioPanel } from './nestedDock.ts';

export const FLOATING_KEY = 'aiverse.studio.floating';
/** AppKit screen frame of a floating window (origin bottom-left of the main screen). */
export interface FloatingWindow { panel: StudioPanel; x?: number; y?: number; width?: number; height?: number }
type Storage = Pick<globalThis.Storage, 'getItem' | 'setItem'>;
function storage(): Storage | null {
  try { return typeof window === 'undefined' ? null : window.localStorage; } catch { return null; }
}
const validPanel = (value: unknown): value is StudioPanel =>
  typeof value === 'string' && (STUDIO_PANELS as readonly string[]).includes(value);
const finite = (value: unknown): number | undefined =>
  typeof value === 'number' && Number.isFinite(value) ? value : undefined;

/** `?panel=<id>` marks a WebView that shows a single floating panel. */
export function floatingPanelParam(search: string = typeof window === 'undefined' ? '' : window.location.search): StudioPanel | null {
  try {
    const panel = new URLSearchParams(search).get('panel');
    return validPanel(panel) ? panel : null;
  } catch { return null; }
}
export function readFloating(store: Storage | null = storage()): FloatingWindow[] {
  try {
    const raw = JSON.parse(store?.getItem(FLOATING_KEY) ?? '[]');
    if (!Array.isArray(raw)) return [];
    const seen = new Set<StudioPanel>();
    const out: FloatingWindow[] = [];
    for (const item of raw) {
      if (!item || !validPanel(item.panel) || seen.has(item.panel)) continue;
      seen.add(item.panel);
      const entry: FloatingWindow = { panel: item.panel };
      const x = finite(item.x), y = finite(item.y), width = finite(item.width), height = finite(item.height);
      if (x !== undefined && y !== undefined && width !== undefined && height !== undefined && width > 0 && height > 0)
        Object.assign(entry, { x, y, width, height });
      out.push(entry);
    }
    return out;
  } catch { return []; }
}
export function saveFloating(list: FloatingWindow[], store: Storage | null = storage()): void {
  try { store?.setItem(FLOATING_KEY, JSON.stringify(list)); } catch { /* storage full or disabled */ }
}
export const isFloating = (list: FloatingWindow[], panel: StudioPanel): boolean => list.some(item => item.panel === panel);
export function addFloating(list: FloatingWindow[], panel: StudioPanel): FloatingWindow[] {
  return isFloating(list, panel) ? list : [...list, { panel }];
}
export function removeFloating(list: FloatingWindow[], panel: StudioPanel): FloatingWindow[] {
  return isFloating(list, panel) ? list.filter(item => item.panel !== panel) : list;
}
/** Native `window.floatFrame` payload → stored frame (ignored for panels that do not float). */
export function updateFloatingFrame(list: FloatingWindow[], payload: unknown): FloatingWindow[] {
  if (!payload || typeof payload !== 'object') return list;
  const p = payload as Record<string, unknown>;
  const x = finite(p.x), y = finite(p.y), width = finite(p.width), height = finite(p.height);
  if (!validPanel(p.panel) || !isFloating(list, p.panel) || x === undefined || y === undefined ||
      width === undefined || height === undefined || width <= 0 || height <= 0) return list;
  return list.map(item => item.panel === p.panel ? { panel: item.panel, x, y, width, height } : item);
}
