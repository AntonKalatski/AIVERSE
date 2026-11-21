/**
 * Legacy (ImGui) editor windows that have no HTML5 implementation yet.
 * Source of truth: docs/studio-html5-parity.md. Shown disabled in Window menu
 * so the gap is visible; remove an entry when its HTML5 panel ships.
 */
export const LEGACY_WINDOWS_NOT_MIGRATED: ReadonlyArray<{ id: string; title: string }> = [
  { id: 'agent', title: 'AI Agent' }, { id: 'animation_assets', title: 'Animation Assets' },
  { id: 'animation_graph', title: 'Animation Graph' }, { id: 'animation_rig', title: 'Animation Rig' },
  { id: 'animation_attachments', title: 'Attachments' }, { id: 'AnimationWindow', title: 'Model Animation' },
  { id: 'fields', title: 'Fields' }, { id: 'FpsWindow', title: 'FPS Authoring' },
  { id: 'ModelingWindow', title: 'Modeling' }, { id: 'physis', title: 'PHYSIS' },
  { id: 'project_import', title: 'Import Project' }, { id: 'raid', title: 'Raid' },
  { id: 'spectrafx_gpu_assets', title: 'SpectraFX GPU Assets' },
  { id: 'spectrafx_graph', title: 'SpectraFX Graph' }, { id: 'stats', title: 'Statistics' },
  { id: 'studio_camera_bookmarks', title: 'Camera Bookmarks' }, { id: 'studio_extensions', title: 'Extensions' },
  { id: 'studio_frame_capture', title: 'Frame Capture' }, { id: 'studio_snapshot_diff', title: 'Snapshot Diff' },
  { id: 'texture_resources', title: 'Texture Resources' },
  { id: 'tools', title: 'Tools' },
];
