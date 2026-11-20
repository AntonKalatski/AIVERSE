# AIVERSE Studio UI — Visual Design System v1

**Location:** apps/studio-web/src/ui/ (shared) and apps/studio-web/src/theme/tokens.css (values).
**Scope:** reusable for every editor panel, every genre, future Metal/Vulkan/GLES hosts.
**Imports:** \`import { StudioPanel, StudioButton, ... } from '../ui/StudioUI';\` (relative path) or \`../ui\` after Stage 57 barrel setup.

## Principle
One clean dark visual system, not a set of one-off screens. Panels are low-contrast navy surfaces, with subtle gradients only for meaningful focus surfaces; text is readable and accents are reserved for active/interactive state. Icons: \`lucide-react\`. Design for dock panes as narrow as 240px.

## How to use

\`\`\`tsx
import { FolderOpen, RefreshCw } from 'lucide-react';
import {
  StudioPanel, StudioButton, StudioIconButton, StudioBadge,
  StudioField, StudioInput, StudioSection, StudioEmptyState
} from '../ui';

export function WorldStatsPanel({name,onReload}:{name:string;onReload:()=>void}) {
  return <StudioPanel
    title="World Statistics"
    eyebrow="WORLD / ANALYTICS"
    subtitle={name}
    actions={<StudioIconButton aria-label="Refresh statistics" icon={<RefreshCw size={15}/>} onClick={onReload}/>}
    footer={<StudioBadge tone="success">Ready</StudioBadge>}>
    <div className="au-stack">
      <StudioSection title="Filters">
        <StudioField label="Name" htmlFor="world-stats-name" hint="Filter by name">
          <StudioInput id="world-stats-name" placeholder="Find objects…"/>
        </StudioField>
      </StudioSection>
      <StudioEmptyState title="Nothing to show" icon={<FolderOpen size={24}/>}
        description="Open a scene to view its statistics."/>
      <StudioButton onClick={onReload}>Refresh</StudioButton>
    </div>
  </StudioPanel>;
}
\`\`\`

## Component catalogue
- **Layout:** \`StudioPanel\` (header, body, footer), \`StudioSection\`, \`au-row\`, \`au-grid\`, \`au-stack\`, \`au-card\`, \`au-toolbar\`, \`au-divider\`, \`au-scroll\`.
- **Actions:** \`StudioButton\` (primary/secondary/ghost/danger; sm/md/lg; disabled/loading), \`StudioIconButton\` (required \`aria-label\`).
- **Forms:** \`StudioField\` (associated label/hint/error), \`StudioInput\`, \`StudioSelect\`, \`StudioTextarea\`, \`StudioCheckbox\`. Engine values go through typed \`src/bridge.ts\`. Validation uses \`src/propertyLogic.ts\`.
- **Status and feedback:** \`StudioBadge\` (neutral/accent/success/warning/danger), \`StudioAlert\` (status or alert), \`StudioEmptyState\`, \`StudioSkeleton\` (loading), \`au-spinner\`, \`StudioProgress\`.
- **Navigation:** \`StudioTabs\` (controlled active ID), \`StudioListRow\` (selected state), \`au-kbd\`.

## Tokens / naming policy
- **Use \`--au-*\` tokens** and the stable \`.au-*\` class prefix in new panels. Keep all new color/spacing values in \`src/theme/tokens.css\`.
- Old \`--studio-*\` aliases are preserved because previous panels rely on them.
- Don't use raw hex colors in generated panel CSS; add a semantic token to the central palette if needed.
- Space: \`--au-space-1,2,3,4,5,6,8\` (4/8/12/16/20/24/32px).
- Radius: \`--au-radius-xs/sm/md/lg/xl\`; type: \`--au-font-xs/sm/md/lg/xl/title\`.
- Use \`aria-label\` for icon-only buttons; \`StudioField htmlFor\` must match the underlying input id.
- UI state: include loading, empty, error, readonly, disabled; use \`StudioAlert\`, \`StudioEmptyState\`, and \`StudioSkeleton\`.
- Avoid repainting the Scene HTML viewport. Keep Metal sibling layer and geometry bridge unchanged. No CSS applied globally except original tokens.

## Responsive / accessibility
Dock-safe components have \`min-width:0\` and scrollable bodies. \`StudioButton\` has visible keyboard focus, reduces motion when requested, handles loading/disabled, and never submits forms by accident. Never remove the visible focus ring. Controls use accessible semantics.

## Adding components
When adding a new reusable primitive, add it to \`src/ui/StudioUI.tsx\`, style it in \`src/ui/studio-ui.css\`, document it here, and add a test to \`tools/studio/test_design_system.py\`. Avoid introducing new visual libraries or duplicating existing primitives.

## Validation and scope
The new library is source-level and does not require a new npm dependency. The full React + Vite build and native WKWebView visual tests must run on Mac before declaring a design system migration complete. Legacy windows can continue using their CSS as the next agent migrates them incrementally.
