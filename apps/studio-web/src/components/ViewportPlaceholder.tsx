import { useEffect, useRef, useState } from 'react';
import { Box } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';

/** Layout source of truth for a real native GPU surface. Never streams raster frames to JS. */
export default function ViewportPlaceholder({ title, active }: { title: string; active: boolean }) {
  const element = useRef<HTMLElement>(null);
  const native = nativeConnected();
  const [gpuReady, setGpuReady] = useState(false);
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

  return <section ref={element} className={'viewport' + (native && active && gpuReady ? ' viewport-native' : '')}
    aria-label={native && active ? 'Native Scene viewport' : 'Viewport placeholder'}>
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
