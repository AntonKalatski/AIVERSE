-- Bunker Wars meta screens over the `backend` Lua table (apps/client/ClientMeta.cpp, agent-bw-meta;
-- docs/meta-bunkerwars.md § Client screens). Player state lives on the BW servers: every screen reads
-- the meta view (backend.view) and sends BW requests (backend.request). Meta tutorials of the BW
-- client (ui/bw_tutorials.json) run on screen events and draw hints through bw.tutorial.
local lang = game.json("ui/bw_meta_lang.json") or {}
local function T(k)
    local t = lang[game.language()] or lang.en or {}
    return t[k] or (lang.en or {})[k] or k
end

bw = {}
local state = {t = {}, status = "", name = "", league = 0, rating = 0, gold = 0, gems = 0, heroes = {}, heroId = 0,
               offers = {}, shop = {}, clan = {}, clans = {}, inClan = false, queue = {status = "", players = 0},
               result = {}, tutorial = {visible = false, text = "", tag = ""}}
local function refresh()
    local t = lang[game.language()] or lang.en or {}
    for k, _ in pairs(lang.en or {}) do state.t[k] = t[k] or lang.en[k] end
    ui.set('bw', state)
end
bw.T = T

-- BW window classes (TutorialData.forObject) of each screen and the components it shows
local windows = {
    bw_home = {"MW2.UI.Mobile.MainScreen.MainScreen", "PlayComponents", "HeroAnimationComponent", "HeroComponents"},
    bw_heroes = {"HeroMainComponent", "MW2.UI.Mobile.Craft.HeroInventory.HeroInventoryScreen"},
    bw_shop = {"MW2.UI.Mobile.Shop.ShopScreen"},
    bw_clan = {"MW2.UI.Mobile.Clan.ClanScreen"},
    bw_results = {"CompletedMatchComponent"},
}

local function guest()
    local s = storage.load('bw_guest') or {}
    if not s.guid then
        s.guid = string.format("aiverse-%08x%06x", os.time(), math.random(0, 0xffffff))
        storage.save('bw_guest', s)
    end
    return s.guid
end

local function apply(v)
    local p = v.profile or {}
    local a, b = p.attributes or {}, p.balances or {}
    state.name = a.name or ""
    state.league, state.rating = a.leagueIndex or 0, a.leagueRating or 0
    state.gold, state.gems = b.gold or 0, b.gems or 0
    state.heroId = a.heroClientId or 0
    state.heroes = {}
    for _, h in ipairs(v.heroes or {}) do
        table.insert(state.heroes, {id = h.clientId, level = h.level or 0, selected = h.clientId == state.heroId})
    end
    state.offers = {}
    for _, o in ipairs(v.offers or {}) do table.insert(state.offers, {id = tostring(o.id or ""), price = tostring(o.price or "")}) end
    state.clan = v.clan or {}
    state.inClan = (state.clan.clanId or 0) ~= 0
end

function bw.view(done)
    backend.view(function(ok, v)
        if ok then apply(v) end
        refresh()
        if done then done(ok) end
    end)
end

function bw.login()
    state.status = T('connecting'); refresh()
    backend.login({guid = guest()}, function(ok, data)
        if not ok then state.status = T('login_failed') .. " " .. tostring(data); refresh(); return end
        bw.view(function() ui.replace('bw_home') end)
    end)
end

local function request(service, action, args, after)
    backend.request(service, action, args or {}, function(ok, data, err)
        if not ok then game.notice(T('error') .. " " .. tostring(err)) end
        if after then after(ok, data) end
        if ok then bw.view() end   -- balances / heroes / clan changed on the server
    end)
end
bw.request = request

-- heroes
function bw.selectHero(id) request('heroes', 'save', {heroClientId = id}) end
function bw.buyHero(id) request('heroes', 'buy', {heroClientId = id}) end
-- shop: soft-currency market and offers (real-money receipts go through the store + purchase.receipt)
function bw.loadShop()
    backend.request('shop', 'get', {}, function(ok, d)
        state.shop = {}
        if ok then
            for _, it in ipairs(d.items or d.shop or {}) do table.insert(state.shop, {id = tostring(it.id or it.clientId or ""), price = tostring(it.price or "")}) end
        end
        refresh()
    end)
end
function bw.activateOffer(id) request('shop', 'offerActivate', {id = id}) end
-- clans
function bw.loadClans()
    backend.request('clan', 'list', {}, function(ok, d)
        state.clans = {}
        if ok then for _, c in ipairs(d.clans or d.list or {}) do table.insert(state.clans, {id = c.id or c.clanId, name = c.name or "", members = c.membersCount or c.members or 0}) end end
        refresh()
    end)
end
function bw.joinClan(id) request('clan', 'membershipRequest', {clanId = id}) end

-- matchmaking: BW matchmaker -> engine battle server (ClientMeta joins it on "handoff")
function bw.play(params)
    state.queue = {status = T('searching'), players = 0}; refresh()
    if not backend.match(params or {matcher = 0, mode = 1, winMode = 1}) then game.notice(T('error') .. " matchmaking") return end
    ui.open('bw_queue')
end
function bw.cancel() backend.cancel(); ui.back() end

game.on('backend', function(e)
    if e.event == 'match' then
        local d = e.data or {}
        if d.connectedPlayers then state.queue.players = #d.connectedPlayers end
        if e.func == 'Match.onReady' and not (d.battleTicket and d.battleHost) then state.queue.status = T('no_battle_node') end
        refresh()
    elseif e.event == 'handoff' then
        state.queue.status = T('battle_found'); refresh()
    elseif e.event == 'tutorial' then
        bw.tutorialEvent(e)
    elseif e.event == 'notAuthorized' then
        ui.replace('bw_login')
    end
end)

game.on('matchEnd', function(r)
    state.result = {won = r.won, draw = r.result == -1, seconds = math.floor(r.seconds or 0)}
    refresh()
    bw.view()
end)

-- meta tutorials
function bw.tutorialEvent(e)
    local doc = ui.document()
    local el = doc and e.tag ~= "" and doc:GetElementById(e.tag) or nil
    if e.kind == 'show' or e.kind == 'hide' then
        if el then el:SetClass('bw-hidden', e.kind == 'hide') end
    elseif e.kind == 'lock' then
        if el then el:SetClass('bw-tutorial-focus', true) end
        local key = (e.action.highlightKey or {}).key
        state.tutorial = {visible = true, tag = e.tag, text = (key and game.hasText(key) and game.tr(key)) or T('tap')}
        refresh()
    elseif e.kind == 'action' then
        local a = e.action or {}
        if a.type == 'ShowScreen' and a.screen then ui.open(a.screen) end
        if a.type == 'PushNotification' and push and push.available() then push.schedule({id = (a.data or {}).tag or "bw", title = state.t.title, body = "", delay = 3600}) end
        if a.type == 'UnlockAchievement' and social and social.available() then social.unlock(a.achievementId or "") end
    end
end
function bw.tap(tag)   -- buttons with a tutorial tag: WaitButtonClick
    if tag then backend.tutorialButton(tag) end
    if state.tutorial.visible then
        state.tutorial = {visible = false, text = "", tag = ""}
        local doc = ui.document()
        local el = doc and tag and doc:GetElementById(tag)
        if el then el:SetClass('bw-tutorial-focus', false) end
        refresh()
    end
    backend.tutorialClick()
end

game.on('open', function(e)
    for _, w in ipairs(windows[e.screen] or {e.screen}) do backend.tutorial(w, 'active') end
end)
game.on('close', function(e)
    for _, w in ipairs(windows[e.screen] or {e.screen}) do backend.tutorial(w, 'inactive') end
end)

refresh()
