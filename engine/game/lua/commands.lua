-- Command rules of the economy: "auto build" (rules "autoBuild": the BUILD button picks a structure
-- from the pool and the engine places it) and selling structures (rules "sellRefund").
-- Engine standard library: std.auto_build(tick, player, param) and std.sell(tick, player, unit) are
-- called from RtsModule::applyCommand. Mirror the former C++ code exactly (bit-identical matches).

local AB = core.auto_build_rules()
local SELL = core.sell_rules()
std.period.auto_build = 1
std.period.sell = 1

local function autoBuildCost(player, t)
  local base = AB.cost or core.type_cost(t)
  local built = core.auto_built(player)
  local cost = {}
  for i, c in ipairs(base) do cost[i] = { c[1], c[2] + c[2] * AB.growth * built } end
  return cost
end

-- param: a type of the pool, or < 0 = a random buildable one (simulation RNG)
function std.auto_build(tick, player, param)
  local t = nil
  if param >= 0 and param < #SELL.types then
    for _, p in ipairs(AB.pool) do if p == param then t = param end end
  else
    local options = {}
    for _, p in ipairs(AB.pool) do if core.can_train(player, p) then options[#options + 1] = p end end
    if #options > 0 then t = options[core.rng_below(#options) + 1] end
  end
  if not t or not core.can_train(player, t) then return end
  local x, y, z = core.find_auto_spot(player, t)
  if not x then core.emit(core.EV_NOT_ENOUGH, player, nil, nil, 254); return end
  if not core.pay(player, autoBuildCost(player, t)) then return end
  core.auto_built(player, core.auto_built(player) + 1)
  core.place_structure(player, t, x, y, z, false)
end

-- one structure of a sell command (ownership and liveness already checked by the command)
function std.sell(tick, player, unit)
  local t = core.unit_type(unit)
  local info = SELL.types[t + 1]
  if not info.sellable then return end
  core.refund(player, core.type_cost(t), SELL.refund * (core.hp(unit) / fx.max(info.max_hp, 1)))
  local x, y, z = core.pos(unit)
  core.emit(core.EV_SOLD, player, unit, nil, t, x, y, z)
  core.hp(unit, 0)
  core.add_unit_flags(unit, core.UNIT_SILENT)
end
