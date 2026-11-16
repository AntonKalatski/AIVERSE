#include "apps/place/PlaceLink.h"
#include <algorithm>
#include <stdexcept>
#include "engine/net/SecureUdp.h"

namespace aiverse::place {

LinkConfig linkConfig(const std::string& address, const std::optional<net::ClientTicket>& ticket, const std::string& guest, bool insecure) {
    LinkConfig c;
    c.guest = guest;
    std::string addr = address.empty() && ticket ? ticket->server : address;
    if (addr.empty()) throw std::runtime_error("no server address (--connect, or a ticket issued with --server)");
    if (addr.rfind("ws://", 0) == 0 || addr.rfind("wss://", 0) == 0) {
        c.gate = true;
        c.ticket = ticket;
        c.dial = [addr] { return net::connectWebSocket(addr); };
        return c;
    }
    if (addr.rfind("udp://", 0) == 0) addr = addr.substr(6);
    if (insecure) {
        c.gate = true;
        c.ticket = ticket;
        c.dial = [addr] { return net::connectTo("udp://" + addr); };
        return c;
    }
    if (!ticket) throw std::runtime_error("encrypted UDP needs a ticket (--ticket / --ticket-file); for a keyless dev server (serve --insecure) pass --insecure");
    net::ClientTicket t = *ticket;
    t.server = addr;
    c.dial = [t] { return net::connectSecure(t); };
    return c;
}

const char* linkStatusKey(LinkStatus s) {
    switch (s) {
    case LinkStatus::Connecting: return "place.link.connecting";
    case LinkStatus::Playing: return "place.link.playing";
    case LinkStatus::Unstable: return "place.link.unstable";
    case LinkStatus::Reconnecting: return "place.link.reconnecting";
    case LinkStatus::Suspended: return "place.link.suspended";
    case LinkStatus::Rejected: return "place.link.rejected";
    case LinkStatus::Replaced: return "place.link.replaced";
    }
    return "place.link.connecting";
}

PlaceLink::PlaceLink(LinkConfig config) : config_(std::move(config)), rng_(config_.seed * 6364136223846793005ull + 1442695040888963407ull) {}
PlaceLink::~PlaceLink() = default;

void PlaceLink::hook() {
    // Chain the place notices in front of PlaceClient's own message handler.
    auto& r = client_->replica();
    auto previous = r.onMessage;
    r.onMessage = [this, previous](uint16_t channel, const net::Bytes& payload) {
        if (channel == kChannelNotice) {
            if (payload.size() == 1 && payload[0] == kNoticeReplaced) replaced_ = true;
            if (payload.size() == 1 && payload[0] == kNoticeResumed) resumed_ = true;
            return;
        }
        if (previous) previous(channel, payload);
    };
}

void PlaceLink::dialNow(uint64_t now) {
    ++attempts_;
    attemptAt_ = now;
    lastSnapshots_ = 0;
    lastSnapshotAt_ = now;
    replaced_ = resumed_ = false;
    auto conn = config_.dial ? config_.dial() : nullptr;
    if (!conn) { lost(now, status_); return; }
    conn_ = conn.get();
    if (config_.gate) gate_ = std::make_unique<GateClient>(std::move(conn), config_.ticket, config_.guest);
    else { client_ = std::make_unique<PlaceClient>(std::move(conn), config_.client); hook(); }
}

void PlaceLink::lost(uint64_t now, LinkStatus next) {
    if (client_ && wasPlaying_) {   // keep the last world for drawing; the connection goes now
        if (conn_) conn_->close();
        stale_ = std::move(client_);
    }
    client_.reset();   // closes the connection
    gate_.reset();
    conn_ = nullptr;
    wasPlaying_ = false;
    status_ = next;
    if (next == LinkStatus::Rejected || next == LinkStatus::Replaced) return;
    backoff_ = backoff_ ? std::min(backoff_ * 2, config_.backoffMaxMs) : config_.backoffMs;
    rng_ = rng_ * 6364136223846793005ull + 1442695040888963407ull;
    double jitter = 0.75 + 0.5 * double(rng_ >> 40) / double(1ull << 24);   // spread a crowd of reconnects
    retryAt_ = now + uint64_t(double(backoff_) * jitter);
}

void PlaceLink::drop(uint64_t now) {
    if (status_ == LinkStatus::Rejected || status_ == LinkStatus::Replaced) return;
    backoff_ = 0;
    lost(now, everPlayed_ ? LinkStatus::Reconnecting : LinkStatus::Connecting);
}

void PlaceLink::pump(uint64_t now) {
    if (status_ == LinkStatus::Rejected || status_ == LinkStatus::Replaced || suspendedAt_) return;
    const LinkStatus again = everPlayed_ ? LinkStatus::Reconnecting : LinkStatus::Connecting;
    if (!gate_ && !client_) {
        if (now < retryAt_) return;
        dialNow(now);
    }
    if (gate_) {
        auto st = gate_->poll(now);
        if (st == GateClient::State::Admitted) {
            client_ = std::make_unique<PlaceClient>(gate_->take(), config_.client);
            gate_.reset();
            hook();
        } else if (st == GateClient::State::Rejected) {
            reject_ = gate_->reason();
            lost(now, gateRejectFinal(reject_) ? LinkStatus::Rejected : again);
            return;
        } else if (now - attemptAt_ > config_.attemptTimeoutMs) {
            lost(now, again);
            return;
        }
    }
    if (!client_) return;
    if (replaced_) { lost(now, LinkStatus::Replaced); return; }
    uint64_t snaps = client_->replica().stats().snapshots;
    if (snaps != lastSnapshots_) { lastSnapshots_ = snaps; lastSnapshotAt_ = now; }
    if (client_->ready() && !wasPlaying_) {
        wasPlaying_ = true;
        if (everPlayed_) ++reconnects_;
        everPlayed_ = true;
        backoff_ = 0;
        stale_.reset();
        lastSnapshotAt_ = now;
    }
    const bool closed = !client_->connected() && (wasPlaying_ || client_->replica().slot());   // closed, rejected or kicked
    if (closed || (wasPlaying_ && now - lastSnapshotAt_ > config_.staleMs) || (!wasPlaying_ && now - attemptAt_ > config_.attemptTimeoutMs)) {
        lost(now, everPlayed_ ? LinkStatus::Reconnecting : LinkStatus::Connecting);
        return;
    }
    if (wasPlaying_) status_ = now - lastSnapshotAt_ > config_.unstableMs ? LinkStatus::Unstable : LinkStatus::Playing;
    else status_ = again;
    if (wasPlaying_ && now - lastPingSample_ >= 1000) {
        lastPingSample_ = now;
        pings_[pingIndex_++ % 5] = client_->stats().rttMs;
    }
}

void PlaceLink::poll(uint64_t now) {
    pump(now);
    if (client_ && !suspendedAt_) client_->poll(now);
}

void PlaceLink::update(uint64_t now, const Input& input) {
    pump(now);
    if (client_ && !suspendedAt_) client_->update(now, input);
}

void PlaceLink::suspend(uint64_t now) {
    if (suspendedAt_ || status_ == LinkStatus::Rejected || status_ == LinkStatus::Replaced) return;
    suspendedAt_ = now ? now : 1;
    status_ = LinkStatus::Suspended;
}

void PlaceLink::resume(uint64_t now) {
    if (!suspendedAt_) return;
    const uint64_t away = now - std::min(now, suspendedAt_);
    suspendedAt_ = 0;
    status_ = everPlayed_ ? LinkStatus::Reconnecting : LinkStatus::Connecting;
    if (away > config_.resumeReconnectMs) {
        // The OS has probably killed the socket or the NAT mapping: do not wait for the stale timeout.
        backoff_ = 0;
        lost(now, status_);
        retryAt_ = now;
    } else {
        lastSnapshotAt_ = now;   // the silence while away is not the link's fault
    }
}

uint32_t PlaceLink::bestPingMs() const {
    uint32_t best = 0;
    for (uint32_t p : pings_) if (p && (!best || p < best)) best = p;
    return best;
}
}
