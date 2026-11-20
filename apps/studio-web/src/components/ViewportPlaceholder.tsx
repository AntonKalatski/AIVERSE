import { useEffect, useRef, useState } from 'react';
import { Box } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';

/** Layout source of truth for a real native GPU surface. Never streams raster frames to JS. */
export default function ViewportPlaceholder({ title, active }: { title: string; active: boolean }) {
  const element = useRef<HTMLElement>(null);
  const drag = useRef<{ x: number; y: number; startX: number; startY: number; moved: boolean; mode: 'orbit' | 'pan'; button: number } | null>(null);
  const orbitDelta = useRef({ x: 0, y: 0, raf: 0 });
  const native = nativeConnected();
  const [gpuReady, setGpuReady] = useState(false);
  const [navigationActive, setNavigationActive] = useState(false);
  const [gpuError, setGpuError] = useState('');
  useEffect(() => {
    if (!active) { setGpuReady(false); setGpuError(''); }
  }, [active]);
  useEffect(() => {
    if (!native) return;
    return onNativeEvent((event, payload) => {
      if (event === 'viewport.ready') { setGpuReady(true); setGpuError(''); }
      if (event === 'viewport.error') {
        setGpuReady(false);
        setGpuError(typeof payload === 'object' && payload !== null && 'message' in payload
          ? String(payload.message) : 'Native renderer could not initialize');
      }
    });
  }, [native]);

  useEffect(() => {
    if (!native) return;
    const host = element.current;
    if (!host) return;
    let frame = 0;
    let closed = false;
    const update = () => {
      if (frame) cancelAnimationFrame(frame);
      frame = requestAnimationFrame(() => {
        frame = 0;
        if (closed) return;
        const bounds = host.getBoundingClientRect();
        void invoke('studio.viewportRect', {
          id: 'scene',
          x: bounds.left, y: bounds.top,
          width: bounds.width, height: bounds.height,
          dpr: window.devicePixelRatio || 1,
          visible: active && document.visibilityState === 'visible' &&
                   bounds.width > 16 && bounds.height > 16
        }).catch(error => console.warn('AIVERSE viewport layout:', error));
      });
    };
    const observer = new ResizeObserver(update);
    observer.observe(host);
    window.addEventListener('resize', update);
    window.addEventListener('scroll', update, true);
    document.addEventListener('visibilitychange', update);
    update();
    return () => {
      closed = true;
      if (frame) cancelAnimationFrame(frame);
      observer.disconnect();
      window.removeEventListener('resize', update);
      window.removeEventListener('scroll', update, true);
      document.removeEventListener('visibilitychange', update);
      // On tab switch/unmount, remove native surface even if no further resize event occurs.
      void invoke('studio.viewportRect', {
        id: 'scene', x: 0, y: 0, width: 0, height: 0, dpr: 1, visible: false
      }).catch(() => {});
    };
  }, [active, native]);

  // Free scene navigation is editor-only and never modifies gameplay camera state.
  useEffect(() => {
    if (!active || !native || !gpuReady) return;
    const down=(event:KeyboardEvent) => {
      const host=element.current;
      if (!host || !host.matches(':focus-within')) return;
      const target=event.target;
      if (target instanceof HTMLElement &&
          target.closest('input, textarea, select, [contenteditable="true"]')) return;
      const speed=event.shiftKey?3:1;
      const key=event.key.toLowerCase();
      if (key==='f') {
        event.preventDefault();
        void invoke('studio.viewportInput',{kind:'focus'}).catch(console.warn);
      } else if (key==='home') {
        event.preventDefault();
        void invoke('studio.viewportInput',{kind:'reset'}).catch(console.warn);
      } else if (['w','a','s','d','q','e'].includes(key)) {
        event.preventDefault();
        const right=(key==='d'?1:key==='a'?-1:0)*speed;
        const up=(key==='e'?1:key==='q'?-1:0)*speed;
        const forward=(key==='w'?1:key==='s'?-1:0)*speed;
        void invoke('studio.viewportInput',{kind:'move',right,up,forward}).catch(console.warn);
      }
    };
    window.addEventListener('keydown',down);
    return ()=>window.removeEventListener('keydown',down);
  },[active,native,gpuReady]);
  const composite = () => native && document.documentElement.classList.contains('native-composite');
  const sendOrbit = (dx: number, dy: number, mode: 'orbit' | 'pan' = 'orbit') => {
    const state = orbitDelta.current;
    state.x += dx;
    state.y += dy;
    if (state.raf) return;
    state.raf = requestAnimationFrame(() => {
      state.raf = 0;
      const x = state.x, y = state.y;
      state.x = state.y = 0;
      if (x !== 0 || y !== 0) void invoke('studio.viewportInput', { kind: mode, dx: x, dy: y }).catch(console.warn);
    });
  };
  return <section ref={element} className={'viewport' + (native && active && gpuReady ? ' viewport-native' : '')}
    aria-label={native && active ? 'Native Scene viewport' : 'Viewport placeholder'}
    tabIndex={0}
    onContextMenu={event=>event.preventDefault()}
    onPointerDown={event => {
      if (!composite() || !active || !gpuReady) return;
      event.currentTarget.focus();
      if (event.button!==0 && event.button!==1 && event.button!==2) return;
      drag.current = { x: event.clientX, y: event.clientY, startX: event.clientX, startY: event.clientY,
        moved: false, mode: event.button===1 || (event.button===0 && event.shiftKey)?'pan':'orbit',
        button:event.button };
      setNavigationActive(true);
      event.currentTarget.setPointerCapture(event.pointerId);
    }}
    onPointerMove={event => {
      const state=drag.current;
      if (!state || !composite() || !gpuReady) return;
      const dx=event.clientX-state.x,dy=event.clientY-state.y;
      state.x=event.clientX; state.y=event.clientY;
      if (Math.hypot(event.clientX-state.startX,event.clientY-state.startY)>5) state.moved=true;
      if (state.moved) sendOrbit(dx,dy,state.mode);
    }}
    onPointerUp={event => {
      const state=drag.current;
      drag.current=null;
      setNavigationActive(false);
      if (!state || !composite() || !gpuReady) return;
      if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId);
      if (!state.moved && state.button===0 && state.mode==='orbit') {
        const bounds=event.currentTarget.getBoundingClientRect();
        void invoke('studio.viewportInput',{kind:'pick', x:event.clientX-bounds.left,y:event.clientY-bounds.top,width:bounds.width,height:bounds.height}).catch(console.warn);
      }
    }}
    onPointerCancel={() => { drag.current=null; setNavigationActive(false); }}
    onWheel={event => {
      if (!composite() || !active || !gpuReady) return;
      void invoke('studio.viewportInput',{kind:'zoom',amount:-event.deltaY * 0.12}).catch(console.warn);
    }}>
    {native && gpuReady && <div className="studio-scene-nav-help" aria-hidden="true">
      {navigationActive?'Navigating Scene':'LMB orbit · MMB / Shift+LMB pan · Wheel zoom · WASD/QE move · F focus · Home reset'}
    </div>}
    <div className="viewport-grid"/>
    <div className="viewport-placeholder">
      <div className="viewport-cube"><Box size={44} strokeWidth={1.1}/></div>
      <h2>{title}</h2>
      <p>{native ? gpuError ? 'Native renderer error: ' + gpuError : 'Starting AIVERSE native GPU renderer…' :
        'The native AIVERSE renderer is not connected to the browser preview.'}</p>
      <span className="viewport-tag">Metal · Vulkan · GLES</span>
    </div>
    <div className="viewport-axis"><span className="axis-y">Y</span><span className="axis-x">X</span><span className="axis-z">Z</span></div>
  </section>;
}
