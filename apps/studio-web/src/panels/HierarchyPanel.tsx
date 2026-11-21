import { useEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { Box, Copy, Focus, Lightbulb, Plus, Search, Shield, Trash2, User, X } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioEntity, StudioEntityCatalog, StudioSnapshot } from '../types';
import { StudioButton, StudioContextMenu, StudioIconButton, StudioSelect, type StudioMenuItem } from '../ui';
import type { StudioPanelContext } from './StudioPanelRenderer';
import './HierarchyPanel.css';

const DRAG_MIME = 'application/x-aiverse-entity';
const kindIcon = (node: StudioEntity) => node.kind === 'Light' ? Lightbulb
  : node.kind === 'Collider' ? Shield : node.kind === 'Spawn' ? User : Box;

/** Compact, content-first Hierarchy: search on top, dense rows, drag reorder, context menu. */
export default function HierarchyPanel({ snapshot, busy, connected, search, setSearch,
  rows, select, updateSnapshot, setBusy }: StudioPanelContext) {
  const [catalog, setCatalog] = useState<StudioEntityCatalog | null>(null);
  const [error, setError] = useState('');
  const [creating, setCreating] = useState(false);
  const [type, setType] = useState('');
  const [menu, setMenu] = useState<{ x: number; y: number; id: string } | null>(null);
  const [drop, setDrop] = useState<{ id: string; placement: 'before' | 'after' } | null>(null);
  const searchRef = useRef<HTMLInputElement>(null);
  const listRef = useRef<HTMLDivElement>(null);
  const project = snapshot?.project.path;
  useEffect(() => {
    let active = true;
    setCatalog(null); setCreating(false); setError('');
    if (connected && project) void invoke('studio.entityCatalog', {}).then(result => {
      if (active) { setCatalog(result); setType(result.types[0]?.id ?? ''); }
    }).catch(e => { if (active) setError(String(e)); });
    return () => { active = false; };
  }, [connected, project]);
  const mapEntities = !!catalog?.mapEntities;
  const canCreate = mapEntities && (catalog?.types.length ?? 0) > 0;
  const selectedId = snapshot?.selectedId ?? null;
  const editable = (id: string | null | undefined) => mapEntities && !!id?.startsWith('entity:');
  const visible = useMemo(() => {
    const q = search.trim().toLowerCase();
    return q ? rows.filter(({node}) => (node.name + ' ' + node.kind).toLowerCase().includes(q)) : rows;
  }, [rows, search]);

  const mutate = async (operation: () => Promise<StudioSnapshot>) => {
    if (busy) return;
    setBusy(true); setError('');
    try { updateSnapshot(await operation()); setCreating(false); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  // Native delete/duplicate act on the current selection: select the target first.
  const onSelected = (id: string, op: (id: string) => Promise<StudioSnapshot>) => mutate(async () => {
    if (selectedId !== id) await invoke('studio.select', { id });
    return op(id);
  });
  const remove = (id: string) => editable(id) && void onSelected(id, entityId => invoke('studio.entityDelete', { entityId }));
  const duplicate = (id: string) => editable(id) && void onSelected(id, entityId => invoke('studio.entityDuplicate', { entityId }));
  const focus = async (id: string) => {
    if (selectedId !== id) await select(id);
    void invoke('studio.viewportInput', { kind: 'focus' }).catch(console.warn);
  };
  const create = () => { if (canCreate && type) void mutate(() => invoke('studio.entityCreate', { type, x: 0, z: 0, owner: 0 })); };

  const onKeyDown = (event: React.KeyboardEvent) => {
    if (event.target instanceof HTMLElement && event.target.closest('input,select,textarea')) {
      if (event.key === 'ArrowDown' && event.target === searchRef.current) {
        event.preventDefault(); listRef.current?.querySelector<HTMLElement>('[role=option]')?.focus();
      }
      return;
    }
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'f') { event.preventDefault(); searchRef.current?.focus(); return; }
    if (!selectedId || busy) return;
    const index = visible.findIndex(({node}) => node.id === selectedId);
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault();
      const next = visible[Math.max(0, Math.min(visible.length - 1, index + (event.key === 'ArrowDown' ? 1 : -1)))];
      if (next) { void select(next.node.id);
        listRef.current?.querySelector<HTMLElement>(`[data-id="${CSS.escape(next.node.id)}"]`)?.focus(); }
    } else if (event.key === 'Delete' || event.key === 'Backspace') { event.preventDefault(); remove(selectedId); }
    else if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'd') { event.preventDefault(); duplicate(selectedId); }
    else if (event.key.toLowerCase() === 'f' && !event.metaKey && !event.ctrlKey) { event.preventDefault(); void focus(selectedId); }
  };

  const dragOver = (event: DragEvent<HTMLElement>, id: string) => {
    if (!event.dataTransfer.types.includes(DRAG_MIME) || !editable(id)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'move';
    const r = event.currentTarget.getBoundingClientRect();
    const placement = event.clientY < r.top + r.height / 2 ? 'before' : 'after';
    if (drop?.id !== id || drop.placement !== placement) setDrop({ id, placement });
  };
  const onDrop = (event: DragEvent<HTMLElement>, targetId: string) => {
    const entityId = event.dataTransfer.getData(DRAG_MIME);
    const placement = drop?.placement ?? 'before';
    setDrop(null);
    if (!entityId || entityId === targetId || !editable(entityId) || !editable(targetId)) return;
    event.preventDefault();
    void mutate(() => invoke('studio.entityMove', { entityId, targetId, placement }));
  };

  const menuItems = (id: string): StudioMenuItem[] => [
    { id: 'focus', label: 'Focus in Scene', shortcut: 'F', icon: <Focus size={13}/>, onSelect: () => void focus(id) },
    { id: 'duplicate', label: 'Duplicate', shortcut: '⌘D', icon: <Copy size={13}/>, disabled: !editable(id) || busy, onSelect: () => duplicate(id) },
    { id: 'delete', label: 'Delete', shortcut: '⌫', icon: <Trash2 size={13}/>, danger: true, disabled: !editable(id) || busy, onSelect: () => remove(id) },
    { id: 'create', label: 'Create Entity…', icon: <Plus size={13}/>, separator: true, disabled: !canCreate || busy, onSelect: () => setCreating(true) },
  ];

  return <div className="hier" onKeyDown={onKeyDown}>
    <div className="hier__toolbar">
      <label className="hier__search">
        <Search size={12} aria-hidden="true"/>
        <input ref={searchRef} type="search" placeholder="Search" aria-label="Search scene objects"
          value={search} onChange={event => setSearch(event.target.value)}
          onKeyDown={event => { if (event.key === 'Escape' && search) { event.stopPropagation(); setSearch(''); } }}/>
        {search && <button type="button" aria-label="Clear search" onClick={() => setSearch('')}><X size={11}/></button>}
      </label>
      <StudioIconButton className="hier__tool" aria-label="Create entity" title="Create entity"
        icon={<Plus size={14}/>} disabled={busy || !canCreate} aria-expanded={creating}
        onClick={() => setCreating(value => !value)}/>
    </div>
    {creating && <form className="hier__create" onSubmit={event => { event.preventDefault(); create(); }}>
      <StudioSelect aria-label="Entity type" value={type} disabled={busy} onChange={event => setType(event.target.value)}>
        {catalog?.types.map(item => <option key={item.id} value={item.id}>{item.name || item.id}</option>)}
      </StudioSelect>
      <StudioButton type="submit" variant="primary" disabled={busy || !type}>Create</StudioButton>
      <StudioIconButton aria-label="Cancel" icon={<X size={13}/>} onClick={() => setCreating(false)}/>
    </form>}
    {error && <div className="hier__error" role="alert">{error}
      <button type="button" aria-label="Dismiss" onClick={() => setError('')}><X size={11}/></button></div>}
    <div ref={listRef} className="hier__list" role="listbox" aria-label="Scene objects"
      onDragLeave={event => { if (!event.currentTarget.contains(event.relatedTarget as Node)) setDrop(null); }}>
      {!project && <p className="hier__empty">Open a project to see its scene objects.</p>}
      {project && visible.length === 0 && <p className="hier__empty">{search ? 'No matching objects.' : 'The scene is empty.'}</p>}
      {visible.map(({node, depth}) => {
        const Icon = kindIcon(node);
        const selected = node.id === selectedId;
        const dragging = editable(node.id) && !search;
        return <div key={node.id} data-id={node.id} role="option" aria-selected={selected} tabIndex={selected ? 0 : -1}
          className={'hier__row' + (selected ? ' is-selected' : '') +
            (drop?.id === node.id ? ' is-drop-' + drop.placement : '')}
          style={{ paddingInlineStart: 6 + depth * 12 }}
          draggable={dragging && !busy}
          onDragStart={event => { event.dataTransfer.setData(DRAG_MIME, node.id); event.dataTransfer.effectAllowed = 'move'; }}
          onDragEnd={() => setDrop(null)}
          onDragOver={event => dragOver(event, node.id)}
          onDrop={event => onDrop(event, node.id)}
          onClick={() => void select(node.id)}
          onDoubleClick={() => void focus(node.id)}
          onContextMenu={event => { event.preventDefault(); void select(node.id); setMenu({ x: event.clientX, y: event.clientY, id: node.id }); }}>
          <Icon size={12} className="hier__icon"/>
          <span className="hier__name" title={node.name}>{node.name}</span>
          {node.kind && node.kind !== node.name && <span className="hier__kind">{node.kind}</span>}
        </div>;
      })}
    </div>
    <div className="hier__footer">{search ? `${visible.length} / ${rows.length}` : rows.length} objects</div>
    {menu && <StudioContextMenu x={menu.x} y={menu.y} label="Entity actions"
      items={menuItems(menu.id)} onClose={() => setMenu(null)}/>}
  </div>;
}
