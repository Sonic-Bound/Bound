#pragma once

#include "rpc/server.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace bound {

// Milestone 8: the calling side of JSON-RPC, for bound-cli and the explorer. Talks to a node on
// this machine (127.0.0.1) and proves it may with the node's cookie file.
class RpcClient {
public:
    RpcClient(std::filesystem::path cookie_file, std::uint16_t port, int timeout_ms = 30'000);

    // The method's result. Throws:
    //  - RpcError when the node answers with an error;
    //  - HttpConnectError when nothing is listening on the port;
    //  - std::runtime_error for the rest: no cookie file, a cookie the node refuses, a reply
    //    that isn't JSON-RPC.
    // The cookie is read once and again whenever the node refuses it (it changes at each start).
    json call(const std::string& method, const json& params = json::array());

    std::uint16_t port() const { return port_; }

private:
    std::string read_cookie() const;

    std::filesystem::path cookie_file_;
    std::uint16_t port_;
    int timeout_ms_;
    std::string cookie_;
};

}  // namespace bound
