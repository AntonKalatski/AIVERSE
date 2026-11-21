/** Pure helpers for Inspector component actions (reset / copy / paste). */
import type { StudioInspectorComponent, StudioProperty } from '../types.ts';

export type FieldValue = string | number | boolean;
export interface CopiedComponent { componentId: string; values: Record<string, FieldValue> }

const editable = (p: StudioProperty) => p.editable !== false && p.type !== 'asset' || (p.type === 'asset' && p.editable !== false);

/** Values written by "Reset": Transform rotation 0 / scale 1 (position kept), Mesh Renderer built-in cube + default materials. */
export function resetValues(component: StudioInspectorComponent): Record<string, FieldValue> | null {
  const out: Record<string, FieldValue> = {};
  for (const p of component.properties) {
    if (!editable(p)) continue;
    if (component.id === 'Transform') {
      if (/^rotation[XYZ]$|^yaw$/.test(p.key) && p.value !== 0) out[p.key] = 0;
      else if (/^scale([XYZ])?$/.test(p.key) && p.value !== 1) out[p.key] = 1;
    } else if (component.id === 'MeshRenderer') {
      if (p.key === 'asset' && p.value !== 'builtin:cube') out[p.key] = 'builtin:cube';
      else if (p.key.startsWith('material:') && p.value !== '') out[p.key] = '';
    } else return null;
  }
  return out;
}
export function canReset(component: StudioInspectorComponent): boolean {
  const values = resetValues(component);
  return !!values && Object.keys(values).length > 0;
}
export function copyValues(component: StudioInspectorComponent): CopiedComponent {
  const values: Record<string, FieldValue> = {};
  for (const p of component.properties) values[p.key] = p.value;
  return { componentId: component.id, values };
}
/** Only keys that exist, are editable and differ on the target are pasted. */
export function pasteValues(copied: CopiedComponent | null, target: StudioInspectorComponent): Record<string, FieldValue> | null {
  if (!copied || copied.componentId !== target.id) return null;
  const out: Record<string, FieldValue> = {};
  for (const p of target.properties)
    if (editable(p) && p.key in copied.values && copied.values[p.key] !== p.value) out[p.key] = copied.values[p.key];
  return Object.keys(out).length ? out : null;
}
export function canRemove(component: StudioInspectorComponent): boolean {
  if (component.id === 'Transform' || component.id === 'Unit' || component.id === 'NereidWater' || component.id === 'Light') return false;
  if (component.id === 'MeshRenderer') return component.properties.some(p => p.key === 'asset' && p.editable !== false);
  return true;
}
/** A material slot whose value is not in the library (shown as missing with actions). */
export function missingOption(p: StudioProperty): boolean {
  return !!p.options?.length && p.value !== '' && !p.options.some(o => String(o.value) === String(p.value));
}
