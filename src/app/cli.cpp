// bound-cli: talks to a running boundd over JSON-RPC, like bitcoin-cli.
//   bound-cli [--datadir <dir>] [--rpcport <port>] <method> [param]...
// It reads the node's cookie from the data directory (the same default as boundd's), so it works
// for whoever can read that directory. Parameters are passed as strings, except those the table
// below marks as numbers or true/false. `bound-cli help` lists the methods.
#include "app/datadir.h"
#include "core/params.h"
#include "rpc/client.h"
#include "rpc/http.h"
#include "rpc/server.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace bound;

// The parameters that are JSON values rather than strings (Bitcoin's vRPCConvertParams).
struct Convert {
    std::string_view method;
    std::size_t index;
};
constexpr Convert kConvert[] = {
    {"getblockhash", 0},     {"getblock", 1},         {"getrawtransaction", 1}, {"getrawmempool", 0},
    {"generatetoaddress", 0}, {"listtransactions", 0}, {"sendtoaddress", 2},     {"bumpfee", 1},
    {"walletpassphrase", 1}, {"setban", 2},           {"gettxout", 1},          {"gettxout", 2},
    {"getaddressdeltas", 1}, {"getaddressdeltas", 2}, {"getaddressutxos", 1},   {"getaddressutxos", 2},
    {"getnetworkhashps", 0},
};

json param_value(std::string_view method, std::size_t index, const std::string& text) {
    for (const Convert& c : kConvert) {
        if (c.method != method || c.index != index) continue;
        json value = json::parse(text, nullptr, false);
        if (value.is_discarded() || !(value.is_number() || value.is_boolean() || value.is_null())) {
            throw std::invalid_argument("parameter " + std::to_string(index + 1) + " of " + std::string(method) +
                                        " must be a number or true/false, not " + text);
        }
        return value;
    }
    return text;
}

int usage() {
    std::cerr << "usage: bound-cli [--datadir <dir>] [--rpcport <port>] <method> [param]...\n"
                 "       bound-cli help             lists the methods\n"
                 "       bound-cli help <method>    explains one\n"
                 "The data directory defaults to "
              << default_datadir().string() << ".\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = testnet_params().default_rpc_port;
    try {
        std::vector<std::string_view> args(argv + 1, argv + argc);
        std::filesystem::path datadir = default_datadir();
        while (args.size() >= 2 && (args[0] == "--datadir" || args[0] == "--rpcport")) {
            if (args[0] == "--datadir") {
                datadir = std::filesystem::path(args[1]);
            } else {
                port = static_cast<std::uint16_t>(std::stoul(std::string(args[1])));
            }
            args.erase(args.begin(), args.begin() + 2);
        }
        if (args.empty() || args[0].starts_with("-")) return usage();

        const std::string method(args[0]);
        json params = json::array();
        for (std::size_t i = 1; i < args.size(); ++i) params.push_back(param_value(method, i - 1, std::string(args[i])));

        // 15 minutes: generatetoaddress and restorewallet can take a while.
        RpcClient node(cookie_path(datadir), port, 900'000);
        const json result = node.call(method, params);
        if (result.is_string()) {
            const std::string text = result.get<std::string>();
            std::cout << text << (text.ends_with('\n') ? "" : "\n");
        } else if (!result.is_null()) {
            std::cout << dump_json(result, 2) << "\n";
        }
        return 0;
    } catch (const RpcError& e) {
        std::cerr << "error code: " << e.code << "\nerror message:\n" << e.what() << "\n";
        return 1;
    } catch (const HttpConnectError&) {
        std::cerr << "bound-cli: can't connect to the node at 127.0.0.1:" << port << ": is `boundd node` running?\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "bound-cli: " << e.what() << "\n";
        return 1;
    }
}
