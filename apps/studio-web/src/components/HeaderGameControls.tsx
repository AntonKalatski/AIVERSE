import { useEffect, useState } from 'react';
import { Play, Pause, Square } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioGameState } from '../types';
export default function HeaderGameControls({enabled,onPlay,onError}:{enabled:boolean;onPlay:()=>void;onError:(message:string)=>void}) {
  const [state,setState]=useState<StudioGameState|null>(null);
  const [pending,setPending]=useState(false);
  useEffect(()=>{
    if(!enabled)return;
    let live=true,inFlight=false;
    const refresh=async()=>{
      if(inFlight)return;inFlight=true;
      try {const next=await invoke('studio.gameState',{});if(live)setState(next);}
      catch {} finally {inFlight=false;}
    };
    void refresh();const timer=window.setInterval(refresh,500);
    return ()=>{live=false;window.clearInterval(timer);};
  },[enabled]);
  const action=async(kind:'play'|'pause'|'stop')=>{
    setPending(true);
    try {
      if(kind==='play') {onPlay();setState(await invoke('studio.gameStart',{entry:'map'}));}
      else if(kind==='stop')setState(await invoke('studio.gameStop',{}));
      else setState(await invoke('studio.gameControl',{action:state?.paused?'resume':'pause'}));
    }catch(e){onError(e instanceof Error?e.message:String(e));}
    finally{setPending(false);}
  };
  return <div className="header-game-controls">
    <button className="icon-button" aria-label="Play" title="Play saved map" disabled={!enabled||pending||state?.active}
      onClick={()=>void action('play')}><Play size={17}/></button>
    <button className="icon-button" aria-label={state?.paused?'Resume':'Pause'} aria-pressed={!!state?.paused}
      disabled={!state?.canControl||pending} onClick={()=>void action('pause')}><Pause size={17}/></button>
    <button className="icon-button" aria-label="Stop" disabled={!state?.active||pending}
      onClick={()=>void action('stop')}><Square size={15}/></button>
  </div>;
}
