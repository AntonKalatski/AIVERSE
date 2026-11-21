import { useEffect, useRef, useState } from 'react';
import type { StudioProperty } from '../types';
import AssetReferencePicker from './AssetReferencePicker';
import { isSameFieldValue, parseFieldDraft, validateBooleanChange } from '../propertyLogic';

interface PropertyFieldProps {
  property: StudioProperty;
  disabled: boolean;
  onChange: (value: string | number | boolean) => void;
  /** 'axis': compact single-letter label for X/Y/Z vector rows. */
  variant?: 'row' | 'axis';
}

/** Controlled, schema-driven property editor; writes happen only after validation. */
export default function PropertyField({ property, disabled, onChange, variant = 'row' }: PropertyFieldProps) {
  const [draft, setDraft] = useState(String(property.value));
  const [error, setError] = useState('');
  const skipBlur = useRef(false);
  const locked = disabled || property.editable === false;

  useEffect(() => {
    setDraft(String(property.value));
    setError('');
  }, [property.key, property.value]);

  const validateAndCommit = (text: string) => {
    const parsed = parseFieldDraft(property, text);
    if (!parsed.ok) { setError(parsed.error); return; }
    setError('');
    if (!isSameFieldValue(property.value, parsed.value)) onChange(parsed.value);
  };

  if (property.type === 'asset') {
    return <div className="property-row">
      <span>{property.label}</span>
      <AssetReferencePicker label={property.label} value={String(property.value)}
        kinds={property.assetKinds} disabled={locked}
        onChange={value => { if (value !== property.value) onChange(value); }}/>
      {property.description && <small className="property-description">{property.description}</small>}
    </div>;
  }

  if (property.type === 'boolean') {
    return <label className="property-row">
      <span>{property.label}</span>
      <input type="checkbox" className="au-checkbox__input" disabled={locked} checked={Boolean(property.value)}
        aria-label={property.label}
        onChange={event => {
          const next = validateBooleanChange(property, event.target.checked);
          if (next.ok && !isSameFieldValue(property.value, next.value)) onChange(next.value);
        }}/>
      {property.description && <small className="property-description au-field__hint">{property.description}</small>}
    </label>;
  }

  return <label className={'property-row' + (variant === 'axis' ? ' property-row--axis' : '')}>
    <span>{property.label}</span>
    {property.options?.length ?
      <select className="au-select" disabled={locked} value={String(property.value)}
        aria-label={property.label}
        onChange={event => validateAndCommit(event.target.value)}>
        {property.options.map(option =>
          <option key={String(option.value)} value={String(option.value)}>{option.label}</option>)}
      </select>
      : <input
          className="au-input"
          disabled={locked}
          type={property.type === 'number' ? 'number' : 'text'}
          value={draft}
          min={property.type === 'number' ? property.min : undefined}
          max={property.type === 'number' ? property.max : undefined}
          step={property.type === 'number' ? (property.step ?? 'any') : undefined}
          aria-label={property.label}
          aria-invalid={error ? true : undefined}
          onChange={event => { setDraft(event.target.value); setError(''); }}
          onBlur={() => {
            if (skipBlur.current) { skipBlur.current = false; return; }
            if (!locked) validateAndCommit(draft);
          }}
          onKeyDown={event => {
            if (event.key === 'Enter') event.currentTarget.blur();
            if (event.key === 'Escape') {
              skipBlur.current = true;
              setDraft(String(property.value));
              setError('');
              event.currentTarget.blur();
            }
          }}
        />}
    {error && <small className="property-error au-field__error" role="alert">{error}</small>}
    {property.description && <small className="property-description">{property.description}</small>}
  </label>;
}
