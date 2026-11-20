/** Pure field validation shared by all AI-authored Studio inspectors. */
export type PropertyType = 'text' | 'number' | 'boolean' | 'asset';
export interface FieldOption { label: string; value: string | number }
export interface FieldDescriptor {
  key: string;
  label: string;
  type: PropertyType;
  value: string | number | boolean;
  editable?: boolean;
  min?: number;
  max?: number;
  step?: number;
  integer?: boolean;
  options?: FieldOption[];
  description?: string;
  /** Optional asset kinds accepted by reference-picker fields. */
  assetKinds?: Array<'image' | 'model' | 'audio' | 'code' | 'document'>;
}
export type ParsedField = { ok: true; value: string | number | boolean } | { ok: false; error: string };

/** Never silently round, clamp or replace empty numeric input with zero. */
export function parseFieldDraft(field: FieldDescriptor, draft: string): ParsedField {
  if (field.editable === false) return { ok: false, error: 'Field is read-only' };
  if (field.type === 'boolean') return { ok: false, error: 'Boolean fields must use the checkbox control' };
  if (field.type === 'text' || field.type === 'asset') {
    if (field.options?.length && !field.options.some(option => option.value === draft)) {
      return { ok: false, error: 'Select an available option' };
    }
    return { ok: true, value: draft };
  }
  const text = draft.trim();
  if (text.length === 0) return { ok: false, error: 'Enter a number' };
  const value = Number(text);
  if (!Number.isFinite(value)) return { ok: false, error: 'Enter a finite number' };
  if (field.integer && !Number.isSafeInteger(value)) return { ok: false, error: 'Enter a whole number' };
  if (typeof field.min === 'number' && Number.isFinite(field.min) && value < field.min) {
    return { ok: false, error: `Minimum: ${field.min}` };
  }
  if (typeof field.max === 'number' && Number.isFinite(field.max) && value > field.max) {
    return { ok: false, error: `Maximum: ${field.max}` };
  }
  if (field.options?.length && !field.options.some(option => option.value === value)) {
    return { ok: false, error: 'Select an available option' };
  }
  return { ok: true, value };
}

export function validateBooleanChange(field: FieldDescriptor, checked: boolean): ParsedField {
  if (field.editable === false) return { ok: false, error: 'Field is read-only' };
  if (field.type !== 'boolean') return { ok: false, error: 'Expected a boolean field' };
  return { ok: true, value: checked };
}

export function isSameFieldValue(previous: string | number | boolean, next: string | number | boolean): boolean {
  return typeof previous === typeof next && previous === next;
}
