# 109 — In-editor agent panel (stage 6 of the editor plan)

## Done
- **Agent panel** (`panels/AgentPanel.tsx`, `agentLogic.ts`). It sits in the bottom dock next to Assets and Console (Scene and Logic workspaces) and replaces the legacy "AI Agent" window.
  - Chat box: ⌘↩ sends.
  - Transcript: the user's requests, the agent's texts, its tool calls (`→ op {args}`), and the finish or error state.
  - **Change list**: every op the agent ran that changed something, or failed. Clicking a change jumps the History to right after it.
  - **Undo all** goes back to before the conversation's first change.
  - **New** starts a fresh conversation. Follow-ups resume the same Claude Code session (`--resume <session_id>`).
  - A hint appears when the CLI is not signed in.
- **Native** (`StudioSession::pollAgentLink` / `agentRequest`, called each frame from `Main.mm tick`):
  - Studio serves the agent link protocol 2 (`docs/mcp-agent-link.md`: hello + token file, then `call`) on `LineServer::listenAvailable()`, the same as the ImGui editor. It opens on the first `studio.agentInfo`, that is, when the panel is shown.
  - Calls run on the main thread through `ws_.call(name, args, "agent")`: validated, logged, undoable.
  - Each MapDoc snapshot a call adds becomes a History step "Agent · <Op>". The host gets `document.changed` + `agent.changed`, so the Scene and panels refresh live.
  - The selection is dropped only when the entity count changes.
  - `studio.agentStart {prompt}` runs Claude Code headless (`editor::AgentRun`, the Game Wizard's runner) in the project folder:
    - It writes `logs/studio-agent/{task.md, mcp.json, run.log}`.
    - `mcp.json` starts **this build's aiversemcp** (`AIVERSE_STUDIO_MCP_TOOL`; CMake adds the dependency) with `--port <Studio's port>`.
    - The run uses `--strict-mcp-config`, so the user's global "aiverse" entry, which points at the ImGui editor, is not used.
    - Allowed tools: `mcp__aiverse` only (no file edits or shell).
    - The first task carries the context: project, map, "change only through the tools, don't save".
  - `studio.agentStop` kills the run. `studio.agentReset` clears the conversation and the list.
  - `AgentRun::start` gained an optional `extraArgs`, and `sessionId()` parsed from stream-json. Existing callers are unchanged.

## Verified
- Session test (native, Mac): the panel opens a port, a `LineClient` does the hello with the token file, then calls `entity_place`.
  - Checked: the entity is added, the change list has one undoable entry, and the History's last step is "Agent · Entity place". Undo removes it.
  - An empty prompt is refused.
- On screen (private lua_lab copy):
  - The panel shows "Claude Code" (CLI found) and port 47321.
  - Sending a request started the run. run.log shows the `aiverse` MCP server **connected** (dynamic config → Studio).
  - The run then stopped with "Not logged in · Please run /login": the `claude` CLI on this Mac is not signed in. The panel shows the error and the sign-in hint.
  - A real agent edit on screen was not possible for that reason; the link path is covered by the session test.

## Next
- Sign in the CLI (`claude` → `/login`) and try a real request; screenshot tools (`view_screenshot`) are not served by Studio yet.
- Keep runtime changes made during play; this needs a stable link between runtime and map entities.
