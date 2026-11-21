// MetaHuman adapter card: checks the chosen model against MetaHuman export
// conventions and sets up the Character Creator for it (material roles, face
// meshes), with an honest list of what is not supported.
import { useState } from 'react';
import { ScanFace } from 'lucide-react';
import { invoke } from '../bridge';
import type { HumanFaceShape, HumanLook, MetaHumanReport } from '../types';
import { StudioButton } from '../ui';
import { HcCard, HcNote } from './HumanCreatorUi';

export default function HumanMetaHumanControls({ guid, draft, face, onApply }:
  { guid: string; draft: HumanLook; face: HumanFaceShape | null; onApply: (look: HumanLook, face: HumanFaceShape | null) => void }) {
  const [report, setReport] = useState<MetaHumanReport | null>(null);
  const [busy, setBusy] = useState(false), [error, setError] = useState('');
  const run = () => { setBusy(true); setError('');
    void invoke('studio.humanMetaHuman', { guid }).then(r => {
      setReport(r);
      if (r.detected) onApply({ ...draft, materials: { ...r.look.materials, ...draft.materials } },
        face?.meshes.length ? face : { ...r.face, landmarks: face?.landmarks ?? [], values: face?.values ?? {} });
    }).catch(e => setError(String(e).replace(/^Error: /, ''))).finally(() => setBusy(false)); };
  return <HcCard title="MetaHuman" hint="Экспорт MetaHuman (glTF / GLB / FBX) без Unreal в рантайме: проверка и настройка редактора">
    {error && <HcNote tone="error">{error}</HcNote>}
    <StudioButton disabled={busy || !guid} onClick={run}><ScanFace size={14}/> {busy ? 'Проверяю…' : 'Проверить и настроить'}</StudioButton>
    {report && (report.detected ? <>
      <p className="hc-hint">Распознан MetaHuman: кость головы «{report.headBone}», мешей лица {report.faceMeshes.length}, ролей материалов {Object.keys(report.materialRoles).length},
        ARKit-каналов {report.channelsFound} из 52, LOD {report.lodLevels}, скинов {report.skins}. Роли и меши лица применены к образу.</p>
      {report.notes.map(n => <HcNote key={n} tone="warn">{n}</HcNote>)}
    </> : <HcNote tone="warn">{report.notes[0] ?? 'Это не экспорт MetaHuman.'}</HcNote>)}
  </HcCard>;
}
