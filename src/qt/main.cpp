// bound-qt: the BOUND desktop wallet. Like Bitcoin-Qt it's a full node with a window: it runs the
// node inside itself (or uses a boundd already running on the same data directory) and shows the
// wallet, payments, mining and the network.
//   bound-qt [--datadir <dir>] [the node options of `boundd node`: --connect, --port, --index, ...]
#include "app/datadir.h"
#include "node/node.h"
#include "qt/mainwindow.h"
#include "qt/nodehost.h"
#include "qt/style.h"

#include <QApplication>
#include <QMessageBox>
#include <QStringList>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BOUND wallet"));
    QApplication::setOrganizationName(QStringLiteral("BOUND"));
    bound::gui::apply_style(app);

    // Qt has taken its own options (-style and so on) out already.
    std::vector<std::string> words;
    const QStringList arguments = QApplication::arguments();
    for (qsizetype i = 1; i < arguments.size(); ++i) words.push_back(arguments[i].toStdString());
    std::filesystem::path datadir = bound::default_datadir();
    if (words.size() >= 2 && words[0] == "--datadir") {
        datadir = std::filesystem::path(std::u8string(words[1].begin(), words[1].end()));
        words.erase(words.begin(), words.begin() + 2);
    }
    const std::vector<std::string_view> views(words.begin(), words.end());
    std::optional<bound::NodeOptions> options;
    try {
        options = bound::parse_node_options(views);
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, QStringLiteral("BOUND wallet"), QString::fromStdString(e.what()));
        return 2;
    }
    if (!options) {
        QMessageBox::information(nullptr, QStringLiteral("BOUND wallet"),
                                 QStringLiteral("usage: bound-qt [--datadir <dir>] [node options]\n\nThe node options are "
                                                "those of `boundd node`: --connect, --seednode, --port, --rpcport, --index, "
                                                "--externalip, ..."));
        return 2;
    }

    bound::gui::NodeHost host(datadir, *options);
    host.start();
    bound::gui::MainWindow window(host);
    window.show();
    return app.exec();
}
