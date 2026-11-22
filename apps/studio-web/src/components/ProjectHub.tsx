import { useEffect, useState } from 'react';
import { Clock, FolderOpen, Plus, Sparkles } from 'lucide-react';
import { invoke } from '../bridge';
import { StudioLoader } from '../ui';
import type { StudioHub, StudioSnapshot } from '../types';
import './ProjectHub.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const ago = (t: number) => {
  if (!t) return '';
  const d = Date.now() / 1000 - t;
  return d < 3600 ? Math.max(1, Math.round(d / 60)) + ' min ago' : d < 86400 ? Math.round(d / 3600) + ' h ago' : Math.round(d / 86400) + ' d ago';
};

/** Project hub: recent projects, a new project from a template (folder chosen in macOS), Open… for anything else. */
export default function ProjectHub({ onOpened, openOther, compact }: {
  onOpened: (s: StudioSnapshot) => void; openOther: () => void; compact?: boolean;
}) {
  const [hub, setHub] = useState<StudioHub | null>(null);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState('');
  const [templ, setTempl] = useState<string | null>(null);
  const [name, setName] = useState('');
  useEffect(() => { void invoke('studio.hub', {}).then(setHub).catch(e => setError(message(e))); }, []);
  const done = (r: { snapshot?: StudioSnapshot | null; cancelled?: boolean }) => { if (r.snapshot) onOpened(r.snapshot); };
  const open = async (dir: string) => {
    setBusy(dir);
    try { done(await invoke('studio.hubOpen', { dir })); setError(''); } catch (e) { setError(message(e)); } finally { setBusy(''); }
  };
  const create = async () => {
    if (!templ || !name.trim()) return;
    setBusy('create');
    try { done(await invoke('studio.hubCreate', { template: templ, name: name.trim() })); setError(''); } catch (e) { setError(message(e)); } finally { setBusy(''); }
  };
  const recent = (hub?.recent ?? []).filter(p => p.exists).slice(0, compact ? 5 : 12);
  return <div className={'hub' + (compact ? ' hub--compact' : '')}>
    <header className="hub__head">
      <h2>AIVERSE</h2>
      <button type="button" className="au-button au-button--sm" onClick={openOther}><FolderOpen size={14}/>Open…</button>
    </header>
    {error && <div className="hub__error" role="alert">{error}</div>}
    <div className="hub__cols">
      <section>
        <h3><Clock size={13}/>Recent</h3>
        {!hub && <p className="hub__muted"><StudioLoader size={22} label="Loading…"/></p>}
        {hub && !recent.length && <p className="hub__muted">No recent projects yet.</p>}
        {recent.map(p => <button key={p.dir} type="button" className="hub__item" disabled={!!busy} onClick={() => void open(p.dir)} title={p.dir}>
          <b>{p.name || p.dir.split('/').pop()}</b><span>{p.dir}</span><small>{ago(p.lastOpened)}</small>
          {busy === p.dir && <StudioLoader size={14}/>}</button>)}
      </section>
      <section>
        <h3><Sparkles size={13}/>New from template</h3>
        <div className="hub__templates">
          {(hub?.templates ?? []).map(t => <button key={t.id} type="button" className={'hub__tmpl' + (templ === t.id ? ' is-on' : '')}
            onClick={() => { setTempl(t.id); if (!name) setName(t.id === 'empty' ? 'My Game' : 'My ' + t.name); }} title={t.description}>
            <b>{t.name}</b><span>{t.genre || (t.id === 'empty' ? 'minimal' : '')}{t.maps ? ` · ${t.maps} map${t.maps > 1 ? 's' : ''}` : ''}</span></button>)}
        </div>
        {templ && <form className="hub__create" onSubmit={e => { e.preventDefault(); void create(); }}>
          <input className="au-input" autoFocus value={name} onChange={e => setName(e.target.value)} placeholder="Project name"/>
          <button type="submit" className="au-button au-button--sm au-button--primary" disabled={!name.trim() || !!busy}>
            {busy === 'create' ? <StudioLoader size={14}/> : <Plus size={13}/>}Create…</button>
        </form>}
        {templ && <p className="hub__muted">A copy of the template goes to a folder you choose (default {hub?.defaultLocation}); the template itself is never changed.</p>}
      </section>
    </div>
  </div>;
}
