#pragma once

// The wallet app's pages. Each shows what the main window hands it from the node's answers
// (show_*), and acts through the session.

#include "qt/session.h"

#include <QLabel>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <string>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QVBoxLayout;

namespace bound::gui {

// The chain and network as the status bar and overview need them.
struct ChainStatus {
    std::int64_t blocks = 0;
    std::int64_t headers = 0;
    std::int64_t tip_time = 0;
    std::int64_t peers = 0;
};

class OverviewPage : public QWidget {
public:
    explicit OverviewPage(Session& session, QWidget* parent = nullptr);
    void show_wallet(const json& info);  // null: no wallet
    void show_transactions(const json& list);
    void show_status(const ChainStatus& status, const json& mining);

private:
    Session& session_;
    QLabel* available_;
    QLabel* pending_;
    QLabel* immature_;
    QLabel* total_;
    QWidget* balances_;
    QWidget* no_wallet_;
    QLabel* height_;
    QLabel* peers_;
    QLabel* sync_;
    QLabel* mining_;
    QVBoxLayout* recent_;
    QWidget* rows_ = nullptr;  // the recent transactions, rebuilt on each change
    std::string shown_;        // what rows_ shows, to skip rebuilding it for nothing
};

class SendPage : public QWidget {
public:
    explicit SendPage(Session& session, QWidget* parent = nullptr);
    void show_wallet(const json& info);
    void show_fee_estimate(double units_per_byte);

private:
    void check();
    void send();

    Session& session_;
    QLineEdit* address_;
    QLabel* address_note_;
    QLineEdit* amount_;
    QLabel* available_;
    QLineEdit* memo_;
    QRadioButton* auto_fee_;
    QRadioButton* custom_fee_;
    QLineEdit* fee_rate_;
    QLabel* fee_note_;
    QPushButton* send_;
    std::optional<double> estimate_;
};

class ReceivePage : public QWidget {
public:
    explicit ReceivePage(Session& session, QWidget* parent = nullptr);
    void show_wallet(const json& info);

private:
    void new_address();

    Session& session_;
    QLabel* address_;
    QPushButton* copy_;
    QPushButton* new_;
    QLabel* note_;
    bool asked_ = false;
};

class TransactionsPage : public QWidget {
public:
    explicit TransactionsPage(Session& session, QWidget* parent = nullptr);
    void show_transactions(const json& list);

private:
    void fill();

    Session& session_;
    QComboBox* filter_;
    QTableWidget* table_;
    QLabel* empty_;
    json list_ = json::array();
};

class MiningPage : public QWidget {
public:
    explicit MiningPage(Session& session, QWidget* parent = nullptr);
    void show_mining(const json& info);
    void show_network_hashrate(double hashes_per_second);

private:
    void toggle();

    Session& session_;
    QPushButton* toggle_;
    QLineEdit* payee_;
    QSpinBox* threads_;  // milestone 12
    QLabel* status_;
    QLabel* rate_;
    QLabel* found_;
    QLabel* difficulty_;
    QLabel* network_rate_;
    QLabel* paying_;
    bool mining_ = false;
};

class NetworkPage : public QWidget {
public:
    explicit NetworkPage(Session& session, QWidget* parent = nullptr);
    void show_network(const json& info);
    void show_peers(const json& list);

private:
    Session& session_;
    QLabel* counts_;
    QLabel* port_;
    QLabel* reachable_;
    QLabel* version_;
    QTableWidget* peers_;
};

class ConsolePage : public QWidget {
public:
    explicit ConsolePage(Session& session, QWidget* parent = nullptr);
    void append_log(const QString& line);
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void run(const QString& line);
    void print(const QString& html);

    Session& session_;
    QTabWidget* tabs_;
    QPlainTextEdit* output_;
    QLineEdit* input_;
    QPlainTextEdit* log_;
    QStringList history_;
    int history_pos_ = 0;
};

}  // namespace bound::gui
