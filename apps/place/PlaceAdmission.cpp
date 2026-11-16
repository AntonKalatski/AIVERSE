#include "apps/place/PlaceAdmission.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace aiverse::place {
namespace {
constexpr uint8_t kMagic0 = 'P', kMagic1 = 'L';
constexpr uint8_t kHello = 'H', kChallenge = 'C', kProof = 'P', kAdmit = 'A', kReject = 'R';
constexpr uint8_t kGateVersion = 1;
constexpr size_t kMaxGuest = 24, kMaxSealed = 512;
const char kProofLabel[] = "aiverse-place-gate-v1";

net::Bytes head(uint8_t type) { return {kMagic0, kMagic1, type}; }
bool isMsg(const net::Bytes& m, uint8_t type) { return m.size() >= 3 && m[0] == kMagic0 && m[1] == kMagic1 && m[2] == type; }

std::vector<uint8_t> proofOf(const net::Key& c2s, const std::array<uint8_t, 32>& nonce) {
    std::vector<uint8_t> msg(kProofLabel, kProofLabel + sizeof(kProofLabel) - 1);
    msg.insert(msg.end(), nonce.begin(), nonce.end());
    return net::keyedHash(c2s, msg.data(), msg.size(), 32);
}

std::string cleanGuest(const std::string& s) {
    std::string out;
    for (char c : s)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
    if (out.size() > kMaxGuest) out.resize(kMaxGuest);
    return out;
}
}

net::ClientTicket placeTicket(const net::Key& key, const std::string& player, double seconds, const std::string& server) {
    net::TicketClaims c;
    c.matchId = kPlaceMatch;
    c.playerId = player;
    c.slot = 1;   // tickets need a slot; a place assigns its own
    c.expiresAtMs = net::unixMs() + uint64_t(std::max(1.0, seconds) * 1000);
    return net::issueTicket(key, c, server);
}

const char* gateRejectText(GateReject r) {
    switch (r) {
    case GateReject::None: return "none";
    case GateReject::BadTicket: return "invalid or expired ticket";
    case GateReject::NeedTicket: return "the server needs a ticket";
    case GateReject::WrongPlace: return "ticket for another server";
    case GateReject::RateLimited: return "too many connection attempts";
    case GateReject::BadHello: return "malformed hello";
    case GateReject::Timeout: return "hello timeout";
    }
    return "unknown";
}

bool RateLimiter::allow(const std::string& key, uint64_t now) {
    if (now - lastSweep_ > 60000 || buckets_.size() > 8192) {   // forget idle sources (their bucket is full again)
        lastSweep_ = now;
        for (auto it = buckets_.begin(); it != buckets_.end();)
            it = double(now - std::min(now, it->second.at)) / 1000.0 * rate_ + it->second.tokens >= burst_ ? buckets_.erase(it) : std::next(it);
    }
    auto [it, fresh] = buckets_.try_emplace(key, Bucket{burst_, now});
    Bucket& b = it->second;
    if (!fresh) b.tokens = std::min(burst_, b.tokens + double(now - std::min(now, b.at)) / 1000.0 * rate_);
    b.at = now;
    if (b.tokens < 1) return false;
    b.tokens -= 1;
    return true;
}

std::string sourceOf(const net::Conn& c) {
    std::string d = c.describe();                       // "<transport> <address> [more]"
    if (size_t sp = d.find(' '); sp != std::string::npos) d = d.substr(sp + 1);
    if (size_t sp = d.find(' '); sp != std::string::npos) d = d.substr(0, sp);
    if (!d.empty() && d[0] == '[') return d.substr(0, d.find(']') + 1);   // [v6]:port
    size_t colon = d.find(':');
    if (colon != std::string::npos && d.find(':', colon + 1) == std::string::npos) d = d.substr(0, colon);   // v4:port
    return d;
}

net::Bytes gateHello(const std::string& guest, const net::Bytes& sealed) {
    net::Bytes m = head(kHello);
    m.push_back(kGateVersion);
    std::string g = cleanGuest(guest);
    m.push_back(uint8_t(g.size()));
    m.insert(m.end(), g.begin(), g.end());
    size_t n = std::min(sealed.size(), kMaxSealed);
    m.push_back(uint8_t(n & 255));
    m.push_back(uint8_t(n >> 8));
    m.insert(m.end(), sealed.begin(), sealed.begin() + long(n));
    return m;
}

PlaceGate::PlaceGate(GateConfig config) : config_(std::move(config)), limiter_(config_.hellosPerSecond, config_.helloBurst) {}

void PlaceGate::reject(Pending& p, GateReject why, uint64_t now) {
    net::Bytes m = head(kReject);
    m.push_back(uint8_t(why));
    p.conn->send(m);
    p.conn->update(now);
    p.closeAt = now + 1000;   // closing at once could drop the reason in flight
    switch (why) {
    case GateReject::BadTicket: case GateReject::WrongPlace: ++stats_.badTicket; break;
    case GateReject::NeedTicket: ++stats_.needTicket; break;
    case GateReject::RateLimited: ++stats_.rateLimited; break;
    case GateReject::Timeout: ++stats_.timeouts; break;
    default: ++stats_.badHello; break;
    }
}

void PlaceGate::add(std::unique_ptr<net::Conn> conn, uint64_t now) {
    if (!conn) return;
    Pending p;
    p.conn = std::move(conn);
    p.since = now;
    if (!limiter_.allow(sourceOf(*p.conn), now)) reject(p, GateReject::RateLimited, now);
    if (pending_.size() >= config_.maxPending) { pending_.front().conn->close(); pending_.pop_front(); }
    pending_.push_back(std::move(p));
}

std::vector<GateAdmitted> PlaceGate::poll(uint64_t now) {
    std::vector<GateAdmitted> out;
    for (auto& p : pending_) {
        p.conn->update(now);
        if (p.closeAt) {
            if (now >= p.closeAt || p.conn->state() == net::ConnState::Closed) { p.conn->close(); p.conn.reset(); }
            continue;
        }
        if (p.conn->state() == net::ConnState::Closed) { p.conn.reset(); continue; }
        net::Bytes m;
        if (!p.conn->recv(m)) {
            if (now - p.since > config_.timeoutMs) reject(p, GateReject::Timeout, now);
            continue;
        }
        GateAdmitted a;
        if (!p.challenged) {
            // hello: magic, version, guest name, sealed ticket part
            if (!isMsg(m, kHello) || m.size() < 7 || m[3] != kGateVersion) { reject(p, GateReject::BadHello, now); continue; }
            size_t g = m[4];
            if (5 + g + 2 > m.size() || g > kMaxGuest) { reject(p, GateReject::BadHello, now); continue; }
            std::string guest(m.begin() + 5, m.begin() + long(5 + g));
            size_t n = size_t(m[5 + g]) | size_t(m[6 + g]) << 8;
            if (7 + g + n != m.size() || n > kMaxSealed) { reject(p, GateReject::BadHello, now); continue; }
            net::Bytes sealed(m.begin() + long(7 + g), m.end());
            if (!config_.key) {
                a.guest = true;
                guest = cleanGuest(guest);
                a.player = "guest-" + (guest.empty() ? std::to_string(++guests_) : guest);
                ++stats_.guests;
            } else {
                if (sealed.empty()) { reject(p, GateReject::NeedTicket, now); continue; }
                auto claims = net::openTicket(*config_.key, sealed, config_.clock());
                if (!claims) { reject(p, GateReject::BadTicket, now); continue; }
                if (claims->matchId != kPlaceMatch) { reject(p, GateReject::WrongPlace, now); continue; }
                p.claims = *claims;
                p.challenged = true;
                net::secureRandom(p.nonce.data(), p.nonce.size());
                net::Bytes c = head(kChallenge);
                c.insert(c.end(), p.nonce.begin(), p.nonce.end());
                p.conn->send(c);
                continue;
            }
        } else {
            auto want = proofOf(p.claims.c2s, p.nonce);
            bool ok = isMsg(m, kProof) && m.size() == 3 + want.size() && net::equalBytes(m.data() + 3, want.data(), want.size());
            net::wipe(p.claims.c2s.data(), 32);
            net::wipe(p.claims.s2c.data(), 32);
            if (!ok) { reject(p, GateReject::BadTicket, now); continue; }
            a.player = p.claims.playerId;
        }
        net::Bytes ack = head(kAdmit);
        ack.push_back(uint8_t(std::min<size_t>(a.player.size(), 255)));
        ack.insert(ack.end(), a.player.begin(), a.player.begin() + long(std::min<size_t>(a.player.size(), 255)));
        p.conn->send(ack);
        a.conn = std::move(p.conn);
        ++stats_.admitted;
        out.push_back(std::move(a));
    }
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [](const Pending& p) { return !p.conn; }), pending_.end());
    return out;
}

GateClient::GateClient(std::unique_ptr<net::Conn> conn, const std::optional<net::ClientTicket>& ticket, const std::string& guest)
    : conn_(std::move(conn)) {
    if (!conn_) { state_ = State::Rejected; reason_ = GateReject::Timeout; return; }
    if (ticket) c2s_ = ticket->claims.c2s;
    conn_->send(gateHello(guest, ticket ? ticket->sealed : net::Bytes{}));
}

GateClient::~GateClient() {
    if (c2s_) net::wipe(c2s_->data(), 32);
}

GateClient::State GateClient::poll(uint64_t now) {
    if (state_ != State::Waiting || !conn_) return state_;
    conn_->update(now);
    net::Bytes m;
    while (state_ == State::Waiting && conn_->recv(m)) {
        if (isMsg(m, kChallenge) && m.size() == 35 && c2s_) {
            std::array<uint8_t, 32> nonce;
            std::memcpy(nonce.data(), m.data() + 3, 32);
            net::Bytes p = head(kProof);
            auto proof = proofOf(*c2s_, nonce);
            p.insert(p.end(), proof.begin(), proof.end());
            conn_->send(p);
        } else if (isMsg(m, kAdmit) && m.size() >= 4 && m.size() == 4 + size_t(m[3])) {
            player_.assign(m.begin() + 4, m.end());
            state_ = State::Admitted;   // the rest of the stream is the replica's
        } else if (isMsg(m, kReject) && m.size() == 4) {
            reason_ = GateReject(m[3]);
            state_ = State::Rejected;
        } else {
            reason_ = GateReject::BadHello;
            state_ = State::Rejected;
        }
    }
    if (state_ == State::Waiting && conn_->state() == net::ConnState::Closed) { state_ = State::Rejected; reason_ = GateReject::Timeout; }
    return state_;
}
}
