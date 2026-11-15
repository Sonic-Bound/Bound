#include "qt/dialogs.h"
#include "qt/format.h"
#include "qt/pages.h"
#include "qt/style.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>

namespace bound::gui {

namespace {

constexpr int kTxidRole = Qt::UserRole;
constexpr int kSortRole = Qt::UserRole + 1;

// An item sorted by a number of its own (kSortRole) rather than by its text.
class SortedItem : public QTableWidgetItem {
public:
    SortedItem(const QString& text, double key) : QTableWidgetItem(text) { setData(kSortRole, key); }
    bool operator<(const QTableWidgetItem& other) const override {
        return data(kSortRole).toDouble() < other.data(kSortRole).toDouble();
    }
};

bool passes(const QString& category, int filter) {
    switch (filter) {
    case 1: return category == QLatin1String("receive");
    case 2: return category == QLatin1String("send");
    case 3: return category == QLatin1String("generate") || category == QLatin1String("immature") || category == QLatin1String("orphan");
    default: return true;
    }
}

}  // namespace

TransactionsPage::TransactionsPage(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 24);
    page->setSpacing(14);
    auto* top = new QHBoxLayout;
    top->addWidget(page_title(QStringLiteral("Transactions"), this));
    top->addStretch();
    filter_ = new QComboBox(this);
    filter_->addItems({QStringLiteral("All"), QStringLiteral("Received"), QStringLiteral("Sent"), QStringLiteral("Mined")});
    top->addWidget(filter_);
    page->addLayout(top);

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Date"), QStringLiteral("Type"), QStringLiteral("Address"), QStringLiteral("Amount"), QStringLiteral("Confirmations")});
    table_->verticalHeader()->hide();
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setSortIndicator(0, Qt::DescendingOrder);  // newest first
    table_->setSortingEnabled(true);
    page->addWidget(table_, 1);
    empty_ = muted_label(QStringLiteral("No transactions yet."), this);
    page->addWidget(empty_);
    page->addWidget(muted_label(QStringLiteral("Double-click a transaction for its details."), this));

    connect(filter_, &QComboBox::currentIndexChanged, this, &TransactionsPage::fill);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (const QTableWidgetItem* item = table_->item(row, 0)) show_transaction(session_, this, item->data(kTxidRole).toString());
    });
}

void TransactionsPage::show_transactions(const json& list) {
    if (list == list_) return;
    list_ = list.is_array() ? list : json::array();
    fill();
}

void TransactionsPage::fill() {
    const int sort_column = table_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder order = table_->horizontalHeader()->sortIndicatorOrder();
    table_->setSortingEnabled(false);
    table_->setRowCount(0);
    for (auto it = list_.rbegin(); it != list_.rend(); ++it) {
        const json& tx = *it;
        const QString category = text_of(tx, "category");
        if (!passes(category, filter_->currentIndex())) continue;
        const int row = table_->rowCount();
        table_->insertRow(row);
        const std::int64_t time = int_of(tx, "time");
        auto* date = new SortedItem(date_text(time), static_cast<double>(time));
        date->setData(kTxidRole, text_of(tx, "txid"));
        table_->setItem(row, 0, date);
        table_->setItem(row, 1, new QTableWidgetItem(category_text(category)));
        auto* address = new QTableWidgetItem(text_of(tx, "address"));
        address->setFont(mono_font());
        table_->setItem(row, 2, address);
        const Amount amount = amount_of(tx, "amount").value_or(0);
        auto* value = new SortedItem(coins_text(amount, true), static_cast<double>(amount));
        value->setForeground(amount < 0 ? colors().negative : colors().positive);
        value->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 3, value);
        const std::int64_t confirmations = int_of(tx, "confirmations");
        auto* conf = new SortedItem(confirmations < 0    ? QStringLiteral("lost")
                                    : confirmations == 0 ? QStringLiteral("unconfirmed")
                                                         : QString::number(confirmations),
                                    static_cast<double>(confirmations));
        conf->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 4, conf);
    }
    table_->setSortingEnabled(true);
    table_->sortByColumn(sort_column, order);
    empty_->setVisible(table_->rowCount() == 0);
}

}  // namespace bound::gui
