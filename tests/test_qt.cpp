// Milestone 9: the wallet app (bound-qt) against a real node running inside it, drawn off screen.
// With BOUND_SCREENSHOTS=<dir> it also saves a picture of each page there.
#include "app/datadir.h"
#include "core/address.h"
#include "core/amount.h"
#include "core/params.h"
#include "net/socket.h"
#include "node/node.h"
#include "qt/format.h"
#include "qt/mainwindow.h"
#include "qt/nodehost.h"
#include "qt/style.h"
#include "rpc/client.h"
#include "test_util.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QTableWidget>
#include <QThread>

#include <exception>
#include <functional>

using namespace bound;
using namespace bound::gui;

namespace {

std::uint16_t free_port() { return Socket::listen(0, true).local_port(); }

// Runs the event loop until `done` holds, or `ms` pass.
bool wait_until(const std::function<bool()>& done, int ms = 30'000) {
    QElapsedTimer clock;
    clock.start();
    while (!done()) {
        if (clock.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

bool shows_text(const QWidget* root, const QString& text) {
    for (const QLabel* label : root->findChildren<QLabel*>()) {
        if (label->text() == text) return true;
    }
    return false;
}

void screenshot(MainWindow& window, MainWindow::Page page, const char* name) {
    const QString dir = qEnvironmentVariable("BOUND_SCREENSHOTS");
    if (dir.isEmpty()) return;
    window.show_page(page);
    wait_until([] { return false; }, 2500);  // a refresh for this page's own data
    QDir().mkpath(dir);
    window.grab().save(dir + QStringLiteral("/") + QString::fromLatin1(name) + QStringLiteral(".png"));
}

}  // namespace

TEST(WalletApp, FormatsWhatTheNodeSays) {
    EXPECT_EQ(coins_text(150'000'000), QStringLiteral("1.50000000 BOUND"));
    EXPECT_EQ(coins_text(5 * kCoin, true), QStringLiteral("+5.00000000 BOUND"));
    EXPECT_EQ(coins_text(-191, true), QStringLiteral("-0.00000191 BOUND"));
    EXPECT_EQ(short_hash(QStringLiteral("0000a7ba8cc2ed04fa56bb3eaf23d5879874e7ffd060d81a2aca994f79b79293")),
              QStringLiteral("0000a7ba") + QChar(0x2026) + QStringLiteral("79293"));
    EXPECT_EQ(hashrate_text(1'234'567), QStringLiteral("1.23 MH/s"));
    EXPECT_EQ(hashrate_text(950), QStringLiteral("950 H/s"));
    EXPECT_EQ(age_text(30), QStringLiteral("30 s"));
    EXPECT_EQ(age_text(7200), QStringLiteral("2 h"));
    EXPECT_EQ(category_text(QStringLiteral("generate")), QStringLiteral("Mined"));
    EXPECT_EQ(amount_of(json{{"a", "0.00000420"}}, "a"), std::optional<Amount>(420));
    EXPECT_EQ(amount_of(json{{"a", 1}}, "a"), std::nullopt);
    EXPECT_EQ(int_of(json{{"n", 7}}, "n"), 7);
    EXPECT_EQ(text_of(json{{"s", "x"}, {"n", 3}}, "n"), QStringLiteral("3"));
}

TEST(WalletApp, ConsoleWordsAreSplitLikeAShell) {
    EXPECT_EQ(split_words("help"), (std::vector<std::string>{"help"}));
    EXPECT_EQ(split_words("  sendtoaddress  9abc 1.5 "), (std::vector<std::string>{"sendtoaddress", "9abc", "1.5"}));
    EXPECT_EQ(split_words(R"(walletpassphrase "two words" 60)"), (std::vector<std::string>{"walletpassphrase", "two words", "60"}));
    EXPECT_EQ(split_words(R"(a "" b)"), (std::vector<std::string>{"a", "", "b"}));
    EXPECT_EQ(split_words(R"(say \"hi\" \\)"), (std::vector<std::string>{"say", "\"hi\"", "\\"}));
    EXPECT_THROW(split_words(R"(a "open)"), std::invalid_argument);
    EXPECT_EQ(cli_param("setmining", 0, "true"), json(true));
    EXPECT_EQ(cli_param("sendtoaddress", 2, "2.5"), json(2.5));
    EXPECT_EQ(cli_param("sendtoaddress", 1, "2.5"), json("2.5"));
    EXPECT_THROW(cli_param("setmining", 0, "yes"), std::invalid_argument);
}

// The whole app: a node inside it, a wallet, mining switched on from the app, and the pages
// showing what happened. Then closing the window stops the node.
TEST(WalletApp, RunsANodeInsideAndShowsItsWallet) {
    TempDir dir;
    NodeOptions options;
    options.listen = false;
    options.local = true;
    options.use_seeds = false;  // never the real network
    options.rpc_port = free_port();
    NodeHost host(dir.path(), options);
    host.start();
    ASSERT_TRUE(host.embedded());

    RpcClient rpc(cookie_path(dir.path()), options.rpc_port, 60'000);
    const auto answers = [&rpc] {
        try {
            rpc.call("getblockcount");
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    ASSERT_TRUE(wait_until(answers));
    rpc.call("createwallet", json::array({"correct horse battery"}));

    {
        MainWindow window(host);
        window.show();
        ASSERT_TRUE(wait_until([&] { return window.connected(); }));
        screenshot(window, MainWindow::Overview, "1-overview-empty");

        // Mining is switched on and off through the node, as the Mining page does.
        const json on = rpc.call("setmining", json::array({true}));
        EXPECT_EQ(on["mining"], true);
        EXPECT_TRUE(on["address"].is_string());
        ASSERT_TRUE(wait_until([&] { return rpc.call("getblockcount").get<int>() >= 3; }, 120'000));
        const json off = rpc.call("setmining", json::array({false}));
        EXPECT_EQ(off["mining"], false);
        EXPECT_GE(off["blocksfound"].get<int>(), 3);
        const int height = rpc.call("getblockcount").get<int>();

        // The overview shows the mined coins as maturing, and the list has every block's coinbase.
        const json wallet = rpc.call("getwalletinfo");
        const QString maturing = coins_text(parse_coins(wallet["immature_balance"].get<std::string>()).value());
        EXPECT_EQ(maturing, coins_text(Amount{height} * 5 * kCoin));
        window.refresh();
        EXPECT_TRUE(wait_until([&] { return shows_text(window.page(MainWindow::Overview), maturing); }));
        auto* table = window.page(MainWindow::Transactions)->findChild<QTableWidget*>();
        ASSERT_NE(table, nullptr);
        EXPECT_TRUE(wait_until([&] { return table->rowCount() == height; }));
        EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("Mined (maturing)"));
        EXPECT_TRUE(table->item(0, 2)->text().startsWith(QLatin1Char('9')));
        EXPECT_TRUE(shows_text(window.page(MainWindow::Overview), QStringLiteral("#%1").arg(height)));

        if (!qEnvironmentVariable("BOUND_SCREENSHOTS").isEmpty()) {
            // For the pictures, a wallet with some history: coins old enough to spend, and a payment.
            rpc.call("setmining", json::array({true}));
            ASSERT_TRUE(wait_until([&] { return rpc.call("getblockcount").get<int>() >= 106; }, 600'000));
            rpc.call("setmining", json::array({false}));
            rpc.call("walletpassphrase", json::array({"correct horse battery", 60}));
            rpc.call("sendtoaddress", json::array({encode_address(test_key("bob").public_key(), testnet_params().address_version), "7.25"}));
            rpc.call("walletlock");
            window.refresh();
        }
        screenshot(window, MainWindow::Overview, "2-overview");
        screenshot(window, MainWindow::Send, "3-send");
        screenshot(window, MainWindow::Receive, "4-receive");
        screenshot(window, MainWindow::Transactions, "5-transactions");
        screenshot(window, MainWindow::Mining, "6-mining");
        screenshot(window, MainWindow::Network, "7-network");
        screenshot(window, MainWindow::Console, "8-console");

        // Closing the window stops the node inside, and waits for it.
        window.close();
        EXPECT_TRUE(wait_until([&] { return !host.running(); }));
        EXPECT_TRUE(wait_until([&] { return !window.isVisible(); }));
    }
    EXPECT_FALSE(answers());
}

// A second app on the same data directory uses the node that's already running.
TEST(WalletApp, UsesANodeAlreadyRunning) {
    TempDir dir;
    NodeOptions options;
    options.listen = false;
    options.local = true;
    options.use_seeds = false;
    options.rpc_port = free_port();
    NodeHost first(dir.path(), options);
    first.start();
    ASSERT_TRUE(first.embedded());
    RpcClient rpc(cookie_path(dir.path()), options.rpc_port);
    ASSERT_TRUE(wait_until([&rpc] {
        try {
            rpc.call("getblockcount");
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }));
    NodeHost second(dir.path(), options);
    second.start();
    EXPECT_FALSE(second.embedded());
    EXPECT_FALSE(second.running());
}

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", BOUND_TEST_PLATFORM);
    QApplication app(argc, argv);
    apply_style(app);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
