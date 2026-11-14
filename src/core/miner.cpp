#include "core/miner.h"

#include "core/consensus.h"
#include "core/pow.h"
#include "core/validation.h"
#include "util/serialize.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace bound {

std::optional<BlockHeader> mine(BlockHeader header, const uint256& pow_limit, std::uint32_t start_nonce,
                                std::uint64_t max_tries) {
    std::uint64_t nonce = start_nonce;
    for (std::uint64_t tries = 0; tries < max_tries && nonce <= 0xffffffff; ++tries, ++nonce) {
        header.nonce = static_cast<std::uint32_t>(nonce);
        if (check_proof_of_work(header.hash(), header.bits, pow_limit)) return header;
    }
    return std::nullopt;
}

Transaction make_coinbase(int height, Amount reward, const PublicKey& recipient, ByteSpan memo) {
    Transaction tx;
    tx.outputs.push_back(TxOut{reward, recipient});
    tx.lock_time = static_cast<std::uint32_t>(height);
    tx.memo.assign(memo.begin(), memo.end());
    return tx;
}

namespace {

Bytes extra_nonce(std::uint64_t n) {
    ByteWriter w;
    w.u64(n);
    return std::move(w).take();
}

}  // namespace

std::optional<Block> make_block_template(const Chainstate& chain, const PublicKey& recipient,
                                         std::vector<Transaction> txs, std::uint32_t time) {
    const ConsensusParams& params = chain.params();
    const BlockIndex& tip = chain.tip();
    const int height = tip.height + 1;

    // Apply the transactions to a copy of the UTXO set, so later ones can spend earlier ones.
    UtxoSet view = chain.utxos();
    Amount fees = 0;
    for (const Transaction& tx : txs) {
        if (tx.is_coinbase() || check_transaction(tx) != TxCheck::Ok) return std::nullopt;
        Amount fee = 0;
        if (check_tx_inputs(tx, view, height, params, &fee) != InputsCheck::Ok) return std::nullopt;
        fees += fee;
        for (const OutPoint& in : tx.inputs) view.spend(in);
        const uint256 txid = tx.txid();
        for (std::uint32_t i = 0; i < tx.outputs.size(); ++i) view.add({txid, i}, Coin{tx.outputs[i], height, false});
    }

    Block block;
    block.txs.reserve(txs.size() + 1);
    block.txs.push_back(make_coinbase(height, block_subsidy(height, params) + fees, recipient, extra_nonce(0)));
    std::move(txs.begin(), txs.end(), std::back_inserter(block.txs));

    block.header.prev_hash = tip.hash;
    block.header.merkle_root = block.compute_merkle_root();
    block.header.time = std::max(time, tip.median_time_past(params.median_time_span) + 1);
    block.header.bits = next_work_required(tip, params);
    return block;
}

Block mine_block(Block block, const uint256& pow_limit) {
    for (std::uint64_t extra = 1;; ++extra) {
        if (const std::optional<BlockHeader> solved = mine(block.header, pow_limit)) {
            block.header = *solved;
            return block;
        }
        block.txs[0].memo = extra_nonce(extra);
        block.header.merkle_root = block.compute_merkle_root();
    }
}

BackgroundMiner::BackgroundMiner(const uint256& pow_limit)
    : pow_limit_(pow_limit), thread_([this](std::stop_token stop) { run(stop); }) {}

BackgroundMiner::~BackgroundMiner() {
    thread_.request_stop();
    wake_.notify_all();
}

void BackgroundMiner::set_work(std::optional<Block> block) {
    {
        const std::lock_guard lock(mutex_);
        work_ = std::move(block);
        ++generation_;
    }
    wake_.notify_all();
}

std::optional<Block> BackgroundMiner::take_solved() {
    const std::lock_guard lock(mutex_);
    return std::exchange(solved_, std::nullopt);
}

void BackgroundMiner::run(std::stop_token stop) {
    constexpr std::uint64_t kSlice = 1 << 16;  // nonces between looks for new work
    std::uint64_t mining = 0;                  // the generation `block` came from
    bool idle = true;                          // nothing to mine: none given yet, paused, or solved
    Block block;
    std::uint64_t nonce = 0;
    std::uint64_t extra = 0;
    while (!stop.stop_requested()) {
        {
            std::unique_lock lock(mutex_);
            if (idle && !wake_.wait(lock, stop, [&] { return generation_ != mining && work_.has_value(); })) return;
            if (generation_ != mining) {
                mining = generation_;
                idle = !work_.has_value();
                if (idle) continue;
                block = *work_;
                nonce = 0;
                extra = 0;
            }
        }
        const std::uint64_t tries = std::min(kSlice, (std::uint64_t{1} << 32) - nonce);
        const std::optional<BlockHeader> solved = mine(block.header, pow_limit_, static_cast<std::uint32_t>(nonce), tries);
        hashes_ += solved ? solved->nonce - nonce + 1 : tries;
        if (solved) {
            block.header = *solved;
            const std::lock_guard lock(mutex_);
            solved_ = block;
            idle = true;
            continue;
        }
        nonce += tries;
        if (nonce == std::uint64_t{1} << 32) {  // every nonce tried: change the coinbase and start over
            block.txs[0].memo = extra_nonce(++extra);
            block.header.merkle_root = block.compute_merkle_root();
            nonce = 0;
        }
    }
}

}  // namespace bound
