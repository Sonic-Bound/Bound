#include "qt/dialogs.h"

#include "core/params.h"
#include "qt/format.h"
#include "qt/session.h"
#include "qt/style.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <string>

namespace bound::gui {

namespace {

constexpr int kMinPassphrase = 8;

QLineEdit* password_field(QWidget* parent) {
    auto* field = new QLineEdit(parent);
    field->setEchoMode(QLineEdit::Password);
    field->setMinimumWidth(320);
    return field;
}

// A busy indicator while something slow (a rescan) runs; closed by deleting it.
QProgressDialog* busy(QWidget* parent, const QString& text) {
    auto* dialog = new QProgressDialog(text, QString(), 0, 0, parent);
    dialog->setWindowTitle(QStringLiteral("Please wait"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setCancelButton(nullptr);
    dialog->setMinimumDuration(0);
    dialog->show();
    return dialog;
}

// Two passphrase fields that must match, at least kMinPassphrase long; "" if cancelled. With
// `old`, a field for the current passphrase too, whose text goes there.
QString new_passphrase(QWidget* parent, const QString& title, const QString& explanation, QString* old = nullptr) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(muted_label(explanation, &dialog));
    auto* form = new QFormLayout;
    QLineEdit* old_field = nullptr;
    if (old != nullptr) {
        old_field = password_field(&dialog);
        form->addRow(QStringLiteral("Current passphrase"), old_field);
    }
    QLineEdit* first = password_field(&dialog);
    QLineEdit* second = password_field(&dialog);
    form->addRow(QStringLiteral("New passphrase"), first);
    form->addRow(QStringLiteral("Repeat it"), second);
    layout->addLayout(form);
    auto* problem = new QLabel(&dialog);
    problem->setStyleSheet(QStringLiteral("color: %1;").arg(colors().negative.name()));
    layout->addWidget(problem);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    ok->setObjectName(QStringLiteral("primary"));
    const auto check = [&] {
        QString why;
        if (old_field != nullptr && old_field->text().isEmpty()) {
            why = QStringLiteral("Type the current passphrase.");
        } else if (first->text().size() < kMinPassphrase) {
            why = QStringLiteral("At least %1 characters.").arg(kMinPassphrase);
        } else if (first->text() != second->text()) {
            why = QStringLiteral("The two don't match.");
        }
        problem->setText(why);
        ok->setEnabled(why.isEmpty());
    };
    QObject::connect(first, &QLineEdit::textChanged, &dialog, check);
    QObject::connect(second, &QLineEdit::textChanged, &dialog, check);
    if (old_field != nullptr) QObject::connect(old_field, &QLineEdit::textChanged, &dialog, check);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    check();
    if (dialog.exec() != QDialog::Accepted) return {};
    if (old != nullptr) *old = old_field->text();
    return first->text();
}

// The seed's dialog. A fresh seed's can't be closed (Esc, the title bar's X) before the box is ticked.
class SeedDialog : public QDialog {
public:
    using QDialog::QDialog;
    QCheckBox* must_tick = nullptr;

    void reject() override {
        if (must_tick == nullptr || must_tick->isChecked()) QDialog::reject();
    }
};

QString seed_grouped(const QString& seed) {
    QString text;
    for (int i = 0; i < seed.size(); i += 8) {
        if (i > 0) text += (i % 32 == 0) ? QStringLiteral("\n") : QStringLiteral("  ");
        text += seed.mid(i, 8);
    }
    return text;
}

}  // namespace

std::optional<QString> ask_passphrase(QWidget* parent, const QString& purpose) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Unlock the wallet"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Your passphrase, to %1:").arg(purpose), &dialog));
    QLineEdit* field = password_field(&dialog);
    layout->addWidget(field);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Unlock"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || field->text().isEmpty()) return std::nullopt;
    return field->text();
}

void offer_wallet(Session& session, QWidget* parent) {
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("Welcome to BOUND"));
    box.setIconPixmap(logo(64));
    box.setText(QStringLiteral("<b>You don't have a wallet yet.</b>"));
    const ConsensusParams& p = session.params();
    QString network_note;
    if (p.network_name != "mainnet") {
        network_note = QStringLiteral("\n\nThis is the %1: its coins have no value, so it's the place to try things.").arg(session.network_name());
    } else if (!p.launched) {
        network_note = QStringLiteral("\n\nMainnet hasn't launched: this chain is a preview, and its coins will be thrown away.");
    } else {
        network_note = QStringLiteral("\n\nThis is mainnet: keep the seed safe, it's the only way back to your coins.");
    }
    box.setInformativeText(QStringLiteral("A wallet holds the keys to your coins. It lives in this app's data folder, encrypted "
                                          "with a passphrase, and all of its keys come from one seed you write down.") +
                           network_note);
    QPushButton* create = box.addButton(QStringLiteral("Create a new wallet"), QMessageBox::AcceptRole);
    create->setObjectName(QStringLiteral("primary"));
    QPushButton* restore = box.addButton(QStringLiteral("Restore from a seed"), QMessageBox::ActionRole);
    box.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == create) create_wallet(session, parent);
    if (box.clickedButton() == restore) restore_wallet(session, parent);
}

void create_wallet(Session& session, QWidget* parent) {
    QString passphrase = new_passphrase(
        parent, QStringLiteral("Create a wallet"),
        QStringLiteral("Choose a passphrase. It encrypts the wallet on this computer and is asked for whenever you "
                       "spend. If you forget it, only the seed (shown next) can bring the coins back."));
    if (passphrase.isEmpty()) return;
    QPointer<QWidget> owner(parent);
    session.rpc().call(
        "createwallet", json::array({passphrase.toStdString()}), &session,
        [&session, owner](const json& result) {
            show_seed(owner, text_of(result, "seed"), true);
            session.refresh();
        },
        [owner](const RpcFailure& f) { Session::show_error(owner, QStringLiteral("Can't create the wallet"), f); });
    passphrase.fill(QLatin1Char(' '));
}

void restore_wallet(Session& session, QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Restore a wallet"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(muted_label(QStringLiteral("Type the wallet's seed: 64 hexadecimal digits (spaces and line breaks "
                                                 "don't matter). The node then scans the whole chain for its coins."),
                                  &dialog));
    auto* seed_field = new QPlainTextEdit(&dialog);
    seed_field->setFont(mono_font());
    seed_field->setFixedHeight(70);
    layout->addWidget(seed_field);
    auto* problem = new QLabel(&dialog);
    problem->setStyleSheet(QStringLiteral("color: %1;").arg(colors().negative.name()));
    layout->addWidget(problem);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Next"));
    layout->addWidget(buttons);
    const auto cleaned = [seed_field] {
        return seed_field->toPlainText().remove(QRegularExpression(QStringLiteral("\\s"))).toLower();
    };
    const auto check = [&] {
        const QString seed = cleaned();
        const bool hex = QRegularExpression(QStringLiteral("^[0-9a-f]*$")).match(seed).hasMatch();
        const QString why = !hex ? QStringLiteral("Only the digits 0-9 and letters a-f.")
                            : seed.size() != 64 ? QStringLiteral("%1 of 64 digits.").arg(seed.size())
                                                : QString();
        problem->setText(why);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(why.isEmpty());
    };
    QObject::connect(seed_field, &QPlainTextEdit::textChanged, &dialog, check);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    check();
    if (dialog.exec() != QDialog::Accepted) return;
    QString seed = cleaned();
    seed_field->clear();

    QString passphrase = new_passphrase(parent, QStringLiteral("Restore a wallet"),
                                        QStringLiteral("Choose a passphrase to encrypt the restored wallet on this computer."));
    if (passphrase.isEmpty()) return;
    QPointer<QWidget> owner(parent);
    QPointer<QProgressDialog> wait = busy(parent, QStringLiteral("Scanning the chain for the wallet's coins..."));
    session.rpc().call(
        "restorewallet", json::array({seed.toStdString(), passphrase.toStdString()}), &session,
        [&session, owner, wait](const json& result) {
            delete wait.data();
            QMessageBox::information(owner, QStringLiteral("Wallet restored"),
                                     QStringLiteral("Found %1 transactions. Spendable now: %2 BOUND.")
                                         .arg(text_of(result, "transactions"), text_of(result, "balance")));
            session.refresh();
        },
        [owner, wait](const RpcFailure& f) {
            delete wait.data();
            Session::show_error(owner, QStringLiteral("Can't restore the wallet"), f);
        });
    seed.fill(QLatin1Char(' '));
    passphrase.fill(QLatin1Char(' '));
}

void show_seed(QWidget* parent, const QString& seed, bool fresh) {
    SeedDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Your wallet's seed"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        fresh ? QStringLiteral("<b>Write this seed down</b> and keep it somewhere safe, offline.")
              : QStringLiteral("<b>Your wallet's seed.</b> Keep it somewhere safe, offline."),
        &dialog));
    layout->addWidget(muted_label(QStringLiteral("Every key of the wallet comes from it: with the seed, the wallet can be "
                                                 "restored on any computer. Anyone who sees it can take your coins, so "
                                                 "never type it into a website or send it to anyone."),
                                  &dialog));
    auto* text = new QLabel(seed_grouped(seed), &dialog);
    text->setFont(mono_font(13));
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text->setAlignment(Qt::AlignCenter);
    text->setObjectName(QStringLiteral("banner"));
    layout->addWidget(text);
    auto* written = new QCheckBox(QStringLiteral("I've written it down"), &dialog);
    if (fresh) layout->addWidget(written);
    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* copy = buttons->addButton(QStringLiteral("Copy"), QDialogButtonBox::ActionRole);
    QPushButton* done = buttons->addButton(QStringLiteral("Done"), QDialogButtonBox::AcceptRole);
    done->setObjectName(QStringLiteral("primary"));
    done->setEnabled(!fresh);
    layout->addWidget(buttons);
    QObject::connect(copy, &QPushButton::clicked, &dialog, [seed] { QApplication::clipboard()->setText(seed); });
    QObject::connect(written, &QCheckBox::toggled, done, &QPushButton::setEnabled);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    if (fresh) dialog.must_tick = written;
    dialog.exec();
    if (QApplication::clipboard()->text() == seed) QApplication::clipboard()->clear();
}

void backup_seed(Session& session, QWidget* parent) {
    if (!session.has_wallet()) return;
    QPointer<QWidget> owner(parent);
    session.with_unlocked(parent, QStringLiteral("show the seed"), [&session, owner](Session::Finish finish) {
        session.rpc().call(
            "dumpseed", json::array(), &session,
            [owner, finish](const json& seed) {
                show_seed(owner, QString::fromStdString(seed.get<std::string>()), false);
                finish();
            },
            [owner, finish](const RpcFailure& f) {
                Session::show_error(owner, QStringLiteral("Can't show the seed"), f);
                finish();
            });
    });
}

void change_passphrase(Session& session, QWidget* parent) {
    if (!session.has_wallet()) return;
    QString old;
    QString next = new_passphrase(parent, QStringLiteral("Change the passphrase"),
                                  QStringLiteral("The wallet is encrypted again under the new passphrase. The seed doesn't change."),
                                  &old);
    if (next.isEmpty()) return;
    QPointer<QWidget> owner(parent);
    session.rpc().call(
        "walletpassphrasechange", json::array({old.toStdString(), next.toStdString()}), &session,
        [owner](const json&) { QMessageBox::information(owner, QStringLiteral("Passphrase changed"), QStringLiteral("Done.")); },
        [owner](const RpcFailure& f) { Session::show_error(owner, QStringLiteral("Can't change the passphrase"), f); });
    old.fill(QLatin1Char(' '));
    next.fill(QLatin1Char(' '));
}

void show_transaction(Session& session, QWidget* parent, const QString& txid) {
    QPointer<QWidget> owner(parent);
    session.rpc().call(
        "gettransaction", json::array({txid.toStdString()}), &session,
        [&session, owner, txid](const json& tx) {
            QDialog dialog(owner);
            dialog.setWindowTitle(QStringLiteral("Transaction"));
            dialog.setMinimumWidth(620);
            auto* layout = new QVBoxLayout(&dialog);
            auto* form = new QFormLayout;
            form->setHorizontalSpacing(18);
            const std::int64_t confirmations = int_of(tx, "confirmations");
            const QString category = text_of(tx, "category");
            const QString status = confirmations < 0    ? QStringLiteral("Lost: its block left the chain in a reorganisation")
                                   : confirmations == 0 ? QStringLiteral("Unconfirmed: waiting in the mempool")
                                                        : QStringLiteral("%1 confirmations").arg(confirmations);
            const auto add = [&](const QString& name, const QString& value, bool mono = false) {
                auto* label = new QLabel(value, &dialog);
                label->setTextInteractionFlags(Qt::TextSelectableByMouse);
                label->setWordWrap(true);
                if (mono) label->setFont(mono_font());
                form->addRow(name, label);
            };
            add(QStringLiteral("Status"), status);
            add(QStringLiteral("Date"), date_text(int_of(tx, "time")));
            add(QStringLiteral("Type"), category_text(category));
            const std::optional<Amount> amount = amount_of(tx, "amount");
            add(QStringLiteral("Amount"), amount ? coins_text(*amount, true) : text_of(tx, "amount"));
            if (tx.contains("fee")) add(QStringLiteral("Fee"), text_of(tx, "fee") + QStringLiteral(" BOUND"));
            if (tx.contains("address")) add(QStringLiteral("Address"), text_of(tx, "address"), true);
            add(QStringLiteral("Transaction ID"), text_of(tx, "txid"), true);
            if (tx.contains("blockheight")) {
                add(QStringLiteral("Block"), QStringLiteral("#%1  %2").arg(text_of(tx, "blockheight"), short_hash(text_of(tx, "blockhash"))));
            }
            QString outputs;
            if (const auto details = tx.find("details"); details != tx.end() && details->is_array()) {
                for (const json& out : *details) {
                    outputs += QStringLiteral("%1  %2%3\n")
                                   .arg(text_of(out, "address"), text_of(out, "amount"),
                                        out.value("mine", false) ? QStringLiteral("  (yours)") : QString());
                }
            }
            add(QStringLiteral("Outputs"), outputs.trimmed(), true);
            layout->addLayout(form);

            auto* buttons = new QDialogButtonBox(&dialog);
            QPushButton* copy = buttons->addButton(QStringLiteral("Copy ID"), QDialogButtonBox::ActionRole);
            QObject::connect(copy, &QPushButton::clicked, &dialog, [txid] { QApplication::clipboard()->setText(txid); });
            if (category == QLatin1String("send") && confirmations == 0) {
                QPushButton* bump = buttons->addButton(QStringLiteral("Raise the fee"), QDialogButtonBox::ActionRole);
                bump->setToolTip(QStringLiteral("Replace it with the same payment paying a higher fee (replace-by-fee)"));
                QObject::connect(bump, &QPushButton::clicked, &dialog, [&session, &dialog, txid] {
                    QPointer<QWidget> top(dialog.parentWidget());
                    dialog.accept();
                    session.with_unlocked(top, QStringLiteral("raise the fee"), [&session, top, txid](Session::Finish finish) {
                        session.rpc().call(
                            "bumpfee", json::array({txid.toStdString()}), &session,
                            [&session, top, finish](const json& r) {
                                finish();
                                QMessageBox::information(top, QStringLiteral("Fee raised"),
                                                         QStringLiteral("The replacement pays %1 BOUND (was %2).\nIts ID: %3")
                                                             .arg(text_of(r, "fee"), text_of(r, "origfee"), text_of(r, "txid")));
                                session.refresh();
                            },
                            [top, finish](const RpcFailure& f) {
                                finish();
                                Session::show_error(top, QStringLiteral("Can't raise the fee"), f);
                            });
                    });
                });
            }
            QPushButton* close = buttons->addButton(QDialogButtonBox::Close);
            close->setObjectName(QStringLiteral("primary"));
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            dialog.exec();
        },
        [owner](const RpcFailure& f) { Session::show_error(owner, QStringLiteral("Can't show the transaction"), f); });
}

void show_about(QWidget* parent) {
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("About BOUND"));
    box.setIconPixmap(logo(72));
    box.setText(QStringLiteral("<h3>BOUND wallet %1</h3>").arg(QStringLiteral(BOUND_VERSION)));
    box.setInformativeText(
        QStringLiteral("<p>A proof-of-work cryptocurrency written from scratch in C++20: UTXO ledger, SHA-256d mining, "
                       "Ed25519 signatures. This app is a full node with a wallet, like Bitcoin-Qt.</p>"
                       "<p>Networks: mainnet (from launch day), testnet and regtest, whose coins have no value.</p>"
                       "<p>MIT License. Built with Qt %1.<br>"
                       "<a href=\"https://github.com/Sonic-Bound/Bound\">github.com/Sonic-Bound/Bound</a></p>")
            .arg(QString::fromLatin1(qVersion())));
    box.setTextFormat(Qt::RichText);
    box.exec();
}

}  // namespace bound::gui
