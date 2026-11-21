import { mockInvoke } from './mock';
import type { StudioMethod, StudioParams, StudioResult } from './types';

interface NativeRequest { id: number; method: string; params: unknown }
interface NativeReply { id?: number; result?: unknown; error?: string; event?: string; payload?: unknown }
type NativeHandler = { postMessage: (message: NativeRequest) => void };
declare global {
  interface Window {
    webkit?: { messageHandlers?: { aiverse?: NativeHandler } };
    __aiverseReceive?: (message: NativeReply) => void;
  }
}

const pending = new Map<number, {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
  timer: ReturnType<typeof setTimeout> | null;
}>();
const listeners = new Set<(event: string, payload: unknown) => void>();
let nextId = 1;

window.__aiverseReceive = (message: NativeReply) => {
  if (message && typeof message.event === 'string') {
    listeners.forEach(listener => listener(message.event!, message.payload));
    return;
  }
  if (!message || typeof message.id !== 'number') return;
  const call = pending.get(message.id);
  if (!call) return;
  if (call.timer !== null) clearTimeout(call.timer);
  pending.delete(message.id);
  if (typeof message.error === 'string') call.reject(new Error(message.error));
  else call.resolve(message.result);
};

export const nativeConnected = (): boolean => !!window.webkit?.messageHandlers?.aiverse;
export function onNativeEvent(listener: (event: string, payload: unknown) => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}
export async function invoke<M extends StudioMethod>(method: M, params: StudioParams[M]): Promise<StudioResult[M]> {
  const native = window.webkit?.messageHandlers?.aiverse;
  if (!native) return mockInvoke(method, params);
  return new Promise<StudioResult[M]>((resolve, reject) => {
    const id = nextId++;
    // Interactive native sheets wait for a person, including dirty-document prompts.
    const interactive = ['studio.openProject','studio.openMap','studio.newMap','studio.saveAs','studio.save','studio.assetImportPreview'].includes(method);
    const timer = interactive ? null : setTimeout(() => {
      pending.delete(id);
      reject(new Error('Native bridge timeout: ' + method));
    }, 8000);
    pending.set(id, { resolve: resolve as (value: unknown) => void, reject, timer });
    try { native.postMessage({ id, method, params }); }
    catch (error) { if (timer !== null) clearTimeout(timer); pending.delete(id); reject(error); }
  });
}
