// Real native GPU facial preview controls. There is NO browser canvas or
// synthetic avatar: WKWebView sends typed controls to StudioSession, and
// StudioMetalScene renders the actual imported Model via MotionWeave.
import { useEffect, useMemo, useState } from 'react';
import { Eye, Square } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import { StudioAlert, StudioBadge, StudioButton, StudioField,
  StudioInput, StudioSection } from '../ui';
import type { FacialRigDocument } from './facialRigSchema';

interface Props {
  projectPath: string;
  rigName: string;
  saved: FacialRigDocument | null;
  dirty: boolean;
  onActiveChange?: (active: boolean) => void;
}
const errorMessage=(e:unknown)=>e instanceof Error?e.message:String(e);

export default function FacialLivePreview({
  projectPath,rigName,saved,dirty,onActiveChange
}:Props) {
  const connected=nativeConnected();
  const [model,setModel]=useState('');
  const [active,setActive]=useState(false);
  const [ready,setReady]=useState(false);
  const [pending,setPending]=useState(false);
  const [error,setError]=useState('');
  const [values,setValues]=useState<Record<string,number>>({});
  const [targets,setTargets]=useState<Record<string,[number,number,number]>>({});
  const parameters=useMemo(
    ()=>[...new Set(saved?.morphs.map(m=>m.parameter)??[])],
    [saved]);
  const vectorDefaults=useMemo(()=>{
    const defaults:Record<string,[number,number,number]>={};
    for(const unit of saved?.rig??[]) {
      if(unit.target)defaults[unit.target]=[0,1,3];
      if(unit.pole)defaults[unit.pole]=[0,0,1];
    }
    return defaults;
  },[saved]);
  const vectors=useMemo(()=>Object.keys(vectorDefaults),[vectorDefaults]);
  useEffect(()=>{
    let mounted=true;
    if(!connected || !projectPath) {
      setActive(false);setReady(false);onActiveChange?.(false);
      return;
    }
    void invoke('studio.facialPreviewState',{}).then(snapshot=>{
      if(!mounted)return;
      setActive(snapshot.active);
      setReady(snapshot.gpuReady);
      if(snapshot.model)setModel(snapshot.model);
      onActiveChange?.(snapshot.active);
    }).catch(e=>{if(mounted)setError(errorMessage(e));});
    return ()=>{mounted=false;};
  },[projectPath,connected,onActiveChange]);
  useEffect(()=>onNativeEvent((event,payload)=>{
    if(event==='facial.preview.ready') {
      setActive(true);setReady(true);setError('');onActiveChange?.(true);
    } else if(event==='facial.preview.error') {
      const note=typeof payload==='object'&&payload!==null&&'message' in payload
        ?String(payload.message):'Native facial GPU rendering failed';
      setActive(false);setReady(false);setError(note);onActiveChange?.(false);
    } else if(event==='facial.preview.stopped') {
      setActive(false);setReady(false);onActiveChange?.(false);
    } else if(event==='facial.preview.requested')setReady(false);
  }),[onActiveChange]);

  const start=async()=>{
    if(!connected||!projectPath||!saved||!rigName||dirty||pending||!model.trim())return;
    setPending(true);setError('');setReady(false);
    try {
      const result=await invoke('studio.facialPreviewStart',
                                {name:rigName,model:model.trim()});
      setActive(result.active);setValues({});setTargets({});
      onActiveChange?.(result.active);
    }catch(e){setError(errorMessage(e));setActive(false);onActiveChange?.(false);}
    finally{setPending(false);}
  };
  const stop=async()=>{
    if(!active||pending)return;
    setPending(true);
    try {
      await invoke('studio.facialPreviewStop',{});
      setActive(false);setReady(false);onActiveChange?.(false);
    }catch(e){setError(errorMessage(e));}
    finally{setPending(false);}
  };
  const setScalar=(name:string,value:number)=>{
    if(!active||!Number.isFinite(value))return;
    setValues(current=>({...current,[name]:value}));
    void invoke('studio.facialPreviewScalar',{parameter:name,value})
      .catch(e=>setError(errorMessage(e)));
  };
  const setVector=(name:string,axis:number,value:number)=>{
    if(!active||!Number.isFinite(value))return;
    const old=targets[name]??vectorDefaults[name]??[0,1,3];
    const next:[number,number,number]=[old[0],old[1],old[2]];
    next[axis]=value;
    setTargets(current=>({...current,[name]:next}));
    void invoke('studio.facialPreviewVector',{
      parameter:name,x:next[0],y:next[1],z:next[2]
    }).catch(e=>setError(errorMessage(e)));
  };

  return <StudioSection title="Live GPU model preview"
    actions={<StudioBadge tone={ready?'success':active?'warning':'neutral'}>
      {ready?'Real GPU frame':active?'Preparing GPU':'Stopped'}
    </StudioBadge>}>
    <div className="au-stack facial-live">
      <StudioField label="Model in current project" htmlFor="facial-preview-model"
        hint="Real GLB/glTF/FBX relative path, e.g. models/hero.glb">
        <StudioInput id="facial-preview-model" value={model}
          maxLength={512} disabled={active||pending}
          placeholder="models/hero.glb" onChange={e=>setModel(e.target.value)}/>
      </StudioField>
      <div className="au-row facial-actions">
        {active?
          <StudioButton size="sm" loading={pending}
            onClick={()=>void stop()}><Square size={14}/> Stop preview</StudioButton>:
          <StudioButton size="sm" variant="primary" loading={pending}
            disabled={!connected||!projectPath||!saved||dirty||!model.trim()}
            onClick={()=>void start()}><Eye size={14}/> Render real model</StudioButton>}
      </div>
      {dirty&&<StudioAlert tone="warning">
        Save this facial rig before loading it into the GPU preview.
      </StudioAlert>}
      {!connected&&<StudioAlert tone="warning">
        Real facial rendering is available only in native macOS Studio.
        Browser demo does not fabricate model frames.
      </StudioAlert>}
      {error&&<StudioAlert tone="danger" title="Native preview error">{error}</StudioAlert>}
      {active&&<StudioAlert title="Live MotionWeave controls">
        Changes below drive the actual imported model pose, not authoring gain.
        A missing morph or IK bone fails during native compilation.
      </StudioAlert>}
      {active&&<div className="facial-live-channels">
        {parameters.map(parameter=><StudioField key={parameter}
          label={parameter} htmlFor={'facial-live-'+parameter}>
          <StudioInput id={'facial-live-'+parameter} type="range"
            min={0} max={1} step={0.01} value={values[parameter]??0}
            aria-label={'Live expression '+parameter}
            onChange={e=>setScalar(parameter,Number(e.target.value))}/>
        </StudioField>)}
      </div>}
      {active&&vectors.length>0&&<div className="facial-live-vectors">
        {vectors.map(name=><div className="au-card" key={name}>
          <strong>IK target: {name}</strong>
          <div className="au-row facial-new">
            {(['X','Y','Z'] as const).map((axis,index)=>
              <StudioField label={axis} key={axis}
                htmlFor={'facial-live-ik-'+name+'-'+axis}>
                <StudioInput type="number" step={0.1} min={-100} max={100}
                  id={'facial-live-ik-'+name+'-'+axis}
                  value={(targets[name]??vectorDefaults[name]??[0,1,3])[index]}
                  onChange={e=>{
                    const value=Number(e.target.value);
                    if(e.target.value!==''&&Number.isFinite(value))
                      setVector(name,index,value);
                  }}/>
              </StudioField>)}
          </div>
        </div>)}
      </div>}
    </div>
  </StudioSection>;
}
