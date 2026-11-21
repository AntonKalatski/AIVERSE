import { useEffect, useState } from 'react';
import { Box, Copy, Plus, Trash2 } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioEntityCatalog, StudioSnapshot } from '../types';
import {
  StudioAlert, StudioButton, StudioEmptyState, StudioField, StudioIconButton,
  StudioInput, StudioListRow, StudioPanel, StudioSection, StudioSelect
} from '../ui';
import type { StudioPanelContext } from './StudioPanelRenderer';

export default function HierarchyPanel({ snapshot, busy, connected, search, setSearch,
  rows, select, updateSnapshot, setBusy }: StudioPanelContext) {
  const [catalog, setCatalog] = useState<StudioEntityCatalog | null>(null);
  const [error, setError] = useState('');
  const [creating, setCreating] = useState(false);
  const [type, setType] = useState('');
  const [x, setX] = useState('0'), [z, setZ] = useState('0'), [owner, setOwner] = useState('0');
  const project = snapshot?.project.path;
  useEffect(() => {
    let active = true;
    setCatalog(null); setCreating(false); setError('');
    if (connected && project) void invoke('studio.entityCatalog', {}).then(result => {
      if (active) { setCatalog(result); setType(result.types[0]?.id ?? ''); }
    }).catch(e => { if (active) setError(String(e)); });
    return () => { active = false; };
  }, [connected, project]);
  const canCreate = !!catalog?.mapEntities && catalog.types.length > 0;
  const id = snapshot?.selectedId;
  const canEdit = !!catalog?.mapEntities && !!id?.startsWith('entity:');
  const mutate = async (operation: () => Promise<StudioSnapshot>) => {
    if (busy) return;
    setBusy(true); setError('');
    try { updateSnapshot(await operation()); setCreating(false); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const remove = () => { if (canEdit && id) void mutate(() => invoke('studio.entityDelete', {entityId: id})); };
  const duplicate = () => { if (canEdit && id) void mutate(() => invoke('studio.entityDuplicate', {entityId: id})); };
  const visible = rows.filter(({node}) => (node.name+' '+node.kind).toLowerCase().includes(search.toLowerCase()));
  return <StudioPanel title="Hierarchy" subtitle={snapshot?.scene.name}
    actions={<>
      <StudioIconButton aria-label="Create entity" icon={<Plus size={15}/>} disabled={busy || !canCreate}
        onClick={() => setCreating(value => !value)}/>
      <StudioIconButton aria-label="Duplicate selected entity" icon={<Copy size={15}/>} disabled={busy || !canEdit} onClick={duplicate}/>
      <StudioIconButton aria-label="Delete selected entity" icon={<Trash2 size={15}/>} disabled={busy || !canEdit} onClick={remove}/>
    </>} footer={<span>{visible.length} / {rows.length} objects</span>}
    onKeyDown={event => {
      if (busy || (event.target instanceof HTMLElement && event.target.closest('input,select,textarea,[contenteditable="true"]'))) return;
      if (event.key === 'Delete' && canEdit) { event.preventDefault(); remove(); }
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'd' && canEdit) { event.preventDefault(); duplicate(); }
    }}>
    <div className="au-stack">
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      <StudioField label="Search objects" htmlFor="hierarchy-search">
        <StudioInput id="hierarchy-search" type="search" value={search} onChange={event => setSearch(event.target.value)}/>
      </StudioField>
      {creating && <StudioSection title="Create entity">
        <form className="au-stack" onSubmit={event => {
          event.preventDefault();
          if (![x,z,owner].every(value => value.trim() && Number.isFinite(Number(value)))) { setError('Enter valid numeric values.'); return; }
          void mutate(() => invoke('studio.entityCreate', {type, x: Number(x), z: Number(z), owner: Number(owner)}));
        }}>
          <StudioField label="Type" htmlFor="entity-type"><StudioSelect id="entity-type" value={type} onChange={event => setType(event.target.value)} disabled={busy}>
            {catalog?.types.map(item => <option key={item.id} value={item.id}>{item.name || item.id}</option>)}
          </StudioSelect></StudioField>
          <StudioField label="Position X" htmlFor="entity-x"><StudioInput id="entity-x" type="number" step="any" min={-1000000} max={1000000} required value={x} onChange={event => setX(event.target.value)} disabled={busy}/></StudioField>
          <StudioField label="Position Z" htmlFor="entity-z"><StudioInput id="entity-z" type="number" step="any" min={-1000000} max={1000000} required value={z} onChange={event => setZ(event.target.value)} disabled={busy}/></StudioField>
          <StudioField label="Owner" htmlFor="entity-owner" hint="0: neutral; -1: hostile; positive: player slot"><StudioInput id="entity-owner" type="number" step="1" min={-1} max={10000} required value={owner} onChange={event => setOwner(event.target.value)} disabled={busy}/></StudioField>
          <StudioButton type="submit" variant="primary" disabled={busy || !type}>Create</StudioButton>
        </form>
      </StudioSection>}
      {!project && <StudioEmptyState title="Open a project" description="Scene objects appear here after opening a project."/>}
      {project && visible.length === 0 && <StudioEmptyState title={search ? 'No matching objects' : 'No scene objects'} description={canCreate ? 'Use Create entity to add an object.' : 'Objects from the document appear here.'}/>}
      <div role="listbox" aria-label="Scene objects">
        {visible.map(({node,depth}) => <StudioListRow key={node.id} role="option" label={node.name}
          leading={<Box size={15}/>} meta={node.kind} selected={node.id === id} disabled={busy}
          style={{paddingInlineStart: `calc(var(--au-space-2) + ${depth} * var(--au-space-3))`}}
          onClick={() => void select(node.id)}/>)}
      </div>
    </div>
  </StudioPanel>;
}
