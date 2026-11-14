// Plain HTTP/1.1 over the engine's non-blocking sockets, poll-driven (no threads): a small server and
// client for internal APIs inside one cluster (a game server next to a matchmaker / meta backend) and
// for local tests. No TLS: public HTTPS goes through pack::httpRequest (engine/pack/http/HttpRequest.h,
// the platform HTTP stack). Requests and replies use its HttpCall / HttpReply. Native builds only.
#pragma once
#ifndef __EMSCRIPTEN__
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Socket.h"
#include "engine/pack/http/HttpRequest.h"

namespace aiverse::net {

struct UrlParts { std::string scheme, host, path; uint16_t port = 0; };
bool parseUrl(const std::string& url, UrlParts& out);   // http://host[:port]/path

class HttpServer {
public:
    using Handler = std::function<pack::HttpReply(const pack::HttpCall&)>;   // call.url = path?query
    explicit HttpServer(Handler h) : handler_(std::move(h)) {}
    bool listen(uint16_t port, bool loopbackOnly = false);
    uint16_t port() const { return listener_.localPort(); }
    void update(uint64_t nowMs);
    size_t maxBody = 1u << 20;

private:
    struct Conn { std::unique_ptr<TcpSocket> s; std::string in, out; uint64_t since = 0; bool responded = false; };
    Handler handler_;
    TcpListener listener_;
    std::vector<Conn> conns_;
};

// Asynchronous client: request() returns at once, update() drives the sockets and calls back.
class HttpClient {
public:
    void request(const pack::HttpCall& req, std::function<void(pack::HttpReply)> done);
    void update(uint64_t nowMs);
    size_t pending() const { return jobs_.size(); }

private:
    struct Job {
        pack::HttpCall req;
        std::function<void(pack::HttpReply)> done;
        std::unique_ptr<TcpSocket> s;
        std::string out, in;
        uint64_t started = 0;
        bool connected = false, sent = false;
    };
    std::vector<std::unique_ptr<Job>> jobs_;
    uint64_t now_ = 0;
};

// "HTTP/1.1 200 OK\r\n..." -> response (Content-Length or read-to-close bodies; chunked bodies are decoded).
bool parseHttpResponse(const std::string& raw, pack::HttpReply& out, bool complete);

} // namespace aiverse::net
#endif
