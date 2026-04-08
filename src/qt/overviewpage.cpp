// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/tapyrusunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>
#include <QAbstractItemDelegate>
#include <QPainter>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

Q_DECLARE_METATYPE(interfaces::WalletBalances)

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(TapyrusUnits::TPC),
        platformStyle(_platformStyle)
    {
    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const override
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        bool isToken = index.data(TransactionTableModel::IsTokenRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        QString amountText;
        qint64 netAmount;
        if (isToken) {
            netAmount = index.data(TransactionTableModel::TokenAmountRole).toLongLong();
            amountText = TapyrusUnits::format(TapyrusUnits::TOKEN, netAmount, true, TapyrusUnits::separatorAlways);
        } else {
            netAmount = index.data(TransactionTableModel::AmountRole).toLongLong();
            amountText = TapyrusUnits::formatWithUnit(unit, netAmount, true, TapyrusUnits::separatorAlways);
        }

        if(netAmount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled);
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, &QAbstractItemView::clicked, this, &OverviewPage::handleTransactionClicked);

    // Token box: hidden until a wallet with tokens is loaded
    ui->frameToken->setVisible(false);

    connect(ui->comboToken, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OverviewPage::onTokenSelectionChanged);

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, &QAbstractButton::clicked, this, &OverviewPage::handleOutOfSyncWarningClicks);
    connect(ui->labelTransactionsStatus, &QAbstractButton::clicked, this, &OverviewPage::handleOutOfSyncWarningClicks);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::updateTpcBalances()
{
    if (!walletModel || !walletModel->getOptionsModel())
        return;

    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    ColorIdentifier tpc; // default-constructed = NONE = TPC

    auto conf_it = m_balances.balances.find(tpc);
    CAmount balance = (conf_it != m_balances.balances.end()) ? conf_it->second : 0;

    auto unconf_it = m_balances.unconfirmed_balances.find(tpc);
    CAmount unconfirmed = (unconf_it != m_balances.unconfirmed_balances.end()) ? unconf_it->second : 0;

    auto wo_it = m_balances.watch_only_balances.find(tpc);
    CAmount watchOnly = (wo_it != m_balances.watch_only_balances.end()) ? wo_it->second : 0;

    auto wou_it = m_balances.unconfirmed_watch_only_balances.find(tpc);
    CAmount watchOnlyUnconf = (wou_it != m_balances.unconfirmed_watch_only_balances.end()) ? wou_it->second : 0;

    ui->labelBalance->setText(TapyrusUnits::formatWithUnit(unit, balance, false, TapyrusUnits::separatorAlways));
    ui->labelUnconfirmed->setText(TapyrusUnits::formatWithUnit(unit, unconfirmed, false, TapyrusUnits::separatorAlways));
    ui->labelTotal->setText(TapyrusUnits::formatWithUnit(unit, balance + unconfirmed, false, TapyrusUnits::separatorAlways));
    ui->labelWatchAvailable->setText(TapyrusUnits::formatWithUnit(unit, watchOnly, false, TapyrusUnits::separatorAlways));
    ui->labelWatchPending->setText(TapyrusUnits::formatWithUnit(unit, watchOnlyUnconf, false, TapyrusUnits::separatorAlways));
    ui->labelWatchTotal->setText(TapyrusUnits::formatWithUnit(unit, watchOnly + watchOnlyUnconf, false, TapyrusUnits::separatorAlways));
}

void OverviewPage::refreshTokenList()
{
    if (!walletModel)
        return;

    // Remember the currently selected colorId so we can restore it after repopulating
    QString selectedColorId;
    int cur = ui->comboToken->currentIndex();
    if (cur >= 0 && cur < m_tokenRecords.size())
        selectedColorId = m_tokenRecords[cur].colorId;

    // Fetch all issued tokens and keep only those with a non-zero total balance
    m_tokenRecords.clear();
    for (const WalletModel::IssuedTokenRecord& rec : walletModel->getIssuedTokens()) {
        if (rec.balance + rec.unconfirmedBalance != 0)
            m_tokenRecords.append(rec);
    }

    // Block signals while repopulating to avoid spurious onTokenSelectionChanged calls
    ui->comboToken->blockSignals(true);
    ui->comboToken->clear();

    static const QMap<QString, QString> typeIcons = {
        {"REISSUABLE",     ":/icons/token_reissuable"},
        {"NON_REISSUABLE", ":/icons/token_nonreissuable"},
        {"NFT",            ":/icons/token_nft"},
    };

    for (const WalletModel::IssuedTokenRecord& rec : m_tokenRecords) {
        QIcon icon(typeIcons.value(rec.tokenType));
        ui->comboToken->addItem(icon, rec.colorId, rec.colorId);
        int idx = ui->comboToken->count() - 1;
        ui->comboToken->setItemData(idx, rec.colorId, Qt::ToolTipRole);
    }

    ui->comboToken->blockSignals(false);

    // Restore previous selection by colorId (stored as item data), else default to first item
    int restoreIndex = 0;
    if (!selectedColorId.isEmpty()) {
        int found = ui->comboToken->findData(selectedColorId);
        if (found >= 0)
            restoreIndex = found;
    }

    bool hasTokens = ui->comboToken->count() > 0;
    ui->frameToken->setVisible(hasTokens);

    if (hasTokens) {
        ui->comboToken->setCurrentIndex(restoreIndex);
        updateTokenBalance(restoreIndex);
    }
}

void OverviewPage::onTokenSelectionChanged(int index)
{
    updateTokenBalance(index);
}

void OverviewPage::updateTokenBalance(int index)
{
    if (index < 0 || index >= m_tokenRecords.size()) {
        ui->labelTokenBalance->setText("0");
        ui->labelTokenUnconfirmed->setText("0");
        ui->labelTokenTotal->setText("0");
        return;
    }

    const WalletModel::IssuedTokenRecord& rec = m_tokenRecords[index];

    // Balance values
    ui->labelTokenBalance->setText(TapyrusUnits::format(TapyrusUnits::TOKEN, rec.balance, false, TapyrusUnits::separatorAlways));
    ui->labelTokenUnconfirmed->setText(TapyrusUnits::format(TapyrusUnits::TOKEN, rec.unconfirmedBalance, false, TapyrusUnits::separatorAlways));
    ui->labelTokenTotal->setText(TapyrusUnits::format(TapyrusUnits::TOKEN, rec.balance + rec.unconfirmedBalance, false, TapyrusUnits::separatorAlways));

    // Tooltips: append colorId to existing tooltip text
    QString colorTip = QString("\nColor ID: %1").arg(rec.colorId);
    ui->labelTokenBalance->setToolTip(tr("Confirmed token balance for the selected token") + colorTip);
    ui->labelTokenUnconfirmed->setToolTip(tr("Unconfirmed token balance for the selected token") + colorTip);
    ui->labelTokenTotal->setToolTip(tr("Total token balance (confirmed + unconfirmed)") + colorTip);
}

void OverviewPage::setBalance(const interfaces::WalletBalances& balances)
{
    m_balances = balances;
    updateTpcBalances();
    refreshTokenList();
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);
    ui->labelWatchonly->setVisible(showWatchOnly);
    ui->lineWatchBalance->setVisible(showWatchOnly);
    ui->labelWatchAvailable->setVisible(showWatchOnly);
    ui->labelWatchPending->setVisible(showWatchOnly);
    ui->labelWatchTotal->setVisible(showWatchOnly);
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        connect(model, &ClientModel::alertsChanged, this, &OverviewPage::updateAlerts);
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        interfaces::Wallet& wallet = model->wallet();
        interfaces::WalletBalances balances = wallet.getBalances();
        setBalance(balances);
        connect(model, &WalletModel::balanceChanged, this, &OverviewPage::setBalance);
        connect(model, &WalletModel::tokenListChanged, this, [this](const QList<WalletModel::IssuedTokenRecord>&){ refreshTokenList(); });

        connect(model->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &OverviewPage::updateDisplayUnit);

        updateWatchOnlyLabels(wallet.haveWatchOnly());
        connect(model, &WalletModel::notifyWatchonlyChanged, this, &OverviewPage::updateWatchOnlyLabels);
    }

    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        updateTpcBalances();
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();
        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}
