// MetaHuman-style face blend: drag the handle between six face archetypes;
// region sliders limit which parts of the face follow the blend.
import { useRef, useState } from 'react';
import type { HumanFaceShape, HumanLookInfo } from '../types';
import { invoke } from '../bridge';
import { StudioAlert } from '../ui';
import { HcCard } from './HumanCreatorUi';
import { FACE_ARCHETYPES, FACE_REGIONS, archetypePoint, blendFace, blendWeights, guessFaceMeshes } from './humanCreatorSchema';

const R = 110, C = 150; // ring radius and centre in the 300x300 SVG
export default function HumanFaceBlend({ guid, face, schema, disabled, onCommit }:
  { guid: string; face: HumanFaceShape | null; schema: HumanLookInfo['faceSchema']; disabled: boolean; onCommit: (f: HumanFaceShape) => void }) {
  const [handle, setHandle] = useState<[number, number]>([0, -.999]);
  const [regions, setRegions] = useState<Record<string, number>>(() => Object.fromEntries(Object.keys(FACE_REGIONS).map(k => [k, 1])));
  const [error, setError] = useState('');
  const dragging = useRef(false);
  const weights = blendWeights(handle);
  const toUnit = (e: React.PointerEvent<SVGSVGElement>): [number, number] => {
    const r = e.currentTarget.getBoundingClientRect();
    let x = ((e.clientX - r.left) / r.width * 300 - C) / R, y = ((e.clientY - r.top) / r.height * 300 - C) / R;
    const l = Math.hypot(x, y); if (l > 1) { x /= l; y /= l; }
    return [x, y];
  };
  const commit = async (h: [number, number], reg = regions) => {
    setError('');
    try {
      let meshes = face?.meshes ?? [];
      if (!meshes.length) {
        if (!guid) throw Error('Сначала выберите персонажа в «Пресетах»');
        meshes = guessFaceMeshes((await invoke('studio.humanWardrobeInfo', { guid })).meshes);
        if (!meshes.length) throw Error('Не нашёл меш лица: отметьте его во вкладке «Пропорции»');
      }
      onCommit({ version: 1, meshes, landmarks: face?.landmarks ?? [], values: blendFace(h, reg, schema) });
    } catch (e) { setError(String(e)); }
  };
  const top = weights.map((w, i) => ({ w, a: FACE_ARCHETYPES[i] })).sort((a, b) => b.w - a.w).filter(x => x.w > .05).slice(0, 3);
  return <><HcCard title="Круг смешивания" hint="Тяните ручку к архетипам — лицо смешается после отпускания">
    {error && <StudioAlert tone="danger">{error}</StudioAlert>}
    <svg className="hc-blend" viewBox="0 0 300 300" role="slider" aria-label="Круг смешивания формы лица"
      aria-valuetext={top.map(t => t.a.label + ' ' + Math.round(t.w * 100) + '%').join(', ')}
      onPointerDown={e => { if (disabled) return; dragging.current = true; e.currentTarget.setPointerCapture(e.pointerId); setHandle(toUnit(e)); }}
      onPointerMove={e => { if (dragging.current) setHandle(toUnit(e)); }}
      onPointerUp={e => { if (!dragging.current) return; dragging.current = false; const h = toUnit(e); setHandle(h); void commit(h); }}>
      <circle cx={C} cy={C} r={R} className="hc-blend__ring"/>
      <circle cx={C} cy={C} r={R * .62} className="hc-blend__inner"/>
      {FACE_ARCHETYPES.map((a, i) => { const [x, y] = archetypePoint(i); return <line key={a.id} x1={C} y1={C} x2={C + x * R} y2={C + y * R} className="hc-blend__spoke"/>; })}
      <line x1={C} y1={C} x2={C + handle[0] * R} y2={C + handle[1] * R} className="hc-blend__arm"/>
      {FACE_ARCHETYPES.map((a, i) => { const [x, y] = archetypePoint(i); const active = weights[i] > .3;
        return <g key={a.id} className={'hc-blend__node' + (active ? ' hc-blend__node--active' : '')}>
          <circle cx={C + x * R} cy={C + y * R} r={25}/>
          <text x={C + x * R} y={C + y * R + 4} textAnchor="middle">{a.label}</text></g>; })}
      <circle cx={C + handle[0] * R} cy={C + handle[1] * R} r={11} className="hc-blend__handle"/>
    </svg>
    <p className="hc-hint hc-center">{top.map(t => t.a.label + ' ' + Math.round(t.w * 100) + '%').join(' · ')}</p>
    </HcCard>
    <HcCard title="Области" hint="Сколько смешивания получает каждая часть лица">
    <div className="hc-sliders">{Object.keys(FACE_REGIONS).map(r => <div key={r} className="hc-slider">
      <label htmlFor={'blend-' + r}>{r}</label>
      <input id={'blend-' + r} type="range" min={0} max={1} step={.01} value={regions[r]} disabled={disabled}
        onChange={e => setRegions(v => ({ ...v, [r]: Number(e.target.value) }))}
        onPointerUp={() => void commit(handle, regions)}/>
      <output className="hc-mono">{Math.round(regions[r] * 100)}%</output><span/></div>)}</div>
  </HcCard></>;
}
