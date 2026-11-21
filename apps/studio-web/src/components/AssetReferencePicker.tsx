import { useEffect, useMemo, useState } from 'react';
import type { DragEvent } from 'react';
import { FolderSearch, Search, X } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import { assetFileName, assetKind, safeAssetPath } from '../assetTree';
import type { StudioAsset, StudioAssetList } from '../types';
import './AssetReferencePicker.css';

export const STUDIO_ASSET_DRAG_MIME = 'application/x-aiverse-content-path';

export interface AssetReferencePickerProps {
  value: string;
  onChange: (value: string) => void;
  disabled?: boolean;
  label: string;
  /** Only matching kinds are selectable; omit for all files. */
  kinds?: ReadonlyArray<ReturnType<typeof assetKind>>;
  allowClear?: boolean;
}

const emptyCatalog: StudioAssetList = { root: 'content', files: [], folders: [] };
const normalized = (path: string) =>
  safeAssetPath(path.startsWith('content/') ? path.slice('content/'.length) : path);

/** Asset reference editor shared by any inspector/panel, never a filesystem picker. */
export default function AssetReferencePicker({
  value, onChange, disabled=false, label, kinds, allowClear=true
}: AssetReferencePickerProps) {
  const [open, setOpen] = useState(false);
  const [search, setSearch] = useState('');
  const [catalog, setCatalog] = useState<StudioAssetList>(emptyCatalog);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const [version, setVersion] = useState(0);

  useEffect(() => onNativeEvent((event) => {
    if (event === 'workspace.changed' || event === 'document.changed') setVersion(n => n + 1);
  }), []);

  useEffect(() => {
    if (!open) return;
    let alive = true;
    setLoading(true);
    void invoke('studio.assets', {}).then(result => {
      if (alive) { setCatalog(result); setError(''); }
    }).catch(cause => {
      if (alive) setError(String(cause instanceof Error ? cause.message : cause));
    }).finally(() => { if (alive) setLoading(false); });
    return () => { alive = false; };
  }, [open, version]);

  useEffect(() => {
    if (!open) return;
    const keydown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') { event.preventDefault(); setOpen(false); }
    };
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  }, [open]);

  const permitted = (asset: StudioAsset) => (!kinds?.length || kinds.includes(assetKind(asset.path))) &&
    (!kinds?.includes('image') || kinds.length !== 1 || !asset.importer || asset.importer === 'texture');
  const files = useMemo(() => catalog.files.filter(asset => permitted(asset) &&
    (asset.path.toLocaleLowerCase().includes(search.toLocaleLowerCase()) ||
     asset.guid?.toLocaleLowerCase().includes(search.toLocaleLowerCase())))
    .sort((a,b) => a.path.localeCompare(b.path)).slice(0, 500),
  [catalog, kinds, search]);

  const choose = (asset: StudioAsset) => {
    if (disabled || !permitted(asset)) return;
    if (!asset.guid) { setError('Asset is missing a GUID. Refresh the project Assets catalog.'); return; }
    onChange(asset.guid);
    setOpen(false);
  };
  const drop = (event: DragEvent<HTMLElement>) => {
    if (disabled || !event.dataTransfer.types.includes(STUDIO_ASSET_DRAG_MIME)) return;
    const path = normalized(event.dataTransfer.getData(STUDIO_ASSET_DRAG_MIME));
    event.preventDefault();
    event.stopPropagation();
    if (!path) return;
    // Recheck against authoritative catalog on every drop: stale React state
    // must not assign deleted files or accidentally replace GUIDs with paths.
    void invoke('studio.assets', {}).then(result => {
      setCatalog(result);
      const resolved = result.files.find(item => item.path === path);
      if (resolved && permitted(resolved) && resolved.guid) {
        onChange(resolved.guid);
        setOpen(false);
      } else setError('Dropped asset is not present in project/content or has an incompatible type.');
    }).catch(cause => setError(String(cause instanceof Error ? cause.message : cause)));
  };
  const dragOver = (event: DragEvent<HTMLElement>) => {
    if (!disabled && event.dataTransfer.types.includes(STUDIO_ASSET_DRAG_MIME)) {
      event.preventDefault();
      event.stopPropagation();
      event.dataTransfer.dropEffect = 'copy';
    }
  };

  return <div className="studio-asset-reference" onDragOver={dragOver} onDrop={drop}>
    <div className="studio-asset-reference__control">
      <input className="au-input" value={value} readOnly aria-label={label}
        placeholder="None (drop an asset)" title={value} disabled={disabled}/>
      <button type="button" disabled={disabled} title="Choose from Assets" aria-label={'Choose '+label}
        onClick={() => { setSearch(''); setOpen(true); }}><FolderSearch size={16}/></button>
      {allowClear && value && <button type="button" disabled={disabled} onClick={() => onChange('')}
        aria-label={'Clear '+label}><X size={14}/></button>}
    </div>
    {open && <div className="studio-asset-reference__overlay" onMouseDown={event => {
      if (event.target === event.currentTarget) setOpen(false);
    }}>
      <section role="dialog" aria-modal="true" aria-label={'Choose '+label}
        className="studio-asset-reference__dialog">
        <header><strong>Choose {label}</strong>
          <button type="button" aria-label="Close asset chooser" onClick={() => setOpen(false)}><X size={18}/></button>
        </header>
        <label className="studio-asset-reference__search"><Search size={16}/>
          <input autoFocus value={search} onChange={event => setSearch(event.target.value)}
            placeholder="Search name, path or GUID…" aria-label="Search assets"/>
        </label>
        <div className="studio-asset-reference__results" role="listbox" aria-label="Available assets">
          {loading && <p>Loading assets…</p>}
          {error && <p role="alert">{error}</p>}
          {!loading && !error && !files.length && <p>No matching assets.</p>}
          {!loading && files.map(asset => <button type="button" key={asset.path}
            role="option" aria-selected={value === asset.guid || value === asset.path || value === 'content/' + asset.path}
            onClick={() => choose(asset)}>
            <span>{assetFileName(asset.path)}</span>
            <small>{asset.path}{asset.guid ? ' · '+asset.guid : ''}</small>
          </button>)}
        </div>
        <footer>Only assets under project/content are selectable. Drag from Assets to assign.</footer>
      </section>
    </div>}
  </div>;
}
