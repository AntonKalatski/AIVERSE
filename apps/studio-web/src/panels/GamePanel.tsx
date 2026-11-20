import { useEffect, useRef, useState } from 'react';
import { Play, Square, Pause, StepForward, RotateCcw } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioGameState } from '../types';
import type { StudioPanelContext } from './StudioPanelRenderer';
import { StudioPanel, StudioButton, StudioAlert, StudioEmptyState, StudioBadge } from '../ui';
import './GamePanel.css';

export default function GamePanel({snapshot,connected,busy,setBusy}:StudioPanelContext) {
  const [state,setState]=useState<StudioGameState|null>(null);
  const [error,setError]=useState('');
  const surface=useRef<HTMLDivElement>(null);
  const loaded=connected && !!snapshot?.project.path;
  useEffect(()=>{
    let live=true,inFlight=false;
    const refresh=async()=>{
      if(!connected || inFlight)return;
      inFlight=true;
      try {const value=await invoke('studio.gameState',{});if(live)setState(value);}
      catch(e){if(live)setError(String(e));}
      finally{inFlight=false;}
    };
    void refresh();const timer=window.setInterval(()=>void refresh(),300);
    return ()=>{live=false;window.clearInterval(timer);};
  },[connected]);
  useEffect(()=>{
    if(!connected || !surface.current)return;
    const host=surface.current;let frame=0;
    const update=()=>{
      cancelAnimationFrame(frame);
      frame=requestAnimationFrame(()=>{
        const b=host.getBoundingClientRect();
        void invoke('studio.viewportRect',{id:'game',x:b.left,y:b.top,width:b.width,height:b.height,
          dpr:window.devicePixelRatio||1,visible:!!state?.active && document.visibilityState==='visible' && b.width>16 && b.height>16}).catch(e=>setError(String(e)));
      });
    };
    const observer=new ResizeObserver(update);observer.observe(host);
    window.addEventListener('resize',update);window.addEventListener('scroll',update,true);
    document.addEventListener('visibilitychange',update);update();
    return ()=>{
      cancelAnimationFrame(frame);observer.disconnect();
      window.removeEventListener('resize',update);window.removeEventListener('scroll',update,true);
      document.removeEventListener('visibilitychange',update);
      void invoke('studio.viewportRect',{id:'game',x:0,y:0,width:0,height:0,dpr:1,visible:false}).catch(()=>{});
    };
  },[connected,state?.active]);
  const act=async(entry:'map'|'menu'|'stop')=>{
    if(busy)return;setBusy(true);setError('');
    try {setState(entry==='stop'?await invoke('studio.gameStop',{}):await invoke('studio.gameStart',{entry}));}
    catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  };
  const control=async(action:'pause'|'resume'|'step'|'restart')=>{
    if(busy)return;setBusy(true);setError('');
    try{setState(await invoke('studio.gameControl',{action}));}
    catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  };
  return <StudioPanel title="Game" className="studio-game" flush
    actions={<>
      <StudioButton disabled={!loaded||busy||!!state?.active} onClick={()=>void act('map')}><Play size={14}/> Play map</StudioButton>
      <StudioButton disabled={!loaded||busy||!!state?.active} onClick={()=>void act('menu')}>Project menu</StudioButton>
      <StudioButton disabled={!state?.canControl||busy} onClick={()=>void control(state?.paused?'resume':'pause')}><Pause size={14}/>{state?.paused?'Resume':'Pause'}</StudioButton>
      <StudioButton disabled={!state?.canStep||busy} onClick={()=>void control('step')}><StepForward size={14}/>Step</StudioButton>
      <StudioButton disabled={!state?.active||busy} onClick={()=>void control('restart')}><RotateCcw size={14}/>Restart</StudioButton>
      <StudioButton disabled={!state?.active||busy} onClick={()=>void act('stop')}><Square size={14}/> Stop</StudioButton>
    </>}
    footer={<><StudioBadge>{state?.active?(state.loading?`Loading ${Math.round(state.progress*100)}%`:state.inMatch?`${state.paused?'Paused':'Running'} · tick ${state.tick}`:'Project menu'):'Stopped'}</StudioBadge><span>Native game client · click the viewport to focus input</span></>}>
    {(error||state?.error) && <StudioAlert tone="danger">{error||state?.error}</StudioAlert>}
    <div ref={surface} className="studio-game__surface" aria-label="Native Game viewport">
      {!state?.active && <StudioEmptyState title={loaded?'Ready to play':'Open a project'}
        description={loaded?'Run the saved map or open the project’s own menus. Stop returns to authoring without copying runtime changes into the map.':'Play uses the native game client and renderer.'}/>}
    </div>
  </StudioPanel>;
}
