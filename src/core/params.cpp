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

    // Milestone 8. Seeds: the host names (or addresses) of the testnet's DNS seeders and always-on
    // nodes, e.g. "seed.example.org" (the default port is assumed; "host:port" names another).
    // A node asks them when it knows too few peers; after that, peers come from address gossip.
    p.seeds = {
        "seed.bound.sarl",   // DNS seeder on the Azure server: answers with nodes that are up
        "seed2.bound.sarl",  // DNS seeder on the Google Cloud server
        "136.86.151.244",    // the always-on nodes themselves (static addresses), in case DNS fails:
        "104.211.211.39",    // Google Cloud (us-west1) and Microsoft Azure (South India)
    };
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

// Milestone 10: mainnet, with Bitcoin's schedule (the project owner's choice): 10-minute blocks,
// 50 BOUND halving every 210,000 blocks, 20,999,999.9769 BOUND in all, no premine.
ConsensusParams make_mainnet() {
    ConsensusParams p;
    p.network_name = "mainnet";
    p.launched = false;  // the genesis below is provisional: launch day mines the real one

    // The easiest difficulty, as testnet's: about 65,000 hashes a block. ASERT can fall this far if
    // the hash rate collapses, so the chain never stalls. Where it starts is the genesis block's
    // bits (equal for this provisional genesis); launch day sets those to fit the hash rate then.
    p.pow_limit_bits = 0x1f00ffff;
    p.pow_limit = target_from_compact(p.pow_limit_bits).value();
    p.max_future_block_time = 2 * 60 * 60;
    p.median_time_span = 11;

    p.target_spacing = 10 * 60;
    p.initial_subsidy = 50 * kCoin;
    p.halving_interval = 210'000;
    p.max_block_size = 1'000'000;
    p.address_version = {0x46, 0xfb};  // every address starts with 'B' (52 characters)
    p.coinbase_maturity = 100;
    // Milestone 11 (the project owner's choice): ASERT sets every block's difficulty, with Bitcoin
    // Cash's half-life of 2 days. The retarget interval below isn't used.
    p.asert_half_life = 2 * 24 * 60 * 60;
    p.retarget_interval = 2016;

    p.message_start = {0xd6, 0xb3, 0xe8, 0xf7};  // high bits set, as testnet's (see there)
    p.default_port = 7333;
    p.default_rpc_port = 7332;
    p.seeds = {};  // the launch's always-on nodes and DNS seeders
    p.minimum_chain_work = 0;

    p.genesis_coinbase = make_coinbase(0, 50 * kCoin, PublicKey{},
                                       bytes_of("BOUND mainnet: a provisional genesis, replaced on launch day"));
    p.genesis.version = 1;
    p.genesis.merkle_root = genesis_block(p).compute_merkle_root();
    p.genesis.time = 1794700800;  // 2026-11-15 00:00:00 UTC
    p.genesis.bits = p.pow_limit_bits;
    p.genesis.nonce = 17410;
    p.genesis_hash = uint256::from_hex("0000725c9029a8709917e390fa8e914a370e611f7216086116af27104c093ebd").value();
    return p;
}

// Milestone 10: regtest, Bitcoin's private test network: blocks cost a couple of hashes, the
// difficulty never moves, and halvings come every 150 blocks so tests can reach them.
ConsensusParams make_regtest() {
    ConsensusParams p;
    p.network_name = "regtest";

    p.pow_limit_bits = 0x207fffff;  // half of all hashes meet it
    p.pow_limit = target_from_compact(p.pow_limit_bits).value();
    p.pow_no_retargeting = true;
    p.max_future_block_time = 2 * 60 * 60;
    p.median_time_span = 11;

    p.target_spacing = 10 * 60;
    p.initial_subsidy = 50 * kCoin;
    p.halving_interval = 150;
    p.max_block_size = 1'000'000;
    p.address_version = 0xf0;  // testnet's, as Bitcoin's regtest shares testnet's
    p.coinbase_maturity = 100;
    p.retarget_interval = 2016;

    p.message_start = {0xc1, 0xe5, 0xa9, 0xd2};
    p.default_port = 18544;
    p.default_rpc_port = 18543;
    p.seeds = {};
    p.minimum_chain_work = 0;

    p.genesis_coinbase = make_coinbase(0, 50 * kCoin, PublicKey{}, bytes_of("BOUND regtest"));
    p.genesis.version = 1;
    p.genesis.merkle_root = genesis_block(p).compute_merkle_root();
    p.genesis.time = 1793491200;  // 2026-11-01 00:00:00 UTC
    p.genesis.bits = p.pow_limit_bits;
    p.genesis.nonce = 0;  // half of all hashes meet regtest's target, and the first try did
    p.genesis_hash = uint256::from_hex("50c65d2c8b59bc4dd295e7afa6d42c52a138df9a3b02df467db4c4693d2e0456").value();
    return p;
}

}  // namespace

const ConsensusParams& mainnet_params() {
    static const ConsensusParams params = make_mainnet();
    return params;
}

const ConsensusParams& testnet_params() {
    static const ConsensusParams params = make_testnet();
    return params;
}

const ConsensusParams& regtest_params() {
    static const ConsensusParams params = make_regtest();
    return params;
}

const ConsensusParams& params_for(Network network) {
    switch (network) {
    case Network::Mainnet: return mainnet_params();
    case Network::Testnet: return testnet_params();
    case Network::Regtest: return regtest_params();
    }
    return testnet_params();
}

std::string_view network_name(Network network) { return params_for(network).network_name; }

std::optional<Network> network_flag(std::string_view arg) {
    if (arg == "--mainnet") return Network::Mainnet;
    if (arg == "--testnet") return Network::Testnet;
    if (arg == "--regtest") return Network::Regtest;
    return std::nullopt;
}

}  // namespace bound
