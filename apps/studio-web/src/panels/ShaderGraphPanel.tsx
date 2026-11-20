import { useEffect, useMemo, useRef, useState } from 'react';
import { GitBranch, RefreshCw, Save, Search, RotateCcw, CheckCircle2 } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type { StudioShaderGraphDocument } from '../types';
import { layoutShaderGraph, moveShaderGraphNode } from './shaderGraphLayout';
import './ShaderGraphPanel.css';

export default function ShaderGraphPanel({projectPath}:{projectPath:string}) {
 const [names,setNames]=useState<string[]>([]);
 const [selected,setSelected]=useState('');
 const [document,setDocument]=useState<StudioShaderGraphDocument|null>(null);
 const [draft,setDraft]=useState<StudioShaderGraphDocument['graph']|null>(null);
 const drag=useRef<{id:string; x:number; y:number; originalX:number; originalY:number}|null>(null);
 const [message,setMessage]=useState('');
 const [search,setSearch]=useState('');
 const [error,setError]=useState('');
 const [busy,setBusy]=useState(false);
 const [nonce,setNonce]=useState(0);
 const connected=nativeConnected();
 useEffect(()=>onNativeEvent(event=>{if(event==='workspace.changed')setNonce(n=>n+1)}),[]);
 useEffect(()=>{
  let closed=false;
  setSelected('');setDocument(null);setDraft(null);setError('');setMessage('');
  if(connected&&!projectPath){setNames([]);return;}
  setBusy(true);
  void invoke('studio.shaderGraphs',{})
    .then(result=>{if(!closed)setNames(result.graphs)})
    .catch(e=>{if(!closed)setError(String(e))})
    .finally(()=>{if(!closed)setBusy(false)});
  return ()=>{closed=true};
 },[connected,projectPath,nonce]);
 useEffect(()=>{
  let closed=false;
  setDocument(null);setDraft(null);setMessage('');
  if(!selected)return;
  setBusy(true);setError('');
  void invoke('studio.shaderGraphGet',{name:selected})
    .then(result=>{if(!closed){setDocument(result);setDraft(structuredClone(result.graph));}})
    .catch(e=>{if(!closed)setError(String(e))})
    .finally(()=>{if(!closed)setBusy(false)});
  return ()=>{closed=true};
 },[selected]);
 const diagram=useMemo(()=>layoutShaderGraph(draft??document?.graph??{}),[draft,document]);
 const dirty=!!document&&!!draft&&JSON.stringify(draft)!==JSON.stringify(document.graph);
 const validate=async()=>{
  if(!draft||busy)return;
  setBusy(true);setError('');setMessage('');
  try{
   const result=await invoke('studio.shaderGraphValidate',{graph:draft});
   setMessage('Compiled: '+result.nodes+' nodes, '+result.samples+' samples. No file written.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const save=async()=>{
  if(!draft||!selected||busy||!dirty)return;
  setBusy(true);setError('');setMessage('');
  try{
   const result=await invoke('studio.shaderGraphSave',{name:selected,graph:draft});
   setDocument(result);setDraft(structuredClone(result.graph));
   setMessage('Graph saved to shaders/'+selected+'.json. MapDoc Undo does not apply.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const discard=()=>{if(document){setDraft(structuredClone(document.graph));setMessage('Draft discarded.');setError('');}};
 return <section className="studio-shader-graphs">
  <header className="graph-heading"><span><GitBranch size={16}/> Shader Graphs</span>
   <button className="icon-button" aria-label="Reload Shader Graphs"
    disabled={busy} onClick={()=>setNonce(n=>n+1)}><RefreshCw size={15}/></button>
  </header>
  <p className="graph-readonly">Drag nodes to arrange the graph. Validate before explicit Save.
    Wiring, shaders and GPU graph preview still require the legacy editor.</p>
  {error&&<p className="graph-error" role="alert">{error}</p>}
  {message&&<p className="graph-message" role="status">{message}</p>}
  <div className="graph-columns">
   <aside className="graph-library">
    <label className="graph-search"><Search size={14}/>
     <input value={search} aria-label="Search Shader Graphs"
      onChange={e=>setSearch(e.target.value)} placeholder="Graph name…"/>
    </label>
    {names.filter(x=>x.toLowerCase().includes(search.toLowerCase())).map(name=>
     <button key={name} className={selected===name?'active':''} onClick={()=>setSelected(name)}>{name}</button>)}
    {!busy&&names.length===0&&<p>No project graphs found.</p>}
   </aside>
   <main className="graph-inspector">
    {!selected ? <div className="graph-empty">Select a graph to inspect its nodes and connections.</div> :
     busy ? <div className="graph-empty">Compiling and loading graph…</div> :
     document ? <>
      <div className="graph-summary"><strong>{selected}</strong><span>{document.graph.domain??'Unknown domain'}</span>
       <span>{document.nodes} nodes · {document.samples} texture samples</span>
       <span>{dirty?'● Unsaved node positions':'Saved'}</span>
       <button disabled={!draft||busy} onClick={()=>void validate()}><CheckCircle2 size={13}/> Validate</button>
       <button disabled={!dirty||busy} onClick={discard}><RotateCcw size={13}/> Discard</button>
       <button disabled={!dirty||busy} onClick={()=>void save()}><Save size={13}/> Save positions</button>
      </div>
      <div className="graph-canvas-scroll">
       <svg width={diagram.width} height={diagram.height} viewBox={'0 0 '+diagram.width+' '+diagram.height}
        role="group" aria-label={'Editable node layout for '+selected}
        onPointerMove={event=>{
          if(!drag.current||!draft)return;
          const current=drag.current;
          const x=current.originalX+event.clientX-current.x;
          const y=current.originalY+event.clientY-current.y;
          setDraft(previous=>previous?moveShaderGraphNode(previous,current.id,x,y):previous);
        }}
        onPointerUp={()=>{drag.current=null;}}
        onPointerCancel={()=>{drag.current=null;}}>
        {diagram.edges.map(edge=><path key={edge.key} d={'M '+edge.x1+' '+edge.y1+
          ' C '+(edge.x1+55)+' '+edge.y1+', '+(edge.x2-55)+' '+edge.y2+', '+edge.x2+' '+edge.y2}
          fill="none" stroke="#7aa3d2" strokeWidth="2"/>)}
        {diagram.nodes.map(node=><g key={node.id} className="graph-draggable" transform={'translate('+node.x+' '+node.y+')'}
          onPointerDown={event=>{
            if(!draft||busy)return;
            event.preventDefault();
            drag.current={id:node.id,x:event.clientX,y:event.clientY,originalX:node.x,originalY:node.y};
            event.currentTarget.setPointerCapture(event.pointerId);
          }}
          onPointerUp={event=>{
            drag.current=null;
            if(event.currentTarget.hasPointerCapture(event.pointerId))
              event.currentTarget.releasePointerCapture(event.pointerId);
          }}>
         <rect width="130" height="54" rx="7" fill="#233751" stroke="#6581a8"/>
         <text x="10" y="20" fill="#eef5ff" fontSize="11">{node.id}</text>
         <text x="10" y="39" fill="#a6bad5" fontSize="10">{node.op}</text>
        </g>)}
       </svg>
      </div>
      <div className="graph-outputs">Outputs: {Object.entries(document.graph.outputs??{}).map(
       ([k,v])=>k+' ← '+v).join(' · ')||'none'}</div>
     </> : <div className="graph-empty">No graph loaded.</div>}
   </main>
  </div>
  <footer className="panel-footer">Existing project assets only · generated GLSL/MSL is not exposed through this UI</footer>
 </section>;
}
