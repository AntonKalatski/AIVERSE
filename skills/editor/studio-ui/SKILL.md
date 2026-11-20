# AIVERSE Studio UI — Skill for AI window authors

Use this skill whenever you **create, redesign, or migrate an HTML5 editor window** in AIVERSE.

## Start here
1. Read \`docs/studio-design-system.md\` and \`apps/studio-web/AGENTS.md\`.
2. Confirm you're on the feature branch \`feature/studio-html5\` and inspect its last handoff.
3. Prefer \`python3 tools/studio/new_panel.py --id <id> --title "<Title>" --area right\` for a new window. Generated TSX already uses the style library.
4. Do not introduce a new UI framework, palette, icon set, CSS reset or project-file access.
5. Keep UI data in typed React state; access engine state only via \`src/bridge.ts\` and allowlisted native \`StudioSession\` methods.

## Standard imports
\`\`\`tsx
import { Search, RefreshCw } from 'lucide-react';
import {
  StudioPanel, StudioSection, StudioButton, StudioIconButton,
  StudioField, StudioInput, StudioBadge, StudioEmptyState,
  StudioAlert, StudioTabs, StudioListRow
} from '../../ui';
\`\`\`
Adjust relative import depth for the actual file location.

## Visual recipe
- Wrap every new window in \`StudioPanel\`; it has a title, optional eyebrow, subtitle, actions and footer.
- Arrange content with \`StudioSection\`, \`au-stack\`, \`au-row\`, \`au-grid\`, \`au-card\`. Let dock panels shrink to 240px.
- Use shared buttons and fields. Never create a second style for inputs, borders, shadows or focus rings.
- For an icon-only action use \`StudioIconButton aria-label="..."\`, not an unlabeled \`button\`.
- For loading use \`StudioSkeleton\`; for empty use \`StudioEmptyState\`; for failure use \`StudioAlert tone="danger"\`; for status use \`StudioBadge\`.
- Only feature-specific layout (not colors) goes in the screen's CSS. All tokens are in \`src/theme/tokens.css\`.
- Use \`lucide-react\` icons. Exclude decorative complexity and preserve readable type at narrow widths.

## Nonnegotiable engine boundaries
- Renderer stays in C++/RHI, no fake web 3D surface or per-frame texture streaming.
- Do not change \`.viewport\`, native GPU layer, \`html.native-composite\`, \`StudioViewportLayout\` as a shortcut to style other windows.
- No ImGui in new UI. Legacy ImGui remains until parity.
- Backend mutations are native operations; HTML5 buttons must not execute arbitrary file paths, shell or unvalidated JSON.

## Checks before marking the window done
\`\`\`bash
cd apps/studio-web
npm run typecheck
npm run build
npm run test:web
cd ../..
python3 -m unittest discover -s tools/studio -p 'test_*.py' -v
\`\`\`
Inspect visual states in the browser and on WKWebView: desktop, 240px pane, empty/loading/error, focus and disabled. When done, append a separate handoff with commit, changed files, actual test commands and outcomes, known limitations and next step.
