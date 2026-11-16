---
name: aiverse-campaign
description: Build an AIVERSE campaign - campaign.json mission chain, briefings, objectives, dialogs, camera, endMission triggers, carry-over of army and resources, verified with campaign_run.
---

# Campaign

Format: `docs/game-format.md` "Campaigns", `modules/rts/RtsCampaign.h`.

1. Maps first (`skills/world/aiverse-map-build`); players[0] of every mission map is the campaign player.
2. `campaign_set` with `{"start", "missions": [{"id", "name", "map", "briefing", "next", "carry": {"resources", "tags", "types", "max"}}]}`. Briefings are lang keys (`game_file_write lang/*.json`), never hard-coded text.
3. Mission scripting with trigger actions in `map_set`:
   - `objective` (`id`, `text`, `op` add/done/fail, `primary`);
   - `dialog` (`speaker`, `portrait`, `voice`, `text`, `duration`);
   - `camera` (`at` or `area`, `zoom`, `duration`, `lock`);
   - `endMission` (`result` victory/defeat, `next`).
   A mission also ends on a normal victory.
4. Carry: survivors (hp, rank, hero level) and listed resources go into the next map; keep `max` realistic for the next map's space.
5. Verify: `campaign_get`, then `campaign_run` (bots play the chain): every mission ends, outcomes and carry as intended. Check the HUD (objective list, dialogs) with `play_start` + `view_screenshot`.
6. A mission with no winner by the limit is a broken objective chain, not a bot problem - trace triggers in the `match_run` log.

Runtime library: none. Workflow over the engine MCP campaign tools.
