import { useCallback, useEffect, useMemo, useState } from 'react';
import { FileAudio, FileBox2, FileCode2, FileImage, FileText, RefreshCw, Search } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type { StudioAsset, StudioAssetList } from '../types';

function formatBytes(bytes: number): string {
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}
function AssetIcon({ path }: { path: string }) {
  const extension = path.split('.').pop()?.toLowerCase();
  if (['png', 'jpg', 'jpeg', 'webp', 'dds', 'ktx2', 'tga'].includes(extension ?? '')) return <FileImage size={15}/>;
  if (['glb', 'gltf', 'fbx', 'obj', 'mesh'].includes(extension ?? '')) return <FileBox2 size={15}/>;
  if (['wav', 'ogg', 'mp3', 'flac'].includes(extension ?? '')) return <FileAudio size={15}/>;
  if (['json', 'lua', 'ts', 'js', 'cpp', 'h', 'glsl', 'hlsl'].includes(extension ?? '')) return <FileCode2 size={15}/>;
  return <FileText size={15}/>;
}

/** Project-local, read-only asset browser. All file IO belongs to C++ Workspace. */
export default function AssetsPanel({ projectPath }: { projectPath: string }) {
  const [catalog, setCatalog] = useState<StudioAssetList>({ files: [] });
  const [search, setSearch] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(true);
  const [refreshToken, setRefreshToken] = useState(0);
  const reload = useCallback(() => setRefreshToken(value => value + 1), []);
  useEffect(() => onNativeEvent(event => {
    if (event === 'workspace.changed') reload();
  }), [reload]);
  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError('');
    if (nativeConnected() && !projectPath) {
      setCatalog({ files: [] });
      setLoading(false);
      return;
    }
    void invoke('studio.assets', {})
      .then(result => { if (!cancelled) setCatalog(result); })
      .catch(e => { if (!cancelled) setError(e instanceof Error ? e.message : String(e)); })
      .finally(() => { if (!cancelled) setLoading(false); });
    return () => { cancelled = true; };
  }, [projectPath, refreshToken]);
  const shown = useMemo(() =>
    [...catalog.files].filter(asset => asset.path.toLowerCase().includes(search.toLowerCase()))
      .sort((a, b) => a.path.localeCompare(b.path)), [catalog, search]);

  return <>
    <div className="assets-toolbar">
      <label className="search-box"><Search size={15}/>
        <input aria-label="Search project assets" placeholder="Find assets…" value={search}
          onChange={e => setSearch(e.target.value)}/></label>
      <button className="icon-button" onClick={reload} disabled={loading} aria-label="Reload project assets" title="Reload"><RefreshCw size={15}/></button>
    </div>
    {error && <div role="alert" className="assets-state assets-error">{error}</div>}
    {loading ? <div className="assets-state">Loading project assets…</div> :
      shown.length === 0 ? <div className="assets-state">{projectPath ? 'No matching assets' : 'Open a project to browse its assets'}</div> :
        <div className="tree-list assets-list" role="list">
          {shown.map((asset: StudioAsset) =>
            <div className="asset-row" key={asset.path} role="listitem" title={asset.path}>
              <AssetIcon path={asset.path}/>
              <span className="asset-path">{asset.path}</span>
              <span className="asset-size">{formatBytes(asset.bytes)}</span>
            </div>)}
        </div>}
    <div className="panel-footer">{catalog.files.length} project files · read-only</div>
  </>;
}
