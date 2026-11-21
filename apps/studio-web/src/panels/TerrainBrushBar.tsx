import { useEffect, useState } from 'react';
import { Brush } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioTerrainBrush } from '../types';

const OPS: Array<[StudioTerrainBrush['op'], string]> = [['raise', 'Raise'], ['lower', 'Lower'], ['smooth', 'Smooth'], ['flatten', 'Flatten'], ['noise', 'Noise'], ['erode', 'Erode']];

/** Terrain panel: arm a Scene brush. While armed, LMB drag in the Scene sculpts (one History step per stroke); Alt+drag still orbits. */
export default function TerrainBrushBar() {
  const [brush, setBrush] = useState<StudioTerrainBrush | null>(null);
  const [error, setError] = useState('');
  const set = (patch: Partial<StudioTerrainBrush>) =>
    void invoke('studio.terrainBrush', patch).then(b => { setBrush(b); setError(''); }).catch(e => setError(e instanceof Error ? e.message : String(e)));
  useEffect(() => {
    void invoke('studio.terrainBrush', {}).then(setBrush).catch(() => {});
    // Leaving the Terrain panel puts the brush down: Scene clicks select again.
    return () => { void invoke('studio.terrainBrush', { armed: false }).catch(() => {}); };
  }, []);
  if (!brush) return null;
  return <div className={'tbrush' + (brush.armed ? ' is-armed' : '')}>
    <div className="tbrush__row">
      <button type="button" className={'au-button au-button--sm' + (brush.armed ? ' au-button--primary' : '')} aria-pressed={brush.armed}
        onClick={() => set({ armed: !brush.armed })} title="Sculpt by dragging in the Scene (Alt+drag orbits)">
        <Brush size={13}/>{brush.armed ? 'Brush on' : 'Brush'}</button>
      <div className="ptp__seg" role="radiogroup" aria-label="Brush operation">
        {OPS.map(([id, label]) => <button key={id} type="button" role="radio" aria-checked={brush.op === id}
          className={brush.op === id ? 'is-on' : ''} onClick={() => set({ op: id })}>{label}</button>)}
      </div>
    </div>
    <label className="ptp__row"><span>Radius</span>
      <input type="range" min={0.5} max={24} step={0.5} defaultValue={brush.radius} onPointerUp={e => set({ radius: Number(e.currentTarget.value) })}/>
      <output>{brush.radius}</output></label>
    <label className="ptp__row"><span>Strength</span>
      <input type="range" min={0.05} max={brush.op === 'smooth' || brush.op === 'flatten' || brush.op === 'erode' ? 1 : 6} step={0.05}
        defaultValue={brush.strength} onPointerUp={e => set({ strength: Number(e.currentTarget.value) })}/>
      <output>{brush.strength}</output></label>
    {brush.armed && <p className="ptp__hint">Drag in the Scene to sculpt · each stroke is one History step</p>}
    {error && <div className="tool__error" role="alert">{error}</div>}
  </div>;
}
