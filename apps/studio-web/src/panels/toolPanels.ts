/** Tool panels migrated from the legacy editor windows: their op group (native StudioTools.h) and what they show first. */
export const TOOL_PANEL_IDS = ['terrain', 'water', 'roads', 'tiles', 'generate', 'postfx', 'effects', 'lightbake', 'models', 'overlays', 'branding'] as const;
export type ToolPanelId = typeof TOOL_PANEL_IDS[number];
export interface ToolPanelSpec {
  /** Op name prefix dropped from card titles ("terrain_sculpt" -> "Sculpt"). */
  prefix: string;
  /** Read-only ops shown as the panel's current state (re-read after every change). */
  state: string[];
  /** Ops opened by default. */
  open: string[];
  intro: string;
}
export const TOOL_PANELS: Record<ToolPanelId, ToolPanelSpec> = {
  terrain: { prefix: 'terrain', state: ['map_info'], open: ['terrain_sculpt'],
    intro: 'Heights, levels and surface materials of the open map. Each change is one History step.' },
  water: { prefix: 'water', state: ['water_network'], open: ['water_set'],
    intro: 'Spline water: straits, rivers and open sea with their shores.' },
  roads: { prefix: 'road', state: ['road_network'], open: ['road_add'],
    intro: 'Spline road network: nodes, edges, road types, stops and driveways.' },
  tiles: { prefix: 'tile', state: ['tileset_list'], open: ['tile_paint'],
    intro: 'Tile layer of the map: tilesets and painted tiles.' },
  generate: { prefix: 'map', state: ['gen_presets'], open: ['map_generate'],
    intro: 'Generate, mirror or resize the map. Generating replaces the map content (undo brings it back).' },
  postfx: { prefix: 'postfx', state: ['postfx_get'], open: ['postfx_add'],
    intro: 'PRISMA post-processing chain with its cost per device class.' },
  effects: { prefix: 'fx', state: ['fx_list'], open: ['fx_set'],
    intro: 'Particle effects (fx/*.json) and what they are bound to.' },
  lightbake: { prefix: 'lightbake', state: ['lightbake_get'], open: ['lightbake_set'],
    intro: 'Baked lighting of the map.' },
  models: { prefix: 'model', state: ['geometry_catalog'], open: ['model_info'],
    intro: 'Model information, geometry analysis and proxy LODs.' },
  overlays: { prefix: 'overlay', state: ['overlay_list'], open: ['overlay_set'],
    intro: 'Map overlays (decals, markings, zones) and their templates.' },
  branding: { prefix: 'branding', state: ['branding_get'], open: ['branding_set'],
    intro: 'Game name, icons and splash screen used by builds.' },
};
export function isToolPanel(id: string): id is ToolPanelId { return (TOOL_PANEL_IDS as readonly string[]).includes(id); }
