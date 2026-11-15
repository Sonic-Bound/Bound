#include "core/address.h"
#include "core/params.h"
#include "core/transaction.h"
#include "qt/format.h"
#include "qt/pages.h"
#include "qt/style.h"

#include <QButtonGroup>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <string>

namespace bound::gui {

namespace {

std::optional<Amount> typed_amount(const QLineEdit* field) {
    const std::optional<Amount> a = parse_coins(field->text().trimmed().toStdString());
    if (!a || *a <= 0) return std::nullopt;
    return a;
}

std::optional<double> typed_rate(const QLineEdit* field) {
    bool ok = false;
    const double v = field->text().trimmed().toDouble(&ok);
    if (!ok || v < 0 || v > 1e8) return std::nullopt;
    return v;
}

}  // namespace

SendPage::SendPage(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 24);
    page->setSpacing(16);
    page->addWidget(page_title(QStringLiteral("Send"), this));

    QFrame* form = card(this);
    auto* g = new QGridLayout(form);
    g->setContentsMargins(22, 20, 22, 20);
    g->setHorizontalSpacing(16);
    g->setVerticalSpacing(10);

    address_ = new QLineEdit(form);
    address_->setFont(mono_font());
    address_->setPlaceholderText(QStringLiteral("A BOUND testnet address: 51 characters starting with 9"));
    address_note_ = muted_label(QString(), form);
    g->addWidget(new QLabel(QStringLiteral("Pay to"), form), 0, 0);
    g->addWidget(address_, 0, 1, 1, 3);
    g->addWidget(address_note_, 1, 1, 1, 3);

    amount_ = new QLineEdit(form);
    amount_->setPlaceholderText(QStringLiteral("0.00000000"));
    amount_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^\\d{0,8}(\\.\\d{0,8})?$")), amount_));
    amount_->setMaximumWidth(220);
    available_ = muted_label(QString(), form);
    available_->setWordWrap(false);
    g->addWidget(new QLabel(QStringLiteral("Amount"), form), 2, 0);
    g->addWidget(amount_, 2, 1);
    g->addWidget(new QLabel(QStringLiteral("BOUND"), form), 2, 2);
    g->addWidget(available_, 2, 3, Qt::AlignRight);

    memo_ = new QLineEdit(form);
    memo_->setPlaceholderText(QStringLiteral("Optional, at most 100 bytes. Everyone can read it on the chain."));
    g->addWidget(new QLabel(QStringLiteral("Memo"), form), 3, 0);
    g->addWidget(memo_, 3, 1, 1, 3);

    auto_fee_ = new QRadioButton(QStringLiteral("Automatic: what gets it into the next block"), form);
    custom_fee_ = new QRadioButton(QStringLiteral("Custom:"), form);
    auto* group = new QButtonGroup(this);
    group->addButton(auto_fee_);
    group->addButton(custom_fee_);
    auto_fee_->setChecked(true);
    fee_rate_ = new QLineEdit(form);
    fee_rate_->setPlaceholderText(QStringLiteral("1"));
    fee_rate_->setMaximumWidth(120);
    fee_rate_->setEnabled(false);
    fee_note_ = muted_label(QString(), form);
    auto* custom = new QHBoxLayout;
    custom->addWidget(custom_fee_);
    custom->addWidget(fee_rate_);
    custom->addWidget(new QLabel(QStringLiteral("units per byte"), form));
    custom->addStretch();
    g->addWidget(new QLabel(QStringLiteral("Fee"), form), 4, 0, Qt::AlignTop);
    g->addWidget(auto_fee_, 4, 1, 1, 3);
    g->addLayout(custom, 5, 1, 1, 3);
    g->addWidget(fee_note_, 6, 1, 1, 3);

    send_ = new QPushButton(QStringLiteral("Send"), form);
    send_->setObjectName(QStringLiteral("primary"));
    send_->setMinimumWidth(140);
    auto* clear = new QPushButton(QStringLiteral("Clear"), form);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(clear);
    buttons->addWidget(send_);
    g->addLayout(buttons, 7, 0, 1, 4);
    g->setColumnStretch(3, 1);
    page->addWidget(form);

    page->addWidget(warning_banner(QStringLiteral("A payment can't be called back once a block has it. While it waits "
                                                  "unconfirmed, Transactions can raise its fee to speed it up."),
                                   this));
    page->addStretch();

    connect(address_, &QLineEdit::textChanged, this, &SendPage::check);
    connect(amount_, &QLineEdit::textChanged, this, &SendPage::check);
    connect(memo_, &QLineEdit::textChanged, this, &SendPage::check);
    connect(fee_rate_, &QLineEdit::textChanged, this, &SendPage::check);
    connect(custom_fee_, &QRadioButton::toggled, this, [this](bool on) {
        fee_rate_->setEnabled(on);
        check();
    });
    connect(send_, &QPushButton::clicked, this, &SendPage::send);
    connect(clear, &QPushButton::clicked, this, [this] {
        address_->clear();
        amount_->clear();
        memo_->clear();
        auto_fee_->setChecked(true);
    });
    check();
}

void SendPage::show_wallet(const json& info) {
    if (!info.is_object()) {
        available_->setText(QStringLiteral("No wallet"));
    } else {
        available_->setText(QStringLiteral("Available: ") + coins_text(amount_of(info, "balance").value_or(0)));
    }
    check();
}

void SendPage::show_fee_estimate(double units_per_byte) {
    estimate_ = units_per_byte;
    check();
}

void SendPage::check() {
    const QString address = address_->text().trimmed();
    const bool address_ok = decode_address(address.toStdString(), testnet_params().address_version).has_value();
    if (address.isEmpty()) {
        address_note_->setText(QString());
    } else if (address_ok) {
        address_note_->setText(QStringLiteral("A valid testnet address."));
    } else {
        address_note_->setText(QStringLiteral("Not a testnet address: check it (the last characters are a checksum)."));
    }
    const bool memo_ok = memo_->text().toUtf8().size() <= static_cast<qsizetype>(Transaction::kMaxMemo);
    const bool fee_ok = auto_fee_->isChecked() || typed_rate(fee_rate_).has_value();
    fee_note_->setText(estimate_ ? QStringLiteral("The next block takes about %1 units per byte now; a payment is about 150 bytes.")
                                       .arg(*estimate_, 0, 'f', 1)
                                 : QString());
    if (!memo_ok) fee_note_->setText(QStringLiteral("The memo is longer than 100 bytes."));
    send_->setEnabled(session_.has_wallet() && address_ok && typed_amount(amount_).has_value() && memo_ok && fee_ok);
}

void SendPage::send() {
    const QString address = address_->text().trimmed();
    const std::optional<Amount> amount = typed_amount(amount_);
    if (!amount) return;
    const bool automatic = auto_fee_->isChecked();
    const std::optional<double> rate = automatic ? std::nullopt : typed_rate(fee_rate_);
    const QString memo = memo_->text();
    const QString fee = automatic ? QStringLiteral("automatic") : QStringLiteral("%1 units per byte").arg(*rate);
    if (QMessageBox::question(this, QStringLiteral("Send BOUND"),
                              QStringLiteral("Send %1 to\n%2?\n\nFee: %3").arg(coins_text(*amount), address, fee)) !=
        QMessageBox::Yes) {
        return;
    }
    json params = json::array({address.toStdString(), format_coins(*amount)});
    if (rate || !memo.isEmpty()) params.push_back(rate ? json(*rate) : json(nullptr));
    if (!memo.isEmpty()) params.push_back(memo.toStdString());

    QPointer<SendPage> self(this);
    session_.with_unlocked(this, QStringLiteral("send the payment"), [this, self, params](Session::Finish finish) {
        session_.rpc().call(
            "sendtoaddress", params, this,
            [this, finish](const json& txid) {
                finish();
                address_->clear();
                amount_->clear();
                memo_->clear();
                QMessageBox::information(this, QStringLiteral("Sent"),
                                         QStringLiteral("The payment is on its way.\nTransaction ID: %1")
                                             .arg(QString::fromStdString(txid.get<std::string>())));
                session_.refresh();
            },
            [self, finish](const RpcFailure& f) {
                finish();
                Session::show_error(self, QStringLiteral("Can't send"), f);
            });
    });
}

}  // namespace bound::gui
