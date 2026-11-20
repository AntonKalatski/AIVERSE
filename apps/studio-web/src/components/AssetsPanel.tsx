import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  FileAudio, Box, FileCode2, FileImage, FileText, Folder,
  FolderOpen, Grid2X2, List, RefreshCw, Search, ChevronRight, Library
} from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type { StudioAsset, StudioAssetList } from '../types';
import { assetKind, assetFolders, assetFileName, assetBreadcrumbs,
  childFolders, visibleAssetFiles } from '../assetTree';
import type { AssetKind } from '../assetTree';
import './AssetsPanel.css';

function formatBytes(bytes: number): string {
  if(bytes<1024)return Math.max(0,bytes).toLocaleString()+' B';
  if(bytes<1024*1024)return (bytes/1024).toFixed(1)+' KB';
  return (bytes/(1024*1024)).toFixed(1)+' MB';
}
function AssetIcon({kind,size=27}:{kind:AssetKind;size?:number}) {
  switch(kind) {
    case 'image':return <FileImage size={size}/>;
    case 'model':return <Box size={size}/>;
    case 'audio':return <FileAudio size={size}/>;
    case 'code':return <FileCode2 size={size}/>;
    default:return <FileText size={size}/>;
  }
}

/** One project-local read-only asset browser. Real file list from native Workspace. */
export default function AssetsPanel({projectPath}:{projectPath:string}) {
  const [catalog,setCatalog]=useState<StudioAssetList>({files:[],root:'content'});
  const [folder,setFolder]=useState('');
  const [selected,setSelected]=useState<string|null>(null);
  const [search,setSearch]=useState('');
  const [mode,setMode]=useState<'grid'|'list'>('grid');
  const [error,setError]=useState('');
  const [loading,setLoading]=useState(true);
  const [refreshToken,setRefreshToken]=useState(0);
  const reload=useCallback(()=>setRefreshToken(value=>value+1),[]);
  useEffect(()=>onNativeEvent(event=>{
    if(event==='workspace.changed')reload();
  }),[reload]);
  useEffect(()=>{
    setFolder('');setSelected('');setSearch('');
  },[projectPath]);
  useEffect(()=>{
    let cancelled=false;
    setLoading(true);setError('');
    if(nativeConnected()&&!projectPath){
      setCatalog({files:[],root:'content'});setLoading(false);return;
    }
    void invoke('studio.assets',{})
      .then(result=>{if(!cancelled)setCatalog(result)})
      .catch(e=>{if(!cancelled)setError(e instanceof Error?e.message:String(e))})
      .finally(()=>{if(!cancelled)setLoading(false)});
    return ()=>{cancelled=true};
  },[projectPath,refreshToken]);
  const folders=useMemo(()=>assetFolders(catalog.files),[catalog.files]);
  const shownFolders=useMemo(()=>search.trim()?[]:childFolders(folders,folder),[folders,folder,search]);
  const shownFiles=useMemo(()=>visibleAssetFiles(catalog.files,folder,search),[catalog.files,folder,search]);
  const crumbs=useMemo(()=>assetBreadcrumbs(folder),[folder]);
  const selectedAsset=useMemo(()=>catalog.files.find(f=>f.path===selected),[catalog.files,selected]);

  return <section className="studio-assets" aria-label="Project Assets">
    <div className="studio-assets-toolbar">
      <div className="studio-assets-breadcrumb" aria-label="Asset location">
        {crumbs.map((part,index)=><span className="studio-assets-crumb" key={part.path}>
          {index>0 && <ChevronRight size={13} aria-hidden="true"/>}
          <button type="button" title={part.path ? 'content/'+part.path : 'content/ root'}
            aria-current={part.path===folder?'location':undefined}
            onClick={()=>{setFolder(part.path);setSearch('');setSelected(null)}}>
            {part.name}
          </button>
        </span>)}
      </div>
      <label className="studio-assets-search"><Search size={14}/>
        <input value={search} onChange={e=>setSearch(e.target.value)}
          aria-label="Search project assets" placeholder="Find assets…"/>
      </label>
      <div className="studio-assets-views" role="group" aria-label="Asset layout">
        <button type="button" aria-label="Grid view" title="Grid view"
          aria-pressed={mode==='grid'} onClick={()=>setMode('grid')}><Grid2X2 size={15}/></button>
        <button type="button" aria-label="List view" title="List view"
          aria-pressed={mode==='list'} onClick={()=>setMode('list')}><List size={15}/></button>
      </div>
      <button className="studio-assets-reload" onClick={reload} disabled={loading}
        type="button" aria-label="Reload project assets" title="Reload project assets">
        <RefreshCw size={15}/>
      </button>
    </div>
    <div className="studio-assets-layout">
      <nav className="studio-assets-folders" aria-label="Project asset folders">
        <div className="studio-assets-sidebar-title">CONTENT / PROJECT ASSETS</div>
        <button type="button" className={'studio-assets-folder'+(folder===''?' active':'')}
          onClick={()=>{setFolder('');setSearch('');setSelected(null)}}>
          <Library size={15}/><span>content</span>
          <small>{catalog.files.length}</small>
        </button>
        {folders.slice(0,300).map(entry=>
          <button type="button" key={entry.path} title={entry.path}
            className={'studio-assets-folder'+(folder===entry.path?' active':'')}
            style={{paddingLeft:12+entry.depth*13}}
            onClick={()=>{setFolder(entry.path);setSearch('');setSelected(null)}}>
            {folder===entry.path?<FolderOpen size={15}/>:<Folder size={15}/>}
            <span>{entry.name}</span><small>{entry.count}</small>
          </button>)}
      </nav>
      <div className="studio-assets-content">
        {error && <div className="studio-assets-state studio-assets-error" role="alert">{error}</div>}
        {loading ? <div className="studio-assets-state">Loading project assets…</div> :
          shownFiles.length===0 && shownFolders.length===0 ?
            <div className="studio-assets-empty">
              <Folder size={26}/>
              <strong>{projectPath ? (search || folder ? 'No matching assets' : 'content/ is empty') : 'Open a project'}</strong>
              <p>{projectPath ? (search || folder ?
                'Choose another folder or change the search.' :
                'Place your models, textures, audio and other resources in the project/content folder.') :
                'Assets from the project/content folder appear here after opening an AIVERSE project.'}</p>
            </div> :
            <div className={'studio-assets-items studio-assets-items--'+mode} role="list">
              {shownFolders.map(entry=>
                <button key={entry.path} type="button" className="studio-assets-item studio-assets-item--folder"
                  title={entry.path} role="listitem"
                  onClick={()=>{setFolder(entry.path);setSelected(null)}}>
                  <span className="studio-assets-icon"><Folder size={mode==='grid'?37:22}/></span>
                  <span className="studio-assets-name">{entry.name}</span>
                  <span className="studio-assets-meta">{entry.count} files</span>
                </button>)}
              {shownFiles.map((asset:StudioAsset)=>{
                const kind=assetKind(asset.path);
                return <button key={asset.path} type="button" role="listitem"
                  className={'studio-assets-item'+(selected===asset.path?' active':'')}
                  data-kind={kind} aria-selected={selected===asset.path}
                  title={'content/'+asset.path} onClick={()=>setSelected(asset.path)}>
                  <span className="studio-assets-icon"><AssetIcon kind={kind} size={mode==='grid'?32:20}/></span>
                  <span className="studio-assets-name">{assetFileName(asset.path)}</span>
                  <span className="studio-assets-meta">{formatBytes(asset.bytes)}</span>
                </button>;
              })}
            </div>}
      </div>
    </div>
    <footer className="studio-assets-footer">
      <span>{catalog.files.length} assets in content/ · read-only</span>
      <span className="studio-assets-current">{selectedAsset?'content/'+selectedAsset.path:
        search ? 'Search in content/' : folder?'content/'+folder:'content/'}</span>
    </footer>
  </section>;
}
