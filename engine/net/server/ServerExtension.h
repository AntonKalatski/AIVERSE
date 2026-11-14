// Server extensions: code a game project links into aiverse_server (its native/ folder) to connect the
// game server to its own backend — reserve matches an external matchmaker formed (Server::reserve),
// check tickets, report results and statistics, run an internal API. The server knows no backend:
//   aiverse_server --extension <name>[=config.json]
// creates the extension registered under <name>, lets it adjust the ServerConfig (ticketCheck,
// onExternalMatchStarted / onExternalMatchEnd, queues...) before the Server exists, then calls
// start() once the server listens and update() after every Server::update(). Everything runs on the
// server thread.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Server.h"
#include "engine/data/Json.h"

namespace aiverse::net {

class ServerExtension {
public:
    virtual ~ServerExtension() = default;
    // config: the JSON file given on the command line (null when none). Throw std::runtime_error with a
    // readable message to refuse to start.
    virtual void configure(const Json& config, ServerConfig& server) = 0;
    virtual bool start(Server& /*server*/, std::string* /*error*/) { return true; }
    virtual void update(uint64_t /*nowMs*/) {}
};
using ServerExtensionFactory = std::function<std::unique_ptr<ServerExtension>()>;

void registerServerExtension(const std::string& name, ServerExtensionFactory factory);
std::unique_ptr<ServerExtension> makeServerExtension(const std::string& name);   // nullptr: not in this build
std::vector<std::string> serverExtensions();                                     // sorted names

// Static registration in the extension's .cpp:  AIVERSE_SERVER_EXTENSION("my-backend", MyExtension)
#define AIVERSE_SERVER_EXTENSION(name, Class) \
    static const bool aiverseServerExtension_##Class = \
        (::aiverse::net::registerServerExtension(name, [] { return std::unique_ptr<::aiverse::net::ServerExtension>(new Class()); }), true)

} // namespace aiverse::net
