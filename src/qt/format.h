#pragma once

// How the wallet app shows the node's answers: amounts, times, hashes, transaction kinds.

#include "core/amount.h"
#include "rpc/server.h"

#include <QString>

#include <cstdint>
#include <optional>

namespace bound::gui {

QString text_of(const json& j, const char* key);           // a string or number field as text; "" if missing
std::int64_t int_of(const json& j, const char* key, std::int64_t fallback = 0);
std::optional<Amount> amount_of(const json& j, const char* key);  // an RPC amount ("1.50000000")

// "1.50000000 BOUND"; with `sign`, positive amounts get a "+".
QString coins_text(Amount amount, bool sign = false);
QString date_text(std::int64_t unix_time);    // local time, "2026-11-15 11:38"
QString age_text(std::int64_t seconds);       // "40 s", "3 min", "5 h", "2 days"
QString short_hash(const QString& hash);      // "0000a7ba…79293"
QString hashrate_text(double hashes_per_second);
// listtransactions' category as the app shows it: Received, Sent, Mined, ...
QString category_text(const QString& category);

}  // namespace bound::gui
