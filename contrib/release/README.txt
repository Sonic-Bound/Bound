BOUND
=====

A proof-of-work cryptocurrency written from scratch in C++20.
Source, documentation and releases: https://github.com/Sonic-Bound/Bound

These programs run the BOUND testnet unless told otherwise, and testnet coins have no value.
Mainnet hasn't launched yet: --mainnet runs a preview chain that will be thrown away.


The programs
  bound-qt        the desktop wallet: a full node with a window (in the Windows package)
  boundd          a node without a window: boundd node
  bound-cli       talks to a running node: bound-cli help
  bound-explorer  a block explorer for a node started with --index: http://127.0.0.1:8080
  bound-seeder    a DNS seeder, for those running the network's seeds

  Each answers --version. Each takes --testnet (the default), --mainnet or --regtest, and
  --datadir <dir>.


Getting started
  Windows: double-click bound-qt.exe. The programs aren't code-signed yet, so Windows may say
  "Windows protected your PC". Check the download first (below), then choose More info, Run anyway.
  To mine, open Mining and press Start mining.

  Linux: bin/boundd node starts a node; bin/bound-cli getblockchaininfo shows how far it has synced.
  bin/bound-cli setmining true mines into the node's wallet (bound-cli createwallet makes one).
  The programs need glibc 2.35 or newer: Ubuntu 22.04, Debian 12, Fedora 36 or later.
  share/bound/systemd has service files for running a node, the explorer or the seeder on a server.

  The chain and the wallet are kept in %APPDATA%\BOUND\testnet on Windows and ~/.bound/testnet on
  Linux. Write down the seed the wallet shows when it is made: it is the only way to get the
  coins back. For others to reach your node, allow TCP port 18333 in the firewall.


Checking the download
  SHA256SUMS, on the release page, lists the SHA-256 of every file:
    Windows:  Get-FileHash bound-<version>-win64.zip
    Linux:    sha256sum --ignore-missing -c SHA256SUMS

  GitHub also attests each file: proof that it was built by the project's release workflow, from
  the tagged source, and not changed since. With the GitHub CLI:
    gh attestation verify <file> --repo Sonic-Bound/Bound


Licenses
  BOUND is under the MIT license (LICENSE). The libraries built into it keep their own licenses,
  in licenses/: libsodium (ISC), LevelDB (BSD 3-clause) and nlohmann/json (MIT); in the Windows
  package also Qt (LGPL v3), zlib, libpng, PCRE2 and double-conversion, which Qt uses.
