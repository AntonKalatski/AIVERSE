import { useEffect, useMemo, useState } from 'react';
import { GitBranch, RefreshCw, Search } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type { StudioShaderGraphDocument } from '../types';
import { layoutShaderGraph } from './shaderGraphLayout';
import './ShaderGraphPanel.css';

export default function ShaderGraphPanel({projectPath}:{projectPath:string}) {
 const [names,setNames]=useState<string[]>([]);
 const [selected,setSelected]=useState('');
 const [document,setDocument]=useState<StudioShaderGraphDocument|null>(null);
 const [search,setSearch]=useState('');
 const [error,setError]=useState('');
 const [busy,setBusy]=useState(false);
 const [nonce,setNonce]=useState(0);
 const connected=nativeConnected();
 useEffect(()=>onNativeEvent(event=>{if(event==='workspace.changed')setNonce(n=>n+1)}),[]);
 useEffect(()=>{
  let closed=false;
  setSelected('');setDocument(null);setError('');
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
  setDocument(null);
  if(!selected)return;
  setBusy(true);setError('');
  void invoke('studio.shaderGraphGet',{name:selected})
    .then(result=>{if(!closed)setDocument(result)})
    .catch(e=>{if(!closed)setError(String(e))})
    .finally(()=>{if(!closed)setBusy(false)});
  return ()=>{closed=true};
 },[selected]);
 const diagram=useMemo(()=>layoutShaderGraph(document?.graph??{}),[document]);
 return <section className="studio-shader-graphs">
  <header className="graph-heading"><span><GitBranch size={16}/> Shader Graphs</span>
   <button className="icon-button" aria-label="Reload Shader Graphs"
    disabled={busy} onClick={()=>setNonce(n=>n+1)}><RefreshCw size={15}/></button>
  </header>
  <p className="graph-readonly">Read-only graph inspection. Node editing, saving and shader application remain in the legacy editor.</p>
  {error&&<p className="graph-error" role="alert">{error}</p>}
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
      </div>
      <div className="graph-canvas-scroll">
       <svg width={diagram.width} height={diagram.height} viewBox={'0 0 '+diagram.width+' '+diagram.height}
        role="img" aria-label={'Read-only node diagram of '+selected}>
        {diagram.edges.map(edge=><path key={edge.key} d={'M '+edge.x1+' '+edge.y1+
          ' C '+(edge.x1+55)+' '+edge.y1+', '+(edge.x2-55)+' '+edge.y2+', '+edge.x2+' '+edge.y2}
          fill="none" stroke="#7aa3d2" strokeWidth="2"/>)}
        {diagram.nodes.map(node=><g key={node.id} transform={'translate('+node.x+' '+node.y+')'}>
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
