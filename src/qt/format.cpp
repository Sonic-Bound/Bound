#include "qt/format.h"

#include "core/params.h"

#include <QDateTime>

#include <string>

namespace bound::gui {

QString text_of(const json& j, const char* key) {
    if (!j.is_object()) return {};
    const auto found = j.find(key);
    if (found == j.end() || found->is_null()) return {};
    if (found->is_string()) return QString::fromStdString(found->get<std::string>());
    return QString::fromStdString(found->dump());
}

std::int64_t int_of(const json& j, const char* key, std::int64_t fallback) {
    if (!j.is_object()) return fallback;
    const auto found = j.find(key);
    if (found == j.end() || !found->is_number()) return fallback;
    return found->get<std::int64_t>();
}

std::optional<Amount> amount_of(const json& j, const char* key) {
    if (!j.is_object()) return std::nullopt;
    const auto found = j.find(key);
    if (found == j.end() || !found->is_string()) return std::nullopt;
    return parse_coins(found->get<std::string>());
}

QString coins_text(Amount amount, bool sign) {
    QString text = QString::fromStdString(format_coins(amount));
    if (sign && amount > 0) text.prepend(QLatin1Char('+'));
    return text + QLatin1Char(' ') + QString::fromUtf8(kTicker.data(), static_cast<qsizetype>(kTicker.size()));
}

QString date_text(std::int64_t unix_time) {
    return QDateTime::fromSecsSinceEpoch(unix_time).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString age_text(std::int64_t seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds < 90) return QStringLiteral("%1 s").arg(seconds);
    if (seconds < 90 * 60) return QStringLiteral("%1 min").arg(seconds / 60);
    if (seconds < 36 * 3600) return QStringLiteral("%1 h").arg(seconds / 3600);
    return QStringLiteral("%1 days").arg(seconds / 86400);
}

QString short_hash(const QString& hash) {
    if (hash.size() <= 16) return hash;
    return hash.left(8) + QChar(0x2026) + hash.right(5);
}

QString hashrate_text(double h) {
    if (h >= 1e9) return QStringLiteral("%1 GH/s").arg(h / 1e9, 0, 'f', 2);
    if (h >= 1e6) return QStringLiteral("%1 MH/s").arg(h / 1e6, 0, 'f', 2);
    if (h >= 1e3) return QStringLiteral("%1 kH/s").arg(h / 1e3, 0, 'f', 1);
    return QStringLiteral("%1 H/s").arg(h, 0, 'f', 0);
}

QString category_text(const QString& category) {
    if (category == QLatin1String("send")) return QStringLiteral("Sent");
    if (category == QLatin1String("receive")) return QStringLiteral("Received");
    if (category == QLatin1String("generate")) return QStringLiteral("Mined");
    if (category == QLatin1String("immature")) return QStringLiteral("Mined (maturing)");
    if (category == QLatin1String("orphan")) return QStringLiteral("Mined (lost to a reorg)");
    return category;
}

}  // namespace bound::gui
