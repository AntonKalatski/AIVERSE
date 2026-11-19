import { useEffect, useState } from 'react';
import type { StudioProperty } from '../types';

export default function PropertyField({ property, disabled, onChange }: { property: StudioProperty; disabled: boolean; onChange: (value: string | number | boolean) => void }) {
  const [draft, setDraft] = useState(String(property.value));
  useEffect(() => setDraft(String(property.value)), [property.key, property.value]);
  if (property.type === 'boolean') {
    return <label className="property-row"><span>{property.label}</span><input disabled={disabled} type="checkbox" checked={Boolean(property.value)} onChange={e => onChange(e.target.checked)}/></label>;
  }
  return <label className="property-row"><span>{property.label}</span><input
    disabled={disabled}
    type={property.type === 'number' ? 'number' : 'text'} value={draft}
    onChange={e => setDraft(e.target.value)}
    onBlur={() => {
      const value = property.type === 'number' ? Number(draft) : draft;
      if (value !== property.value && (property.type !== 'number' || Number.isFinite(value))) onChange(value);
    }}
    onKeyDown={e => { if (e.key === 'Enter') e.currentTarget.blur(); if (e.key === 'Escape') { setDraft(String(property.value)); e.currentTarget.blur(); } }}
  /></label>;
}
