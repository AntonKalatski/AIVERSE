import {useEffect,useState} from 'react';
import {invoke} from '../bridge';
import type {HumanPreviewAnimationSettings} from '../types';
import {StudioSection,StudioField,StudioSelect,StudioInput,StudioButton,StudioAlert} from '../ui';
const defaults:HumanPreviewAnimationSettings={pose:'relaxed',playing:true,faceIdle:true,speed:1,automaticLod:true};
export default function HumanPreviewAnimationControls(){
 const [draft,setDraft]=useState(defaults),[ready,setReady]=useState(false),[busy,setBusy]=useState(false),[error,setError]=useState('');
 useEffect(()=>{let live=true;void invoke('studio.humanPreviewAnimation',{}).then(s=>{if(live){setDraft(s);setReady(true);}}).catch(e=>{if(live)setError(String(e));});return ()=>{live=false;};},[]);
 const apply=async(settings:HumanPreviewAnimationSettings)=>{setBusy(true);setError('');try{setDraft(await invoke('studio.humanPreviewAnimation',{settings}));}catch(e){setError(String(e));}finally{setBusy(false);}};
 return <StudioSection title="Idle лица и позы">
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  <StudioField htmlFor="human-preview-pose" label="Поза"><StudioSelect id="human-preview-pose" disabled={busy||!ready} value={draft.pose} onChange={e=>void apply({...draft,pose:e.target.value})}>{[['relaxed','Расслабленная стойка'],['a_pose','A-pose'],['t_pose','T-pose'],['portrait','Портретная']].map(([id,label])=><option key={id} value={id}>{label}</option>)}</StudioSelect></StudioField>
  <StudioField htmlFor="human-preview-lod" label="Детализация персонажа"><StudioSelect id="human-preview-lod" disabled={busy||!ready} value={draft.automaticLod===false?'highest':'auto'} onChange={e=>void apply({...draft,automaticLod:e.target.value==='auto'})}><option value="auto">Автоматически по размеру на экране</option><option value="highest">LOD0 — максимальная детализация</option></StudioSelect></StudioField>
  <StudioField htmlFor="human-preview-face" label="Моргание и лёгкая мимика"><StudioInput id="human-preview-face" type="checkbox" checked={draft.faceIdle} disabled={busy||!ready} onChange={e=>void apply({...draft,faceIdle:e.target.checked})}/></StudioField>
  <StudioField htmlFor="human-preview-speed" label="Скорость"><StudioSelect id="human-preview-speed" value={draft.speed} disabled={busy||!ready} onChange={e=>void apply({...draft,speed:Number(e.target.value)})}>{[.25,.5,1,1.5,2].map(v=><option key={v} value={v}>{v}×</option>)}</StudioSelect></StudioField>
  <div className="au-row"><StudioButton disabled={busy||!ready} onClick={()=>void apply({...draft,playing:!draft.playing})}>{draft.playing?'Пауза':'Воспроизвести'}</StudioButton><StudioButton disabled={busy||!ready} onClick={()=>void apply(defaults)}>Дефолтный idle</StudioButton></div>
  <StudioAlert>Предпросмотр через общий проигрыватель анимации. Позы требуют костей upperarm / lowerarm / neck / spine; лицо — именованных морфов. Несовместимые каналы пропускаются. В крайних позах проверьте пересечения одежды. Исходная модель и сборка персонажа не изменяются.</StudioAlert>
 </StudioSection>;
}
