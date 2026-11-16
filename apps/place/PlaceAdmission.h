// Who may enter a place server (docs/replica.md § Place hosting).
//
// - With a server key, every player needs a ticket: net::MatchTicket (player id + expiry + session keys, sealed
//   with the 32-byte key), issued by a login service that shares the key or by `aiverse_place ticket`.
//   Over SecureUdp the ticket is the transport handshake itself (SecureListener). Over WebSocket and plain UDP the
//   PlaceGate below runs first on the raw connection: the client sends the sealed part, the server answers with a
//   random challenge, the client proves it holds the ticket's c2s key (keyed BLAKE2b). A copied sealed part alone
//   gets nobody in; the WebSocket stream itself is only private behind a TLS proxy (wss://).
// - Without a key (dev mode) anybody enters as a guest: "guest-<name>" from the hello, else "guest-<n>".
// - Hellos are rate-limited per source address (and SecureUdp handshakes per player id); a player id already in
//   the place is taken over by the newest connection (PlaceServer::attach).
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "engine/net/Conn.h"
#include "engine/net/Crypto.h"
#include "engine/net/MatchTicket.h"

namespace aiverse::place {

// Tickets of a place carry this match id (a place server admits nothing else).
constexpr uint64_t kPlaceMatch = 0x504C414345ull;   // "PLACE"
net::ClientTicket placeTicket(const net::Key& key, const std::string& player, double seconds, const std::string& server = "");

enum class GateReject : uint8_t {
    None = 0,
    BadTicket = 1,      // forged, for another key or expired
    NeedTicket = 2,     // the server has a key and the hello carried no ticket
    WrongPlace = 3,     // a valid ticket for something else (match id)
    RateLimited = 4,
    BadHello = 5,
    Timeout = 6,
};
const char* gateRejectText(GateReject r);
// Retrying with the same ticket cannot help.
inline bool gateRejectFinal(GateReject r) { return r == GateReject::BadTicket || r == GateReject::NeedTicket || r == GateReject::WrongPlace; }

// Token bucket per key (source address, player id); idle keys are forgotten.
class RateLimiter {
public:
    RateLimiter(double perSecond, double burst) : rate_(perSecond), burst_(burst) {}
    bool allow(const std::string& key, uint64_t nowMs);
    size_t size() const { return buckets_.size(); }
private:
    struct Bucket { double tokens; uint64_t at; };
    double rate_, burst_;
    std::map<std::string, Bucket> buckets_;
    uint64_t lastSweep_ = 0;
};
// "udp 1.2.3.4:5" / "ws [::1]:5" -> the address without the port (what a rate limit is keyed by).
std::string sourceOf(const net::Conn& c);

struct GateConfig {
    std::optional<net::Key> key;                        // none: guests (dev mode)
    std::function<uint64_t()> clock = net::unixMs;      // ticket expiry clock
    uint32_t timeoutMs = 5000;                          // hello + proof must arrive by then
    size_t maxPending = 256;                            // the oldest pending connection is dropped beyond this
    double hellosPerSecond = 2, helloBurst = 6;         // per source address
};

struct GateAdmitted {
    std::unique_ptr<net::Conn> conn;
    std::string player;     // ticket player id, or "guest-..."
    bool guest = false;
};

struct GateStats { uint64_t admitted = 0, guests = 0, badTicket = 0, needTicket = 0, rateLimited = 0, badHello = 0, timeouts = 0; };

// Server side of the hello on connections that are not SecureUdp (WebSocket, plain UDP).
class PlaceGate {
public:
    explicit PlaceGate(GateConfig config);
    void add(std::unique_ptr<net::Conn> conn, uint64_t nowMs);
    std::vector<GateAdmitted> poll(uint64_t nowMs);
    size_t pending() const { return pending_.size(); }
    const GateStats& stats() const { return stats_; }
    bool keyed() const { return config_.key.has_value(); }
private:
    struct Pending {
        std::unique_ptr<net::Conn> conn;
        uint64_t since = 0;
        bool challenged = false;
        uint64_t closeAt = 0;           // rejected: flush the reason, then close
        std::array<uint8_t, 32> nonce{};
        net::TicketClaims claims;
    };
    void reject(Pending& p, GateReject why, uint64_t nowMs);
    GateConfig config_;
    RateLimiter limiter_;
    std::deque<Pending> pending_;
    uint64_t guests_ = 0;
    GateStats stats_;
};

// Client side: runs the hello on a fresh connection, then hands it to PlaceClient.
class GateClient {
public:
    enum class State { Waiting, Admitted, Rejected };
    GateClient(std::unique_ptr<net::Conn> conn, const std::optional<net::ClientTicket>& ticket, const std::string& guest);
    ~GateClient();
    State poll(uint64_t nowMs);
    std::unique_ptr<net::Conn> take() { return std::move(conn_); }   // after Admitted
    GateReject reason() const { return reason_; }
    const std::string& player() const { return player_; }   // as the server named us
private:
    std::unique_ptr<net::Conn> conn_;
    std::optional<net::Key> c2s_;
    State state_ = State::Waiting;
    GateReject reason_ = GateReject::None;
    std::string player_;
};

// Hello wire (exposed for the hostile-peer tests).
net::Bytes gateHello(const std::string& guest, const net::Bytes& sealed);
}
