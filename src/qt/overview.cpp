#include "qt/dialogs.h"
#include "qt/format.h"
#include "qt/pages.h"
#include "qt/style.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace bound::gui {

namespace {

QLabel* card_title(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setObjectName(QStringLiteral("cardTitle"));
    return label;
}

QLabel* value_label(QWidget* parent, const char* object_name = nullptr) {
    auto* label = new QLabel(QStringLiteral("-"), parent);
    if (object_name != nullptr) label->setObjectName(QString::fromLatin1(object_name));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

}  // namespace

OverviewPage::OverviewPage(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 24);
    page->setSpacing(16);
    page->addWidget(page_title(QStringLiteral("Overview"), this));

    auto* top = new QHBoxLayout;
    top->setSpacing(16);

    // Balances, or the offer of a wallet.
    QFrame* money = card(this);
    auto* money_layout = new QVBoxLayout(money);
    money_layout->setContentsMargins(22, 18, 22, 18);
    balances_ = new QWidget(money);
    auto* b = new QGridLayout(balances_);
    b->setContentsMargins(0, 0, 0, 0);
    b->setVerticalSpacing(6);
    b->addWidget(card_title(QStringLiteral("Available"), balances_), 0, 0, 1, 2);
    available_ = value_label(balances_, "bigAmount");
    b->addWidget(available_, 1, 0, 1, 2);
    pending_ = value_label(balances_);
    immature_ = value_label(balances_);
    total_ = value_label(balances_, "mediumValue");
    b->addWidget(muted_label(QStringLiteral("Pending"), balances_), 2, 0);
    b->addWidget(pending_, 2, 1, Qt::AlignRight);
    QLabel* maturing = muted_label(QStringLiteral("Maturing"), balances_);
    maturing->setToolTip(QStringLiteral("Mined coins: spendable once 100 more blocks are on top of theirs"));
    b->addWidget(maturing, 3, 0);
    b->addWidget(immature_, 3, 1, Qt::AlignRight);
    b->addWidget(muted_label(QStringLiteral("Total"), balances_), 4, 0);
    b->addWidget(total_, 4, 1, Qt::AlignRight);
    money_layout->addWidget(balances_);

    no_wallet_ = new QWidget(money);
    auto* n = new QVBoxLayout(no_wallet_);
    n->setContentsMargins(0, 0, 0, 0);
    n->addWidget(card_title(QStringLiteral("Wallet"), no_wallet_));
    auto* none = new QLabel(QStringLiteral("No wallet yet"), no_wallet_);
    none->setObjectName(QStringLiteral("mediumValue"));
    n->addWidget(none);
    n->addWidget(muted_label(QStringLiteral("Create one to get an address and receive coins, or restore one from its seed."), no_wallet_));
    auto* buttons = new QHBoxLayout;
    auto* create = new QPushButton(QStringLiteral("Create a wallet"), no_wallet_);
    create->setObjectName(QStringLiteral("primary"));
    auto* restore = new QPushButton(QStringLiteral("Restore from a seed"), no_wallet_);
    buttons->addWidget(create);
    buttons->addWidget(restore);
    buttons->addStretch();
    n->addLayout(buttons);
    n->addStretch();
    connect(create, &QPushButton::clicked, this, [this] { create_wallet(session_, this); });
    connect(restore, &QPushButton::clicked, this, [this] { restore_wallet(session_, this); });
    money_layout->addWidget(no_wallet_);
    balances_->hide();
    top->addWidget(money, 3);

    // The node.
    QFrame* node = card(this);
    auto* g = new QGridLayout(node);
    g->setContentsMargins(22, 18, 22, 18);
    g->setVerticalSpacing(8);
    g->addWidget(card_title(QStringLiteral("Network"), node), 0, 0, 1, 2);
    height_ = value_label(node);
    peers_ = value_label(node);
    sync_ = value_label(node);
    mining_ = value_label(node);
    g->addWidget(muted_label(QStringLiteral("Height"), node), 1, 0);
    g->addWidget(height_, 1, 1, Qt::AlignRight);
    g->addWidget(muted_label(QStringLiteral("Peers"), node), 2, 0);
    g->addWidget(peers_, 2, 1, Qt::AlignRight);
    g->addWidget(muted_label(QStringLiteral("Chain"), node), 3, 0);
    g->addWidget(sync_, 3, 1, Qt::AlignRight);
    g->addWidget(muted_label(QStringLiteral("Mining"), node), 4, 0);
    g->addWidget(mining_, 4, 1, Qt::AlignRight);
    g->setRowStretch(5, 1);
    top->addWidget(node, 2);
    page->addLayout(top);

    // The latest transactions.
    QFrame* recent = card(this);
    recent_ = new QVBoxLayout(recent);
    recent_->setContentsMargins(22, 18, 22, 18);
    recent_->addWidget(card_title(QStringLiteral("Recent transactions"), recent));
    show_transactions(json::array());
    page->addWidget(recent, 1);
}

void OverviewPage::show_wallet(const json& info) {
    const bool have = info.is_object();
    balances_->setVisible(have);
    no_wallet_->setVisible(!have);
    if (!have) return;
    const Amount spendable = amount_of(info, "balance").value_or(0);
    const Amount pending = amount_of(info, "unconfirmed_balance").value_or(0);
    const Amount immature = amount_of(info, "immature_balance").value_or(0);
    available_->setText(coins_text(spendable));
    pending_->setText(coins_text(pending));
    immature_->setText(coins_text(immature));
    total_->setText(coins_text(spendable + pending + immature));
}

void OverviewPage::show_transactions(const json& list) {
    const std::string key = list.dump();
    if (rows_ != nullptr && key == shown_) return;
    shown_ = key;
    delete rows_;
    rows_ = new QWidget(this);
    auto* grid = new QGridLayout(rows_);
    grid->setContentsMargins(0, 6, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(10);
    int row = 0;
    if (list.is_array()) {
        for (auto it = list.rbegin(); it != list.rend() && row < 8; ++it, ++row) {
            const json& tx = *it;
            const QString category = text_of(tx, "category");
            auto* type = new QLabel(category_text(category), rows_);
            type->setStyleSheet(QStringLiteral("font-weight: 600;"));
            const std::int64_t confirmations = int_of(tx, "confirmations");
            auto* when = muted_label(date_text(int_of(tx, "time")) +
                                         (confirmations == 0 ? QStringLiteral("  (unconfirmed)") : QString()),
                                     rows_);
            when->setWordWrap(false);
            auto* address = muted_label(short_hash(text_of(tx, "address")), rows_);
            address->setFont(mono_font());
            const Amount amount = amount_of(tx, "amount").value_or(0);
            auto* value = new QLabel(coins_text(amount, true), rows_);
            value->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                                     .arg((amount < 0 ? colors().negative : colors().positive).name()));
            grid->addWidget(type, row, 0);
            grid->addWidget(when, row, 1);
            grid->addWidget(address, row, 2);
            grid->addWidget(value, row, 3, Qt::AlignRight);
        }
    }
    if (row == 0) grid->addWidget(muted_label(QStringLiteral("Nothing yet. Payments you send and receive show up here."), rows_), 0, 0);
    grid->setColumnStretch(2, 1);
    grid->setRowStretch(row + 1, 1);
    recent_->addWidget(rows_, 1);
}

void OverviewPage::show_status(const ChainStatus& s, const json& mining) {
    height_->setText(QStringLiteral("#%1").arg(s.blocks));
    peers_->setText(QString::number(s.peers));
    const std::int64_t age = QDateTime::currentSecsSinceEpoch() - s.tip_time;
    if (s.headers > s.blocks) {
        sync_->setText(QStringLiteral("Catching up: %1 blocks to go").arg(s.headers - s.blocks));
    } else if (s.peers == 0) {
        sync_->setText(QStringLiteral("Looking for peers"));
    } else {
        sync_->setText(QStringLiteral("Up to date (last block %1 ago)").arg(age_text(age)));
    }
    const QString status = text_of(mining, "status");
    if (status == QLatin1String("mining")) {
        mining_->setText(QStringLiteral("On, %1").arg(hashrate_text(mining.value("hashespersec", 0.0))));
    } else if (status.isEmpty() || status == QLatin1String("off")) {
        mining_->setText(QStringLiteral("Off"));
    } else {
        mining_->setText(QStringLiteral("On, %1").arg(status));
    }
}

}  // namespace bound::gui
