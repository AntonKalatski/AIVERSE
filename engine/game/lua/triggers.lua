-- Triggers (rules / map "triggers": event -> conditions -> actions), docs/game-format.md § Triggers.
-- Engine standard library: the "triggers" system (std.triggers), last C++ system of the tick so it
-- sees every event. Parsing stays in C++ (RtsTriggers.cpp); the runtime state (fired / edge / inside
-- flags, variables, messages) lives in GameState::trig, so it is hashed and rolled back like before.
-- Unit scans (counts, targets, area membership) are C++ helpers; this file decides what fires and
-- what happens. Mirrors the former C++ RtsModule::sysTriggers exactly (bit-identical matches).

local DEFS = core.trigger_defs()
local AREAS = core.trigger_areas()
local TYPES = core.type_flags()
local HOSTILE = core.HOSTILE_NEUTRAL

local function compare(a, cmp, b)
  if cmp == ">=" then return a >= b end
  if cmp == ">" then return a > b end
  if cmp == "<=" then return a <= b end
  if cmp == "<" then return a < b end
  if cmp == "==" then return a == b end
  return not (a == b)
end

local function filterMatches(f, t)       -- t: type index
  if f.type and f.type ~= t then return false end
  if f.tags ~= 0 and TYPES.tags[t + 1] & f.tags == 0 then return false end
  if f.structures and not TYPES.structure[t + 1] then return false end
  if f.units and TYPES.structure[t + 1] then return false end
  return true
end

local function areaContains(a, x, z)
  return x >= a.x and z >= a.z and x < a.x + a.w and z < a.z + a.h
end

local function tickRate() return game.tick_rate() end
local function secondsToTicks(s) return fx.int(s * tickRate()) end

-- ---------------------------------------------------------------- conditions
local function conditionsHold(di, d, player)
  for ci, c in ipairs(d.conditions) do
    local ok = false
    local k = c.kind
    if k == "count" then
      ok = compare(fx(core.trig_count(di, ci, player)), c.cmp, c.value)
    elseif k == "resource" then
      ok = true
      local slots = core.player_slots()
      for _, p in ipairs(core.trig_players(di, "c", ci, player)) do
        if p < slots and not compare(core.stock(p, c.index), c.cmp, c.value) then ok = false end
      end
    elseif k == "variable" then
      ok = compare(core.var(c.name), c.cmp, c.value)
    elseif k == "researched" then
      ok = true
      local slots = core.player_slots()
      for _, p in ipairs(core.trig_players(di, "c", ci, player)) do
        if p >= slots or not core.researched(p, c.index) then ok = false end
      end
    elseif k == "random" then
      ok = core.rng_unit() < c.value
    elseif k == "time" then
      ok = compare(core.ratio(game.tick(), tickRate()), c.cmp, c.value)
    elseif k == "alive" then
      ok = true
      for _, p in ipairs(core.trig_players(di, "c", ci, player)) do
        if not core.alive(p) then ok = false end
      end
    elseif k == "regions" then
      ok = compare(fx(core.regions_owned(player)), c.cmp, c.value)
    elseif k == "script" then
      ok = core.trig_script_condition(di, ci, player)
    end
    if ok == c.negate then return false end
  end
  return true
end

-- ---------------------------------------------------------------- actions
local SEVEN_TENTHS = core.ratio(7, 10)

local function runActions(di, d, player, unit, wx, wy, wz)
  for ai, a in ipairs(d.actions) do
    local players = core.trig_players(di, "a", ai, player)
    local function targets() return core.trig_targets(di, ai, player, unit) end
    -- where: explicit point, area centre, the unit, else the event's position
    local ax, ay, az = wx, wy, wz
    if a.has_at then ax, ay, az = a.at_x, fx(0), a.at_z
    elseif a.area >= 0 then
      local ar = AREAS[a.area + 1]
      ax, ay, az = ar.x + ar.w / 2, fx(0), ar.z + ar.h / 2
    elseif unit and core.alive_entity(unit) then
      local x, y, z = core.pos(unit)
      if x then ax, ay, az = x, y, z end
    end
    local k = a.kind
    if k == "spawn" then
      if a.type then
        local owner = players[1] or 0
        local made = {}
        local structure = TYPES.structure[a.type + 1]
        for n = 0, a.count - 1 do
          local px = ax + fx(n % 4) * SEVEN_TENTHS
          local pz = az + fx(n // 4) * SEVEN_TENTHS
          local e
          if structure then e = core.spawn(a.type, owner, px, ay, pz)
          else e = core.spawn_on_cell(a.type, owner, px, ay, pz, 6) end
          if e then made[#made + 1] = e end
        end
        if a.attack_move then
          for _, n in ipairs(made) do
            if core.has_orders(n) then
              local nx, ny, nz = core.pos(n)
              local bx, _, bz = core.nearest_enemy_structure(owner, nx, ny, nz)
              if bx then core.order(n, core.ORDER_ATTACK_MOVE, bx, bz, 4) end
            end
          end
        end
      end
    elseif k == "give" then
      for _, p in ipairs(players) do core.add_resource(p, a.index, a.amount) end
    elseif k == "take" then
      local slots = core.player_slots()
      for _, p in ipairs(players) do
        if p < slots then core.stock(p, a.index, fx.max(fx(0), core.stock(p, a.index) - a.amount)) end
      end
    elseif k == "setOwner" then
      local to = players[1] or player
      local ts = a.triggering_unit and targets() or core.trig_owner_targets(di, ai)
      for _, e in ipairs(ts) do core.set_owner(e, to) end
    elseif k == "kill" then
      for _, e in ipairs(targets()) do core.hp(e, 0) end
    elseif k == "remove" then
      for _, e in ipairs(targets()) do core.hp(e, 0); core.add_unit_flags(e, core.UNIT_SILENT) end
    elseif k == "damage" then
      for _, e in ipairs(targets()) do core.deal_damage(e, a.amount, 0, nil, 0) end
    elseif k == "heal" then
      for _, e in ipairs(targets()) do core.hp(e, fx.min(core.stat(e, core.STAT_MAX_HP), core.hp(e) + a.amount)) end
    elseif k == "buff" then
      local untilTick = a.duration > 0 and game.tick() + secondsToTicks(a.duration) or 0xFFFFFFFF
      for _, e in ipairs(targets()) do core.add_buff(e, a.stat, a.add, a.mul, untilTick) end
    elseif k == "message" then
      local idx = core.message_index(a.text)
      if a.player.rel == 1 or #players == 0 then core.emit(core.EV_MESSAGE, 0, unit, nil, idx, ax, ay, az)
      else for _, p in ipairs(players) do core.emit(core.EV_MESSAGE, p, unit, nil, idx, ax, ay, az) end end
    elseif k == "setVariable" then
      core.var(a.name, a.amount)
    elseif k == "addVariable" then
      core.add_var(a.name, a.amount)
    elseif k == "research" then
      for _, p in ipairs(players) do core.research(p, a.index) end
    elseif k == "enableType" or k == "disableType" then
      if a.type then for _, p in ipairs(players) do core.set_type_disabled(p, a.type, k == "disableType") end end
    elseif k == "victory" then
      if players[1] then core.set_winner(core.team_of(players[1])) end
    elseif k == "defeat" then
      for _, p in ipairs(players) do if core.alive(p) then core.defeat(p, 0) end end
    elseif k == "enableTrigger" or k == "disableTrigger" then
      if a.trigger >= 0 then core.trig_enabled(a.trigger + 1, k == "enableTrigger") end
    elseif k == "addScore" then
      for _, p in ipairs(players) do core.add_score(core.team_of(p), a.amount) end
    elseif k == "setRegionOwner" then
      core.set_region_owner_by_name(a.name, players[1] or 0)
    elseif k == "attackMove" or k == "move" then
      local kind = k == "attackMove" and core.ORDER_ATTACK_MOVE or core.ORDER_MOVE
      for _, e in ipairs(targets()) do
        if core.has_orders(e) then core.order(e, kind, ax, az, 8) end
      end
    elseif k == "setTickets" then
      for _, p in ipairs(players) do core.set_tickets(core.team_of(p), a.amount) end
    elseif k == "castAbility" then
      for _, p in ipairs(players) do core.cast_player_ability(p, a.index, ax, ay, az, unit, a.attack_move) end
    elseif k == "objective" or k == "dialog" or k == "camera" or k == "endMission" then
      core.trig_campaign(di, ai, player, unit, ax, ay, az)   -- campaigns (RtsCampaign.cpp)
    elseif k == "script" then
      core.trig_script_action(di, ai, player, unit, ax, ay, az)
    elseif k == "setResource" then   -- Bunker Wars bot triggers (agent-bw-ai)
      for _, p in ipairs(players) do core.stock(p, a.index, a.amount) end
    elseif k == "resetCooldowns" then
      for _, p in ipairs(players) do core.reset_cooldowns(p) end
    elseif k == "setProduction" then
      for _, e in ipairs(targets()) do core.set_production(e, a.amount) end
    elseif k == "timeScale" then
      core.var("timeScale", a.amount)
    end
  end
end

-- ---------------------------------------------------------------- firing
local function fire(di, player, unit, x, y, z)
  local d = DEFS[di]
  if not core.trig_enabled(di) then return end
  if d.once and core.trig_fired(di, player) then return end
  if not conditionsHold(di, d, player) then return end
  if d.once then core.trig_fired(di, player, true) end
  core.emit(core.EV_TRIGGER_FIRED, player, unit, nil, di - 1, x, y, z)   -- presentation / tools only
  runActions(di, d, player, unit, x, y, z)
end

local ZERO = fx(0)
local function fireGlobal(di)
  if DEFS[di].for_each_player then
    for _, p in ipairs(core.players()) do fire(di, p.slot, nil, ZERO, ZERO, ZERO) end
  else
    fire(di, 0, nil, ZERO, ZERO, ZERO)
  end
end

local EVENT_KIND = {
  [core.EV_DIED] = "unitDied", [core.EV_BUILD_DONE] = "built", [core.EV_TRAIN_DONE] = "trained", [core.EV_RESEARCHED] = "researched",
  [core.EV_CAPTURED] = "captured", [core.EV_WAVE] = "wave", [core.EV_REGION_CLAIMED] = "regionClaimed",
  [core.EV_PLAYER_DEFEATED] = "playerDefeated", [core.EV_ABILITY] = "ability", [core.EV_LEVEL_UP] = "levelUp",
}
local UNIT_EVENT = { unitDied = true, built = true, trained = true }
local TIMED = { start = true, time = true, every = true }
local EDGE = { enterArea = true, leaveArea = true, resource = true, variable = true }

-- what this game's triggers need, decided once (constant data)
local hasTimed, hasEdge, eventMask = false, false, 0
for _, d in ipairs(DEFS) do
  if TIMED[d.event] then hasTimed = true end
  if EDGE[d.event] then hasEdge = true end
  for ty, kind in pairs(EVENT_KIND) do
    if d.event == kind then eventMask = eventMask | (1 << ty) end
  end
end
std.period.triggers = #DEFS > 0 and 1 or 0

function std.triggers(tick)
  core.trig_init()
  local rate = tickRate()

  -- time-based
  if hasTimed then
    local started = core.trig_started()
    for di, d in ipairs(DEFS) do
      if d.event == "start" and not started then fireGlobal(di) end
      if d.event == "time" and tick == fx.int(d.seconds * rate) then fireGlobal(di) end
      if d.event == "every" then
        local period = math.max(1, fx.int(d.seconds * rate))
        if tick > 0 and tick % period == 0 then fireGlobal(di) end
      end
    end
  end
  core.trig_started(true)

  -- simulation events of this tick (including the ones the actions above emitted)
  if eventMask ~= 0 then
    for _, ev in ipairs(core.trig_events(eventMask)) do
      local kind = EVENT_KIND[ev.type]
      for di, d in ipairs(DEFS) do
        if d.event == kind and core.trig_player_matches(di, ev.player, ev.player) then
          local skip = false
          if UNIT_EVENT[kind] and (ev.value < 0 or ev.value >= #TYPES.structure or not filterMatches(d.filter, ev.value)) then skip = true end
          if not skip and kind == "researched" and d.index >= 0 and ev.value ~= d.index then skip = true end
          if not skip and d.area >= 0 and not areaContains(AREAS[d.area + 1], ev.x, ev.z) then skip = true end
          if not skip then fire(di, ev.player == HOSTILE and 0 or ev.player, ev.a, ev.x, ev.y, ev.z) end
        end
      end
    end
  end

  -- area enter / leave and edge-triggered thresholds (every 5 ticks)
  if not hasEdge or tick % 5 ~= 0 then return end
  for di, d in ipairs(DEFS) do
    if d.event == "enterArea" then
      for _, en in ipairs(core.trig_area_update(di)) do
        local e, owner = en[1], en[2]
        local x, y, z = core.pos(e)
        fire(di, owner == HOSTILE and 0 or owner, e, x, y, z)
      end
    elseif d.event == "leaveArea" then
      for _, en in ipairs(core.trig_area_update(di)) do
        local e = en[1]
        if core.alive_entity(e) then
          local owner = core.owner(e)
          local x, y, z = core.pos(e)
          fire(di, owner == HOSTILE and 0 or owner, e, x, y, z)
        end
      end
    elseif d.event == "resource" then
      local slots = core.player_slots()
      for _, p in ipairs(core.players()) do
        if p.slot < slots and core.trig_player_matches(di, p.slot, p.slot) then
          local v = compare(core.stock(p.slot, d.index), d.cmp, d.value)
          if v and not core.trig_edge(di, p.slot) then fire(di, p.slot, nil, ZERO, ZERO, ZERO) end
          core.trig_edge(di, p.slot, v)
        end
      end
    elseif d.event == "variable" then
      local v = compare(core.var(d.name), d.cmp, d.value)
      if v and not core.trig_edge(di, 0) then fireGlobal(di) end
      core.trig_edge(di, 0, v)
    end
  end
end
