/** Play mode: whether Hierarchy/Inspector show the live game, and the live selection (shared, tiny store). */
import { useEffect, useState } from 'react';

export interface LiveRef { epoch: string; ref: string }
let showLive = true;
let liveSelection: LiveRef | null = null;
const listeners = new Set<() => void>();
const notify = () => listeners.forEach(l => l());
export function setShowLive(on: boolean) { if (showLive !== on) { showLive = on; notify(); } }
export function selectLive(ref: LiveRef | null) { liveSelection = ref; notify(); }
export function useLivePlay(): { showLive: boolean; selection: LiveRef | null } {
  const [, tick] = useState(0);
  useEffect(() => { const l = () => tick(x => x + 1); listeners.add(l); return () => { listeners.delete(l); }; }, []);
  return { showLive, selection: liveSelection };
}
/** A finished game session drops its live selection (refs belong to one epoch). */
export function resetLive() { liveSelection = null; showLive = true; notify(); }
