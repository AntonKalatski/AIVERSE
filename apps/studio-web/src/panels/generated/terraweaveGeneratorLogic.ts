/** Pure form logic of the TerraWeave generator panel (no React, no bridge). */
export interface TerrainForm { seed: number; reliefCm: number; basePeriodCm: number; octaves: number; baseHeightCm: number; cellSizeCm: number }
export interface ForestForm { enabled: boolean; spacingCm: number; densityPermille: number; maxSlopePermille: number; seedOffset: number }
export type GenerateMode = 'new' | 'overwrite' | 'preserveEdits';

export const DEFAULT_TERRAIN: TerrainForm = { seed: 20261121, reliefCm: 1400, basePeriodCm: 9000, octaves: 6, baseHeightCm: 0, cellSizeCm: 100 };
export const DEFAULT_FOREST: ForestForm = { enabled: true, spacingCm: 1100, densityPermille: 780, maxSlopePermille: 1250, seedOffset: 0 };

const int = (v: number) => Number.isFinite(v) && Number.isInteger(v);

/** Mirrors the terraweave_generate schema; null when valid. */
export function validateTerrain(t: TerrainForm): string | null {
  if (!int(t.seed) || t.seed < 0) return 'Seed must be a whole number >= 0.';
  if (!int(t.reliefCm) || t.reliefCm < 0) return 'Relief must be >= 0.';
  if (!int(t.basePeriodCm) || t.basePeriodCm < 100) return 'Feature size must be at least 1 m.';
  if (!int(t.octaves) || t.octaves < 1 || t.octaves > 12) return 'Octaves: 1..12.';
  if (!int(t.baseHeightCm)) return 'Base height must be a number.';
  if (!int(t.cellSizeCm) || t.cellSizeCm < 1) return 'Cell size must be at least 1 cm.';
  return null;
}
/** Mirrors terraweave_forest_set limits; null when valid. */
export function validateForest(f: ForestForm): string | null {
  if (!int(f.spacingCm) || f.spacingCm < 300 || f.spacingCm > 5000) return 'Tree spacing: 3..50 m.';
  if (!int(f.densityPermille) || f.densityPermille < 0 || f.densityPermille > 1000) return 'Density: 0..100 %.';
  if (!int(f.maxSlopePermille) || f.maxSlopePermille < 0 || f.maxSlopePermille > 5000) return 'Max slope: 0..500 %.';
  if (!int(f.seedOffset) || f.seedOffset < 0) return 'Seed offset must be a whole number >= 0.';
  return null;
}
export function terrainParams(t: TerrainForm, mode: GenerateMode): Record<string, unknown> {
  const p: Record<string, unknown> = { seed: t.seed, reliefCm: t.reliefCm, basePeriodCm: t.basePeriodCm, octaves: t.octaves,
    baseHeightCm: t.baseHeightCm, cellSizeCm: t.cellSizeCm };
  if (mode === 'overwrite') p.overwrite = true;
  if (mode === 'preserveEdits') p.preserveEdits = true;
  return p;
}
export function forestParams(f: ForestForm): Record<string, unknown> {
  return { enabled: f.enabled, spacingCm: f.spacingCm, densityPermille: f.densityPermille,
    maxSlopePermille: f.maxSlopePermille, seedOffset: f.seedOffset };
}
export function previewStats(rows: number[][]) {
  const all = rows.flat().filter(Number.isFinite);
  const min = all.length ? Math.min(...all) : 0, max = all.length ? Math.max(...all) : 0;
  return { min, max, normalized: (h: number) => max > min ? (h - min) / (max - min) : 0.5 };
}
