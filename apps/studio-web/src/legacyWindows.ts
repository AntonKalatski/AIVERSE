/**
 * Legacy (ImGui) editor windows that have no HTML5 implementation yet.
 * Source of truth: docs/studio-html5-parity.md. Shown disabled in Window menu
 * so the gap is visible; remove an entry when its HTML5 panel ships.
 */
export const LEGACY_WINDOWS_NOT_MIGRATED: ReadonlyArray<{ id: string; title: string }> = [
  { id: 'animation_attachments', title: 'Attachments' }, 
  { id: 'studio_extensions', title: 'Extensions' },
  { id: 'studio_frame_capture', title: 'Frame Capture' },
  { id: 'texture_resources', title: 'Texture Resources' },
];
