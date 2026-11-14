#include "core/address.h"

#include "util/base58.h"

#include <algorithm>

namespace bound {

std::string encode_address(const PublicKey& pk, std::uint8_t version) {
    Bytes payload{version};
    payload.insert(payload.end(), pk.begin(), pk.end());
    return base58check_encode(payload);
}

std::optional<PublicKey> decode_address(std::string_view s, std::uint8_t version) {
    if (s.size() > 64) return std::nullopt;
    const std::optional<Bytes> payload = base58check_decode(s);
    if (!payload || payload->size() != 33 || (*payload)[0] != version) return std::nullopt;
    PublicKey pk;
    std::copy(payload->begin() + 1, payload->end(), pk.begin());
    return pk;
}

}  // namespace bound
