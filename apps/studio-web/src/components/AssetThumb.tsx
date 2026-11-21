import { useEffect, useRef, useState, type ReactNode } from 'react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';

/** Thumbnails of content images and models (native: AppKit scaling / offscreen Scene render, disk cache). */
type Entry = { url?: string; state: 'loading' | 'pending' | 'ready' | 'none' };
const cache = new Map<string, Entry>();
const waiters = new Map<string, Set<() => void>>();
function notify(path: string) { waiters.get(path)?.forEach(fn => fn()); }
function request(path: string) {
  cache.set(path, { state: 'loading' });
  void invoke('studio.assetThumbnail', { path }).then(r => {
    cache.set(path, r.url ? { url: r.url, state: 'ready' } : { state: r.pending ? 'pending' : 'none' });
    notify(path);
  }).catch(() => { cache.set(path, { state: 'none' }); notify(path); });
}
let listening = false;
function listen() {
  if (listening) return;
  listening = true;
  onNativeEvent((event, payload) => {
    if (event !== 'asset.thumbnail' || !payload || typeof payload !== 'object') return;
    const path = String((payload as { path?: unknown }).path ?? '');
    if (cache.get(path)?.state === 'pending') request(path);  // rendered: read it from the cache
  });
}
/** Drop cached thumbnails (files changed on disk). */
export function forgetThumbnails() { cache.clear(); }

export default function AssetThumb({ path, fallback }: { path: string; fallback: ReactNode }) {
  const [, tick] = useState(0);
  const box = useRef<HTMLSpanElement>(null);
  const [visible, setVisible] = useState(false);
  useEffect(() => {
    const el = box.current;
    if (!el || typeof IntersectionObserver === 'undefined') { setVisible(true); return; }
    const io = new IntersectionObserver(entries => { if (entries.some(e => e.isIntersecting)) { setVisible(true); io.disconnect(); } });
    io.observe(el);
    return () => io.disconnect();
  }, []);
  useEffect(() => {
    if (!visible || !nativeConnected()) return;
    listen();
    const set = waiters.get(path) ?? new Set();
    const fn = () => tick(x => x + 1);
    set.add(fn); waiters.set(path, set);
    if (!cache.has(path)) request(path);
    return () => { set.delete(fn); };
  }, [visible, path]);
  const entry = cache.get(path);
  return <span ref={box} className="studio-assets-thumb">
    {entry?.url ? <img src={entry.url} alt="" draggable={false}/> : fallback}
  </span>;
}
