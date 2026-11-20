import {useEffect,useState} from 'react';
import {invoke} from '../bridge';
import type {HumanStudioLightingSettings,HumanStudioLightingInfo} from '../types';
import {StudioSection,StudioField,StudioInput,StudioSelect,StudioButton,StudioAlert} from '../ui';
const defaults:HumanStudioLightingSettings={preset:'neutral',rotation:0,gain:1,backdrop:true};
export default function HumanStudioLightingControls(){
 const [info,setInfo]=useState<HumanStudioLightingInfo|null>(null),[draft,setDraft]=useState(defaults);
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[status,setStatus]=useState('');
 useEffect(()=>{let live=true;void invoke('studio.humanStudioLighting',{}).then(i=>{if(live){setInfo(i);setDraft(i.settings);}}).catch(e=>{if(live)setError(String(e));});return ()=>{live=false;};},[]);
 const apply=async(settings:HumanStudioLightingSettings)=>{setBusy(true);setError('');try{const i=await invoke('studio.humanStudioLighting',{settings});setInfo(i);setDraft(i.settings);setStatus('Свет применён к нативному предпросмотру');}catch(e){setError(String(e));}finally{setBusy(false);}};
 return <StudioSection title="Студийное освещение">
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}{status&&<StudioAlert>{status}</StudioAlert>}
  <StudioField label="Световой пресет" htmlFor="human-studio-preset"><StudioSelect id="human-studio-preset" disabled={busy||!info} value={draft.preset} onChange={e=>{setDraft({...draft,preset:e.target.value});setStatus('Пресет выбран — примените свет');}}>{info?.presets.map(p=><option key={p.id} value={p.id}>{p.label}</option>)}</StudioSelect></StudioField>
  <StudioField label={'Поворот света · '+draft.rotation+'°'} htmlFor="human-studio-rotation"><StudioInput id="human-studio-rotation" type="range" min={-180} max={180} step={1} value={draft.rotation} disabled={busy} onChange={e=>setDraft({...draft,rotation:Number(e.target.value)})}/></StudioField>
  <StudioField label={'Яркость · '+draft.gain.toFixed(2)} htmlFor="human-studio-gain"><StudioInput id="human-studio-gain" type="range" min={.25} max={2} step={.01} value={draft.gain} disabled={busy} onChange={e=>setDraft({...draft,gain:Number(e.target.value)})}/></StudioField>
  <StudioField label="Циклорама · плавный переход пола в стену" htmlFor="human-studio-backdrop"><StudioInput id="human-studio-backdrop" type="checkbox" checked={draft.backdrop} disabled={busy} onChange={e=>setDraft({...draft,backdrop:e.target.checked})}/></StudioField>
  <div className="au-row"><StudioButton variant="primary" disabled={busy||!info} onClick={()=>void apply(draft)}>Применить свет</StudioButton><StudioButton disabled={busy||!info} onClick={()=>void apply(defaults)}>Сбросить свет</StudioButton></div>
  <StudioAlert>4 источника: основной, заполняющий, контровой и дополнительный. Настройки действуют в предпросмотре персонажа и не запекаются в его текстуры. Сохраняются до закрытия проекта.</StudioAlert>
 </StudioSection>;
}
