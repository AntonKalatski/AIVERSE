/**
 * Copy text from a click or key press. navigator.clipboard is missing or refused outside secure origins
 * (the macOS host serves the UI from aiverse-studio://), so fall back to a hidden textarea + execCommand,
 * which WebKit and WebView2 both honour inside a user gesture.
 */
export async function copyText(text: string): Promise<boolean> {
  try {
    if (navigator.clipboard?.writeText) { await navigator.clipboard.writeText(text); return true; }
  } catch { /* fall through */ }
  const area = document.createElement('textarea');
  area.value = text;
  area.setAttribute('readonly', '');
  area.style.cssText = 'position:fixed;left:-9999px;top:0;opacity:0';
  const active = document.activeElement as HTMLElement | null;
  document.body.appendChild(area);
  area.select();
  let ok = false;
  try { ok = document.execCommand('copy'); } catch { ok = false; }
  area.remove();
  active?.focus?.();
  return ok;
}

/** The current text selection if it lies inside `root`, else ''. */
export function selectionIn(root: HTMLElement | null): string {
  const sel = window.getSelection();
  if (!root || !sel || sel.isCollapsed || sel.rangeCount === 0) return '';
  const range = sel.getRangeAt(0);
  return root.contains(range.commonAncestorContainer) ? sel.toString() : '';
}
