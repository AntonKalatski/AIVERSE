import { useEffect, useState } from 'react';
import { Brush } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioSceneBrush } from '../types';

export type BrushKind = StudioSceneBrush['kind'];
const KIND_LABEL: Record<BrushKind, string> = {
  terrain: 'Sculpt', paint: 'Paint', objects: 'Objects', tiles: 'Tiles', field: 'Field', road: 'Draw road', river: 'Draw river',
};
const OPS: Array<[StudioSceneBrush['op'], string]> = [['raise', 'Raise'], ['lower', 'Lower'], ['smooth', 'Smooth'], ['flatten', 'Flatten'], ['noise', 'Noise'], ['erode', 'Erode']];
const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const op = async (name: string, params: Record<string, unknown> = {}) => (await invoke('studio.op', { op: name, params })).result as Record<string, any>;

function Choice({ label, value, options, onChange }: { label: string; value: string; options: string[]; onChange: (v: string) => void }) {
  return <label className="ptp__row"><span>{label}</span>
    <select className="au-input" value={value} onChange={e => onChange(e.target.value)}>
      <option value="">Choose…</option>{options.map(o => <option key={o} value={o}>{o}</option>)}</select></label>;
}
function Slider({ label, value, min, max, step, onCommit }: { label: string; value: number; min: number; max: number; step: number; onCommit: (v: number) => void }) {
  const [v, setV] = useState(value);
  useEffect(() => setV(value), [value]);
  return <label className="ptp__row"><span>{label}</span>
    <input type="range" min={min} max={max} step={step} value={v} onChange={e => setV(Number(e.target.value))}
      onPointerUp={() => v !== value && onCommit(v)} onKeyUp={() => v !== value && onCommit(v)}/><output>{v}</output></label>;
}

/**
 * Scene brush for a World panel: arm it, then drag in the Scene. Dab tools (Sculpt, Paint, Objects, Tiles, Field) apply live;
 * Draw road / Draw river collect the path and build it on release. Each stroke is one History step. [ ] resize, Esc puts it down.
 */
export default function SceneBrushBar({ kinds }: { kinds: BrushKind[] }) {
  const [brush, setBrush] = useState<StudioSceneBrush | null>(null);
  const [error, setError] = useState('');
  const [options, setOptions] = useState<{ tiles: string[]; fields: string[]; types: string[]; roads: string[] }>({ tiles: [], fields: [], types: [], roads: [] });
  const set = (patch: Partial<StudioSceneBrush>) =>
    void invoke('studio.sceneBrush', patch).then(b => { setBrush(b); setError(''); }).catch(e => setError(message(e)));
  useEffect(() => {
    void invoke('studio.sceneBrush', kinds.includes('terrain') ? {} : { kind: kinds[0] }).then(b =>
      kinds.includes(b.kind) ? setBrush(b) : invoke('studio.sceneBrush', { kind: kinds[0], armed: false }).then(setBrush)).catch(() => {});
    const off = onNativeEvent((event, payload) => { if (event === 'brush.changed' && payload) setBrush(payload as StudioSceneBrush); });
    // Leaving the panel puts the brush down: Scene clicks select again.
    return () => { off(); void invoke('studio.sceneBrush', { armed: false }).catch(() => {}); };
  }, [kinds.join()]); // eslint-disable-line react-hooks/exhaustive-deps
  const kind = brush && kinds.includes(brush.kind) ? brush.kind : kinds[0];
  useEffect(() => {
    // Choices for the active tool, from the same ops the panels use.
    if (kind === 'tiles') void op('tileset_list').then(r => setOptions(o => ({ ...o,
      tiles: (r.tilesets ?? []).filter((t: any) => !r.mapTileset || t.name === r.mapTileset || t.path?.includes(r.mapTileset)).flatMap((t: any) => (t.terrains ?? []).map((x: any) => x.id)) }))).catch(() => {});
    if (kind === 'field') void op('field_list').then(r => setOptions(o => ({ ...o, fields: (r.fields ?? []).map((f: any) => f.id) }))).catch(() => {});
    if (kind === 'road') void op('road_types').then(r => setOptions(o => ({ ...o, roads: (r.types ?? []).map((t: any) => t.id ?? t.name).filter(Boolean) }))).catch(() => {});
    if (kind === 'objects') void invoke('studio.rules', {}).then((r: any) => setOptions(o => ({ ...o, types: Object.keys(r.types ?? {}).sort() }))).catch(() => {});
  }, [kind]);
  if (!brush) return null;
  const continuous = kind === 'terrain' || kind === 'paint' || kind === 'tiles' || kind === 'field' || kind === 'objects';
  return <div className={'tbrush' + (brush.armed ? ' is-armed' : '')}>
    <div className="tbrush__row">
      <button type="button" className={'au-button au-button--sm' + (brush.armed ? ' au-button--primary' : '')} aria-pressed={brush.armed}
        onClick={() => set({ armed: !brush.armed, kind })} title="Drag in the Scene (Alt+drag orbits, [ ] size, Esc stops)">
        <Brush size={13}/>{brush.armed ? 'Brush on' : 'Brush'}</button>
      {kinds.length > 1 && <div className="ptp__seg" role="radiogroup" aria-label="Brush tool">
        {kinds.map(k => <button key={k} type="button" role="radio" aria-checked={kind === k} className={kind === k ? 'is-on' : ''}
          onClick={() => set({ kind: k })}>{KIND_LABEL[k]}</button>)}</div>}
    </div>
    {kind === 'terrain' && <div className="ptp__seg" role="radiogroup" aria-label="Sculpt operation">
      {OPS.map(([id, label]) => <button key={id} type="button" role="radio" aria-checked={brush.op === id}
        className={brush.op === id ? 'is-on' : ''} onClick={() => set({ op: id })}>{label}</button>)}</div>}
    {kind === 'paint' && <Slider label="Layer" value={brush.layer} min={0} max={3} step={1} onCommit={v => set({ layer: v })}/>}
    {kind === 'tiles' && <Choice label="Terrain" value={brush.tile} options={options.tiles} onChange={v => set({ tile: v })}/>}
    {kind === 'field' && <>
      <Choice label="Field" value={brush.field} options={options.fields} onChange={v => set({ field: v })}/>
      <Choice label="Mode" value={brush.fieldMode} options={['set', 'add', 'max', 'erase']} onChange={v => v && set({ fieldMode: v as StudioSceneBrush['fieldMode'] })}/>
      <Slider label="Value" value={brush.value} min={0} max={100} step={1} onCommit={v => set({ value: v })}/></>}
    {kind === 'objects' && <>
      <Choice label="Type" value={brush.type} options={options.types} onChange={v => set({ type: v })}/>
      <Slider label="Owner" value={brush.owner} min={-1} max={8} step={1} onCommit={v => set({ owner: v })}/>
      <Slider label="Per dab" value={brush.count} min={1} max={10} step={1} onCommit={v => set({ count: v })}/>
      <Slider label="Spread" value={brush.spread} min={0} max={8} step={0.5} onCommit={v => set({ spread: v })}/></>}
    {kind === 'road' && <Choice label="Road type" value={brush.roadType} options={options.roads} onChange={v => set({ roadType: v })}/>}
    {kind === 'river' && <Slider label="Width, m" value={brush.width} min={2} max={80} step={1} onCommit={v => set({ width: v })}/>}
    {continuous && <Slider label={kind === 'objects' ? 'Spacing' : 'Radius'} value={brush.radius} min={0.5} max={24} step={0.5} onCommit={v => set({ radius: v })}/>}
    {(kind === 'terrain' || kind === 'paint') && <Slider label="Strength" value={brush.strength} min={0.05}
      max={kind === 'paint' || brush.op === 'smooth' || brush.op === 'flatten' || brush.op === 'erode' ? 1 : 6} step={0.05} onCommit={v => set({ strength: v })}/>}
    {brush.armed && <p className="ptp__hint">{continuous ? 'Drag in the Scene to paint' : 'Drag the path in the Scene; it is built on release'} · one History step per stroke · [ ] size · Esc stops</p>}
    {error && <div className="tool__error" role="alert">{error}</div>}
  </div>;
}
