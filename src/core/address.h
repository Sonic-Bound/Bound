#pragma once

#include "crypto/ed25519.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace bound {

// Base58Check(version || 32-byte Ed25519 public key): 37 bytes, 51 characters. Longer than a
// Bitcoin address (34) because BOUND pays to the key itself, not to a 20-byte hash of it. The
// version byte (ConsensusParams::address_version) fixes the first character and keeps networks
// apart; with a 37-byte payload only the digits 2-9 can be fixed that way.
std::string encode_address(const PublicKey& pk, std::uint8_t version);

// The key, or nullopt unless s is valid Base58Check carrying exactly 33 bytes that start with
// `version`. Rejects strings over 64 characters before decoding: Base58 decoding is quadratic,
// and addresses come from users and peers.
std::optional<PublicKey> decode_address(std::string_view s, std::uint8_t version);

}  // namespace bound
