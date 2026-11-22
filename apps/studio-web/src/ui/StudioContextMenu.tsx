import { useEffect, useLayoutEffect, useRef, useState, type ReactNode } from 'react';
import './StudioContextMenu.css';

export interface StudioMenuItem {
  id: string;
  label: string;
  icon?: ReactNode;
  shortcut?: string;
  disabled?: boolean;
  danger?: boolean;
  /** Visual separator above this item. */
  separator?: boolean;
  /** Shows a check mark (toggle items such as open panels). */
  checked?: boolean;
  /** Non-interactive section caption. */
  caption?: boolean;
  /** Nested menu, opened beside this row (Add Tab). */
  children?: StudioMenuItem[];
  onSelect: () => void;
}

/**
 * Shared right-click menu for every Studio panel. Positioned at the pointer and
 * kept inside the window; closes on outside click, Escape, scroll or blur.
 * Arrow keys move focus, Enter activates.
 */
export function StudioContextMenu({ x, y, items, onClose, label = 'Context menu' }: {
  x: number; y: number; items: StudioMenuItem[]; onClose: () => void; label?: string;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState({ x, y });
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    setPos({ x: Math.max(4, Math.min(x, window.innerWidth - r.width - 4)),
      y: Math.max(4, Math.min(y, window.innerHeight - r.height - 4)) });
    el.querySelector<HTMLButtonElement>('button:not(:disabled)')?.focus();
  }, [x, y]);
  useEffect(() => {
    const down = (event: PointerEvent) => { if (!ref.current?.contains(event.target as Node)) onClose(); };
    const key = (event: KeyboardEvent) => { if (event.key === 'Escape') { event.preventDefault(); onClose(); } };
    // A long menu (Window) scrolls itself. Wheel over the menu must not dismiss it.
    const wheel = (event: WheelEvent) => { if (!ref.current?.contains(event.target as Node)) onClose(); };
    window.addEventListener('pointerdown', down, true);
    window.addEventListener('keydown', key);
    window.addEventListener('blur', onClose);
    window.addEventListener('wheel', wheel, { passive: true });
    return () => {
      window.removeEventListener('pointerdown', down, true);
      window.removeEventListener('keydown', key);
      window.removeEventListener('blur', onClose);
      window.removeEventListener('wheel', wheel);
    };
  }, [onClose]);
  return <div ref={ref} className="au-menu" role="menu" aria-label={label}
    style={{ left: pos.x, top: pos.y }}
    onContextMenu={event => event.preventDefault()}
    onKeyDown={event => {
      if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;
      event.preventDefault();
      const buttons = [...(ref.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') ?? [])];
      const index = buttons.indexOf(document.activeElement as HTMLButtonElement);
      const next = buttons[(index + (event.key === 'ArrowDown' ? 1 : buttons.length - 1)) % buttons.length];
      next?.focus();
    }}>
    <MenuRows items={items} onClose={onClose}/>
  </div>;
}

function MenuRows({ items, onClose }: { items: StudioMenuItem[]; onClose: () => void }) {
  const [open, setOpen] = useState<string | null>(null);
  return <>
    {items.map(item => <div key={item.id}
      className={'au-menu__row' + (item.separator ? ' au-menu__group' : '')}
      onMouseEnter={() => setOpen(item.children ? item.id : null)}>
      {item.caption ? <div className="au-menu__caption">{item.label}</div> : <button type="button" role="menuitem"
        aria-haspopup={item.children ? 'menu' : undefined}
        aria-expanded={item.children ? open === item.id : undefined}
        className={'au-menu__item' + (item.danger ? ' is-danger' : '')}
        disabled={item.disabled}
        onClick={() => { if (item.children) { setOpen(item.id); return; } onClose(); item.onSelect(); }}>
        <span className="au-menu__icon">{item.checked ? '✓' : item.icon}</span>
        <span className="au-menu__label">{item.label}</span>
        {item.children ? <span className="au-menu__shortcut" aria-hidden="true">›</span>
          : item.shortcut && <kbd className="au-menu__shortcut">{item.shortcut}</kbd>}
      </button>}
      {item.children && open === item.id && <div className="au-menu au-menu--sub" role="menu" aria-label={item.label}>
        <MenuRows items={item.children} onClose={onClose}/>
      </div>}
    </div>)}
  </>;
}
