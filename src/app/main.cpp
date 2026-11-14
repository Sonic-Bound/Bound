// boundd, the BOUND node, on testnet rules. The chain is kept in a data directory (default
// %APPDATA%\BOUND\testnet, or ~/.bound/testnet):
//   boundd [--datadir <dir>] node [--port <p>] [--rpcport <p>] [--nolisten | --local]
//                                 [--connect <host[:port]>]... [--seednode <host[:port]>]...
//                                 [--noseeds] [--maxinbound <n>] [--allowprivate] [--index]
//                                 [--mine <address>]
//                                                 run a node on the network until Ctrl+C or
//                                                 `bound-cli stop`. JSON-RPC on 127.0.0.1
//                                                 (bound-cli talks to it).
//       --connect      a peer to stay connected to
//       --seednode     a name to ask for peers' addresses, besides the built-in seeds (--noseeds
//                      leaves those out)
//       --local        listen on 127.0.0.1 only, and accept local addresses: several nodes on one
//                      machine
//       --allowprivate take and pass on private addresses (10.x, 192.168.x, ...): nodes on one LAN
//       --index        keep the transaction and address indexes an explorer needs
//   boundd [--datadir <dir>] mine <n> [address]   mine n blocks on the stored chain, offline
//   boundd [--datadir <dir>] info                 height, tip, difficulty, work, supply
//   boundd [--datadir <dir>] balance <address>
// and two commands run entirely in memory:
//   boundd demo                     mine, wait out coinbase maturity, pay someone through the
//                                   mempool, lose the payment to a reorganisation, and see the
//                                   mempool put it back
//   boundd genesis [message] [time] mine a testnet genesis block (prints the values for params.cpp)
#include "app/datadir.h"
#include "core/address.h"
#include "core/consensus.h"
#include "core/leveldb_store.h"
#include "core/mempool.h"
#include "core/miner.h"
#include "core/params.h"
#include "core/pow.h"
#include "core/storage.h"
#include "core/validation.h"
#include "core/wallet.h"
#include "index/chainindex.h"
#include "net/addrman.h"
#include "net/connman.h"
#include "net/peerman.h"
#include "rpc/http.h"
#include "rpc/methods.h"
#include "rpc/server.h"
#include "util/serialize.h"
#include "util/time.h"
#include "wallet/wallet.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using namespace bound;

// What a data directory holds: the database (block tree, UTXO set, undo data) and the block files.
struct DataDir {
    explicit DataDir(const std::filesystem::path& dir) : db(dir / "chainstate"), blocks(dir / "blocks") {}

    LevelDbStore db;
    FlatFileBlockStore blocks;
};

PublicKey parse_address(std::string_view address) {
    const std::optional<PublicKey> key = decode_address(address, testnet_params().address_version);
    if (!key) throw std::invalid_argument("not a testnet address: " + std::string(address));
    return *key;
}

std::string format_amount(Amount a) { return format_coins(a) + " " + std::string(kTicker); }

std::string address_of(const KeyPair& key) { return encode_address(key.public_key(), testnet_params().address_version); }

// Room a block leaves for transactions after its header and coinbase.
std::size_t block_budget(const ConsensusParams& p) { return p.max_block_size - 1000; }

// Mines the next block on `chain`, paying `miner` and including `txs`, accepts it, and prints a line.
void mine_next(Chainstate& chain, const PublicKey& miner, std::vector<Transaction> txs, bool print = true) {
    const auto start = std::chrono::steady_clock::now();
    std::optional<Block> tmpl = make_block_template(chain, miner, std::move(txs), static_cast<std::uint32_t>(unix_now()));
    if (!tmpl) throw std::runtime_error("a transaction for the block template is invalid");
    const Block block = mine_block(std::move(*tmpl), chain.params().pow_limit);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (!chain.accept_block(block, unix_now()).accepted()) throw std::runtime_error("our own block was rejected");
    if (print) {
        const double khs = seconds > 0 ? (block.header.nonce + 1.0) / seconds / 1000 : 0;
        std::cout << std::format("  #{:<4} {}  nonce {:>10}  {:5.2f} s  {:6.0f} kH/s  difficulty {:.2f}  {} tx\n",
                                 chain.height(), block.hash().to_hex(), block.header.nonce, seconds, khs,
                                 difficulty(block.header.bits, chain.params()), block.txs.size());
    }
}

// Accepts a block from another node; anything but a clean accept is a bug in the demo.
void receive(Chainstate& chain, const Block& block) {
    if (!chain.accept_block(block, unix_now()).accepted()) throw std::runtime_error("a node rejected a valid block");
}

int cmd_demo() {
    const ConsensusParams& p = testnet_params();
    Chainstate chain(p);  // Alice's node
    Mempool mempool(chain);
    const KeyPair alice = KeyPair::generate();
    const KeyPair bob = KeyPair::generate();
    const KeyPair carol = KeyPair::generate();

    std::cout << "BOUND testnet, in memory. Genesis " << p.genesis_hash.to_hex() << "\n"
              << "Alice " << address_of(alice) << "\nBob   " << address_of(bob) << "\nCarol " << address_of(carol)
              << "\n\n1. Alice mines 101 blocks (coinbase outputs need " << p.coinbase_maturity << " blocks to mature):\n";
    for (int i = 1; i <= 101; ++i) mine_next(chain, alice.public_key(), {}, i <= 3 || i % 25 == 0 || i == 101);

    // Carol runs a node of her own. It has every block so far, and is about to lose touch with Alice's.
    Chainstate carols_node(p);
    for (int h = 1; h <= chain.height(); ++h) receive(carols_node, chain.block_at(h));

    const int next = chain.height() + 1;
    Amount spendable = 0;
    for (const auto& [op, coin] : spendable_coins(chain.utxos(), alice.public_key(), next, p)) spendable += coin.out.amount;
    std::cout << "\n2. Alice owns " << format_amount(balance(chain.utxos(), alice.public_key())) << ", but only "
              << format_amount(spendable) << " is mature enough to spend in block #" << next << ".\n";

    const Amount amount = 7 * kCoin + kCoin / 2;
    const Amount fee = kCoin / 1000;
    std::optional<Transaction> pay = create_payment(chain.utxos(), alice, bob.public_key(), amount, fee, next, p);
    if (!pay) throw std::runtime_error("Alice can't afford the payment");
    const MempoolAccept accepted = mempool.accept(*pay, unix_now());
    if (!accepted.accepted()) throw std::runtime_error("the mempool refused the payment: " + accepted.reason);
    const MempoolEntry& entry = *mempool.find(pay->txid());
    std::cout << "\n3. Alice pays Bob " << format_amount(amount) << " (fee " << format_amount(fee) << "), txid "
              << pay->txid().to_hex() << ".\n   It waits in her node's mempool: " << entry.size << " bytes, "
              << FeeRate::of(entry.fee, entry.size).per_kb / 1000 << " units per byte. The next block takes it:\n";
    mine_next(chain, alice.public_key(), mempool.select(block_budget(p)).txs);

    std::cout << "\n4. After block #" << chain.height() << " (mempool: " << mempool.size() << " waiting):\n"
              << "   Alice  " << format_amount(balance(chain.utxos(), alice.public_key())) << "\n"
              << "   Bob    " << format_amount(balance(chain.utxos(), bob.public_key())) << "\n"
              << "   supply " << format_amount(chain.utxos().total_value()) << " in " << chain.utxos().size()
              << " unspent outputs (" << chain.height() << " blocks x " << format_amount(p.initial_subsidy)
              << "; fees only move coins)\n";

    const BlockIndex& alices_tip = chain.tip();

    std::cout << "\n5. Meanwhile Carol, cut off from Alice, mines two blocks on #101 on her own node:\n";
    mine_next(carols_node, carol.public_key(), {});
    mine_next(carols_node, carol.public_key(), {});

    std::cout << "\n6. The connection comes back, and Alice's node receives Carol's blocks:\n";
    receive(chain, carols_node.block_at(102));
    if (&chain.tip() != &alices_tip) throw std::runtime_error("an equal-work branch replaced the tip");
    std::cout << "   Carol's #102: the same work as Alice's #102. Stored on a side branch; the tip stays Alice's,\n"
              << "   because of two chains with equal work the first one seen wins.\n";
    receive(chain, carols_node.block_at(103));
    if (chain.tip().hash != carols_node.tip().hash) throw std::runtime_error("the branch with more work didn't take over");
    std::cout << std::format("   Carol's #103: her branch now has more work ({:.0f} expected hashes against {:.0f}).\n",
                             chain.tip().chain_work.to_double(), alices_tip.chain_work.to_double())
              << "   Reorg: Alice's #102 is disconnected using its undo data, Carol's #102 and #103 are connected.\n";

    std::cout << "\n7. After the reorg, tip #" << chain.height() << " " << chain.tip().hash.to_hex() << ":\n"
              << "   Bob    " << format_amount(balance(chain.utxos(), bob.public_key())) << "\n"
              << "   Bob's payment was only in the block that lost. Alice's mempool took it back: "
              << (mempool.contains(pay->txid()) ? "it's waiting again" : "it's MISSING") << " (" << mempool.size()
              << " waiting).\n";
    if (!mempool.contains(pay->txid())) throw std::runtime_error("the mempool lost the payment");

    std::cout << "\n8. Alice mines the next block on the new chain, and the payment confirms again:\n";
    mine_next(chain, alice.public_key(), mempool.select(block_budget(p)).txs);
    std::cout << "   Alice  " << format_amount(balance(chain.utxos(), alice.public_key())) << "\n"
              << "   Bob    " << format_amount(balance(chain.utxos(), bob.public_key())) << "\n"
              << "   Carol  " << format_amount(balance(chain.utxos(), carol.public_key())) << "\n"
              << "   supply " << format_amount(chain.utxos().total_value()) << " (" << chain.height() << " blocks x "
              << format_amount(p.initial_subsidy) << ")\n";
    return 0;
}

int cmd_mine(const std::filesystem::path& datadir, int count, std::optional<std::string_view> address) {
    const ConsensusParams& p = testnet_params();
    DataDir data(datadir);
    Chainstate chain(p, data.db, data.blocks);
    PublicKey miner{};
    if (address) {
        miner = parse_address(*address);
    } else {
        miner = KeyPair::generate().public_key();
        std::cout << "No address given: paying a throwaway key, so nobody can spend these coins.\n";
    }
    std::cout << "Mining " << count << " blocks on #" << chain.height() << " in " << datadir.string() << " to "
              << encode_address(miner, p.address_version) << "\n";
    for (int i = 0; i < count; ++i) mine_next(chain, miner, {});
    std::cout << "Tip #" << chain.height() << ". That address holds " << format_amount(balance(chain.utxos(), miner))
              << "\n";
    return 0;
}

// Milestone 6: the node.

std::atomic<bool> g_interrupted{false};  // set by Ctrl+C, or SIGTERM from a service manager

void on_interrupt(int) { g_interrupted = true; }

void log_line(const std::string& text) { std::cout << format_utc(unix_now()) << " " << text << "\n" << std::flush; }

std::uint64_t random_u64() {
    std::random_device rd;
    return std::uint64_t{rd()} << 32 | rd();
}

std::optional<Bytes> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return Bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Written to a temporary file first, so a crash mid-write can't leave half a file behind.
void write_file(const std::filesystem::path& file, const Bytes& data) {
    const std::filesystem::path temp = file.string() + ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) return;
    }
    std::filesystem::rename(temp, file);
}

AddrMan load_peers(const std::filesystem::path& file, std::uint64_t seed) {
    const std::optional<Bytes> data = read_file(file);
    if (!data) return AddrMan(seed);
    try {
        return AddrMan::deserialize(*data, seed);
    } catch (const DeserializeError&) {
        log_line("peers.dat is damaged; starting with no known peers");
        return AddrMan(seed);
    }
}

BanMan load_bans(const std::filesystem::path& file) {
    const std::optional<Bytes> data = read_file(file);
    if (!data) return BanMan();
    try {
        return BanMan::deserialize(*data);
    } catch (const DeserializeError&) {
        log_line("banlist.dat is damaged; starting with no bans");
        return BanMan();
    }
}

// Milestone 7: the RPC cookie, there while the node runs.
class CookieFile {
public:
    CookieFile(std::filesystem::path path, const std::string& cookie) : path_(std::move(path)) {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out << cookie;
        if (!out) throw std::runtime_error("can't write " + path_.string());
    }
    ~CookieFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    CookieFile(const CookieFile&) = delete;
    CookieFile& operator=(const CookieFile&) = delete;

private:
    std::filesystem::path path_;
};

struct NodeOptions {
    std::uint16_t port = 0;
    std::uint16_t rpc_port = 0;
    bool listen = true;
    bool local = false;  // listen on 127.0.0.1 only: for trying several nodes on one machine
    std::vector<std::string> connect;
    std::optional<std::string> mine;  // pay mined blocks to this address
    // Milestone 8.
    std::vector<std::string> seednodes;      // asked for addresses, besides the network's own seeds
    bool use_seeds = true;                   // the network's built-in seeds
    bool allow_private = false;              // take and pass on private addresses (a LAN)
    bool index = false;                      // keep the transaction and address indexes
    std::optional<std::size_t> max_inbound;  // inbound connections to take
};

int cmd_node(const std::filesystem::path& datadir, const NodeOptions& options) {
    const ConsensusParams& p = testnet_params();
    const std::optional<PublicKey> payee = options.mine ? std::optional(parse_address(*options.mine)) : std::nullopt;
    DataDir data(datadir);
    Chainstate chain(p, data.db, data.blocks);
    Mempool mempool(chain);
    const std::uint64_t seed = random_u64();
    const std::filesystem::path peers_file = datadir / "peers.dat";
    AddrMan addrman = load_peers(peers_file, seed);
    // On the public network only public addresses are kept, dialled and passed on. With --local
    // (nodes on one machine) or --allowprivate (on one LAN), any.
    const bool allow_private = options.local || options.allow_private;
    addrman.set_routable_only(!allow_private);
    const std::filesystem::path bans_file = datadir / "banlist.dat";
    BanMan banman = load_bans(bans_file);

    PeerConfig peer_config;
    PeerManager peers(chain, mempool, addrman, banman, peer_config, seed ^ 0x9e3779b97f4a7c15ULL, log_line);
    ConnmanOptions net;
    net.listen = options.listen;
    net.loopback_only = options.local;
    net.port = options.port != 0 ? options.port : p.default_port;
    net.default_port = p.default_port;
    net.connect = options.connect;
    if (options.use_seeds) net.seeds = p.seeds;
    net.seeds.insert(net.seeds.end(), options.seednodes.begin(), options.seednodes.end());
    if (options.max_inbound) net.max_inbound = *options.max_inbound;
    Connman connman(peers, addrman, banman, net, log_line, unix_now);
    peers.set_listen_port(connman.port());

    // Milestone 8: the indexes an explorer needs, kept only when asked for.
    std::optional<LevelDbStore> index_db;
    std::optional<ChainIndex> index;
    if (options.index) {
        index_db.emplace(datadir / "index");
        index.emplace(*index_db, chain, log_line);
    }

    // Milestone 7: the wallet (if one was created) and the RPC server.
    LevelDbStore wallet_db(datadir / "wallet");
    NodeContext node{
        .chain = chain,
        .mempool = mempool,
        .peers = peers,
        .clock = unix_now,
        .wallet_db = &wallet_db,
        .wallet = nullptr,
        .wallet_options = {},
        .request_stop = [&connman] { connman.stop(); },
        .p2p_port = connman.port(),
        .user_agent = peer_config.user_agent,
        .banman = &banman,
        .save_bans = [&] { write_file(bans_file, banman.serialize()); },
        .index = index ? &*index : nullptr,
    };
    if (Wallet::exists(wallet_db)) node.wallet = Wallet::load(wallet_db, chain, mempool);
    RpcTable rpc;
    register_node_methods(rpc, node);
    const std::string cookie = make_cookie();
    HttpServer http(options.rpc_port != 0 ? options.rpc_port : p.default_rpc_port,
                    [&](const HttpRequest& request) { return serve_json_rpc(rpc, cookie, request); });
    const CookieFile cookie_file(cookie_path(datadir), cookie);
    connman.add_client(http);

    log_line(std::format("BOUND testnet node: height {}, {} known peer addresses, data in {}", chain.height(),
                         addrman.size(), datadir.string()));
    log_line(options.listen ? std::format("listening on port {}", connman.port()) : "not listening");
    log_line(std::format("JSON-RPC on 127.0.0.1:{}", http.port()));
    log_line(net.seeds.empty() ? "no seeds: peers come from --connect and from the addresses already known"
                               : std::format("{} seeds to ask for peers if needed", net.seeds.size()));
    if (index) log_line("keeping the transaction and address indexes");
    log_line(node.wallet ? std::format("wallet: {} transactions, {} spendable", node.wallet->transaction_count(),
                                       format_amount(node.wallet->balance().spendable))
                         : "no wallet (bound-cli createwallet <passphrase> makes one)");
    if (payee) log_line("mining to " + encode_address(*payee, p.address_version));

    std::optional<BackgroundMiner> miner;
    if (payee) miner.emplace(p.pow_limit);
    const BlockIndex* work_tip = nullptr;  // the tip the miner's current work builds on
    std::int64_t work_time = 0;
    std::size_t work_mempool = 0;  // the mempool's size when that work was made
    const std::int64_t started = unix_now();
    std::int64_t saved = started;
    std::int64_t expired = started;
    std::int64_t resubmitted = started - 600;  // our own lost transactions go out again soon after starting
    std::int64_t last_rate = started;
    std::uint64_t last_hashes = 0;

    std::signal(SIGINT, on_interrupt);
    std::signal(SIGTERM, on_interrupt);
    connman.run([&] {
        if (g_interrupted) connman.stop();
        const std::int64_t now = unix_now();
        if (miner) {
            if (const std::optional<Block> solved = miner->take_solved()) {
                const BlockAcceptResult r = peers.submit_block(*solved, now);
                const BlockIndex* found = chain.find(solved->hash());
                if (r.accepted() && found != nullptr) {
                    log_line(std::format("mined #{} {} with {} transactions{}", found->height, found->hash.to_hex(), solved->txs.size() - 1,
                                         &chain.tip() == found ? "" : " (not the tip: someone else's block won)"));
                } else {
                    log_line("our own block was rejected");
                }
            }
            // Fresh work when the tip moves, when new transactions arrive (at most every 5 s),
            // and every 30 s (for a current timestamp). No mining during the initial sync: those
            // blocks would only go to waste. (Only then: headers without blocks mustn't be able
            // to stop the miner; see PeerManager::initial_sync.)
            const bool syncing = peers.initial_sync(now);
            const bool stale = &chain.tip() != work_tip || now - work_time >= 30 ||
                               (mempool.size() != work_mempool && now - work_time >= 5);
            if (syncing) {
                if (work_tip != nullptr) miner->set_work(std::nullopt);
                work_tip = nullptr;
            } else if (stale) {
                const auto time = static_cast<std::uint32_t>(now);
                std::optional<Block> tmpl = make_block_template(chain, *payee, mempool.select(block_budget(p)).txs, time);
                if (!tmpl) {
                    log_line("the mempool's transactions don't make a valid block; mining an empty one");
                    tmpl = make_block_template(chain, *payee, {}, time);
                }
                miner->set_work(std::move(tmpl));
                work_tip = &chain.tip();
                work_time = now;
                work_mempool = mempool.size();
            }
            if (now - last_rate >= 60) {
                const std::uint64_t hashes = miner->hashes();
                log_line(std::format("mining at {:.0f} kH/s, difficulty {:.2f}",
                                     static_cast<double>(hashes - last_hashes) / static_cast<double>(now - last_rate) / 1000,
                                     difficulty(next_work_required(chain.tip(), p), p)));
                last_rate = now;
                last_hashes = hashes;
            }
        }
        if (node.wallet) {
            node.wallet->tick(now);
            if (now - resubmitted >= 600) {
                for (const Transaction& tx : node.wallet->to_resubmit()) peers.submit_transaction(tx, now);
                resubmitted = now;
            }
        }
        if (now - expired >= 60) {
            if (const std::size_t n = mempool.expire(now); n > 0) log_line(std::format("{} transactions expired from the mempool", n));
            expired = now;
        }
        if (now - saved >= 300) {
            write_file(peers_file, addrman.serialize());
            saved = now;
        }
    });
    miner.reset();
    write_file(peers_file, addrman.serialize());
    log_line(std::format("stopped at height {}", chain.height()));
    return 0;
}

int cmd_info(const std::filesystem::path& datadir) {
    const ConsensusParams& p = testnet_params();
    DataDir data(datadir);
    const Chainstate chain(p, data.db, data.blocks);
    const BlockIndex& tip = chain.tip();
    const int next_retarget = (tip.height / p.retarget_interval + 1) * p.retarget_interval;
    std::cout << std::format("data directory   {}\n", datadir.string())
              << std::format("network          {}\n", p.network_name)
              << std::format("height           {}\n", tip.height)
              << std::format("tip              {}  {} UTC\n", tip.hash.to_hex(), format_utc(tip.header.time))
              << std::format("difficulty       {:.2f} for the next block, retarget at #{}\n",
                             difficulty(next_work_required(tip, p), p), next_retarget)
              << std::format("chain work       {:.0f} expected hashes\n", tip.chain_work.to_double())
              << std::format("blocks known     {} ({} off the active chain)\n", chain.tree_size(),
                             chain.tree_size() - static_cast<std::size_t>(tip.height + 1))
              << std::format("unspent outputs  {}, worth {}\n", chain.utxos().size(),
                             format_amount(chain.utxos().total_value()));
    return 0;
}

int cmd_balance(const std::filesystem::path& datadir, std::string_view address) {
    const ConsensusParams& p = testnet_params();
    const PublicKey owner = parse_address(address);
    DataDir data(datadir);
    const Chainstate chain(p, data.db, data.blocks);
    Amount spendable = 0;
    for (const auto& [op, coin] : spendable_coins(chain.utxos(), owner, chain.height() + 1, p)) spendable += coin.out.amount;
    std::cout << format_amount(balance(chain.utxos(), owner)) << " at height " << chain.height() << ", of which "
              << format_amount(spendable) << " is spendable in the next block\n";
    return 0;
}

int cmd_genesis(std::string_view message, std::uint32_t time) {
    const ConsensusParams& p = testnet_params();
    if (message.size() > Transaction::kMaxMemo) throw std::invalid_argument("message is longer than 100 bytes");
    Block g;
    g.txs.push_back(make_coinbase(0, block_subsidy(0, p), PublicKey{}, bytes_of(message)));
    g.header.merkle_root = g.compute_merkle_root();
    g.header.time = time;
    g.header.bits = p.pow_limit_bits;
    const std::optional<BlockHeader> solved = mine(g.header, p.pow_limit);
    if (!solved) throw std::runtime_error("no nonce works for this time; try another time");
    std::cout << "message      " << message << "\n"
              << "time         " << time << "\n"
              << "merkle root  " << solved->merkle_root.to_hex() << "\n"
              << "nonce        " << solved->nonce << "\n"
              << "genesis hash " << solved->hash().to_hex() << "\n";
    return 0;
}

int usage() {
    std::cerr << "usage: boundd [--datadir <dir>] node [--port <port>] [--rpcport <port>] [--nolisten | --local]\n"
                 "                                    [--connect <host[:port]>]... [--seednode <host[:port]>]...\n"
                 "                                    [--noseeds] [--maxinbound <n>] [--allowprivate] [--index]\n"
                 "                                    [--mine <address>]\n"
                 "       boundd [--datadir <dir>] mine <count> [address]\n"
                 "       boundd [--datadir <dir>] info\n"
                 "       boundd [--datadir <dir>] balance <address>\n"
                 "       boundd demo\n"
                 "       boundd genesis [message] [unix-time]\n"
                 "The data directory defaults to "
              << default_datadir().string() << ". Talk to a running node with bound-cli.\n";
    return 2;
}

std::uint16_t parse_port(std::string_view text) {
    const unsigned long port = std::stoul(std::string(text));
    if (port == 0 || port > 65535) throw std::invalid_argument("not a port: " + std::string(text));
    return static_cast<std::uint16_t>(port);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string_view> args(argv + 1, argv + argc);
        std::filesystem::path datadir = default_datadir();
        if (args.size() >= 2 && args[0] == "--datadir") {
            datadir = std::filesystem::path(args[1]);
            args.erase(args.begin(), args.begin() + 2);
        }
        if (args.empty()) return usage();
        if (args[0] == "node") {
            NodeOptions options;
            for (std::size_t i = 1; i < args.size(); ++i) {
                const bool has_value = i + 1 < args.size();
                if (args[i] == "--nolisten") {
                    options.listen = false;
                } else if (args[i] == "--local") {
                    options.local = true;
                } else if (args[i] == "--port" && has_value) {
                    options.port = parse_port(args[++i]);
                } else if (args[i] == "--rpcport" && has_value) {
                    options.rpc_port = parse_port(args[++i]);
                } else if (args[i] == "--connect" && has_value) {
                    options.connect.emplace_back(args[++i]);
                } else if (args[i] == "--mine" && has_value) {
                    options.mine = std::string(args[++i]);
                } else if (args[i] == "--seednode" && has_value) {
                    options.seednodes.emplace_back(args[++i]);
                } else if (args[i] == "--noseeds") {
                    options.use_seeds = false;
                } else if (args[i] == "--allowprivate") {
                    options.allow_private = true;
                } else if (args[i] == "--index") {
                    options.index = true;
                } else if (args[i] == "--maxinbound" && has_value) {
                    options.max_inbound = static_cast<std::size_t>(std::stoul(std::string(args[++i])));
                } else {
                    return usage();
                }
            }
            return cmd_node(datadir, options);
        }
        if (args[0] == "mine" && (args.size() == 2 || args.size() == 3)) {
            const std::optional<std::string_view> address = args.size() == 3 ? std::optional(args[2]) : std::nullopt;
            return cmd_mine(datadir, std::stoi(std::string(args[1])), address);
        }
        if (args[0] == "info" && args.size() == 1) return cmd_info(datadir);
        if (args[0] == "balance" && args.size() == 2) return cmd_balance(datadir, args[1]);
        if (args[0] == "demo" && args.size() == 1) return cmd_demo();
        if (args[0] == "genesis" && args.size() <= 3) {
            const std::string default_message = std::string(
                testnet_params().genesis_coinbase.memo.begin(), testnet_params().genesis_coinbase.memo.end());
            const std::string_view message = args.size() >= 2 ? args[1] : std::string_view(default_message);
            const auto time = args.size() == 3 ? static_cast<std::uint32_t>(std::stoul(std::string(args[2])))
                                               : testnet_params().genesis.time;
            return cmd_genesis(message, time);
        }
        return usage();
    } catch (const std::exception& e) {
        std::cerr << "boundd: " << e.what() << "\n";
        return 1;
    }
}
