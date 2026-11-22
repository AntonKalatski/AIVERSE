#!/usr/bin/env python3
"""Register the policy packages added on 2026-11-22 (catalog entries, skills, search aliases).

Run from the repository root: python3 tools/skills/policy_packages.py
Idempotent: existing catalog entries with the same id are replaced, skills are rewritten.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
L = 'libraries/include/aiverselib/'
TEST = 'tests/libraries/policy_mechanics_tests.cpp'

# id: (source, category, area, summary, title, body lines, limits, aliases, deps)
P = {
 'traversal-moves': (L+'mechanics/TraversalMoves.h', 'gameplay', 'F1 P1',
  'Wall-run, wall-slide, wall jump, dash with air charges, grapple hook, glide and one-way platforms with drop-through over LocomotionModes',
  'Traversal moves',
  ['Owner reports TraversalEnv each tick (grounded, wall contact/normal/id, wallRunnable tag, grapple ray hit, onOneWay) and the body velocity.',
   'Call `stepTraversal(config, state, env, input, tick, dt)` after LocomotionModes; when `result.overrides` use `result.velocity` instead of the locomotion velocity.',
   'One-way platforms: the collision owner asks `oneWaySolid(top, previousFeet, feet, vy, result.ignoreOneWay)` before resolving contacts.',
   'An expired wall-run blocks the same wall until the body touches ground; a wall jump blocks it for `wallReattachTicks`.',
   'Persist TraversalState with genre::save/load (kind 200) and bind `traversalConfigHash(config)`.'],
  'No camera tilt or animation; prediction/reconciliation of these modes in engine/actor is not wired yet.',
  'wallrun wall-run wall slide walljump dash air dash grapple hook glide glider one-way platform drop through parkour паркур бег по стене рывок крюк кошка планирование односторонн платформ', ['configuration']),
 'follow-camera': (L+'gameplay/ThirdPersonCamera.h', 'gameplay', 'P2',
  'Third-person camera boom: orbit, shoulder offset and swap, obstacle avoidance by owner sphere sweep, recenter behind the target, profile stack with blends',
  'Third-person follow camera',
  ['Presentation only (float). Never feed camera values into the simulation.',
   'Each frame: `updateBoom(state, baseProfile, stack, input, sweep, pose)`; `sweep(origin, dir, maxDistance, radius)` returns the first hit distance (ignore the followed body).',
   'The boom pulls in at once when blocked and eases back out (`pullInResponse` / `pushOutResponse`).',
   'Push temporary profiles (aim, vehicle, cinematic) with `pushProfile(stack, state, base, id, profile)` and pop them by id; changes blend over `stack.blendSeconds`.',
   'Use `pose.eye`, `pose.lookAt`, `pose.fov` for the host camera.'],
  'No collision with moving bodies unless the sweep reports them; no automatic vehicle profile switching.',
  'third person camera follow camera orbit camera boom shoulder obstacle avoidance aim camera камера от третьего лица плечо камера препятствия', []),
 'nav-links': ('libraries/include/aiverselib/bots/NavLinks.h', 'ai', 'F2',
  'Off-mesh navigation links (jump, drop, ladder, climb, door, teleport) planned together with any walking mesh, link traversal over ticks, cover spot generation from boxes',
  'Off-mesh navigation links and cover generation',
  ['Describe links as data (`NavLink`: from, to, kind, cost, bidirectional, traverseTicks, arcHeight, enabled).',
   '`planWithLinks(links, start, goal, meshCost)` - `meshCost(a, b)` returns the walking distance on your mesh (bots/NavMesh.h, engine/nav) or nullopt; the result is a list of Walk/Link legs.',
   'Drive a link with `beginLink` + `stepLinkTraversal(links, traversal, tick, doorOpen)`; doors make the agent wait until open.',
   '`generateCover(boxes, config, standable)` returns cover spots (half/full by box height) around box obstacles; map them to `bots::CoverCandidate` for ShooterBot.',
   'Persist LinkTraversal with genre::save/load (kind 201).'],
  'Links are not extracted from geometry automatically; the planner calls meshCost for node pairs (fine for hundreds of links).',
  'offmesh off-mesh link navlink jump link ladder door teleport navmesh links cover generation cover points ссылки навмеш прыжок лестница дверь укрытия генерация', []),
 'volume-nav': ('libraries/include/aiverselib/bots/VolumeNav.h', 'ai', 'R5',
  '3D navigation for flyers, ships and swimmers: occupancy grid from boxes, deterministic 26-neighbour A* with smoothing, flock avoidance and 3D formation slots',
  'Volumetric 3D navigation',
  ['Build a grid with `buildVolumeGrid(area, cellSize, agentRadius, obstacleBoxes)`; rebuild when obstacles change.',
   '`findVolumePath(grid, start, goal, maxExpanded)` returns smoothed waypoints; `partial` when the expansion budget ran out.',
   '`flockSteer(config, self, neighbours, goal, obstaclePush)` gives a bounded acceleration for fleets and swarms.',
   '`formationSlot3D(leader, yaw, pitch, offset)` rotates formation offsets in 3D for ships and aircraft.'],
  'Dense grid (up to 4M cells); no hierarchical or streamed volumes yet.',
  '3d navigation flying pathfinding volumetric navigation space ship drone swarm flock boids 3d formation полет 3d навигация корабли дроны стая формация', []),
 'structure-destruction': (L+'mechanics/StructureDestruction.h', 'gameplay', 'F3',
  'Destructible structures of box pieces: area damage, fracture into fragments, support graph collapse, authoritative debris and dirty regions for collision/navmesh/visibility',
  'Destructible structures',
  ['Add pieces with `addPiece` (anchored pieces hold the structure; optional fracture grid and threshold).',
   'Every tick: `stepStructure(config, state, tick, dt, damage)`. Apply `report.removed` / `report.added` to collision, rebuild navmesh and visibility inside `report.dirty`.',
   'Debris bodies are authoritative (gravity, bounce, rest, lifetime): replicate `report.spawned` or the whole state; Box3D may add visual detail only.',
   'Use with PHYSIS fracture events (engine/physis/PhysisFractureOutbox.h) and physical-interactions breakables for single objects.',
   'Persist with genre::save/load (kind 202).'],
  'Box pieces only (no arbitrary mesh slicing); support is connectivity, not stress analysis; no soft bodies.',
  'destruction destructible wall building collapse fracture debris structural support demolition разрушение разрушаемые стены обрушение обломки', ['configuration']),
 'portals-rewind': (L+'mechanics/PortalsRewind.h', 'gameplay', 'Q4',
  'Linked portals with position/velocity/facing transfer and recursive view transforms, plus gameplay time rewind over a bounded snapshot history with an energy budget',
  'Portals and time rewind',
  ['Portals: define pairs (`Portal`: center, normal, up, half size, link). Each tick call `crossPortals(portals, cooldowns, tick, previous, bodies)` after movement and apply the crossings.',
   'Rendering recursion: apply `portalTransformPoint/Direction` to the camera once per depth level.',
   'Rewind: `recordRewind(config, history, tick, snapshotBytes)` every tick (use genre::save output or a world snapshot); `rewindTo(history, tick)` returns the snapshot to restore and spends energy.',
   'This is gameplay rewind; engine/actor rewind history stays lag compensation.',
   'Persist PortalCooldowns (kind 203); RewindHistory (kind 204) only for small histories (save limit 1 MB).'],
  'No portal-in-portal physics for bodies larger than the opening; rewind of single objects (local time bubbles) is left to the owner.',
  'portal portals teleport velocity transfer recursive portal rewind time rewind braid prince of persia time travel портал порталы перемотка времени отмотка', []),
 'item-affixes': (L+'mechanics/ItemAffixes.h', 'gameplay', 'A2',
  'Seeded item generation with prefix/suffix affix pools, tiers, groups and item level, crafting rerolls, coloured sockets and gems, elemental auras and reactions, item migration',
  'Item affixes, sockets and reactions',
  ['Data: `AffixData` (affixes, item bases with socket colours and implicit stats, gems, version).',
   '`rollItem(data, id, base, itemLevel, prefixes, suffixes, seed)` is deterministic; crafting: `addAffix`, `removeAffix`, `rerollAffix`, `rerollValues` with their own seeds.',
   '`socketGem` / `unsocketGem`; `itemStats` sums values per stat id for attribute-modifiers.',
   'Reactions: `applyElements(reactionData, auras, tick, hits)` keeps auras per target and emits reactions with damage multipliers.',
   'Content updates: `migrateItem(data, item, migration)` renames, removes and rescales affixes.',
   'Persist ItemInstance (kind 205) and AuraState (kind 206).'],
  'No UI; affix text and balance are content.',
  'affix affixes prefix suffix item generation diablo path of exile sockets gems runes crafting reroll elemental reaction aura аффиксы префикс суффикс сокеты камни крафт реакции стихий', []),
 'quest-journal': (L+'mechanics/QuestJournal.h', 'gameplay', 'A4',
  'Quests as data: stages, counted and optional objectives, branches, prerequisites, time limits, abandon/fail/repeat, tracked quest, bounded journal and versioned migration',
  'Quest journal',
  ['Define `QuestDefs` (stages with objectives listening to event keys, branches, nextStage, time limits).',
   'Each tick `stepQuests(defs, log, tick, commands, events)`; events are (key, amount) facts from the game (kills, items, places, dialogue).',
   'Commands: Start, Abandon, Fail, Choose (branch), Track. The returned entries are new journal lines for the UI; `log.journal` keeps the last 512.',
   'After loading a save with newer definitions call `migrateQuests(defs, log, tick)`.',
   'Persist QuestLog (kind 207). Combine with narrative-graphs for dialogue and objectives for markers.'],
  'No quest UI widgets yet; journal text is content.',
  'quest quests journal quest log objectives quest stages branching quest tracker квест квесты журнал задания цели трекер', []),
 'wounds-disease': (L+'mechanics/WoundsDisease.h', 'gameplay', 'S1',
  'Wounds by type and body part with bleeding, pain, healing, infection and treatments; diseases with incubation, symptoms, contagion, cures and immunity',
  'Wounds and diseases',
  ['Data: `HealthData` (wound types, diseases, seed).',
   'Each tick `stepHealth(data, state, tick, newWounds, treatments, contacts)`; contacts are pairs of bodies close enough to infect this tick.',
   'Outputs per body: blood, pain, penalty (0..1000), bleeding and death; feed them into Survival.h and movement/work speed.',
   'Rolls are hashed from (seed, tick, body, id): results do not depend on processing order.',
   'Persist HealthState (kind 208).'],
  'No per-limb loss of function; medical UI is not included.',
  'wounds injury bleeding infection disease illness contagion epidemic medicine bandage splint immunity раны кровотечение инфекция болезни зараза эпидемия лечение иммунитет', []),
 'herds-taming': (L+'mechanics/HerdsTaming.h', 'gameplay', 'S2',
  'Herds with leader, cohesion, spreading panic and succession, off-screen aggregate herds, and taming by trust with follow/stay/guard orders',
  'Herds and taming',
  ['`addHerd(state, herd)` with member ids; each tick pass `MemberFact` for on-screen members and the ids of off-screen herds.',
   '`stepHerds(config, tamingDefs, state, tick, facts, commands, offscreen)` returns desired directions/speeds; steer bodies with navmesh or flow fields.',
   'Taming commands: Feed (preferred food gives more trust), Scare, Order. `tamedOwner(state, animal)` returns the owner.',
   'Off-screen herds move their home and change size by daily birth/death rates.',
   'Persist HerdState (kind 209). Single-animal decisions stay in EcologyAnimals.h.'],
  'No breeding of tamed animals or saddles/riding.',
  'herd herds flock pack animals panic taming tame pet follow stay off-screen simulation стадо стая паника приручение питомец', []),
 'colonist-relations': (L+'mechanics/ColonistRelations.h', 'gameplay', 'C1',
  'Colonist families, memories with decay and stacking, opinions, friendships and rivalries with hysteresis, partnerships, item ownership and social mood',
  'Colonist relations',
  ['Each tick `stepSocial(config, memoryDefs, state, tick, commands)`; commands add colonists, family links, memories, proposals, claims and item use.',
   '`opinion(config, defs, state, a, b)` and `moodOf(config, defs, state, a)` feed the colony mood and job choices.',
   'Using an item owned by another colonist emits ItemUsedByOther and the configured memory for the owner.',
   'Persist SocialState (kind 210).'],
  'Births and ageing belong to the owner; no conversation generator.',
  'relationships family friends rivals partner marriage opinion memories thoughts ownership colonist social отношения семья друзья соперники партнер мнение воспоминания колонист', []),
 'utility-networks': (L+'mechanics/UtilityNetworks.h', 'gameplay', 'C2',
  'Pressure-driven pipe networks with sources, sinks, tanks, pumps and valves, and heat exchange between rooms with doors, outdoor boundary and thermostats',
  'Pipes with pressure and heat',
  ['Fluid: `FluidNetwork` (nodes, pipes); each tick `stepFluid(network, state, tick)`; valves in `state.valves`; report has flow per pipe, delivered/shortfall per sink, spill.',
   'Heat: `HeatNetwork` (rooms, links, devices); each tick `stepHeat(network, state, tick, outdoorMilliDegrees)`; doors in `state.doorOpen`, devices in `state.deviceOn`.',
   'Electricity stays in ColonyPower.h; item transport in production-networks.',
   'Persist FluidState (kind 211) and HeatState (kind 212).'],
  'Pressure is a relaxed approximation, not a hydraulic solver; no gas mixing.',
  'pipes pressure pump valve water supply fluid network heat temperature rooms heating cooling thermostat трубы давление насос клапан водопровод тепло отопление термостат', []),
 'railways': (L+'mechanics/Railways.h', 'gameplay', 'C2',
  'Trains on a track graph with block signalling, acceleration and braking, looping routes and station stops with dwell times',
  'Railways and block signals',
  ['Segments carry length, speed limit, block and station. `placeTrains(network, trains)` reserves starting blocks.',
   'Each tick `stepRail(network, trains, state, tick, dt)`; trains enter the next block only when it is free (lowest id first), brake in time and wait at the signal.',
   'Arrived/Departed events at stations are where the owner loads and unloads cargo.',
   'Persist RailState (kind 213).'],
  'No junction pathfinding (routes are explicit), no chain signals, no train length.',
  'railway railways trains rail track signals block signal station timetable openttd factorio trains железная дорога поезда рельсы семафор блок станция', []),
}

def skill_md(pid, spec):
    source, cat, area, summary, title, body, limits, aliases, deps = spec
    name = 'aiverse-' + pid
    lines = [ '---', f'name: {name}', f'description: {summary}.', '---', '', f'# {title}', '',
        f'Header: `{source.replace("libraries/include/", "<")}>`. Library catalog id `{pid}`, CMake target `AIVERSE::lib_{pid.replace("-", "_")}`.',
        f'Capability area {area} in `docs/capability-status.json`. Tests: `{TEST}`.', '',
        ('Presentation policy for the host camera (float math); works in any project.' if pid == 'follow-camera' else
         'Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. '
         'Works in any project: the owner reports facts and applies outputs.'), '', '## Use', '']
    lines += [f'{i+1}. {b}' for i, b in enumerate(body)]
    lines += ['', '## Limits', '', limits, '']
    return '\n'.join(lines)

def main():
    cat_path = ROOT/'libraries/catalog.json'
    text = cat_path.read_text()
    data = json.loads(text)
    ids = [p['id'] for p in data['packages']]
    new_entries = []
    for pid, spec in P.items():
        source, cat, area, summary, title, body, limits, aliases, deps = spec
        skill = f'skills/{cat}/aiverse-{pid}/SKILL.md'
        (ROOT/skill).parent.mkdir(parents=True, exist_ok=True)
        (ROOT/skill).write_text(skill_md(pid, spec))
        entry = {'id': pid, 'version': '0.1.0', 'layer': 'presentation' if pid == 'follow-camera' else 'simulation', 'status': 'cpp-subsystem',
                 'source': source, 'skill': skill, 'summary': summary,
                 'notes': f'Capability {area}. {limits} Tests: {TEST}.', 'verifiedPlatforms': [], 'runtime': 'header',
                 'cmakeTarget': f'AIVERSE::lib_{pid.replace("-", "_")}', 'linkDependencies': ['aiverse_engine'] if pid != 'follow-camera' else [],
                 'packageDependencies': deps}
        new_entries.append(entry)
    # Replace or append entries textually so untouched packages keep their formatting.
    for e in new_entries:
        block = json.dumps(e, ensure_ascii=False, indent=2)
        block = '\n'.join('    ' + l for l in block.split('\n'))
        key = f'"id": "{e["id"]}"'
        if key in text:
            start = text.rindex('\n    {', 0, text.index(key))
            end = text.index('\n    }', text.index(key)) + len('\n    }')
            text = text[:start] + '\n' + block + text[end:]
        else:
            end = text.rindex('\n  ]')
            text = text[:end] + ',\n' + block + text[end:]
    json.loads(text)
    cat_path.write_text(text)
    alias_path = ROOT/'tools/skills/policy_aliases.json'
    aliases = json.loads(alias_path.read_text()) if alias_path.exists() else {}
    for pid, spec in P.items():
        aliases['aiverse-' + pid] = spec[7]
    alias_path.write_text(json.dumps(aliases, ensure_ascii=False, indent=1, sort_keys=True) + '\n')
    # Section in docs/mechanics-packages.md between markers.
    doc = ROOT/'docs/mechanics-packages.md'
    start, end = '<!-- policy-packages:start -->', '<!-- policy-packages:end -->'
    parts = [start, '', '## Policy packages added 2026-11-22', '',
             'Header-only deterministic policies registered by `tools/skills/policy_packages.py`; each has a skill and the tests in '
             f'`{TEST}`. Status per capability area: `docs/capability-status.json`.', '']
    for pid, spec in P.items():
        source, cat, area, summary, title, body, limits, aliases, deps = spec
        parts += [f'### {title} ({pid} 0.1.0, {area})', '', f'`{source}`, target `AIVERSE::lib_{pid.replace("-", "_")}`, skill `skills/{cat}/aiverse-{pid}/SKILL.md`.', '',
                  summary + '.', '', '- ' + '\n- '.join(body), '', f'Limits: {limits}', '']
    parts.append(end)
    section = '\n'.join(parts)
    text = doc.read_text()
    if start in text:
        text = text[:text.index(start)] + section + text[text.index(end) + len(end):]
    else:
        text = text.rstrip('\n') + '\n\n' + section + '\n'
    doc.write_text(text)
    print(f'registered {len(P)} packages')

if __name__ == '__main__':
    main()
