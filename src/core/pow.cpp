#include "core/pow.h"

#include "core/chain.h"
#include "core/params.h"

#include <algorithm>
#include <stdexcept>

namespace bound {

std::optional<uint256> target_from_compact(std::uint32_t bits) {
    const int size = static_cast<int>(bits >> 24);
    std::uint32_t word = bits & 0x007fffff;
    if (size <= 3) word >>= 8 * (3 - size);
    const bool negative = word != 0 && (bits & 0x00800000) != 0;
    const bool overflow = word != 0 && (size > 34 || (word > 0xff && size > 33) || (word > 0xffff && size > 32));
    if (negative || overflow) return std::nullopt;

    Digest32 b{};
    const int low = size <= 3 ? 0 : size - 3;  // byte index of the mantissa's low byte
    for (int i = 0; i < 3; ++i) {
        const int pos = low + i;
        if (pos < 32) b[pos] = static_cast<std::uint8_t>(word >> (8 * i));
    }
    return uint256(b);
}

std::uint32_t compact_from_target(const uint256& target) {
    const Digest32& b = target.bytes();
    int size = 32;
    while (size > 0 && b[size - 1] == 0) --size;
    std::uint32_t compact = 0;
    for (int i = 0; i < 3; ++i) {
        const int pos = size - 1 - i;
        compact <<= 8;
        if (pos >= 0) compact |= b[pos];
    }
    if (compact & 0x00800000) {
        compact >>= 8;
        ++size;
    }
    return compact | (static_cast<std::uint32_t>(size) << 24);
}

bool check_proof_of_work(const uint256& hash, std::uint32_t bits, const uint256& pow_limit) {
    const std::optional<uint256> target = target_from_compact(bits);
    if (!target || target->is_zero() || *target > pow_limit) return false;
    return hash <= *target;
}

arith_uint256 block_proof(std::uint32_t bits) {
    const std::optional<uint256> decoded = target_from_compact(bits);
    if (!decoded || decoded->is_zero()) return 0;
    // 2^256 doesn't fit in 256 bits, but 2^256 / (t + 1) == (2^256 - t - 1) / (t + 1) + 1, and
    // 2^256 - t - 1 is ~t. (t + 1 can't wrap: no compact value decodes to 2^256 - 1.)
    const arith_uint256 target(*decoded);
    return ~target / (target + 1) + 1;
}

std::uint32_t retarget(std::uint32_t bits, std::int64_t actual_timespan, std::int64_t expected_timespan,
                       const uint256& pow_limit) {
    if (expected_timespan <= 0 || expected_timespan > (std::int64_t{1} << 30)) {
        throw std::invalid_argument("expected_timespan out of range");
    }
    const std::int64_t actual = std::clamp(actual_timespan, expected_timespan / 4, expected_timespan * 4);
    // target * actual can need more than 256 bits when the target is near a high pow_limit, so
    // multiply and divide in one exact step.
    const std::optional<arith_uint256> target =
        mul_div(arith_uint256(target_from_compact(bits).value()), static_cast<std::uint32_t>(actual),
                static_cast<std::uint32_t>(expected_timespan));
    if (!target || *target > arith_uint256(pow_limit)) return compact_from_target(pow_limit);
    return compact_from_target(target->to_uint256());
}

std::uint32_t next_work_required(const BlockIndex& parent, const ConsensusParams& params) {
    return next_work_required(
        parent.height, parent.header.bits, parent.header.time,
        [&](int h) { return static_cast<std::int64_t>(parent.ancestor(h)->header.time); }, params);
}

std::uint32_t next_work_required(int parent_height, std::uint32_t parent_bits, std::int64_t parent_time,
                                 const std::function<std::int64_t(int)>& time_at, const ConsensusParams& params) {
    if (params.pow_no_retargeting) return parent_bits;  // regtest
    const int interval = params.retarget_interval;
    if ((parent_height + 1) % interval != 0) return parent_bits;

    // The window starts at the last block of the previous period, so consecutive windows share an
    // endpoint and together measure every second of the chain. Bitcoin starts one block later
    // (2015 gaps for 2016 blocks), which leaves the gap between periods unmeasured: the off-by-one
    // behind the timewarp attack. Bitcoin can only patch that with a soft fork (BIP54); a new
    // chain can fix the window itself, as Litecoin did.
    const int first = std::max(0, parent_height - interval);
    const std::int64_t actual = parent_time - time_at(first);
    const std::int64_t expected = std::int64_t{parent_height - first} * params.target_spacing;
    return retarget(parent_bits, actual, expected, params.pow_limit);
}

double difficulty(std::uint32_t bits, const ConsensusParams& params) {
    const std::optional<uint256> target = target_from_compact(bits);
    if (!target || target->is_zero()) return 0;
    return arith_uint256(params.pow_limit).to_double() / arith_uint256(*target).to_double();
}

}  // namespace bound
