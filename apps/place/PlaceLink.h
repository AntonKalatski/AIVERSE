// A player's connection to a place server that survives drops (docs/replica.md § Place hosting): dials (SecureUdp
// with a ticket, or WebSocket / plain UDP with the PlaceGate hello), plays through a PlaceClient, and when the link
// dies (closed, rejected, or no snapshot for staleMs) dials again with exponential backoff. The server keeps the
// character for its reconnect window, so the new PlaceClient finds the same character where it was.
// App lifecycle like battles: suspend() on FocusLost (mobile), resume() on FocusGained reconnects at once after a
// long absence (the OS has probably killed the socket or the NAT mapping).
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "apps/place/PlaceAdmission.h"
#include "apps/place/PlaceClient.h"

namespace aiverse::place {

struct LinkConfig {
    std::function<std::unique_ptr<net::Conn>()> dial;   // a fresh connection per attempt (nullptr: try again later)
    bool gate = false;                                  // run the PlaceGate hello first (WebSocket, plain UDP)
    std::optional<net::ClientTicket> ticket;            // for the gate (a SecureUdp dial carries its own)
    std::string guest;                                  // guest name for keyless dev servers
    replica::ClientOptions client = defaultClientOptions();
    uint32_t unstableMs = 1000;        // ready, no snapshot this long: HUD "connection unstable"
    uint32_t staleMs = 4000;           // ...this long: the link is dead, dial again
    uint32_t attemptTimeoutMs = 12000; // an attempt that is not playing by then is abandoned
    uint32_t backoffMs = 500, backoffMaxMs = 8000;   // doubled per failed attempt, +-25 % jitter
    uint32_t resumeReconnectMs = 3000; // resume() after a longer suspend dials at once
    uint64_t seed = 1;                 // backoff jitter
};

// Builds a config for an address: "ws://..." / "wss://..." (gate), "udp://host:port" or "host:port" (SecureUdp with
// a ticket; plain UDP + gate when `insecure`). With a ticket and no address, the ticket's server is used.
// Throws std::runtime_error with a readable message.
LinkConfig linkConfig(const std::string& address, const std::optional<net::ClientTicket>& ticket, const std::string& guest, bool insecure);

enum class LinkStatus { Connecting, Playing, Unstable, Reconnecting, Suspended, Rejected, Replaced };
const char* linkStatusKey(LinkStatus s);   // i18n key: place.link.*

class PlaceLink {
public:
    explicit PlaceLink(LinkConfig config);
    ~PlaceLink();
    void poll(uint64_t nowMs);                        // network only
    void update(uint64_t nowMs, const Input& input);  // one fixed tick: poll + send + predict (not while suspended)
    void suspend(uint64_t nowMs);
    void resume(uint64_t nowMs);
    void drop(uint64_t nowMs);                        // close the link now and reconnect (tests, the bots' --drop)

    PlaceClient* client() { return playing() ? client_.get() : nullptr; }   // null while (re)connecting
    // What to draw: the live client, else the last one that played (frozen world under a "reconnecting" HUD).
    PlaceClient* view() { return playing() ? client_.get() : stale_.get(); }
    bool playing() const { return client_ && client_->ready(); }
    LinkStatus status() const { return status_; }
    GateReject rejectReason() const { return reject_; }
    uint32_t attempts() const { return attempts_; }     // dials so far
    uint32_t reconnects() const { return reconnects_; } // sessions that came back after a loss
    uint64_t retryAt() const { return retryAt_; }
    bool dialing() const { return gate_ || client_; }  // an attempt is in progress (else waiting for retryAt)
    bool resumedCharacter() const { return resumed_; }  // the server said this session took over a kept character
    // Best application ping of the last 5 s (the closest to network RTT: one ping also waits for the server tick).
    uint32_t bestPingMs() const;

private:
    void pump(uint64_t now);
    void dialNow(uint64_t now);
    void lost(uint64_t now, LinkStatus next);
    void hook();

    LinkConfig config_;
    std::unique_ptr<GateClient> gate_;
    std::unique_ptr<PlaceClient> client_, stale_;
    net::Conn* conn_ = nullptr;   // owned by client_ / gate_
    LinkStatus status_ = LinkStatus::Connecting;
    GateReject reject_ = GateReject::None;
    bool everPlayed_ = false, wasPlaying_ = false, resumed_ = false, replaced_ = false;
    uint64_t attemptAt_ = 0, lastSnapshotAt_ = 0, lastSnapshots_ = 0, retryAt_ = 0, suspendedAt_ = 0;
    uint32_t backoff_ = 0, attempts_ = 0, reconnects_ = 0;
    uint64_t rng_ = 1;
    uint32_t pings_[5] = {}, pingIndex_ = 0;
    uint64_t lastPingSample_ = 0;
};
}
