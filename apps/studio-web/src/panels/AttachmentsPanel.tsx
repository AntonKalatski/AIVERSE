import { useCallback, useEffect, useState } from 'react';
import { Plus, RotateCcw, Save, Trash2 } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioAttachmentDoc, StudioAttachmentItem, StudioSnapshot } from '../types';
import { fromTrs, toTrs } from './attachMath';
import './ToolPanel.css';
import './ProjectToolPanels.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);

function Num({ label, value, onChange, step = 0.1 }: { label: string; value: number; onChange: (v: number) => void; step?: number }) {
  return <label className="att__num"><span>{label}</span>
    <input className="au-input" type="number" step={step} value={Number.isFinite(value) ? +value.toFixed(4) : 0}
      onChange={e => { const v = Number(e.target.value); if (Number.isFinite(v)) onChange(v); }}/></label>;
}

/** Attachments: a parent model with children on named sockets (offset, yaw, scale), saved through the engine codec. */
export default function AttachmentsPanel({ snapshot }: { snapshot: StudioSnapshot | null }) {
  const project = snapshot?.project.path ?? '';
  const [path, setPath] = useState('attachments.json');
  const [doc, setDoc] = useState<StudioAttachmentDoc>({ parent: '', attachments: [] });
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState<{ ok: boolean; text: string } | null>(null);
  const load = useCallback(async (p: string) => {
    try {
      const r = await invoke('studio.attachmentsGet', { path: p });
      setDoc(r.document); setDirty(false); setStatus(r.error ? { ok: false, text: r.error } : r.exists ? null : { ok: true, text: 'New file: ' + r.path });
    } catch (e) { setStatus({ ok: false, text: message(e) }); }
  }, []);
  useEffect(() => { if (project) void load(path); }, [project]); // eslint-disable-line react-hooks/exhaustive-deps
  const edit = (next: StudioAttachmentDoc) => { setDoc(next); setDirty(true); };
  const setItem = (i: number, patch: Partial<StudioAttachmentItem>) => edit({ ...doc, attachments: doc.attachments.map((a, n) => n === i ? { ...a, ...patch } : a) });
  const save = async () => {
    try { const r = await invoke('studio.attachmentsSet', { path, document: doc }); setDoc(r.document); setDirty(false); setStatus({ ok: true, text: 'Saved ' + r.path }); }
    catch (e) { setStatus({ ok: false, text: message(e) }); }
  };
  if (!project) return <div className="tool__empty">Open a project to author attachments.</div>;
  return <div className="ptp">
    <div className="ptp__edhead">
      <input className="au-input att__path" value={path} onChange={e => setPath(e.target.value)} onKeyDown={e => e.key === 'Enter' && void load(path)} title="Layout file (Enter to open)"/>
      <button type="button" className="au-button au-button--sm au-button--primary" disabled={!dirty} onClick={() => void save()}><Save size={13}/>Save</button>
      <button type="button" className="au-button au-button--sm" disabled={!dirty} onClick={() => void load(path)}><RotateCcw size={13}/>Revert</button>
    </div>
    {status && <div className={'ptp__status ' + (status.ok ? 'is-ok' : 'is-error')} role={status.ok ? 'status' : 'alert'}>{status.text}</div>}
    <div className="ptp__scroll">
      <label className="ptp__row"><span>Parent model</span>
        <input className="au-input att__wide" value={doc.parent} placeholder="content/hero.glb" onChange={e => edit({ ...doc, parent: e.target.value })}/></label>
      <h4>Attachments · {doc.attachments.length}
        <button type="button" className="au-button au-button--ghost au-button--sm" onClick={() => edit({ ...doc, attachments: [...doc.attachments, { model: '', socket: '', enabled: true }] })}><Plus size={12}/>Attachment</button></h4>
      {doc.attachments.map((a, i) => {
        const trs = toTrs(a.local);
        const setTrs = (t: [number, number, number], yaw: number, scale: number) => setItem(i, { local: fromTrs(t, yaw, scale) });
        return <div key={i} className="ptp__ctx">
          <div className="ptp__ctxhead">
            <label className="ptp__check"><input type="checkbox" checked={a.enabled !== false} onChange={e => setItem(i, { enabled: e.target.checked })}/></label>
            <input className="au-input" value={a.model} placeholder="content/sword.glb" onChange={e => setItem(i, { model: e.target.value })}/>
            <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Remove" onClick={() => edit({ ...doc, attachments: doc.attachments.filter((_, n) => n !== i) })}><Trash2 size={12}/></button>
          </div>
          <div className="att__grid">
            <label className="att__num"><span>Socket</span><input className="au-input" value={a.socket} placeholder="hand_r" onChange={e => setItem(i, { socket: e.target.value })}/></label>
            <label className="att__num"><span>Stable id</span><input className="au-input" value={a.id ?? ''} onChange={e => setItem(i, { id: e.target.value || undefined })}/></label>
            <Num label="Offset X" value={trs.t[0]} onChange={v => setTrs([v, trs.t[1], trs.t[2]], trs.yaw, trs.scale)}/>
            <Num label="Offset Y" value={trs.t[1]} onChange={v => setTrs([trs.t[0], v, trs.t[2]], trs.yaw, trs.scale)}/>
            <Num label="Offset Z" value={trs.t[2]} onChange={v => setTrs([trs.t[0], trs.t[1], v], trs.yaw, trs.scale)}/>
            <Num label="Yaw°" step={5} value={trs.yaw} onChange={v => setTrs(trs.t, v, trs.scale)}/>
            <Num label="Scale" step={0.05} value={trs.scale} onChange={v => v > 0 && setTrs(trs.t, trs.yaw, v)}/>
          </div>
          {trs.custom && <p className="ptp__hint">This transform has tilt or non-uniform scale; editing offset, yaw or scale replaces it.</p>}
          <label className="ptp__check"><input type="checkbox" checked={!!a.animated} onChange={e => setItem(i, { animated: e.target.checked })}/>Animated (own MotionWeave graph)</label>
          {a.animated && <label className="ptp__row"><span>Graph asset</span><input className="au-input att__wide" value={a.animationGraph ?? ''} onChange={e => setItem(i, { animationGraph: e.target.value })}/></label>}
        </div>;
      })}
      <p className="ptp__hint">Saved through the engine's attachment codec (validated). The live GPU preview of the old window is not in Studio yet: play the game or view the model to check placement.</p>
    </div>
  </div>;
}
