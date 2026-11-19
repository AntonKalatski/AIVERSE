import { Box } from 'lucide-react';

/** Replace with the actual native GPU surface host; never render fake scene data. */
export default function ViewportPlaceholder({ title }: { title: string }) {
  return <section className="viewport" aria-label="Native viewport placeholder">
          <div className="viewport-grid"/>
          <div className="viewport-placeholder"><div className="viewport-cube"><Box size={44} strokeWidth={1.1}/></div><h2>{title}</h2><p>The AIVERSE C++ renderer will be embedded here in the native host.<br/>This HTML5 shell never renders a fake GPU scene.</p><span className="viewport-tag">Metal · Vulkan · GLES</span></div>
          <div className="viewport-axis"><span className="axis-y">Y</span><span className="axis-x">X</span><span className="axis-z">Z</span></div>
        </section>;
}
