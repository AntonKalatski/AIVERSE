// Pure helpers for the Digital Human Creator window (no React, node-testable).
import type { HumanCreatorField, HumanCreatorInfo, HumanLook, HumanLookField } from '../types';

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

/** Every head part that must deform together with the face (skin, eyes,
 *  brows, lashes, hair cards, teeth): otherwise hair and brows float off a
 *  reshaped skull. LOD0 preferred; body and clothing never included. */
export function guessFaceMeshes(meshes: string[]): string[] {
  const head = meshes.filter(m => /face|head|eye|brow|lash|hair|teeth|tongue|cornea|beard|mustache|cartilage/i.test(m)
    && !/body|shirt|top|btm|slack|pant|shoe|feet|watch|chain|torso|neck_?wear/i.test(m));
  const lod0 = head.filter(m => /lod0/i.test(m) || !/lod\d/i.test(m));
  return (lod0.length ? lod0 : head).slice(0, 16);
}

/** MetaHuman-style blend of two appearance presets: t=0 -> a, t=1 -> b.
 *  Missing values use the schema default; material roles follow the nearer preset. */
export function blendLooks(a: HumanLook, b: HumanLook, t: number, schema: HumanLookField[]): HumanLook {
  const w = Math.min(1, Math.max(0, Number.isFinite(t) ? t : 0));
  const values: Record<string, number> = {};
  for (const f of schema) {
    const va = a.values[f.key] ?? f.initial, vb = b.values[f.key] ?? f.initial;
    const v = va + (vb - va) * w;
    if (Math.abs(v - f.initial) > 1e-6) values[f.key] = Math.min(f.max, Math.max(f.min, v));
  }
  return { version: 1, values, materials: { ...(w < .5 ? a.materials : b.materials) } };
}

/** MetaHuman-style face blend circle: archetypes sit on a ring, the handle's
 *  position gives inverse-distance weights; each region slider scales how much
 *  of the blend reaches that region's keys. Values stay inside the schema. */
export interface FaceArchetype { id: string; label: string; values: Record<string, number> }
export const FACE_ARCHETYPES: FaceArchetype[] = [
  { id: 'base', label: 'Базовое', values: {} },
  { id: 'narrow', label: 'Узкое', values: { width: -0.108, jawWidth: -0.09, cheekWidth: -0.072, noseWidth: -0.072, chinLength: 0.048, height: 0.048 } },
  { id: 'broad', label: 'Широкое', values: { width: 0.12, jawWidth: 0.132, cheekWidth: 0.09, noseWidth: 0.09, mouthWidth: 0.06, height: -0.03 } },
  { id: 'sharp', label: 'Острое', values: { cheekForward: 0.06, chinForward: 0.072, noseForward: 0.06, noseLength: 0.06, jawWidth: -0.048, browHeight: 0.036 } },
  { id: 'soft', label: 'Мягкое', values: { cheekForward: -0.048, lipVolume: 0.042, noseLength: -0.048, chinForward: -0.048, eyeSpacing: 0.03, width: 0.036 } },
  { id: 'long', label: 'Вытянутое', values: { height: 0.12, chinLength: 0.09, noseLength: 0.072, eyeHeight: 0.036, mouthHeight: -0.036, width: -0.048 } }];
export const FACE_REGIONS: Record<string, string[]> = {
  'Нос': ['noseWidth', 'noseLength', 'noseForward'],
  'Глаза': ['eyeSpacing', 'eyeHeight'],
  'Рот': ['mouthWidth', 'mouthHeight', 'lipVolume'],
  'Челюсть': ['jawWidth', 'chinLength', 'chinForward'],
  'Брови': ['browHeight'],
  'Скулы и овал': ['cheekWidth', 'cheekForward', 'width', 'height']};
/** Ring position of archetype i (unit circle, first at the top). */
export function archetypePoint(i: number, n = FACE_ARCHETYPES.length): [number, number] {
  const a = -Math.PI / 2 + (i * 2 * Math.PI) / n; return [Math.cos(a), Math.sin(a)];
}
export function blendWeights(handle: [number, number], n = FACE_ARCHETYPES.length): number[] {
  const d = Array.from({ length: n }, (_, i) => { const p = archetypePoint(i, n); return Math.hypot(p[0] - handle[0], p[1] - handle[1]); });
  const hit = d.findIndex(v => v < 1e-4);
  if (hit >= 0) return d.map((_, i) => i === hit ? 1 : 0);
  const inv = d.map(v => 1 / (v * v)); const sum = inv.reduce((a, b) => a + b, 0);
  return inv.map(v => v / sum);
}
export function blendFace(handle: [number, number], regions: Record<string, number>,
  schema: { key: string; min: number; max: number }[], archetypes = FACE_ARCHETYPES,
  perRegion: Record<string, [number, number]> = {}): Record<string, number> {
  // perRegion: MetaHuman-style independent blend of one face part (picked in
  // the viewport); parts without their own handle follow the main one.
  const w = blendWeights(handle, archetypes.length); const out: Record<string, number> = {};
  for (const f of schema) {
    const region = Object.entries(FACE_REGIONS).find(([, keys]) => keys.includes(f.key))?.[0];
    const rw = region && perRegion[region] ? blendWeights(perRegion[region], archetypes.length) : w;
    let v = 0; archetypes.forEach((a, i) => { v += (a.values[f.key] ?? 0) * rw[i]; });
    const scale = region ? Math.min(1, Math.max(0, regions[region] ?? 1)) : 1;
    v = Math.min(f.max, Math.max(f.min, v * scale));
    if (Math.abs(v) > 1e-5) out[f.key] = Math.round(v * 1e4) / 1e4;
  }
  return out;
}

/** Human-readable face zone of a normalized sculpt point (x 0..1 left->right, y 0..1 up). */
export function sculptZone(p: [number, number, number]): string {
  const [x, y] = p; const side = x < .44 ? ' слева' : x > .56 ? ' справа' : '';
  const zone = y > .78 ? 'Лоб' : y > .66 ? 'Брови' : y > .56 ? 'Глаза' : y > .45 ? (side ? 'Скула' : 'Нос') : y > .34 ? (side ? 'Щека' : 'Нос') : y > .22 ? 'Рот' : y > .1 ? 'Подбородок' : 'Шея';
  return zone + side;
}

/** Face-blend region under a head point (viewport pick), or null off-face. */
export function faceRegionAt(p: [number, number, number]): string | null {
  const zone = sculptZone(p).replace(/ (слева|справа)$/, '');
  const map: Record<string, string> = { 'Лоб': 'Брови', 'Брови': 'Брови', 'Глаза': 'Глаза', 'Нос': 'Нос', 'Скула': 'Скулы и овал',
    'Щека': 'Скулы и овал', 'Рот': 'Рот', 'Подбородок': 'Челюсть' };
  return map[zone] ?? null;
}
