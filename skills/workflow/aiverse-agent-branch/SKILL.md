---
name: aiverse-agent-branch
description: Work as an AIVERSE agent in your own branch and worktree - create it, merge main before every stage, stay in your area, build outside the shared build folder, verify, commit with the owner identity, hand off to MAIN.
---

# Agent branch workflow

1. Worktree: `git worktree add -b <branch> .worktrees/<branch> main` (names: `agent-<area>`, `agent-mech-<code>`). Never work in the user's main checkout.
2. Before every stage: `git merge main` into your branch; resolve conflicts now, not at hand-off.
3. Area: AGENTS.md section 5 table. Outside your area keep changes minimal and additive, prefer new files, say so in the commit message.
4. Builds go inside your worktree (`.worktrees/<branch>/build/...`) or a temp dir - never the shared `build/`. Delete build trees after the merge.
5. Verify before each commit: build without warnings, `aiverse_tests`, headless matches that must end with a winner, screenshots for visual changes, checksum gates for simulation changes.
6. Commit small and descriptive. Author is the repository owner from `git config`; no other identity, no `Co-Authored-By` / session trailers. List unverified paths (Metal, Android, web).
7. Commit to your branch when the stage is done; only MAIN merges into `main` (`skills/workflow/aiverse-main-merge`).
8. Connected folders on the user's Mac: git cannot remove its own `.lock` / `tmp_obj_*` files until deletion is allowed; ask for delete permission once, or move a stale lock you created to a new name directly under `.git` - never touch a lock a running git may hold.

Runtime library: none. Repository process workflow.
