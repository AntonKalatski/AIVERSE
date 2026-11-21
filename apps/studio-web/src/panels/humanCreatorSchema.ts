// Pure helpers for the Digital Human Creator window (no React, node-testable).
import type { HumanCreatorField, HumanCreatorInfo } from '../types';

export type CreatorValue = number | string | number[] | string[];

/** Reads a dotted DNA path ("skin.melanin", "expressionRest.12") from the document DNA. */
export function readCreatorValue(dna: Record<string, unknown>, key: string): CreatorValue | undefined {
  let cursor: unknown = dna;
  for (const part of key.split('.')) {
    if (cursor === null || typeof cursor !== 'object') return undefined;
    cursor = Array.isArray(cursor) ? cursor[Number(part)] : (cursor as Record<string, unknown>)[part];
  }
  return cursor as CreatorValue | undefined;
}

/** Clamps and snaps a numeric field the way native validation accepts it; NaN -> null. */
export function normalizeCreatorNumber(field: HumanCreatorField, raw: number): number | null {
  if (!Number.isFinite(raw)) return null;
  const lo = field.min ?? -Infinity, hi = field.max ?? Infinity;
  let v = Math.min(hi, Math.max(lo, raw));
  if (field.step && field.step >= 1) v = Math.round(v);
  return v;
}

const hex2 = (c: number) => Math.round(Math.min(1, Math.max(0, c)) * 255).toString(16).padStart(2, '0');
/** Linear [r,g,b] 0..1 <-> #rrggbb for <input type=color>. */
export function rgbToHex(rgb: number[]): string {
  return '#' + [0, 1, 2].map(i => hex2(Number(rgb?.[i] ?? 0))).join('');
}
export function hexToRgb(hex: string): number[] | null {
  const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex);
  return m ? [1, 2, 3].map(i => Math.round(parseInt(m[i], 16) / 255 * 1000) / 1000) : null;
}

/** Field value for the native call: colours as numbers, bones list from comma text. */
export function creatorWireValue(field: HumanCreatorField, input: string | number | number[]): CreatorValue | null {
  if (field.kind === 'color') return Array.isArray(input) && input.length === 3 ? input : null;
  if (field.kind === 'number' || field.kind === 'integer') {
    const n = typeof input === 'number' ? input : Number(input);
    if (field.kind === 'integer') return Number.isInteger(n) && n >= 0 ? n : null;
    return normalizeCreatorNumber(field, n);
  }
  if (field.kind === 'bones') {
    const list = String(input).split(',').map(s => s.trim()).filter(Boolean);
    return list.length <= 128 ? list : null;
  }
  const text = String(input);
  return text.length > 0 && text.length <= 128 ? text : null;
}

/** Expression channels currently non-zero, for the "only active" filter. */
export function activeExpressionCount(info: HumanCreatorInfo | null): number {
  const rest = info?.document.dna.expressionRest;
  return Array.isArray(rest) ? rest.filter(v => Number(v) > 0).length : 0;
}
