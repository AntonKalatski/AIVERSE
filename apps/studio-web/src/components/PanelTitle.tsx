import type { ReactNode } from 'react';

export default function PanelTitle({ title, right }: { title: string; right?: ReactNode }) {
  return <div className="panel-heading"><span>{title}</span><div className="panel-actions">{right}</div></div>;
}
