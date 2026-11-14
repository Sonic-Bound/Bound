#include "rpc/client.h"

#include "net/socket.h"
#include "rpc/http.h"
#include "util/base64.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace bound {

RpcClient::RpcClient(std::filesystem::path cookie_file, std::uint16_t port, int timeout_ms)
    : cookie_file_(std::move(cookie_file)), port_(port), timeout_ms_(timeout_ms) {}

std::string RpcClient::read_cookie() const {
    std::ifstream in(cookie_file_, std::ios::binary);
    if (!in) {
        throw std::runtime_error("can't read " + cookie_file_.string() + ": is `boundd node` running with this data directory?");
    }
    std::string cookie{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    while (!cookie.empty() && (cookie.back() == '\n' || cookie.back() == '\r' || cookie.back() == ' ')) cookie.pop_back();
    return cookie;
}

json RpcClient::call(const std::string& method, const json& params) {
    const std::string body = dump_json(json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}});
    const NetAddress node = NetAddress::ipv4(127, 0, 0, 1, port_);
    const auto post = [&] {
        return http_post(node, "/", body, {{"Authorization", "Basic " + base64_encode(bytes_of(cookie_))}}, timeout_ms_);
    };
    if (cookie_.empty()) cookie_ = read_cookie();
    HttpResponse response = post();
    if (response.status == 401) {  // the node was restarted since we read its cookie
        cookie_ = read_cookie();
        response = post();
        if (response.status == 401) throw std::runtime_error("the node refused the cookie: is this its data directory?");
    }
    if (response.status != 200) throw std::runtime_error("HTTP " + std::to_string(response.status) + " from the node: " + response.body);

    const json reply = json::parse(response.body, nullptr, false);
    if (reply.is_discarded() || !reply.is_object()) throw std::runtime_error("the node's reply isn't JSON-RPC");
    if (const auto error = reply.find("error"); error != reply.end() && !error->is_null()) {
        if (!error->is_object()) throw std::runtime_error("the node's reply isn't JSON-RPC");
        throw RpcError(error->value("code", rpc_error::kMisc), error->value("message", std::string()));
    }
    const auto result = reply.find("result");
    return result != reply.end() ? *result : json(nullptr);
}

}  // namespace bound
