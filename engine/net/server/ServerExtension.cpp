#include "ServerExtension.h"

#include <map>

namespace aiverse::net {

namespace {
std::map<std::string, ServerExtensionFactory>& registry() {
    static std::map<std::string, ServerExtensionFactory> r;   // function-local: safe for static registration order
    return r;
}
} // namespace

void registerServerExtension(const std::string& name, ServerExtensionFactory factory) { registry()[name] = std::move(factory); }

std::unique_ptr<ServerExtension> makeServerExtension(const std::string& name) {
    auto it = registry().find(name);
    return it == registry().end() || !it->second ? nullptr : it->second();
}

std::vector<std::string> serverExtensions() {
    std::vector<std::string> out;
    for (auto& [name, f] : registry()) out.push_back(name);
    return out;
}

} // namespace aiverse::net
