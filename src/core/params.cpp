#include "core/params.h"

#include "core/miner.h"
#include "core/pow.h"

namespace bound {

namespace {

ConsensusParams make_testnet() {
    ConsensusParams p;
    p.network_name = "testnet";

    // Milestone 1: proof of work and time rules.
    p.pow_limit_bits = 0x1f00ffff;  // about 65k hashes per block
    p.pow_limit = target_from_compact(p.pow_limit_bits).value();
    p.max_future_block_time = 2 * 60 * 60;
    p.median_time_span = 11;

    // Milestone 2: economics (1-minute blocks, 5 BOUND halving every ~4 years).
    p.target_spacing = 60;
    p.initial_subsidy = 5 * kCoin;
    p.halving_interval = 2'100'000;
    p.max_block_size = 1'000'000;
    p.address_version = 0xf0;

    // Milestone 3.
    p.coinbase_maturity = 100;

    // Milestone 4: Bitcoin's retarget rule over one day of blocks instead of two weeks.
    p.retarget_interval = 1440;

    // Milestone 6. The magic is picked like Bitcoin's: every byte has its high bit set, so it's
    // never ASCII text and never valid UTF-8, and it's unlikely to appear inside other data.
    p.message_start = {0xb0, 0xd4, 0xe7, 0xf5};
    p.default_port = 18333;
    p.default_rpc_port = 18332;

    // Milestone 8. Seeds: add the host names of the testnet's DNS seeders or always-on nodes
    // here, e.g. "seed.example.org" (the default port is assumed; "host:port" names another).
    // None yet: until there are, a first node is found with --seednode or --connect.
    p.seeds = {};
    // Raise this to the public chain's work once there is one (see docs/hardening.md).
    p.minimum_chain_work = 0;

    // Genesis, mined by `boundd genesis`. It pays the all-zero key, which nobody can sign for
    // (a small-order point that libsodium rejects), and its output never enters the UTXO set.
    p.genesis_coinbase = make_coinbase(
        0, 5 * kCoin, PublicKey{},
        bytes_of("BOUND testnet 01/Nov/2026: a proof-of-work coin built from scratch in C++"));
    p.genesis.version = 1;
    p.genesis.merkle_root = genesis_block(p).compute_merkle_root();
    p.genesis.time = 1793491200;  // 2026-11-01 00:00:00 UTC
    p.genesis.bits = p.pow_limit_bits;
    p.genesis.nonce = 26584;
    p.genesis_hash = uint256::from_hex("0000a7ba8cc2ed04fa56bb3eaf23d5879874e7ffd060d81a2aca994f79b79293").value();
    return p;
}

}  // namespace

const ConsensusParams& testnet_params() {
    static const ConsensusParams params = make_testnet();
    return params;
}

}  // namespace bound
