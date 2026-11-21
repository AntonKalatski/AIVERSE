import { useEffect, useState } from 'react';
import {
  Box, ChevronDown, ChevronRight, Info, Lightbulb, Lock, LockOpen, Move3d, Puzzle, Shield, User
} from 'lucide-react';
import type { StudioInspectorComponent, StudioProperty } from '../types';
import PropertyField from '../components/PropertyField';
import { StudioEmptyState, StudioIconButton } from '../ui';
import type { StudioPanelContext } from './StudioPanelRenderer';
import './InspectorPanel.css';

const ICONS: Record<string, typeof Box> = {
  Transform: Move3d, MeshRenderer: Box, Light: Lightbulb, Unit: User,
  BoxCollider: Shield, OrientedBoxCollider: Shield, SpawnPoint: User
};
const COLLAPSE_KEY = 'aiverse.studio.inspector.collapsed';
const readCollapsed = (): Record<string, boolean> => {
  try { return JSON.parse(window.localStorage.getItem(COLLAPSE_KEY) ?? '{}') as Record<string, boolean>; }
  catch { return {}; }
};

/** Unity-like Transform rows: Position X Y Z, Rotation Y, Scale. */
const VECTOR_ROWS: Array<{ label: string; keys: Record<string, string> }> = [
  { label: 'Position', keys: { x: 'X', y: 'Y', height: 'Y', z: 'Z' } },
  { label: 'Rotation', keys: { yaw: 'Y', facing: 'Y' } },
  { label: 'Scale', keys: { scale: 'XYZ' } },
];
function splitVectors(properties: StudioProperty[]) {
  const used = new Set<StudioProperty>();
  const rows = VECTOR_ROWS.map(row => {
    const fields = properties.filter(p => p.type === 'number' && p.key in row.keys);
    fields.forEach(field => used.add(field));
    return { label: row.label, fields: fields.map(field => ({ field, axis: row.keys[field.key] })) };
  }).filter(row => row.fields.length > 0);
  // Only treat as vectors when this looks like a transform (has a position).
  if (!rows.some(row => row.label === 'Position' && row.fields.length >= 2)) return { rows: [], rest: properties };
  return { rows, rest: properties.filter(p => !used.has(p)) };
}

function ComponentCard({ component, entityId, busy, collapsed, onToggle, onChange }: {
  component: StudioInspectorComponent; entityId: string; busy: boolean; collapsed: boolean;
  onToggle: () => void; onChange: (key: string, value: string | number | boolean) => void;
}) {
  const Icon = ICONS[component.id] ?? Puzzle;
  const { rows, rest } = splitVectors(component.properties);
  return <section className={'insp-comp' + (collapsed ? ' is-collapsed' : '')} aria-label={component.title}>
    <button type="button" className="insp-comp__head" aria-expanded={!collapsed} onClick={onToggle}>
      {collapsed ? <ChevronRight size={12}/> : <ChevronDown size={12}/>}
      <Icon size={13} className="insp-comp__icon"/>
      <span className="insp-comp__title">{component.title}</span>
      {component.note && <span className="insp-comp__note" title={component.note}><Info size={12}/></span>}
    </button>
    {!collapsed && <div className="insp-comp__body">
      {rows.map(row => <div key={row.label} className="insp-row insp-row--vector"
        title={row.fields.map(({field}) => field.description).filter(Boolean).join('\n') || undefined}>
        <span className="insp-row__label">{row.label}</span>
        <div className="insp-vector">
          {row.fields.map(({field, axis}) => <PropertyField key={entityId + component.id + field.key} variant="axis"
            property={{ ...field, label: axis, description: undefined }}
            disabled={busy || field.editable === false} onChange={value => onChange(field.key, value)}/>)}
        </div>
      </div>)}
      {rest.map(property => <PropertyField key={entityId + component.id + property.key} property={property}
        disabled={busy || property.editable === false} onChange={value => onChange(property.key, value)}/>)}
      {component.properties.length === 0 && <p className="insp-comp__hint">No editable fields.</p>}
    </div>}
  </section>;
}

/** Unity-style Inspector: entity header + its document components. */
export default function InspectorPanel({ inspector, selected, busy, inspectorPinned, toggleInspectorPin,
  changeProperty }: StudioPanelContext) {
  const [collapsed, setCollapsed] = useState(readCollapsed);
  const [name, setName] = useState(inspector?.name ?? '');
  useEffect(() => setName(inspector?.name ?? ''), [inspector?.id, inspector?.name]);
  useEffect(() => {
    try { window.localStorage.setItem(COLLAPSE_KEY, JSON.stringify(collapsed)); } catch { /* storage optional */ }
  }, [collapsed]);
  const commitName = () => {
    if (inspector?.nameEditable && name.trim() && name !== inspector.name)
      void changeProperty('name', name.trim(), 'Entity');
    else setName(inspector?.name ?? '');
  };
  const lock = <StudioIconButton className="insp__lock"
    aria-label={inspectorPinned ? 'Unlock Inspector' : 'Lock Inspector to current object'}
    title={inspectorPinned ? 'Unlock Inspector' : 'Lock Inspector to current object'}
    aria-pressed={inspectorPinned} disabled={!selected && !inspectorPinned} onClick={toggleInspectorPin}
    icon={inspectorPinned ? <Lock size={12}/> : <LockOpen size={12}/>}/>;
  return <div className="insp">
    {!inspector ? <div className="insp__empty">
      {inspectorPinned && <div className="insp__empty-bar">{lock}</div>}
      <StudioEmptyState title={selected ? 'Loading…' : 'Nothing selected'}
        description={selected ? 'Reading entity components.' : 'Select an entity in the Hierarchy or Scene.'}/>
    </div> : <div className="insp__scroll">
      <div className="insp__header">
        <Box size={16} className="insp__header-icon"/>
        <input className="au-input insp__name" aria-label="Entity name" value={name}
          readOnly={!inspector.nameEditable} disabled={busy}
          title={inspector.nameEditable ? 'Rename' : 'This entity is named by its type'}
          onChange={event => setName(event.target.value)} onBlur={commitName}
          onKeyDown={event => { if (event.key === 'Enter') event.currentTarget.blur();
            if (event.key === 'Escape') { setName(inspector.name); event.currentTarget.blur(); } }}/>
        {lock}
      </div>
      <div className="insp__meta"><span>{inspector.kind}</span><code title="Document entity ID">{inspector.id}</code></div>
      {inspector.components.map(component => <ComponentCard key={inspector.id + ':' + component.id}
        component={component} entityId={inspector.id} busy={busy}
        collapsed={!!collapsed[component.id]}
        onToggle={() => setCollapsed(current => ({ ...current, [component.id]: !current[component.id] }))}
        onChange={(key, value) => void changeProperty(key, value, component.id)}/>)}
    </div>}
  </div>;
}
