#pragma once

#include "core/mempool.h"
#include "core/storage.h"
#include "core/validation.h"
#include "index/chainindex.h"
#include "net/peerman.h"
#include "rpc/server.h"
#include "wallet/wallet.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace bound {

// Milestone 7: what the RPC methods work on, all owned by the thread that runs the node. (Every
// member past the references has a default, so designated initializers may leave any out.)
struct NodeContext {
    Chainstate& chain;
    Mempool& mempool;
    PeerManager& peers;
    std::function<std::int64_t()> clock{};  // Unix time
    // Where the wallet lives (created by createwallet or restorewallet); null if this node has
    // nowhere to keep one.
    KeyValueStore* wallet_db = nullptr;
    std::unique_ptr<Wallet> wallet{};
    WalletOptions wallet_options{};
    std::function<void()> request_stop{};  // for `stop`
    std::uint16_t p2p_port = 0;
    std::string user_agent{};
    // Hardening: bans (setban, listbanned, clearbanned); null: not available. save_bans, if set,
    // runs after every change (to write banlist.dat).
    BanMan* banman = nullptr;
    std::function<void()> save_bans{};
    // Milestone 8: the transaction and address indexes, when the node keeps them (--index). With
    // them getrawtransaction finds any confirmed transaction and the getaddress* methods work.
    ChainIndex* index = nullptr;
};

// The node's methods: blockchain, mempool, network, mining, wallet, control.
void register_node_methods(RpcTable& table, NodeContext& node);

// A transaction as JSON (decoderawtransaction, getrawtransaction verbose).
json transaction_to_json(const Transaction& tx, const ConsensusParams& params);

}  // namespace bound
