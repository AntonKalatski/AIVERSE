// backend.available()                        game.json has a "backend" block this build supports
// backend.login({guid=, duid=, auth=}, fn(ok, data))   entry point + auth (guest unless `auth` is a BW AuthTO)
// backend.request(service, action, args, fn(ok, data, error))   any route of engine/meta/bw/BwRoutes.cpp + chat.*
// backend.view(fn(ok, view)) / backend.cached()                  meta view projection (profile, balances, heroes...)
// backend.match(params) / backend.spectate(roomId[, custom]) / backend.cancel()
//     matchmaker TCP session; events game.on("backend", e): e.event = "match" {kind, func, data},
//     "handoff" {address, queue, spectator} (the client joins the engine battle by itself)
// backend.chat() -> chat state (rooms, members, notifications); backend.connected() -> {meta, match, chat}
// backend.tutorial(window, event) / backend.tutorialClick() / backend.tutorialButton(tag) / backend.tutorialSaved()
//     meta tutorials (ui/bw_tutorials.json): events "tutorial" {kind = show | hide | lock | action, tag, action, window}
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <chrono>
#include <cstdio>

#include "ClientMeta.h"
#include "engine/meta/Client.h"
#include "engine/meta/Protocol.h"
#include "engine/meta/bw/BwApi.h"
#include "engine/meta/bw/BwBackend.h"
#include "engine/meta/bw/BwPlatformHttp.h"
#include "engine/meta/bw/BwTutorials.h"
#ifndef __EMSCRIPTEN__
#include "engine/meta/bw/BwTcp.h"
#endif
#include "engine/platform/Platform.h"
#include "engine/ui/UiLua.h"

namespace aiverse::client {

using namespace aiverse::meta;

namespace {
uint64_t nowMs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
int64_t unixSec() { return int64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }
} // namespace

struct ClientMeta::Impl {
    lua_State* L = nullptr;
    std::shared_ptr<bool> alive;
    Json config;
    std::function<void(const char*, const Json&)> emit;
    std::function<void(const std::string&, const std::string&, bool)> joinBattle;
    std::unique_ptr<bw::ApiClient> api;
    std::unique_ptr<bw::BwBackend> backend;
    bw::MetaTutorials tutorials;
    bool tutorialsLoaded = false;
#ifndef __EMSCRIPTEN__
    std::unique_ptr<bw::MatchConnection> match;
    std::unique_ptr<bw::ChatConnection> chat;
#endif
    uint64_t op = 0;
    std::string token;

    struct Cb {
        std::shared_ptr<bool> alive;
        lua_State* L = nullptr;
        int ref = LUA_NOREF;
        void call(std::initializer_list<Json> args) {
            if (!*alive || ref == LUA_NOREF) return;
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            ref = LUA_NOREF;
            for (auto& a : args) ui::luaPushJson(L, a);
            if (lua_pcall(L, int(args.size()), 0, 0) != LUA_OK) {
                std::fprintf(stderr, "[lua] backend: %s\n", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
    };
    std::shared_ptr<Cb> keep(int idx) {
        auto c = std::make_shared<Cb>();
        c->alive = alive;
        c->L = L;
        if (lua_isfunction(L, idx)) { lua_pushvalue(L, idx); c->ref = luaL_ref(L, LUA_REGISTRYINDEX); }
        return c;
    }
    void event(const char* name, Json data) {
        if (*alive && emit) {
            Json e = std::move(data);
            if (!e.isObject()) e = Json::object();
            e.set("event", Json::string(name));
            emit("backend", e);
        }
    }
    std::string nextOperation() { return "ui-" + std::to_string(unixSec()) + "-" + std::to_string(++op); }
    bool supported() const { return config.getStr("type") == "bunkerwars"; }
    void ensureBackend() {
        if (api || !supported()) return;
        std::vector<std::string> hosts;
        for (auto& h : config["hosts"].items()) hosts.push_back(h.str());
        api = std::make_unique<bw::ApiClient>(bw::platformTransport(), hosts);
        backend = std::make_unique<bw::BwBackend>(*api, config.getInt("profileMask", 0));
        api->onNotAuthorized = [this](const bw::ApiResult&) { event("notAuthorized", Json::object()); };
        std::string text;
        const std::string tpath = config.getStr("tutorials", "ui/bw_tutorials.json");
        if (platform::readContentFile(tpath, text)) {
            try {
                tutorialsLoaded = tutorials.load(Json::parse(text));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "backend: %s: %s\n", tpath.c_str(), e.what());
            }
        }
        tutorials.setClientVersion(config.getStr("client"));
        tutorials.host.showHide = [this](const std::string& tag, bool show, const Json& a) { tutorialEvent(show ? "show" : "hide", tag, a, {}); };
        tutorials.host.lockElement = [this](const std::string& tag, const Json& a) { tutorialEvent("lock", tag, a, {}); };
        tutorials.host.action = [this](const Json& a, const std::string& w) { tutorialEvent("action", a.getStr("tutorialTag"), a, w); };
        tutorials.host.keyChanged = [this](const std::string& key, int64_t value, bool save) {
            // BW binData keys: persisted with profile.setValue (ServerValue) when the tutorial asks to save
            Json args = Json::object();
            args.set("key", Json::string(key));
            args.set("value", Json::string(std::to_string(value)));
            if (!save) return;
            backend->request({"profile", "setValue", nextOperation(), args}, [this](ServiceReply) { tutorials.keySaved(); });
        };
    }
    void tutorialEvent(const char* kind, const std::string& tag, const Json& action, const std::string& window) {
        Json e = Json::object();
        e.set("kind", Json::string(kind));
        e.set("tag", Json::string(tag));
        e.set("action", action);
        e.set("window", Json::string(window));
        event("tutorial", e);
    }
    void startChat() {
#ifndef __EMSCRIPTEN__
        auto hosts = bw::parseHostPorts(api->endpoints().chat);
        if (hosts.empty() || token.empty()) return;
        chat = std::make_unique<bw::ChatConnection>(hosts, token);
        bw::bindChat(*backend, *chat);
        auto inner = chat->onPacket;
        chat->onPacket = [this, inner](const bw::ChatPacket& p) {
            inner(p);
            Json e = Json::object();
            e.set("type", Json::number(int64_t(p.type)));
            try { e.set("data", Json::parse(p.json)); } catch (...) { e.set("data", Json::string(p.json)); }
            event("chat", e);
        };
        chat->start(nowMs());
#endif
    }
};

namespace {
ClientMeta::Impl& self(lua_State* L) { return *static_cast<ClientMeta::Impl*>(lua_touserdata(L, lua_upvalueindex(1))); }

int lAvailable(lua_State* L) { lua_pushboolean(L, self(L).supported()); return 1; }

int lLogin(lua_State* L) {
    auto& m = self(L);
    Json opts = lua_istable(L, 1) ? ui::luaToJson(L, 1) : Json::object();
    auto cb = m.keep(2);
    m.ensureBackend();
    if (!m.api) { cb->call({Json::fromBool(false), Json::string("backend not configured")}); return 0; }
    bw::ClientInfo info;
    info.client = m.config.getStr("client", info.client);
    info.platform = platform::name();
    m.api->entryPoint(info, [&m, opts, info, cb](bw::ApiResult r) {
        if (!r.ok() && !r.transport) { cb->call({Json::fromBool(false), Json::string("entryPoint " + std::to_string(r.error.code))}); return; }
        Json auth = opts["auth"].isObject() ? opts["auth"] : bw::ApiClient::guestAuth(info, opts.getStr("guid"), opts.getStr("duid", opts.getStr("guid")));
        m.api->auth(auth, [&m, cb](bw::ApiResult a) {
            if (!a.ok()) { cb->call({Json::fromBool(false), Json::string("auth " + std::to_string(a.error.code) + " " + a.error.key)}); return; }
            m.token = m.api->token();
            m.startChat();
            cb->call({Json::fromBool(true), a.data});
        });
    });
    return 0;
}

int lRequest(lua_State* L) {
    auto& m = self(L);
    ServiceRequest r;
    r.service = luaL_checkstring(L, 1);
    r.action = luaL_checkstring(L, 2);
    r.arguments = lua_istable(L, 3) ? ui::luaToJson(L, 3) : Json::object();
    if (!r.arguments.isObject()) r.arguments = Json::object();
    r.operation = m.nextOperation();
    auto cb = m.keep(4);
    m.ensureBackend();
    if (!m.backend) { cb->call({Json::fromBool(false), Json::object(), Json::string("backend not configured")}); return 0; }
    m.backend->request(std::move(r), [&m, cb](ServiceReply reply) {
        const bool ok = bool(reply.result);
        if (ok && reply.data.has("profile") && reply.data.getStr("source") == "bunkerwars") m.tutorials.setProfile(reply.data["raw"]);
        cb->call({Json::fromBool(ok), reply.data, Json::string(reply.result.detail)});
    });
    return 0;
}

int lView(lua_State* L) {
    auto& m = self(L);
    auto cb = m.keep(1);
    m.ensureBackend();
    if (!m.backend) { cb->call({Json::fromBool(false), Json::object()}); return 0; }
    m.backend->request(requests::view(), [&m, cb](ServiceReply reply) {
        if (reply.result) { m.tutorials.setProfile(reply.data["raw"]); m.tutorials.setNow(unixSec()); }
        cb->call({Json::fromBool(bool(reply.result)), reply.data});
    });
    return 0;
}

int lCached(lua_State* L) { auto& m = self(L); ui::luaPushJson(L, m.backend ? m.backend->lastView() : Json::object()); return 1; }
int lChat(lua_State* L) { auto& m = self(L); ui::luaPushJson(L, m.backend ? m.backend->chat() : Json::object()); return 1; }

int lMatch(lua_State* L) {
    auto& m = self(L);
#ifndef __EMSCRIPTEN__
    m.ensureBackend();
    auto hosts = m.api ? bw::parseHostPorts(m.api->endpoints().game) : std::vector<bw::HostPort>{};
    if (hosts.empty() || m.token.empty()) { lua_pushboolean(L, 0); return 1; }
    Json p = lua_istable(L, 1) ? ui::luaToJson(L, 1) : Json::object();
    bw::MatchConnectionConfig cfg;
    cfg.hosts = hosts;
    cfg.token = m.token;
    m.match = std::make_unique<bw::MatchConnection>(cfg);
    bw::MatchParams mp;
    mp.mapId = int(p.getInt("mapId"));
    mp.roomId = p.getInt("roomId");
    mp.matcher = int(p.getInt("matcher"));
    mp.mode = int(p.getInt("mode", 1));
    mp.winMode = int(p.getInt("winMode", 1));
    mp.conditions = int(p.getInt("conditions"));
    mp.withBotOnly = p.getBool("withBotOnly");
    mp.group = p.getStr("group");
    mp.specialParameters = p.getStr("specialParameters");
    m.match->start(nowMs());
    if (p.has("spectate")) m.match->session().spectate(p.getInt("spectate"), p.getBool("custom"));
    else m.match->session().match(mp);
    lua_pushboolean(L, 1);
#else
    lua_pushboolean(L, 0);   // browsers cannot open the matchmaker's raw TCP (docs/meta-bunkerwars.md § Web)
#endif
    return 1;
}

int lSpectate(lua_State* L) {
    Json p = Json::object();
    p.set("spectate", Json::number(int64_t(luaL_checkinteger(L, 1))));
    p.set("custom", Json::fromBool(lua_toboolean(L, 2)));
    lua_settop(L, 0);
    ui::luaPushJson(L, p);
    return lMatch(L);
}

int lCancel(lua_State* L) {
#ifndef __EMSCRIPTEN__
    auto& m = self(L);
    if (m.match) m.match->stop();
    m.match.reset();
#else
    (void)L;
#endif
    return 0;
}

int lConnected(lua_State* L) {
    auto& m = self(L);
    Json j = Json::object();
    j.set("meta", Json::fromBool(!m.token.empty()));
#ifndef __EMSCRIPTEN__
    j.set("match", Json::fromBool(m.match && m.match->connected()));
    j.set("chat", Json::fromBool(m.chat && m.chat->connected()));
#endif
    ui::luaPushJson(L, j);
    return 1;
}

int lTutorial(lua_State* L) {
    auto& m = self(L);
    m.ensureBackend();
    const std::string window = luaL_checkstring(L, 1);
    const std::string ev = luaL_optstring(L, 2, "active");
    bw::TutorialEvent e = ev == "init" ? bw::TutorialEvent::OnInitialize : ev == "showBegin" ? bw::TutorialEvent::OnShowBegin
                        : ev == "showEnd" ? bw::TutorialEvent::OnShowEnd : ev == "inactive" ? bw::TutorialEvent::OnWindowInactive
                        : bw::TutorialEvent::OnWindowActive;
    m.tutorials.setNow(unixSec());
    lua_pushinteger(L, m.tutorialsLoaded ? m.tutorials.windowEvent(window, e) : 0);
    return 1;
}
int lTutorialClick(lua_State* L) { self(L).tutorials.click(); return 0; }
int lTutorialButton(lua_State* L) { self(L).tutorials.buttonClick(luaL_checkstring(L, 1)); return 0; }
int lTutorialRun(lua_State* L) { lua_pushboolean(L, self(L).tutorials.run(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""))); return 1; }
int lTutorialKeys(lua_State* L) {
    auto& m = self(L);
    if (lua_istable(L, 1)) m.tutorials.setKeys(ui::luaToJson(L, 1));
    Json k = Json::object();
    for (auto& [key, v] : m.tutorials.keys()) k.set(key, Json::number(v));
    ui::luaPushJson(L, k);
    return 1;
}
} // namespace

ClientMeta::ClientMeta() = default;
ClientMeta::~ClientMeta() { detach(); }

void ClientMeta::attach(lua_State* L, std::shared_ptr<bool> alive, const Json& config, std::function<void(const char*, const Json&)> emit,
                        std::function<void(const std::string&, const std::string&, bool)> joinBattle) {
    detach();
    impl_ = std::make_unique<Impl>();
    impl_->L = L;
    impl_->alive = std::move(alive);
    impl_->config = config;
    if (!impl_->config.isObject()) {   // the game's ui/backend.json when game.json has no "backend"
        std::string text;
        if (platform::readContentFile("ui/backend.json", text)) {
            try { impl_->config = Json::parse(text); } catch (const std::exception& e) { std::fprintf(stderr, "backend: ui/backend.json: %s\n", e.what()); }
        }
    }
    impl_->emit = std::move(emit);
    impl_->joinBattle = std::move(joinBattle);
    lua_newtable(L);
    const luaL_Reg fns[] = {{"available", lAvailable}, {"login", lLogin}, {"request", lRequest}, {"view", lView}, {"cached", lCached},
                            {"chat", lChat}, {"match", lMatch}, {"spectate", lSpectate}, {"cancel", lCancel}, {"connected", lConnected},
                            {"tutorial", lTutorial}, {"tutorialClick", lTutorialClick}, {"tutorialButton", lTutorialButton},
                            {"tutorialRun", lTutorialRun}, {"tutorialKeys", lTutorialKeys}};
    for (auto& f : fns) {
        lua_pushlightuserdata(L, impl_.get());
        lua_pushcclosure(L, f.func, 1);
        lua_setfield(L, -2, f.name);
    }
    lua_setglobal(L, "backend");
}

void ClientMeta::detach() { impl_.reset(); }

void ClientMeta::update(double dt) {
    if (!impl_) return;
    Impl& m = *impl_;
    if (m.api) m.api->pump();
    m.tutorials.update(dt);
#ifndef __EMSCRIPTEN__
    const uint64_t now = nowMs();
    if (m.chat) m.chat->update(now);
    if (m.match) {
        m.match->update(now);
        for (auto& e : m.match->session().takeEvents()) {
            Json j = Json::object();
            j.set("kind", Json::number(int64_t(e.kind)));
            j.set("func", Json::string(e.frame.func));
            j.set("data", e.json);
            m.event("match", j);
            using K = bw::MatchSession::Event::Kind;
            if ((e.kind == K::Ready || e.kind == K::SpectateJoin || e.kind == K::SyncComplete) && m.match->session().handoff()) {
                const auto& h = *m.match->session().handoff();
                if (!h.hasNode()) continue;   // the Java server without battle nodes: nothing to join
                Json hj = Json::object();
                hj.set("address", Json::string(h.address()));
                hj.set("queue", Json::string(h.queueId()));
                hj.set("spectator", Json::fromBool(h.spectator));
                hj.set("roomId", Json::number(h.roomId));
                hj.set("mapId", Json::number(int64_t(h.mapId)));
                m.event("handoff", hj);
                if (m.joinBattle) m.joinBattle(h.address(), h.queueId(), h.spectator);
                m.match->reconnect(h.roomId, 0);
            }
        }
    }
#endif
}

} // namespace aiverse::client
