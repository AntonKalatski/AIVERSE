import type { ProjectLoadState } from './ProjectLoading';

export type StageState = 'done' | 'active' | 'todo';
/** Four stages of opening a project and the overall ratio: project (indeterminate), rules and map, scene models n/m, ready. */
export function splashStages(s: Pick<ProjectLoadState, 'phase' | 'done' | 'total' | 'message'>) {
  const order = ['project', 'scene', 'models', 'done'] as const;
  const at = s.phase === 'error' ? 0 : Math.max(0, order.indexOf(s.phase as typeof order[number]));
  const models = s.phase === 'models' && s.total > 0 ? Math.min(1, s.done / s.total) : 0;
  const steps: Array<{ label: string; state: StageState }> = [
    { label: 'Project', state: at > 0 ? 'done' : 'active' },
    { label: 'Rules and map', state: at > 1 ? 'done' : at === 1 ? 'active' : 'todo' },
    { label: s.phase === 'models' && s.total ? `Scene models ${s.done} / ${s.total}` : 'Scene models', state: at > 2 ? 'done' : at === 2 ? 'active' : 'todo' },
    { label: 'Ready', state: at >= 3 ? 'done' : 'todo' },
  ];
  const ratio = at >= 3 ? 1 : at === 2 ? 0.35 + 0.65 * models : at === 1 ? 0.25 : 0.08;
  const detail = s.phase === 'models' ? (s.message.split('/').slice(-2).join('/') || 'Streaming models') : s.phase === 'scene' ? 'Preparing the scene' : 'Reading game.json, rules and the map';
  return { steps, ratio, detail };
}

/** Stable pick of one of n splash skies for a project: FNV-1a of the name, so a project always opens under the same sky. */
export function pickSky(seed: string, n: number) {
  if (n <= 0) return -1;
  let h = 2166136261;
  for (const c of seed || 'aiverse') { h ^= c.charCodeAt(0); h = Math.imul(h, 16777619); }
  return (h >>> 0) % n;
}
