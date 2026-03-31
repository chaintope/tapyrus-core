// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qt/test/wallettests.h>
#include <qt/test/util.h>

#include <interfaces/node.h>
#include <qt/tapyrusamountfield.h>
#include <base58.h>
#include <qt/callback.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendcoinsentry.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <key_io.h>
#include <test/test_tapyrus.h>
#include <validation.h>
#include <wallet/wallet.h>
#include <qt/overviewpage.h>
#include <qt/receivecoinsdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/receiverequestdialog.h>

#include <memory>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>
#include <QTemporaryFile>
#include <QComboBox>
#include <QLabel>
#include <QRadioButton>
#include <QTableView>
#include <QTableWidget>
#include <coloridentifier.h>
#include <qt/issuetoken.h>
#include <qt/transactionrecord.h>
#include <qt/walletmodeltransaction.h>
#include <wallet/coincontrol.h>

namespace
{
//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfirmSend(QString* text = nullptr, bool cancel = false)
{
    QTimer::singleShot(0, makeCallback([text, cancel](Callback* callback) {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("SendConfirmationDialog")) {
                SendConfirmationDialog* dialog = qobject_cast<SendConfirmationDialog*>(widget);
                if (text) *text = dialog->text();
                QAbstractButton* button = dialog->button(cancel ? QMessageBox::Cancel : QMessageBox::Yes);
                button->setEnabled(true);
                button->click();
            }
        }
        delete callback;
    }), SLOT(call()));
}

//! Send coins to address and return txid.
uint256 SendCoins(CWallet& wallet, SendCoinsDialog& sendCoinsDialog, const CTxDestination& address, CAmount amount, bool rbf)
{
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    SendCoinsEntry* entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    entry->findChild<QValidatedLineEdit*>("payTo")->setText(QString::fromStdString(EncodeDestination(address)));
    entry->findChild<BitcoinAmountField*>("payAmount")->setValue(amount);
    sendCoinsDialog.findChild<QFrame*>("frameFee")
        ->findChild<QFrame*>("frameFeeSelection")
        ->findChild<QCheckBox*>("optInRBF")
        ->setCheckState(rbf ? Qt::Checked : Qt::Unchecked);
    uint256 txid;
    boost::signals2::scoped_connection c(wallet.NotifyTransactionChanged.connect([&txid](CWallet*, const uint256& hash, ChangeType status) {
        if (status == CT_NEW) txid = hash;
    }));
    ConfirmSend();
    QMetaObject::invokeMethod(&sendCoinsDialog, "on_sendButton_clicked");
    return txid;
}

//! Find index of txid in transaction list.
QModelIndex FindTx(const QAbstractItemModel& model, const uint256& txid)
{
    QString hash = QString::fromStdString(txid.ToString());
    int rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        QModelIndex index = model.index(row, 0, {});
        if (model.data(index, TransactionTableModel::TxHashRole) == hash) {
            return index;
        }
    }
    return {};
}

//! Invoke bumpfee on txid and check results.
void BumpFee(TransactionView& view, const uint256& txid, bool expectDisabled, std::string expectError, bool cancel)
{
    QTableView* table = view.findChild<QTableView*>("transactionView");
    QModelIndex index = FindTx(*table->selectionModel()->model(), txid);
    QVERIFY2(index.isValid(), "Could not find BumpFee txid");

    // Select row in table, invoke context menu, and make sure bumpfee action is
    // enabled or disabled as expected.
    QAction* action = view.findChild<QAction*>("bumpFeeAction");
    table->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    action->setEnabled(expectDisabled);
    table->customContextMenuRequested({});
    QCOMPARE(action->isEnabled(), !expectDisabled);

    action->setEnabled(true);
    QString text;
    if (expectError.empty()) {
        ConfirmSend(&text, cancel);
    } else {
        ConfirmMessage(&text);
    }
    action->trigger();
    QVERIFY(text.indexOf(QString::fromStdString(expectError)) != -1);
}

//! Simple qt wallet tests.
//
// Test widgets can be debugged interactively calling show() on them and
// manually running the event loop, e.g.:
//
//     sendCoinsDialog.show();
//     QEventLoop().exec();
//
// This also requires overriding the default minimal Qt platform:
//
//     src/qt/test/test_tapyrus_qt -platform xcb      # Linux
//     src/qt/test/test_tapyrus_qt -platform windows  # Windows
//     src/qt/test/test_tapyrus_qt -platform cocoa    # macOS
void TestGUI()
{
    // Set up wallet and chain with 105 blocks (5 mature blocks for spending).
    TestChainSetup test;
    for (int i = 0; i < 5; ++i) {
        test.CreateAndProcessBlock({}, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));
    }
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>("mock", WalletDatabase::CreateMock());
    bool firstRun;
    wallet->LoadWallet(firstRun);
    {
        LOCK(wallet->cs_wallet);
        wallet->SetAddressBook(GetDestinationForKey(test.coinbaseKey.GetPubKey(), wallet->m_default_address_type), "", "receive");
        wallet->AddKeyPubKey(test.coinbaseKey, test.coinbaseKey.GetPubKey());
    }
    {
        LOCK(cs_main);
        WalletRescanReserver reserver(wallet.get());
        reserver.reserve();
        wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver, true);
    }
    wallet->SetBroadcastTransactions(true);

    // Create widgets for sending coins and listing transactions.
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    SendCoinsDialog sendCoinsDialog(platformStyle.get());
    TransactionView transactionView(platformStyle.get());
    auto node = interfaces::MakeNode();
    OptionsModel optionsModel(*node);
    AddWallet(wallet);
    WalletModel walletModel(std::move(node->getWallets().back()), *node, platformStyle.get(), &optionsModel);
    RemoveWallet(wallet);
    sendCoinsDialog.setModel(&walletModel);
    transactionView.setModel(&walletModel);

    // Send two transactions, and verify they are added to transaction list.
    TransactionTableModel* transactionTableModel = walletModel.getTransactionTableModel();
    QCOMPARE(transactionTableModel->rowCount({}), 10);
    uint256 txid1 = SendCoins(*wallet.get(), sendCoinsDialog, CKeyID(), 5 * COIN, false /* rbf */);
    uint256 txid2 = SendCoins(*wallet.get(), sendCoinsDialog, CKeyID(), 10 * COIN, true /* rbf */);
    QCOMPARE(transactionTableModel->rowCount({}), 12);
    QVERIFY(FindTx(*transactionTableModel, txid1).isValid());
    QVERIFY(FindTx(*transactionTableModel, txid2).isValid());

    // Call bumpfee. Test disabled, canceled, enabled, then failing cases.
    BumpFee(transactionView, txid1, true /* expect disabled */, "not BIP 125 replaceable" /* expected error */, false /* cancel */);
    BumpFee(transactionView, txid2, false /* expect disabled */, {} /* expected error */, true /* cancel */);
    BumpFee(transactionView, txid2, false /* expect disabled */, {} /* expected error */, false /* cancel */);
    BumpFee(transactionView, txid2, true /* expect disabled */, "already bumped" /* expected error */, false /* cancel */);

    // Check current balance on OverviewPage
    OverviewPage overviewPage(platformStyle.get());
    overviewPage.setWalletModel(&walletModel);
    QLabel* balanceLabel = overviewPage.findChild<QLabel*>("labelBalance");
    QString balanceText = balanceLabel->text();
    int unit = walletModel.getOptionsModel()->getDisplayUnit();
    CAmount balance = walletModel.wallet().getBalance();
    QString balanceComparison = TapyrusUnits::formatWithUnit(unit, balance, false, TapyrusUnits::separatorAlways);
    QCOMPARE(balanceText, balanceComparison);

    // Check Request Payment button
    ReceiveCoinsDialog receiveCoinsDialog(platformStyle.get());
    receiveCoinsDialog.setModel(&walletModel);
    RecentRequestsTableModel* requestTableModel = walletModel.getRecentRequestsTableModel();

    // Label input
    QLineEdit* labelInput = receiveCoinsDialog.findChild<QLineEdit*>("reqLabel");
    labelInput->setText("TEST_LABEL_1");

    // Amount input
    BitcoinAmountField* amountInput = receiveCoinsDialog.findChild<BitcoinAmountField*>("reqAmount");
    amountInput->setValue(1);

    // Message input
    QLineEdit* messageInput = receiveCoinsDialog.findChild<QLineEdit*>("reqMessage");
    messageInput->setText("TEST_MESSAGE_1");
    int initialRowCount = requestTableModel->rowCount({});
    QPushButton* requestPaymentButton = receiveCoinsDialog.findChild<QPushButton*>("receiveButton");
    requestPaymentButton->click();
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("ReceiveRequestDialog")) {
            ReceiveRequestDialog* receiveRequestDialog = qobject_cast<ReceiveRequestDialog*>(widget);
            QTextEdit* rlist = receiveRequestDialog->QObject::findChild<QTextEdit*>("outUri");
            QString paymentText = rlist->toPlainText();
            QStringList paymentTextList = paymentText.split('\n');
            QCOMPARE(paymentTextList.at(0), QString("Payment information"));
            QVERIFY(paymentTextList.at(1).indexOf(QString("URI: tapyrus:")) != -1);
            QVERIFY(paymentTextList.at(2).indexOf(QString("Address:")) != -1);
            QCOMPARE(paymentTextList.at(3), QString("Amount: 0.00000001 ") + QString::fromStdString(CURRENCY_UNIT));
            QCOMPARE(paymentTextList.at(4), QString("Label: TEST_LABEL_1"));
            QCOMPARE(paymentTextList.at(5), QString("Message: TEST_MESSAGE_1"));
        }
    }

    // Clear button
    QPushButton* clearButton = receiveCoinsDialog.findChild<QPushButton*>("clearButton");
    clearButton->click();
    QCOMPARE(labelInput->text(), QString(""));
    QCOMPARE(amountInput->value(), CAmount(0));
    QCOMPARE(messageInput->text(), QString(""));

    // Check addition to history
    int currentRowCount = requestTableModel->rowCount({});
    QCOMPARE(currentRowCount, initialRowCount+1);

    // Check Remove button
    QTableView* table = receiveCoinsDialog.findChild<QTableView*>("recentRequestsView");
    table->selectRow(currentRowCount-1);
    QPushButton* removeRequestButton = receiveCoinsDialog.findChild<QPushButton*>("removeRequestButton");
    removeRequestButton->click();
    QCOMPARE(requestTableModel->rowCount({}), currentRowCount-1);
}

void TestGUI_coloredCoin()
{
    // Set up wallet and chain. TestChainSetup creates 100 initial blocks;
    // we add 5 more so the test coinbaseKey's outputs are mature.
    TestChainSetup test;
    for (int i = 0; i < 5; ++i)
        test.CreateAndProcessBlock({}, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));

    std::shared_ptr<CWallet> wallet =
        std::make_shared<CWallet>("mock", WalletDatabase::CreateMock());
    bool firstRun;
    wallet->LoadWallet(firstRun);
    {
        LOCK(wallet->cs_wallet);
        wallet->SetAddressBook(
            GetDestinationForKey(test.coinbaseKey.GetPubKey(), wallet->m_default_address_type),
            "", "receive");
        wallet->AddKeyPubKey(test.coinbaseKey, test.coinbaseKey.GetPubKey());
    }
    {
        LOCK(cs_main);
        WalletRescanReserver reserver(wallet.get());
        reserver.reserve();
        wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver, true);
    }
    wallet->SetBroadcastTransactions(true);

    // ── Create WalletModel and all GUI widgets ──────────────────────────
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    auto node = interfaces::MakeNode();
    OptionsModel optionsModel(*node);
    AddWallet(wallet);
    WalletModel walletModel(std::move(node->getWallets().back()), *node,
                            platformStyle.get(), &optionsModel);
    RemoveWallet(wallet);

    OverviewPage       overviewPage(platformStyle.get());
    IssueTokenDialog   issueTokenDialog(platformStyle.get());
    TransactionView    transactionView(platformStyle.get());
    ReceiveCoinsDialog receiveCoinsDialog(platformStyle.get());

    overviewPage.setWalletModel(&walletModel);
    issueTokenDialog.setModel(&walletModel);
    transactionView.setModel(&walletModel);
    receiveCoinsDialog.setModel(&walletModel);

    TransactionTableModel*    txModel  = walletModel.getTransactionTableModel();
    RecentRequestsTableModel* reqModel = walletModel.getRecentRequestsTableModel();

    QTableWidget* tokenTable = issueTokenDialog.findChild<QTableWidget*>("tokenTable");
    QVERIFY(tokenTable != nullptr);

    // ── Helpers ─────────────────────────────────────────────────────────

    // Mine one block (including all pending mempool transactions), rescan,
    // and force WalletModel to propagate the update to all connected pages.
    // Including mempool txns is essential: CreateAndProcessBlock({}) would
    // otherwise mine an empty block, leaving all token transactions unconfirmed
    // with identical block_height=INT_MAX.  Equal heights collapse the sort
    // order to output-index, making checkLatestTx non-deterministic.
    auto mineAndUpdate = [&]() {
        std::vector<CMutableTransaction> mempoolTxns;
        {
            LOCK(mempool.cs);
            for (const CTxMemPoolEntry& e : mempool.mapTx)
                mempoolTxns.emplace_back(*e.GetSharedTx());
        }
        test.CreateAndProcessBlock(mempoolTxns, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));
        {
            LOCK(cs_main);
            WalletRescanReserver reserver(wallet.get());
            reserver.reserve();
            wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver, true);
        }
        // pollBalanceChanged checks block-count change, calls checkBalanceChanged()
        // (→ balanceChanged → OverviewPage::setBalance → refreshTokenList) and
        // checkTokenListChanged() (→ tokenListChanged → IssueTokenDialog::refreshTokenTable
        // and ReceiveCoinsDialog::refreshTokenCombo).
        QMetaObject::invokeMethod(&walletModel, "pollBalanceChanged");
        QApplication::processEvents();
    };

    // Assert OverviewPage shows the expected confirmed balance for the given colorId.
    auto checkOverviewBalance = [&](const QString& cid, CAmount expected) {
        QComboBox* combo = overviewPage.findChild<QComboBox*>("comboToken");
        QVERIFY(combo != nullptr);
        int idx = combo->findData(cid);
        QVERIFY2(idx >= 0, qPrintable("Token not in comboToken: " + cid));
        combo->setCurrentIndex(idx);
        QLabel* lbl = overviewPage.findChild<QLabel*>("labelTokenBalance");
        QVERIFY(lbl != nullptr);
        QCOMPARE(lbl->text(),
                 TapyrusUnits::format(TapyrusUnits::TOKEN, expected, false,
                                      TapyrusUnits::separatorAlways));
    };

    // Assert the IssueTokenDialog token table has a row matching the given colorId,
    // token type string and confirmed balance.
    auto checkTokenTableRow = [&](const QString& cid,
                                  const QString& expectedType,
                                  CAmount         expectedBalance) {
        bool found = false;
        for (int row = 0; row < tokenTable->rowCount(); ++row) {
            if (tokenTable->item(row, IssueTokenDialog::ColColor)->text() == cid) {
                QCOMPARE(tokenTable->item(row, IssueTokenDialog::ColType)->text(),
                         expectedType);
                QCOMPARE(tokenTable->item(row, IssueTokenDialog::ColBalance)->text(),
                         QString::number(expectedBalance));
                found = true;
                break;
            }
        }
        QVERIFY2(found,
                 qPrintable("Token not found in tokenTable: " + cid));
    };

    // Assert the TransactionTableModel contains at least one token record with
    // the expected type and colorId.  txModel is the unsorted underlying model
    // (row order is by tx hash), so we scan all rows rather than assuming
    // the "latest" operation lands at a particular row index.
    auto checkLatestTx = [&](int expectedType, const QString& cid) {
        QVERIFY(txModel->rowCount({}) > 0);
        for (int row = 0; row < txModel->rowCount({}); ++row) {
            QModelIndex i = txModel->index(row, 0);
            if (txModel->data(i, TransactionTableModel::ColorIdRole).toString() == cid &&
                txModel->data(i, TransactionTableModel::TypeRole).toInt() == expectedType) {
                QVERIFY(txModel->data(i, TransactionTableModel::IsTokenRole).toBool());
                return;
            }
        }
        QFAIL(qPrintable(
            QString("No record of type %1 found for colorId %2").arg(expectedType).arg(cid)));
    };

    // Parse a hex colorId string into a ColorIdentifier value.
    auto parseColorId = [](const QString& hex) {
        std::vector<unsigned char> v = ParseHex(hex.toStdString());
        return ColorIdentifier(v.data(), v.data() + v.size());
    };

    // Look up the encoded colored address for the given colorId from the wallet
    // address book (populated by IssueToken / IssueReissuableToken at issue time).
    auto getColoredAddress = [&](const QString& cid) -> QString {
        for (const auto& wa : walletModel.wallet().getAddresses()) {
            if (wa.name == cid.toStdString() && wa.purpose == "receive")
                return QString::fromStdString(EncodeDestination(wa.dest));
        }
        return {};
    };

    // Build a SendCoinsRecipient for a token transfer and call prepareTransaction
    // + sendCoins.  We send to the wallet's own colored address (self-transfer) so
    // the confirmed balance is unchanged after mining.
    auto sendTokens = [&](const QString& cid, const QString& toAddress, CAmount amount) {
        SendCoinsRecipient rcp;
        rcp.address  = toAddress;
        rcp.colorid  = parseColorId(cid);
        rcp.amount   = amount;
        rcp.fSubtractFeeFromAmount = false;
        WalletModelTransaction txn(QList<SendCoinsRecipient>() << rcp);
        CCoinControl ctrl;
        WalletModel::SendCoinsReturn ret = walletModel.prepareTransaction(txn, ctrl);
        QCOMPARE(ret.status, WalletModel::OK);
        ret = walletModel.sendCoins(txn);
        QCOMPARE(ret.status, WalletModel::OK);
    };

    // ── REISSUABLE token (type 1) ────────────────────────────────────────

    {
        // Issue 100 REISSUABLE tokens.
        auto r = walletModel.issueToken(1, 100);
        QCOMPARE(r.status, WalletModel::IssueTokenResult::OK);
        const QString cid = r.color;
        mineAndUpdate();

        // Tokens page: one REISSUABLE row with balance 100.
        checkTokenTableRow(cid, "REISSUABLE", 100);
        // Overview: balance 100.
        checkOverviewBalance(cid, 100);
        // Transaction list: most recent entry is a TokenIssue for this colorId.
        checkLatestTx(TransactionRecord::TokenIssue, cid);

        // Reissue 50 more tokens of the same color.
        auto rr = walletModel.issueToken(1, 50, cid);
        QCOMPARE(rr.status, WalletModel::IssueTokenResult::OK);
        mineAndUpdate();

        checkTokenTableRow(cid, "REISSUABLE", 150);
        checkOverviewBalance(cid, 150);
        checkLatestTx(TransactionRecord::TokenIssue, cid);

        // Send 30 tokens to the wallet's own colored address (self-transfer).
        const QString addr = getColoredAddress(cid);
        QVERIFY(!addr.isEmpty());
        sendTokens(cid, addr, 30);
        mineAndUpdate();

        // Balance unchanged after self-transfer.
        checkOverviewBalance(cid, 150);
        checkLatestTx(TransactionRecord::SendToAddress, cid);

        // Burn 50 tokens.
        auto br = walletModel.burnToken(cid, 50);
        QCOMPARE(br.status, WalletModel::BurnTokenResult::OK);
        mineAndUpdate();

        checkTokenTableRow(cid, "REISSUABLE", 100);
        checkOverviewBalance(cid, 100);
        checkLatestTx(TransactionRecord::TokenBurn, cid);
    }

    // ── NON_REISSUABLE token (type 2) ───────────────────────────────────

    {
        // Issue 200 NON_REISSUABLE tokens.
        auto r = walletModel.issueToken(2, 200);
        QCOMPARE(r.status, WalletModel::IssueTokenResult::OK);
        const QString cid = r.color;
        mineAndUpdate();

        checkTokenTableRow(cid, "NON_REISSUABLE", 200);
        checkOverviewBalance(cid, 200);
        checkLatestTx(TransactionRecord::TokenIssue, cid);

        // Send 50 to self.
        const QString addr = getColoredAddress(cid);
        QVERIFY(!addr.isEmpty());
        sendTokens(cid, addr, 50);
        mineAndUpdate();

        checkOverviewBalance(cid, 200);
        checkLatestTx(TransactionRecord::SendToAddress, cid);

        // Burn all 200 — token should disappear from both views.
        auto br = walletModel.burnToken(cid, 200);
        QCOMPARE(br.status, WalletModel::BurnTokenResult::OK);
        mineAndUpdate();

        checkLatestTx(TransactionRecord::TokenBurn, cid);
        // Zero-balance token removed from OverviewPage combo and token table.
        QComboBox* combo = overviewPage.findChild<QComboBox*>("comboToken");
        QCOMPARE(combo->findData(cid), -1);
        for (int row = 0; row < tokenTable->rowCount(); ++row)
            QVERIFY(tokenTable->item(row, IssueTokenDialog::ColColor)->text() != cid);
    }

    // ── NFT token (type 3) ──────────────────────────────────────────────

    {
        // Issue one NFT.
        auto r = walletModel.issueToken(3, 1);
        QCOMPARE(r.status, WalletModel::IssueTokenResult::OK);
        const QString cid = r.color;
        mineAndUpdate();

        checkTokenTableRow(cid, "NFT", 1);
        checkOverviewBalance(cid, 1);
        checkLatestTx(TransactionRecord::TokenIssue, cid);

        // Transfer the NFT to self.
        const QString addr = getColoredAddress(cid);
        QVERIFY(!addr.isEmpty());
        sendTokens(cid, addr, 1);
        mineAndUpdate();

        checkOverviewBalance(cid, 1);
        checkLatestTx(TransactionRecord::SendToAddress, cid);

        // Burn the NFT — token disappears.
        auto br = walletModel.burnToken(cid, 1);
        QCOMPARE(br.status, WalletModel::BurnTokenResult::OK);
        mineAndUpdate();

        checkLatestTx(TransactionRecord::TokenBurn, cid);
        QComboBox* combo = overviewPage.findChild<QComboBox*>("comboToken");
        QCOMPARE(combo->findData(cid), -1);
        for (int row = 0; row < tokenTable->rowCount(); ++row)
            QVERIFY(tokenTable->item(row, IssueTokenDialog::ColColor)->text() != cid);
    }

    // ── Receive: payment URL and requested-payments table ───────────────
    // The REISSUABLE token (balance 100) survives all of the above operations.

    {
        QComboBox* comboTok = overviewPage.findChild<QComboBox*>("comboToken");
        QVERIFY(comboTok != nullptr && comboTok->count() > 0);
        // Pick the first token still in the combo (REISSUABLE, balance 100).
        const QString cid = comboTok->itemData(0).toString();

        QLineEdit*          labelInput  = receiveCoinsDialog.findChild<QLineEdit*>("reqLabel");
        QRadioButton*       radioToken  = receiveCoinsDialog.findChild<QRadioButton*>("radioToken");
        QComboBox*          reqToken    = receiveCoinsDialog.findChild<QComboBox*>("reqToken");
        BitcoinAmountField* amtInput    = receiveCoinsDialog.findChild<BitcoinAmountField*>("reqAmount");
        QLineEdit*          msgInput    = receiveCoinsDialog.findChild<QLineEdit*>("reqMessage");
        QVERIFY(labelInput && radioToken && reqToken && amtInput && msgInput);

        labelInput->setText("TOKEN_LABEL");

        // Switch to token mode and select the token.
        radioToken->setChecked(true);
        int idx = reqToken->findData(cid);
        QVERIFY(idx >= 0);
        reqToken->setCurrentIndex(idx);

        amtInput->setValue(10);
        msgInput->setText("TOKEN_MSG");

        int beforeRows = reqModel->rowCount({});
        receiveCoinsDialog.findChild<QPushButton*>("receiveButton")->click();

        // Verify the ReceiveRequestDialog payment text.
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (!w->inherits("ReceiveRequestDialog")) continue;
            QTextEdit* out = w->findChild<QTextEdit*>("outUri");
            QStringList lines = out->toPlainText().split('\n');
            QCOMPARE(lines.at(0), QString("Payment information"));
            QVERIFY(lines.at(1).contains("URI: tapyrus:"));
            QVERIFY(lines.at(2).contains("Address:"));
            QCOMPARE(lines.at(3), QString("Token: ") + cid);
            QCOMPARE(lines.at(4), QString("Amount: 10 token"));
            QCOMPARE(lines.at(5), QString("Label: TOKEN_LABEL"));
            QCOMPARE(lines.at(6), QString("Message: TOKEN_MSG"));
        }

        // Entry added to requested-payments table.
        QCOMPARE(reqModel->rowCount({}), beforeRows + 1);

        // Clear button resets all inputs and disables the token combo.
        receiveCoinsDialog.findChild<QPushButton*>("clearButton")->click();
        QCOMPARE(labelInput->text(), QString(""));
        QCOMPARE(amtInput->value(), CAmount(0));
        QCOMPARE(msgInput->text(), QString(""));
        QVERIFY(!reqToken->isEnabled());

        // Remove the last entry.
        int afterRows = reqModel->rowCount({});
        QTableView* tbl =
            receiveCoinsDialog.findChild<QTableView*>("recentRequestsView");
        tbl->selectRow(afterRows - 1);
        receiveCoinsDialog.findChild<QPushButton*>("removeRequestButton")->click();
        QCOMPARE(reqModel->rowCount({}), afterRows - 1);
    }
}

} // namespace

class SendCoinsRecipient;

void RecipientCatcher::getRecipient(const SendCoinsRecipient& r)
{
    recipient = r;
}

static SendCoinsRecipient handleRequest(PaymentServer* server, std::vector<unsigned char>& data)
{
    RecipientCatcher sigCatcher;
    QObject::connect(server, SIGNAL(receivedPaymentRequest(SendCoinsRecipient)),
        &sigCatcher, SLOT(getRecipient(SendCoinsRecipient)));

    // Write data to a temp file:
    QTemporaryFile f;
    f.open();
    f.write((const char*)data.data(), data.size());
    f.close();

    // Create a QObject, install event filter from PaymentServer
    // and send a file open event to the object
    QObject object;
    object.installEventFilter(server);
    QFileOpenEvent event(f.fileName());
    // If sending the event fails, this will cause sigCatcher to be empty,
    // which will lead to a test failure anyway.
    QCoreApplication::sendEvent(&object, &event);

    QObject::disconnect(server, SIGNAL(receivedPaymentRequest(SendCoinsRecipient)),
        &sigCatcher, SLOT(getRecipient(SendCoinsRecipient)));

    // Return results from sigCatcher
    return sigCatcher.recipient;
}

void paymentServerTest()
{
    SelectParams(TAPYRUS_OP_MODE::PROD);
    auto node = interfaces::MakeNode();
    OptionsModel optionsModel(*node);
    PaymentServer* server = new PaymentServer(nullptr, false);
    server->setOptionsModel(&optionsModel);
    server->uiReady();

    std::vector<unsigned char> data;
    SendCoinsRecipient r;
    QString merchant;

    // Now feed PaymentRequests to server, and observe signals it produces

    // This payment request validates directly against the
    // caCert1 certificate authority:
    data = DecodeBase64("Egt4NTA5K3NoYTI1NhrxAwruAzCCAeowggFToAMCAQICAQEwDQYJKoZIhvcNAQEL\
    BQAwITEfMB0GA1UEAxMWUGF5bWVudFJlcXVlc3QgVGVzdCBDQTAeFw0xMjEyMTAx\
    NjM3MjRaFw0yMjEyMDgxNjM3MjRaMEMxGTAXBgNVBAMMEHRlc3RtZXJjaGFudC5v\
    cmcxJjAkBgNVBAoMHVBheW1lbnQgUmVxdWVzdCBUZXN0IE1lcmNoYW50MIGfMA0G\
    CSqGSIb3DQEBAQUAA4GNADCBiQKBgQDHkMy8W1u6HsWlSqdWTmMKf54gICxNfxbY\
    +rcMtAftr62hCYx2d2QiSRd1pCUzmo12IiSX3WxSHwaTnT3MFD6jRx6+zM6XdGar\
    I2zpYle11ANzu4gAthN17uRQHV2O5QxVtzNaMdKeJLXT2L9tfEdyL++9ZUqoQmdA\
    YG9ix330hQIDAQABoxAwDjAMBgNVHRMEBTADAQH/MA0GCSqGSIb3DQEBCwUAA4GB\
    AIkyO99KC68bi9PFRyQQ7nvn5GlQEb3Ca1bRG5+AKN9N5vc8rZ9G2hejtM8wEXni\
    eGBP+chVMsbTPEHKLrwREn7IvcyCcbAStaklPC3w0B/2idQSHskb6P3X13OR2bTH\
    a2+6wuhsOZRUrVNr24rM95DKx/eCC6JN1VW+qRPU6fqzIjQSHwiw2wYSGXapFJVg\
    igPI+6XpExtNLO/i1WFV8ZmoiKwYsuHFiwUqC1VuaXRUZXN0T25lKoABS0j59iMU\
    Uc9MdIfwsO1BskIET0eJSGNZ7eXb9N62u+qf831PMpEHkmlGpk8rHy92nPcgua/U\
    Yt8oZMn3QaTZ5A6HjJbc3A73eLylp1a0SwCl+KDMEvDQhqMn1jAVu2v92AH3uB7n\
    SiWVbw0tX/68iSQEGGfh9n6ee/8Myb3ICdw=");
    QVERIFY(data.size());
    r = handleRequest(server, data);
    QCOMPARE(r.amount, (int64_t)0);
    QCOMPARE(r.label, QString(""));
    QCOMPARE(r.message, QString(""));
    QCOMPARE(r.address, QString(""));
    QCOMPARE(r.sPaymentRequest.c_str(), "");
}

void WalletTests::walletTests()
{
    paymentServerTest();
    TestGUI();
    TestGUI_coloredCoin();
}
