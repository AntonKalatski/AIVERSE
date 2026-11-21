/**
 * Schema-driven forms for Studio tool panels (Terrain, Roads, PostFX, ...). Each panel runs the
 * same Workspace ops as the MCP agent; their JSON schemas become form fields here. No React.
 */
export type ToolFieldKind = 'number' | 'integer' | 'boolean' | 'string' | 'enum' | 'json';
export interface ToolField {
  key: string; kind: ToolFieldKind; label: string; hint: string; required: boolean;
  options?: string[]; min?: number; max?: number; def?: unknown;
}
export interface ToolOpInfo { name: string; description: string; schema: unknown; readOnly: boolean }

function obj(v: unknown): Record<string, unknown> | null {
  return v !== null && typeof v === 'object' && !Array.isArray(v) ? v as Record<string, unknown> : null;
}

/** "roadType" / "min_quality" -> "Road type" / "Min quality". */
export function fieldLabel(key: string): string {
  const words = key.replace(/_/g, ' ').replace(/([a-z0-9])([A-Z])/g, '$1 $2').toLowerCase().trim();
  return words ? words[0].toUpperCase() + words.slice(1) : key;
}

/** "terrain_paint_material" -> "Paint material" with the panel prefix dropped when it is the op's first word. */
export function opLabel(name: string, prefix = ''): string {
  let words = name.split('_');
  if (prefix && words.length > 1 && (words[0] === prefix || words[0] + 's' === prefix || words[0] === prefix + 's')) words = words.slice(1);
  const text = words.join(' ');
  return text ? text[0].toUpperCase() + text.slice(1) : name;
}

export function schemaFields(schema: unknown): ToolField[] {
  const root = obj(schema);
  const props = obj(root?.properties);
  if (!props) return [];
  const required = new Set(Array.isArray(root?.required) ? (root!.required as unknown[]).filter((k): k is string => typeof k === 'string') : []);
  const out: ToolField[] = [];
  for (const [key, raw] of Object.entries(props)) {
    const p = obj(raw) ?? {};
    const type = p.type;
    let kind: ToolFieldKind = 'json';
    let options: string[] | undefined;
    if (Array.isArray(p.enum) && p.enum.every(e => typeof e === 'string')) { kind = 'enum'; options = p.enum as string[]; }
    else if (type === 'number') kind = 'number';
    else if (type === 'integer') kind = 'integer';
    else if (type === 'boolean') kind = 'boolean';
    else if (type === 'string') kind = 'string';
    const field: ToolField = { key, kind, label: fieldLabel(key), hint: typeof p.description === 'string' ? p.description : '', required: required.has(key) };
    if (options) field.options = options;
    if (typeof p.minimum === 'number') field.min = p.minimum;
    if (typeof p.maximum === 'number') field.max = p.maximum;
    if (p.default !== undefined) field.def = p.default;
    out.push(field);
  }
  // Required fields first, keeping the schema order inside each group.
  return [...out.filter(f => f.required), ...out.filter(f => !f.required)];
}

/** Initial text of a field (forms keep every value as text until Run). */
const CHOICE_KEYS = new Set(['op', 'mode', 'kind', 'shape', 'action']);
export function fieldDraft(field: ToolField): string {
  // Ops whose "op"/"mode" choice is required in practice (the schema drops "required" for planet calls) start on the first option.
  if (field.def === undefined) return field.kind === 'enum' && CHOICE_KEYS.has(field.key) ? field.options![0] : '';
  return typeof field.def === 'string' ? field.def : JSON.stringify(field.def);
}

export interface BuiltParams { params: Record<string, unknown>; errors: Record<string, string> }

/** Form text -> op parameters. Empty optional fields are left out, so the op keeps its own defaults. */
export function buildParams(fields: ToolField[], drafts: Record<string, string>): BuiltParams {
  const params: Record<string, unknown> = {}, errors: Record<string, string> = {};
  for (const f of fields) {
    const text = (drafts[f.key] ?? '').trim();
    if (!text) { if (f.required) errors[f.key] = 'Required'; continue; }
    switch (f.kind) {
      case 'number': case 'integer': {
        const n = Number(text);
        if (!Number.isFinite(n)) { errors[f.key] = 'Not a number'; break; }
        if (f.kind === 'integer' && !Number.isInteger(n)) { errors[f.key] = 'Whole number'; break; }
        if (f.min !== undefined && n < f.min) { errors[f.key] = 'At least ' + f.min; break; }
        if (f.max !== undefined && n > f.max) { errors[f.key] = 'At most ' + f.max; break; }
        params[f.key] = n; break;
      }
      case 'boolean':
        if (text !== 'true' && text !== 'false') { errors[f.key] = 'true or false'; break; }
        params[f.key] = text === 'true'; break;
      case 'enum':
        if (f.options && !f.options.includes(text)) { errors[f.key] = 'One of ' + f.options.join(', '); break; }
        params[f.key] = text; break;
      case 'string': params[f.key] = text; break;
      case 'json':
        try { params[f.key] = JSON.parse(text); }
        catch { params[f.key] = text; }   // a bare word for an untyped field ("json": {} accepts anything)
        break;
    }
  }
  return { params, errors };
}

/** Ops shown as "state" at the top of a panel: read-only and runnable without parameters. */
export function stateOps(ops: ToolOpInfo[], wanted: string[]): ToolOpInfo[] {
  return wanted.map(n => ops.find(o => o.name === n)).filter((o): o is ToolOpInfo =>
    !!o && o.readOnly && schemaFields(o.schema).every(f => !f.required));
}

/** Short one-line text of an op answer for the result strip. */
export function resultLine(result: unknown): string {
  const r = obj(result);
  if (!r) return result === undefined ? 'Done' : String(result);
  for (const k of ['message', 'text', 'summary', 'status']) if (typeof r[k] === 'string' && (r[k] as string).length < 200) return r[k] as string;
  const warnings = Array.isArray(r.warnings) ? r.warnings.length : 0;
  const keys = Object.keys(r).filter(k => k !== 'ok');
  return (warnings ? warnings + ' warning' + (warnings > 1 ? 's' : '') + ' · ' : '') + (keys.length ? keys.slice(0, 6).join(', ') : 'Done');
}
