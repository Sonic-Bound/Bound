#include "qt/format.h"
#include "qt/pages.h"
#include "qt/style.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace bound::gui {

namespace {

const QString kAddressKey = QStringLiteral("receive/address");

}  // namespace

ReceivePage::ReceivePage(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 24);
    page->setSpacing(16);
    page->addWidget(page_title(QStringLiteral("Receive"), this));

    QFrame* box = card(this);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("YOUR ADDRESS"), box);
    title->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(title);
    address_ = new QLabel(QStringLiteral("-"), box);
    address_->setFont(mono_font(14));
    address_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    address_->setWordWrap(true);
    layout->addWidget(address_);
    auto* buttons = new QHBoxLayout;
    copy_ = new QPushButton(QStringLiteral("Copy"), box);
    copy_->setObjectName(QStringLiteral("primary"));
    new_ = new QPushButton(QStringLiteral("New address"), box);
    buttons->addWidget(copy_);
    buttons->addWidget(new_);
    buttons->addStretch();
    layout->addLayout(buttons);
    note_ = muted_label(QString(), box);
    layout->addWidget(note_);
    page->addWidget(box);

    page->addWidget(muted_label(QStringLiteral("Give this address to whoever pays you, or mine to it. A fresh address "
                                               "for each payer keeps who paid you private. Every address the wallet "
                                               "makes stays yours: they all come from your seed, so one backup of the "
                                               "seed covers them all."),
                                this));
    page->addStretch();

    connect(copy_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(address_->text());
        note_->setText(QStringLiteral("Copied."));
    });
    connect(new_, &QPushButton::clicked, this, &ReceivePage::new_address);
    show_wallet(json());
}

void ReceivePage::show_wallet(const json& info) {
    const bool have = info.is_object();
    copy_->setEnabled(have && address_->text().startsWith(QLatin1Char('9')));
    new_->setEnabled(have);
    if (!have) {
        address_->setText(QStringLiteral("No wallet yet: create one first (Overview)."));
        asked_ = false;
        return;
    }
    if (asked_) return;
    asked_ = true;
    // The address shown last time, if it's still this wallet's; otherwise a new one.
    const QString saved = QSettings(session_.settings_file(), QSettings::IniFormat).value(kAddressKey).toString();
    if (saved.isEmpty()) {
        new_address();
        return;
    }
    session_.rpc().call(
        "validateaddress", json::array({saved.toStdString()}), this,
        [this, saved](const json& r) {
            if (r.value("ismine", false)) {
                address_->setText(saved);
                copy_->setEnabled(true);
            } else {
                new_address();
            }
        },
        [this](const RpcFailure&) { asked_ = false; });
}

void ReceivePage::new_address() {
    session_.rpc().call(
        "getnewaddress", json::array(), this,
        [this](const json& r) {
            const QString address = QString::fromStdString(r.get<std::string>());
            address_->setText(address);
            copy_->setEnabled(true);
            note_->setText(QString());
            QSettings(session_.settings_file(), QSettings::IniFormat).setValue(kAddressKey, address);
        },
        [this](const RpcFailure& f) {
            asked_ = false;
            Session::show_error(this, QStringLiteral("Can't make an address"), f);
        });
}

}  // namespace bound::gui
