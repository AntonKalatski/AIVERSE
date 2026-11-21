// Pose & expression test for the Character Creator: every click applies at
// once, so a sculpted or blended face can be checked in motion.
import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { HumanPreviewAnimationSettings } from '../types';
import { HcCard, HcNote, HcSegment, HcSwitch } from './HumanCreatorUi';

const defaults: HumanPreviewAnimationSettings = { pose: 'relaxed', playing: true, faceIdle: true, speed: 1, automaticLod: true, expression: 'idle' };
const POSES = [{ id: 'relaxed', label: 'Стойка' }, { id: 'a_pose', label: 'A-поза' }, { id: 't_pose', label: 'T-поза' }, { id: 'portrait', label: 'Портрет' }];
export const EXPRESSIONS = [
  { id: 'idle', label: 'Спокойное', emoji: '😐' }, { id: 'smile', label: 'Улыбка', emoji: '🙂' },
  { id: 'laugh', label: 'Смех', emoji: '😄' }, { id: 'surprise', label: 'Удивление', emoji: '😮' },
  { id: 'anger', label: 'Злость', emoji: '😠' }, { id: 'sad', label: 'Грусть', emoji: '😢' },
  { id: 'disgust', label: 'Отвращение', emoji: '🤢' }, { id: 'talk', label: 'Речь', emoji: '🗣️' }];

export default function HumanPreviewAnimationControls() {
  const [draft, setDraft] = useState(defaults), [ready, setReady] = useState(false), [busy, setBusy] = useState(false), [error, setError] = useState('');
  useEffect(() => {
    let live = true;
    void invoke('studio.humanPreviewAnimation', {}).then(s => { if (live) { setDraft({ ...defaults, ...s }); setReady(true); } })
      .catch(e => { if (live) setError(String(e)); });
    return () => { live = false; };
  }, []);
  const apply = async (settings: HumanPreviewAnimationSettings) => {
    setDraft(settings); setBusy(true); setError('');
    try { setDraft({ ...defaults, ...await invoke('studio.humanPreviewAnimation', { settings }) }); }
    catch (e) { setError(String(e)); } finally { setBusy(false); }
  };
  const off = busy || !ready;
  return <>
    {error && <HcNote tone="error">{error}</HcNote>}
    <HcCard title="Мимика" hint="Проверьте, как лицо двигается после смешивания и скульпта">
      <div className="hc-chips">{EXPRESSIONS.map(e => <button key={e.id} type="button" className="hc-chip"
        aria-pressed={(draft.expression ?? 'idle') === e.id} disabled={off}
        onClick={() => void apply({ ...draft, expression: e.id })}><span aria-hidden="true">{e.emoji}</span>{e.label}</button>)}</div>
      <HcSwitch label="Моргание и лёгкая мимика" checked={draft.faceIdle} disabled={off} onChange={v => void apply({ ...draft, faceIdle: v })}/>
    </HcCard>
    <HcCard title="Поза">
      <HcSegment label="Поза" value={draft.pose} options={POSES} disabled={off} onChange={pose => void apply({ ...draft, pose })}/>
      <HcSwitch label="Анимация" hint="Дыхание и микродвижения" checked={draft.playing} disabled={off} onChange={v => void apply({ ...draft, playing: v })}/>
      <HcSegment label="Скорость" value={String(draft.speed)} disabled={off}
        options={[{ id: '0.5', label: '×0.5' }, { id: '1', label: '×1' }, { id: '1.5', label: '×1.5' }, { id: '2', label: '×2' }]}
        onChange={v => void apply({ ...draft, speed: Number(v) })}/>
      <HcSwitch label="Максимальная детализация" hint="Всегда LOD0 вместо автоматического выбора" checked={draft.automaticLod === false} disabled={off}
        onChange={v => void apply({ ...draft, automaticLod: !v })}/>
    </HcCard>
  </>;
}
