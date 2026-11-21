import { useState } from 'react';
import { ChevronDown, ChevronRight } from 'lucide-react';

/** Read-only JSON view: objects and arrays fold, long arrays show their first 50 items. */
export default function JsonTree({ value, name, depth = 0, open }: { value: unknown; name?: string; depth?: number; open?: boolean }) {
  const [expanded, setExpanded] = useState(open ?? depth < 1);
  const isArr = Array.isArray(value);
  const isObj = value !== null && typeof value === 'object';
  if (!isObj) {
    const cls = typeof value === 'string' ? 'jt__str' : typeof value === 'number' ? 'jt__num' : 'jt__lit';
    return <div className="jt__row" style={{ paddingLeft: depth * 12 }}>
      {name !== undefined && <span className="jt__key">{name}</span>}
      <span className={cls}>{typeof value === 'string' ? value : String(value)}</span></div>;
  }
  const entries = isArr ? (value as unknown[]).map((v, i) => [String(i), v] as const) : Object.entries(value as Record<string, unknown>);
  const shown = entries.slice(0, 50);
  return <div className="jt">
    <button type="button" className="jt__row jt__fold" style={{ paddingLeft: depth * 12 }} onClick={() => setExpanded(!expanded)}>
      {expanded ? <ChevronDown size={11}/> : <ChevronRight size={11}/>}
      {name !== undefined && <span className="jt__key">{name}</span>}
      <span className="jt__meta">{isArr ? `[${entries.length}]` : `{${entries.length}}`}</span>
    </button>
    {expanded && shown.map(([k, v]) => <JsonTree key={k} name={k} value={v} depth={depth + 1}/>)}
    {expanded && entries.length > shown.length && <div className="jt__row jt__meta" style={{ paddingLeft: (depth + 1) * 12 }}>… {entries.length - shown.length} more</div>}
  </div>;
}
