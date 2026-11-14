# BOUND

A proof-of-work cryptocurrency written from scratch in C++20: UTXO ledger, SHA-256d mining,
Ed25519 signatures (libsodium), Base58Check addresses. It's a testnet-first learning project, not
meant to hold real value.

## Roadmap

| # | Milestone | Status |
|---|---|---|
| 1 | Hashing, wire format, block header, proof of work, header chain | ✅ done ([notes](docs/milestone-1.md)) |
| 2 | Keys, addresses, transactions, merkle tree, blocks, block reward | ✅ done ([notes](docs/milestone-2.md)) |
| 3 | UTXO set, full validation, coinbase maturity, undo data, minimal wallet | ✅ done ([notes](docs/milestone-3.md)) |
| 4 | Difficulty retargeting, block tree, most-work chain, reorgs | ✅ done ([notes](docs/milestone-4.md)) |
| 5 | Storage: blocks and the UTXO set on disk, survive restarts | ✅ done ([notes](docs/milestone-5.md)) |
| 6 | P2P networking: handshake, block relay, initial sync, address gossip | ✅ done ([notes](docs/milestone-6.md)) |
| 7 | Mempool, transaction relay, wallet, command-line client / RPC | ✅ done ([notes](docs/milestone-7.md)) |
| 7½ | Hardening: discouragement, bans, headers presync | ✅ done ([notes](docs/hardening.md)) |
| 8 | Public testnet: seeds and a DNS seeder, block explorer, Linux, a node among strangers | ✅ done ([notes](docs/milestone-8.md)) |

All eight are done. Going public now takes machines rather than code: the launch checklist is in
[docs/milestone-8.md](docs/milestone-8.md#launching-the-public-testnet).

**What it can do today:**
- **Network:** nodes talk to each other over TCP. A node syncs the chain from its peers, relays
  blocks and transactions, finds other nodes through address gossip, and can mine while it runs.
- **Storage:** it keeps its chain on disk and carries on where it stopped, even after a crash.
- **Consensus:** every rule is enforced: double spends, signatures, amounts, reward, maturity and
  difficulty. The node follows the valid branch with the most work, reorganising when another
  overtakes it.
- **Payments:** they wait in each node's mempool until a block takes the best-paying ones, and can
  be replaced by a higher fee.
- **Wallet:** every key comes from one seed, kept encrypted under a passphrase. It sends, receives
  and bumps fees through `bound-cli`, which talks JSON-RPC to the running node.
- **Defences:** a peer that breaks a rule is turned away for a day, and operators can ban
  addresses. Headers of a chain with too little work are checked twice before any is stored, so a
  flood of cheap headers can't fill the disk. Withheld blocks, unrequested blocks and floods of
  requests or addresses can't stall or swamp a node.
- **Going public:** a node finds the network through seed names. `bound-seeder` is a DNS seeder
  for them, and `bound-explorer` is a block explorer for the web. Everything builds and runs on
  Linux as well as Windows, with systemd units for servers.

## Testnet parameters

60-second blocks · difficulty retargets every 1,440 blocks (1 day), at most 4× each time ·
timestamps at most 2 hours ahead · 5 BOUND reward halving every 2,100,000 blocks (~4 years) ·
total supply 20,999,999.727 BOUND · coinbase maturity 100 blocks · max block 1 MB · addresses
start with `9` · P2P port 18333, RPC port 18332 (127.0.0.1 only).

Relay policy, not consensus: at least 1 unit per byte in fees · no outputs under 420 units ·
full replace-by-fee · at most 25 unconfirmed ancestors or descendants.

Genesis `0000a7ba8cc2ed04fa56bb3eaf23d5879874e7ffd060d81a2aca994f79b79293`, message:
*"BOUND testnet 01/Nov/2026: a proof-of-work coin built from scratch in C++"*.

## Build and run

**Linux** (tested on Ubuntu 24.04 with GCC 13):

    sudo apt install build-essential cmake ninja-build pkg-config \
                     libsodium-dev libleveldb-dev libsnappy-dev nlohmann-json3-dev libgtest-dev
    cmake --preset linux
    cmake --build --preset linux-release
    build/linux/Release/bound_tests

The programs are in `build/linux/Release`; the commands below work the same without `.exe`. To
run them as services, see [contrib/systemd](contrib/systemd).

**Windows** needs Visual Studio 2022 with *C++ CMake tools for Windows* and *vcpkg package
manager*. From **Developer PowerShell for VS 2022**:

    cmake --preset msvc                  # first run builds gtest, libsodium, leveldb, nlohmann-json via vcpkg
    cmake --build --preset release
    ctest --preset debug                 # or build\msvc\Release\bound_tests.exe
    build\msvc\Release\boundd.exe demo   # in memory: mine, pay through the mempool, lose it to a
                                         # reorg, see the mempool put it back
    build\msvc\Release\boundd.exe node   # join the network (port 18333), RPC on 127.0.0.1:18332
    build\msvc\Release\boundd.exe node --connect <host[:port]> --mine <address>
    build\msvc\Release\boundd.exe node --seednode <host[:port]> --index   # find peers through a seed;
                                                                          # keep the explorer's indexes
    build\msvc\Release\bound-explorer.exe        # a block explorer for that node: http://127.0.0.1:8080
    build\msvc\Release\bound-seeder.exe --host <seed name> --ns <this machine's name> --node <a node>
                                                 # a DNS seeder (see docs/milestone-8.md)

While a node runs, talk to it with `bound-cli` (`bound-cli help` lists everything):

    build\msvc\Release\bound-cli.exe createwallet "passphrase"   # prints the seed: write it down
    build\msvc\Release\bound-cli.exe getnewaddress
    build\msvc\Release\bound-cli.exe generatetoaddress 101 <address>
    build\msvc\Release\bound-cli.exe walletpassphrase "passphrase" 600
    build\msvc\Release\bound-cli.exe sendtoaddress <address> 1.5
    build\msvc\Release\bound-cli.exe getwalletinfo
    build\msvc\Release\bound-cli.exe setban 203.0.113.7 add 3600   # listbanned, clearbanned
    build\msvc\Release\bound-cli.exe getaddressbalance <address>   # with --index: any address
    build\msvc\Release\bound-cli.exe getrawtransaction <txid> 2    # with --index: any transaction
    build\msvc\Release\bound-cli.exe stop

Offline, on a data directory no node is using:

    build\msvc\Release\boundd.exe mine 10 <address>   # on the stored chain; run again to continue
    build\msvc\Release\boundd.exe info
    build\msvc\Release\boundd.exe balance <address>
    build\msvc\Release\boundd.exe --datadir $env:TEMP\bound-retarget mine 2885
                                         # a fresh chain, ~6 min: difficulty 1 at #1440, 4x at #2880
    build\msvc\Release\boundd.exe genesis "message" 1793491200

(`ctest --preset debug` needs a debug build first: `cmake --build --preset debug`.)

The chain is stored in `%APPDATA%\BOUND\testnet` (`~/.bound/testnet` on Linux), or wherever
`--datadir` points; give `bound-cli` and `bound-explorer` the same one. The wallet lives in its
`wallet` folder, the indexes in `index`.

## Layout

    src/util     hex, serialization, Base58
    src/crypto   SHA-256, Ed25519 (libsodium wrappers)
    src/core     consensus: uint256, arith_uint256, pow, block, transaction, merkle,
                 params, chain (block tree), coins, validation, miner, wallet (single-key helpers);
                 storage: storage (interfaces, block files), leveldb_store, chaindb;
                 policy: mempool, amount
    src/net      P2P: socket (raw OS sockets, TCP and UDP), protocol, addrman, peerman, connman,
                 banman (discouragement, bans), headerssync (headers presync), seeds
    src/wallet   crypter (passphrase encryption), wallet (seed-derived keys, history, payments)
    src/rpc      http (server and client), server (JSON-RPC, cookie auth), methods, client
    src/index    chainindex: transactions, spent outputs, addresses (--index)
    src/explorer the block explorer's pages
    src/seeder   the DNS seeder: crawler, dns, dns_server
    src/app      boundd, bound-cli, bound-explorer, bound-seeder
    tests        GoogleTest suites, one per area
    docs         per-milestone design notes
    contrib      systemd units for the node, the explorer and the seeder
    triplets     vcpkg triplet: build packages with the developer shell's MSVC version
