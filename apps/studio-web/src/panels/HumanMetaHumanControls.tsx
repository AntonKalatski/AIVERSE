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
    // The native side imports the model in the background: poll until ready.
    const poll = async (): Promise<MetaHumanReport> => {
      for (let i = 0; i < 120; ++i) { const r = await invoke('studio.humanMetaHuman', { guid }); if (!r.pending) return r; await new Promise(t => setTimeout(t, 500)); }
      throw Error('Модель слишком долго загружается');
    };
    void poll().then(r => {
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
      <div className="hc-mh">
        <div><b>{report.channelsFound}/52</b><span>мимических каналов</span></div>
        <div><b>{report.rig ? report.rig.halfJoints + report.rig.twistJoints : 0}</b><span>корректоров ведёт VIVID</span></div>
        <div><b>{report.rig?.jawJoint ? 'да' : 'морфы'}</b><span>челюсть</span></div>
        <div><b>{report.rig?.eyeJoints ? 'кости' : 'морфы'}</b><span>глаза</span></div>
      </div>
      {report.channelsMissing.length > 0 && <p className="hc-hint">Нет каналов: {report.channelsMissing.join(', ')}</p>}
      <details className="hc-item__more"><summary>Материалы ({Object.keys(report.materialRoles).length} ролей, скрыто {report.hiddenMaterials.length})</summary>
        {Object.entries(report.materialRoles).map(([m, r]) => <p key={m} className="hc-hint">{m} → {({ skin: 'кожа', eyes: 'глаза', lashes: 'ресницы', hair: 'волосы', cloth: 'ткань' } as Record<string, string>)[r] ?? r}</p>)}
        {report.hiddenMaterials.map(m => <p key={m} className="hc-hint">{m} → скрыт</p>)}
      </details>
      {report.notes.map(n => <HcNote key={n} tone="warn">{n}</HcNote>)}
    </> : <HcNote tone="warn">{report.notes[0] ?? 'Это не экспорт MetaHuman.'}</HcNote>)}
  </HcCard>;
}
