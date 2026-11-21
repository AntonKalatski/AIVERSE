import { useCallback, useEffect, useMemo, useState } from 'react';
import { ArrowLeftRight, Loader2, RefreshCw } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioProjectState, StudioSnapshot, StudioStats } from '../types';
import { jsonDiff, previewValue, type JsonChange } from './jsonDiff';
import './ToolPanel.css';
import './ProjectToolPanels.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
type Ctx = { snapshot: StudioSnapshot | null };

/** Statistics: map contents (by type and owner) and Scene frame time; refreshes with the document and every 2 s. */
export function StatsPanel({ snapshot }: Ctx) {
  const path = snapshot?.project.path ?? '';
  const [stats, setStats] = useState<StudioStats | null>(null);
  const [error, setError] = useState('');
  const read = useCallback(() => void invoke('studio.stats', {}).then(s => { setStats(s); setError(''); }).catch(e => setError(message(e))), []);
  useEffect(() => { if (path) read(); }, [path, snapshot, read]);
  useEffect(() => { if (!path) return; const t = window.setInterval(read, 2000); return () => window.clearInterval(t); }, [path, read]);
  if (!path) return <div className="tool__empty">Open a project to see its statistics.</div>;
  const fps = stats && stats.sceneFrameMs > 0 ? 1000 / stats.sceneFrameMs : 0;
  const max = Math.max(1, ...(stats?.types ?? []).map(t => t.count));
  return <div className="ptp">
    {error && <div className="tool__error" role="alert">{error}</div>}
    {stats && <div className="ptp__scroll">
      <div className="stats__cards">
        <div><b>{stats.entities}</b><span>objects</span></div>
        <div><b>{stats.width}×{stats.height}</b><span>map cells</span></div>
        <div><b>{stats.players}</b><span>players</span></div>
        <div><b>{stats.roads}</b><span>roads</span></div>
        <div><b>{stats.ruleTypes}</b><span>rule types</span></div>
        <div><b>{fps ? fps.toFixed(0) : '—'}</b><span>Scene fps{stats.sceneFrameMs ? ` · ${stats.sceneFrameMs.toFixed(1)} ms` : ''}</span></div>
      </div>
      <h4>By owner</h4>
      <table className="ptp__table"><tbody>{stats.owners.map(o =>
        <tr key={o.owner}><td>{o.name}</td><td className="ptp__meta">{o.owner}</td><td className="stats__num">{o.count}</td></tr>)}</tbody></table>
      <h4>By type · {stats.types.length}</h4>
      <div className="stats__bars">{stats.types.map(t => <div key={t.type} className="stats__bar">
        <span title={t.type}>{t.type}</span><i style={{ width: `${(t.count / max) * 100}%` }}/><b>{t.count}</b></div>)}</div>
      <p className="ptp__hint">{stats.map}{stats.dirty ? ' · unsaved' : ''} · {stats.undoSteps} undo steps</p>
    </div>}
  </div>;
}

/** Snapshot Diff: structural comparison of two JSON files of the project (maps by default). Offline, read-only. */
export function SnapshotDiffPanel({ snapshot }: Ctx) {
  const path = snapshot?.project.path ?? '';
  const [maps, setMaps] = useState<string[]>([]);
  const [a, setA] = useState(''), [b, setB] = useState('');
  const [changes, setChanges] = useState<JsonChange[] | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [filter, setFilter] = useState('');
  useEffect(() => {
    if (!path) return;
    void invoke('studio.project', {}).then((p: StudioProjectState) => {
      const files = p.maps.map(m => m.file);
      setMaps(files); setA(x => x || files[0] || ''); setB(x => x || files[1] || files[0] || '');
    }).catch(() => {});
  }, [path]);
  const read = async (file: string) => {
    const r = (await invoke('studio.op', { op: 'game_file_read', params: { path: file } })).result as { text?: string };
    try { return JSON.parse(r.text ?? ''); } catch { throw new Error(file + ' is not JSON'); }
  };
  const compare = async () => {
    setBusy(true); setChanges(null);
    try { setChanges(jsonDiff(await read(a.trim()), await read(b.trim()))); setError(''); }
    catch (e) { setError(message(e)); }
    finally { setBusy(false); }
  };
  const shown = useMemo(() => (changes ?? []).filter(c => !filter || c.path.toLowerCase().includes(filter.toLowerCase())), [changes, filter]);
  if (!path) return <div className="tool__empty">Open a project to compare its snapshots.</div>;
  return <div className="ptp">
    <div className="ptp__edhead diff__head">
      <input className="au-input" list="diff-maps" value={a} onChange={e => setA(e.target.value)} placeholder="maps/a.json"/>
      <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Swap" onClick={() => { setA(b); setB(a); }}><ArrowLeftRight size={13}/></button>
      <input className="au-input" list="diff-maps" value={b} onChange={e => setB(e.target.value)} placeholder="maps/b.json"/>
      <datalist id="diff-maps">{maps.map(m => <option key={m} value={m}/>)}</datalist>
      <button type="button" className="au-button au-button--sm au-button--primary" disabled={busy || !a || !b} onClick={() => void compare()}>
        {busy ? <Loader2 size={13} className="tool__spin"/> : <RefreshCw size={13}/>}Compare</button>
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      {changes && <div className="diff__summary">
        <span>{changes.length >= 2000 ? '2000+' : changes.length} differences</span>
        <span className="diff__added">+{changes.filter(c => c.kind === 'added').length}</span>
        <span className="diff__removed">−{changes.filter(c => c.kind === 'removed').length}</span>
        <span className="diff__changed">~{changes.filter(c => c.kind === 'changed').length}</span>
        <input className="au-input" placeholder="Filter paths" value={filter} onChange={e => setFilter(e.target.value)}/>
      </div>}
      {changes && !changes.length && <p className="ptp__ok">The files are the same.</p>}
      {!!shown.length && <table className="ptp__table ptp__audit"><tbody>{shown.slice(0, 500).map((c, i) =>
        <tr key={i} className={'diff__' + c.kind}><td>{c.kind === 'added' ? '+' : c.kind === 'removed' ? '−' : '~'} {c.path}</td>
          <td>{previewValue(c.before)}</td><td>{previewValue(c.after)}</td></tr>)}</tbody></table>}
      {!changes && <p className="ptp__hint">Compares two JSON files of the project path by path (numbers within 1e-6 count as equal). Read-only.</p>}
    </div>
  </div>;
}
