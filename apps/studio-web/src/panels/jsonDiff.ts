/** Structural JSON diff for the Snapshot Diff panel: path-level added / removed / changed entries. No React. */
export interface JsonChange { path: string; kind: 'added' | 'removed' | 'changed'; before?: unknown; after?: unknown }

const isObj = (v: unknown): v is Record<string, unknown> => v !== null && typeof v === 'object' && !Array.isArray(v);

/** Numbers within `epsilon` are equal (float noise in saved maps). Stops after `limit` changes. */
export function jsonDiff(a: unknown, b: unknown, epsilon = 1e-6, limit = 2000): JsonChange[] {
  const out: JsonChange[] = [];
  const walk = (x: unknown, y: unknown, path: string) => {
    if (out.length >= limit) return;
    if (typeof x === 'number' && typeof y === 'number') { if (Math.abs(x - y) > epsilon) out.push({ path, kind: 'changed', before: x, after: y }); return; }
    if (Array.isArray(x) && Array.isArray(y)) {
      const n = Math.max(x.length, y.length);
      for (let i = 0; i < n; i++) {
        const p = `${path}[${i}]`;
        if (i >= x.length) out.push({ path: p, kind: 'added', after: y[i] });
        else if (i >= y.length) out.push({ path: p, kind: 'removed', before: x[i] });
        else walk(x[i], y[i], p);
        if (out.length >= limit) return;
      }
      return;
    }
    if (isObj(x) && isObj(y)) {
      for (const k of Object.keys(x)) {
        const p = path ? `${path}.${k}` : k;
        if (!(k in y)) out.push({ path: p, kind: 'removed', before: x[k] }); else walk(x[k], y[k], p);
        if (out.length >= limit) return;
      }
      for (const k of Object.keys(y)) if (!(k in x)) { out.push({ path: path ? `${path}.${k}` : k, kind: 'added', after: y[k] }); if (out.length >= limit) return; }
      return;
    }
    if (JSON.stringify(x) !== JSON.stringify(y)) out.push({ path: path || '(root)', kind: 'changed', before: x, after: y });
  };
  walk(a, b, '');
  return out;
}

/** One-line preview of a value for the diff table. */
export function previewValue(v: unknown, max = 80): string {
  if (v === undefined) return '';
  const s = typeof v === 'string' ? JSON.stringify(v) : JSON.stringify(v) ?? String(v);
  return s.length > max ? s.slice(0, max - 1) + '…' : s;
}
