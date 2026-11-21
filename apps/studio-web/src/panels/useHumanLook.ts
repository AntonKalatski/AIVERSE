// Shared state + native calls for editing an imported character's look
// (VIVID Appearance window and the Human Creator "model" source).
import { useCallback, useEffect, useRef, useState } from 'react';
import type { HumanFaceShape, HumanLook, HumanLookInfo } from '../types';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';

export const emptyLook = (): HumanLook => ({ version: 1, values: {}, materials: {} });

export function useHumanLook(project: string) {
  const [info, setInfo] = useState<HumanLookInfo | null>(null);
  const [face, setFace] = useState<HumanFaceShape | null>(null);
  const [draft, setDraft] = useState<HumanLook>(emptyLook);
  const [guid, setGuid] = useState('');
  const [saved, setSaved] = useState<string[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [status, setStatus] = useState('');
  const [undo, setUndo] = useState<HumanLook[]>([]);
  const [redo, setRedo] = useState<HumanLook[]>([]);
  const generation = useRef(0);

  useEffect(() => {
    const gen = ++generation.current;
    setInfo(null); setFace(null); setGuid(''); setDraft(emptyLook()); setUndo([]); setRedo([]);
    if (!project || !nativeConnected()) return;
    setBusy(true);
    void Promise.all([invoke('studio.humanAppearanceInfo', {}), invoke('studio.humanAppearanceList', {})])
      .then(([i, list]) => { if (gen !== generation.current) return; setInfo(i); setFace(i.face); setDraft(i.look); setGuid(i.preview.guid === 'vivid-creator' ? '' : i.preview.guid); setSaved(list.looks); setError(''); })
      .catch(e => { if (gen === generation.current) setError(String(e)); })
      .finally(() => { if (gen === generation.current) setBusy(false); });
    return () => { ++generation.current; };
  }, [project]);

  const edit = useCallback((next: HumanLook) => {
    setDraft(old => { setUndo(u => [...u.slice(-63), old]); return next; });
    setRedo([]); setStatus('Есть неприменённые изменения');
  }, []);
  const undoLook = () => { if (!undo.length) return; setRedo(r => [...r, draft]); setDraft(undo[undo.length - 1]); setUndo(undo.slice(0, -1)); };
  const redoLook = () => { if (!redo.length) return; setUndo(u => [...u, draft]); setDraft(redo[redo.length - 1]); setRedo(redo.slice(0, -1)); };

  const run = useCallback(async (action: () => Promise<void>) => {
    setBusy(true); setError('');
    try { await action(); } catch (e) { setError(String(e)); } finally { setBusy(false); }
  }, []);
  const loadAsset = () => run(async () => {
    const result = await invoke('studio.humanAppearanceFromAsset', { guid });
    edit(result.look); setStatus('Профиль загружен — нажмите «Показать»');
  });
  const apply = (nextFace: HumanFaceShape | null = face, look: HumanLook = draft) => run(async () => {
    const current = await invoke('studio.humanAppearanceInfo', {});
    const result = await invoke('studio.humanAppearanceApply', { guid, revision: current.preview.revision, look, face: nextFace });
    setInfo(result); setDraft(result.look); setStatus('Применено');
  });
  const refresh = useCallback(async () => {
    const next = await invoke('studio.humanAppearanceInfo', {});
    setInfo(next); setFace(next.face);
    return next;
  }, []);

  // Native preview status polling while a model is bound.
  useEffect(() => {
    if (!info?.preview.bound || !project || !nativeConnected()) return;
    const gen = generation.current;
    const timer = setInterval(() => {
      void invoke('studio.humanAppearanceInfo', {}).then(next => {
        if (gen !== generation.current) return;
        setInfo(old => old?.preview.revision === next.preview.revision ? next : old);
      }).catch(() => {});
    }, 1500);
    return () => clearInterval(timer);
  }, [project, info?.preview.revision, info?.preview.bound]);

  // Viewport sculpt strokes change the native face shape: pull it back.
  useEffect(() => onNativeEvent(event => {
    if (event === 'human.sculpt') void refresh().catch(() => {});
  }), [refresh]);

  return { info, setInfo, face, setFace, draft, setDraft, edit, guid, setGuid, saved, setSaved, busy, error, setError,
    status, setStatus, undo, redo, undoLook, redoLook, run, loadAsset, apply, refresh };
}
export type HumanLookState = ReturnType<typeof useHumanLook>;
