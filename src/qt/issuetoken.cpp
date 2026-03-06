// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/issuetoken.h>
#include <qt/walletmodel.h>
#include <qt/platformstyle.h>

#include <amount.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFont>
#include <QHeaderView>
#include <QIcon>
#include <QMap>
#include <QMenu>
#include <QSettings>
#include <QString>
#include <QTableWidgetItem>
#include <QTimer>

#include <ui_interface.h>
#include <ui_issuetoken.h>

// ── Numeric-sorting table item ────────────────────────────────────────────────
class NumericItem : public QTableWidgetItem {
public:
    explicit NumericItem(const QString &text) : QTableWidgetItem(text) {}
    bool operator<(const QTableWidgetItem &o) const override {
        return text().toLongLong() < o.text().toLongLong();
    }
};

// ── QSettings key helpers ────────────────────────────────────────────────────
// Keys live under group "IssuedTokens/<walletName>/<colorId>/"
static QString settingsGroup(const QString &walletName)
{
    return "IssuedTokens/" + walletName;
}

// ── Constructor / destructor ─────────────────────────────────────────────────

IssueTokenDialog::IssueTokenDialog(const PlatformStyle *_platformStyle, QWidget *parent)
    : QWidget(parent),
      ui(new Ui::IssueTokenDialog),
      model(nullptr),
      platformStyle(_platformStyle)
{
    ui->setupUi(this);

    // Radio buttons → lock NFT amount
    connect(ui->radioReissuable,    &QRadioButton::toggled, this, &IssueTokenDialog::onTokenTypeChanged);
    connect(ui->radioNonReissuable, &QRadioButton::toggled, this, &IssueTokenDialog::onTokenTypeChanged);
    connect(ui->radioNft,           &QRadioButton::toggled, this, &IssueTokenDialog::onTokenTypeChanged);

    // Label edits in the table
    connect(ui->tokenTable, &QTableWidget::itemChanged,
            this, &IssueTokenDialog::onLabelChanged);

    // Burn tab: combo selection updates amount hint
    connect(ui->burnColorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &IssueTokenDialog::onBurnColorChanged);

    // Issue mode: New / Reissue radio buttons
    connect(ui->radioNew,     &QRadioButton::toggled, this, &IssueTokenDialog::onIssueModeChanged);
    connect(ui->radioReissue, &QRadioButton::toggled, this, &IssueTokenDialog::onIssueModeChanged);

    // Configure table: all columns user-resizable
    {
        QHeaderView *hdr = ui->tokenTable->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(false);

        // ColorId column: wide enough to show a full 66-char colorId
        const int charW = ui->tokenTable->fontMetrics().horizontalAdvance('a');
        ui->tokenTable->setColumnWidth(ColColor,   charW * 68);
        ui->tokenTable->setColumnWidth(ColLabel,   charW * 20);
        ui->tokenTable->setColumnWidth(ColType,    charW * 16);
        ui->tokenTable->setColumnWidth(ColBalance, charW * 14);
    }

    connect(ui->tokenTable, &QTableWidget::customContextMenuRequested,
            this, &IssueTokenDialog::onTokenTableContextMenu);

    // Initialise UI state to match defaults (REISSUABLE checked → show issueModeWidget)
    onTokenTypeChanged();
}

IssueTokenDialog::~IssueTokenDialog()
{
    delete ui;
}

// ── Public ───────────────────────────────────────────────────────────────────

void IssueTokenDialog::setModel(WalletModel *_model)
{
    model = _model;
    if (model) {
        loadPersistedTokens();
        refreshBurnCombo();
        // Refresh balances (and burn combo) whenever the wallet reports a balance change
        connect(model, &WalletModel::balanceChanged,
                this,  &IssueTokenDialog::refreshTokenTable);
    }
}

void IssueTokenDialog::clear()
{
    ui->amountEdit->clear();
    ui->amountEdit->setEnabled(true);
    ui->radioReissuable->setChecked(true);
}

// ── Public slots ─────────────────────────────────────────────────────────────

void IssueTokenDialog::refreshTokenTable()
{
    if (!model)
        return;

    // Fetch current live balances
    QMap<QString, CAmount> balances = model->getTokenBalances();

    // Update in-memory records
    for (IssuedTokenRecord &rec : m_tokens) {
        auto it = balances.find(rec.colorId);
        rec.balance = (it != balances.end()) ? it.value() : 0;
    }

    // Remember the currently selected colorId so we can restore it after the rebuild
    QString selectedColorId;
    const QList<QTableWidgetItem*> sel = ui->tokenTable->selectedItems();
    for (QTableWidgetItem *it : sel) {
        if (it->column() == ColColor) { selectedColorId = it->text(); break; }
    }

    // Rebuild table (disable sorting + block signals during fill)
    ui->tokenTable->setSortingEnabled(false);
    ui->tokenTable->blockSignals(true);
    ui->tokenTable->setRowCount(0);
    ui->tokenTable->setRowCount(m_tokens.size());
    for (int i = 0; i < m_tokens.size(); ++i)
        addTokenToTable(m_tokens[i], i);
    ui->tokenTable->blockSignals(false);
    ui->tokenTable->setSortingEnabled(true);

    // Refresh combos first — refreshBurnCombo() calls onBurnColorChanged(0) which
    // calls highlightTokenRow, so the selection restore must come AFTER.
    refreshBurnCombo();
    refreshReissueCombo();

    // Restore selection last so combo side-effects don't override it
    if (!selectedColorId.isEmpty())
        highlightTokenRow(selectedColorId);
}

// ── Private slots ─────────────────────────────────────────────────────────────

void IssueTokenDialog::on_clearButton_clicked()
{
    clear();
}

void IssueTokenDialog::onTokenTypeChanged()
{
    int type = currentTokenType(); // 1 / 2 / 3

    // Issue mode section only relevant for REISSUABLE
    ui->issueModeWidget->setVisible(type == 1);
    if (type != 1) {
        ui->radioNew->setChecked(true);  // reset to "New" when leaving REISSUABLE
        ui->reissueCombo->setEnabled(false);
    } else {
        // Reflect current radio state
        ui->reissueCombo->setEnabled(ui->radioReissue->isChecked());
    }

    // NFT amount is always 1
    if (type == 3) {
        ui->amountEdit->setText("1");
        ui->amountEdit->setEnabled(false);
    } else {
        ui->amountEdit->setEnabled(true);
        // Clear the locked "1" if we switched away from NFT
        if (ui->amountEdit->text() == "1")
            ui->amountEdit->clear();
    }
}

void IssueTokenDialog::on_issueButton_clicked()
{
    if (!model)
        return;

    // --- Validate amount ---
    bool amountOk = false;
    CAmount tokenValue = ui->amountEdit->text().toLongLong(&amountOk);
    if (!amountOk || tokenValue <= 0) {
        Q_EMIT message(tr("Issue Token"),
                       tr("Invalid amount. Please enter a positive integer."),
                       CClientUIInterface::MSG_ERROR);
        return;
    }

    int tokenType = currentTokenType(); // 1 / 2 / 3

    if (tokenType == 3 && tokenValue != 1) {
        Q_EMIT message(tr("Issue Token"), tr("NFT amount must be 1."), CClientUIInterface::MSG_ERROR);
        return;
    }

    // --- Unlock wallet if needed ---
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if (!ctx.isValid())
        return; // user cancelled

    // --- For reissue mode: get the colorId from the combo ---
    QString existingColorId;
    bool isReissue = (tokenType == 1 && ui->radioReissue->isChecked());
    if (isReissue) {
        int idx = ui->reissueCombo->currentIndex();
        if (idx < 0) {
            Q_EMIT message(tr("Issue Token"),
                           tr("No token selected for reissue."),
                           CClientUIInterface::MSG_ERROR);
            return;
        }
        existingColorId = ui->reissueCombo->itemData(idx).toString();
    }

    // --- Call model ---
    WalletModel::IssueTokenResult result = model->issueToken(tokenType, tokenValue, existingColorId);

    if (result.status == WalletModel::IssueTokenResult::OK) {
        if (isReissue) {
            // Update balance on the existing record only
            for (IssuedTokenRecord &rec : m_tokens) {
                if (rec.colorId == result.color) {
                    rec.balance += tokenValue;
                    break;
                }
            }
            refreshTokenTable();
            highlightTokenRow(result.color);
            Q_EMIT message(tr("Issue Token"),
                           tr("Tokens reissued successfully.\nColor: %1").arg(result.color),
                           CClientUIInterface::MSG_INFORMATION);
            return;
        }

        // Build and persist new record
        IssuedTokenRecord rec;
        rec.colorId      = result.color;
        rec.label        = "";
        rec.tokenType    = tokenTypeName(tokenType);
        rec.balance      = tokenValue;
        rec.scriptPubKey = result.scriptPubKey;
        rec.address      = result.address;

        m_tokens.append(rec);
        saveToken(rec);

        refreshTokenTable();
        highlightTokenRow(result.color);

        Q_EMIT message(tr("Issue Token"),
                       tr("Token issued successfully.\nColor: %1").arg(result.color),
                       CClientUIInterface::MSG_INFORMATION);
    } else {
        Q_EMIT message(tr("Issue Token"), result.error, CClientUIInterface::MSG_ERROR);
    }
}

void IssueTokenDialog::onLabelChanged(QTableWidgetItem *item)
{
    if (!item || item->column() != ColLabel)
        return;

    int row = item->row();
    if (row < 0 || row >= m_tokens.size())
        return;

    QString newLabel = item->text();
    m_tokens[row].label = newLabel;

    if (!model)
        return;

    // Store label in the wallet's address book for REISSUABLE tokens that have an address
    if (!m_tokens[row].address.isEmpty())
        model->setTokenLabel(m_tokens[row].address, newLabel);

    // Always persist to QSettings as the canonical source for the token list
    QSettings settings;
    QString grp = settingsGroup(model->getWalletName());
    settings.beginGroup(grp + "/" + m_tokens[row].colorId);
    settings.setValue("label", newLabel);
    settings.endGroup();
}

void IssueTokenDialog::onIssueModeChanged()
{
    bool reissue = ui->radioReissue->isChecked();
    ui->reissueCombo->setEnabled(reissue);
    if (reissue)
        refreshReissueCombo();
}

void IssueTokenDialog::refreshReissueCombo()
{
    ui->reissueCombo->blockSignals(true);
    const QString current = ui->reissueCombo->currentData().toString();
    ui->reissueCombo->clear();

    for (const IssuedTokenRecord &rec : m_tokens) {
        if (rec.tokenType != "REISSUABLE")
            continue;
        ui->reissueCombo->addItem(QIcon(":/icons/token_reissuable"), rec.colorId, rec.colorId);
    }

    int idx = ui->reissueCombo->findData(current);
    if (idx >= 0)
        ui->reissueCombo->setCurrentIndex(idx);

    ui->reissueCombo->blockSignals(false);
}

void IssueTokenDialog::on_burnButton_clicked()
{
    if (!model)
        return;

    // Validate amount
    bool amountOk = false;
    CAmount burnValue = ui->burnAmountEdit->text().toLongLong(&amountOk);
    if (!amountOk || burnValue <= 0) {
        Q_EMIT message(tr("Burn Token"),
                       tr("Invalid amount. Please enter a positive integer."),
                       CClientUIInterface::MSG_ERROR);
        return;
    }

    int idx = ui->burnColorCombo->currentIndex();
    if (idx < 0) {
        Q_EMIT message(tr("Burn Token"), tr("No token selected."), CClientUIInterface::MSG_ERROR);
        return;
    }

    QString colorId = ui->burnColorCombo->itemData(idx).toString();

    // Find balance
    CAmount balance = 0;
    for (const IssuedTokenRecord &rec : m_tokens) {
        if (rec.colorId == colorId) { balance = rec.balance; break; }
    }

    if (burnValue > balance) {
        Q_EMIT message(tr("Burn Token"),
                       tr("Amount exceeds wallet balance (%1).").arg(balance),
                       CClientUIInterface::MSG_ERROR);
        return;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());
    if (!ctx.isValid())
        return;

    WalletModel::BurnTokenResult result = model->burnToken(colorId, burnValue);

    if (result.status == WalletModel::BurnTokenResult::OK) {
        ui->burnAmountEdit->clear();
        refreshTokenTable(); // update balances
        highlightTokenRow(colorId);
        Q_EMIT message(tr("Burn Token"),
                       tr("Tokens burned successfully.\nColor: %1").arg(colorId),
                       CClientUIInterface::MSG_INFORMATION);
    } else {
        Q_EMIT message(tr("Burn Token"), result.error, CClientUIInterface::MSG_ERROR);
    }
}

void IssueTokenDialog::onBurnColorChanged(int index)
{
    if (index < 0 || !model)
        return;

    QString colorId = ui->burnColorCombo->itemData(index).toString();

    // Highlight the corresponding row in the tokens table so the user can see the balance
    highlightTokenRow(colorId);

    // Find the record to get balance and type
    CAmount balance = 0;
    bool isNft = false;
    for (const IssuedTokenRecord &rec : m_tokens) {
        if (rec.colorId == colorId) {
            balance = rec.balance;
            isNft = (rec.tokenType == "NFT");
            break;
        }
    }

    // NFT amount is always 1 — lock the field
    if (isNft) {
        ui->burnAmountEdit->setText("1");
        ui->burnAmountEdit->setEnabled(false);
    } else {
        ui->burnAmountEdit->setEnabled(true);
        ui->burnAmountEdit->clear();
    }
    ui->burnAmountEdit->setPlaceholderText(tr("Amount to burn (max %1)").arg(balance));
}

void IssueTokenDialog::onTokenTableContextMenu(const QPoint &pos)
{
    QTableWidgetItem *colorItem = ui->tokenTable->itemAt(pos);
    if (!colorItem)
        return;

    int row = colorItem->row();
    QTableWidgetItem *colorCell = ui->tokenTable->item(row, ColColor);
    if (!colorCell)
        return;

    const QString colorId = colorCell->text();

    // Find matching record to get the address
    QString address;
    for (const IssuedTokenRecord &rec : m_tokens) {
        if (rec.colorId == colorId) { address = rec.address; break; }
    }

    QMenu menu(this);
    menu.addAction(tr("Copy Color ID"), this, [colorId]{
        QApplication::clipboard()->setText(colorId);
    });

    QAction *copyAddrAction = menu.addAction(tr("Copy Address"), this, [address]{
        QApplication::clipboard()->setText(address);
    });
    copyAddrAction->setEnabled(!address.isEmpty());

    menu.exec(QCursor::pos());
}

void IssueTokenDialog::refreshBurnCombo()
{
    ui->burnColorCombo->blockSignals(true);
    const QString current = ui->burnColorCombo->currentData().toString();
    ui->burnColorCombo->clear();

    static const QMap<QString, QString> typeIcons = {
        {"REISSUABLE",     ":/icons/token_reissuable"},
        {"NON_REISSUABLE", ":/icons/token_nonreissuable"},
        {"NFT",            ":/icons/token_nft"},
    };

    for (const IssuedTokenRecord &rec : m_tokens) {
        if (rec.balance <= 0)
            continue;

        ui->burnColorCombo->addItem(QIcon(typeIcons.value(rec.tokenType)), rec.colorId, rec.colorId);

        int idx = ui->burnColorCombo->count() - 1;
        ui->burnColorCombo->setItemData(idx, rec.colorId, Qt::ToolTipRole);
    }

    // Restore previous selection if still present
    int idx = ui->burnColorCombo->findData(current);
    if (idx >= 0)
        ui->burnColorCombo->setCurrentIndex(idx);

    ui->burnColorCombo->blockSignals(false);
    onBurnColorChanged(ui->burnColorCombo->currentIndex());
}


// ── Private helpers ───────────────────────────────────────────────────────────

void IssueTokenDialog::loadPersistedTokens()
{
    if (!model)
        return;

    m_tokens.clear();

    QSettings settings;
    QString grp = settingsGroup(model->getWalletName());
    settings.beginGroup(grp);
    const QStringList colorIds = settings.childGroups();
    settings.endGroup();

    for (const QString &colorId : colorIds) {
        settings.beginGroup(grp + "/" + colorId);
        IssuedTokenRecord rec;
        rec.colorId      = colorId;
        rec.label        = settings.value("label",        QString()).toString();
        rec.tokenType    = settings.value("type",         "REISSUABLE").toString();
        rec.scriptPubKey = settings.value("scriptPubKey", QString()).toString();
        rec.address      = settings.value("address",      QString()).toString();
        rec.balance      = 0; // will be refreshed below
        settings.endGroup();

        m_tokens.append(rec);
    }

    // Merge live balances
    refreshTokenTable();
}

void IssueTokenDialog::saveToken(const IssuedTokenRecord &rec)
{
    if (!model)
        return;

    QSettings settings;
    QString grp = settingsGroup(model->getWalletName());
    settings.beginGroup(grp + "/" + rec.colorId);
    settings.setValue("label", rec.label);
    settings.setValue("type",  rec.tokenType);
    if (!rec.scriptPubKey.isEmpty())
        settings.setValue("scriptPubKey", rec.scriptPubKey);
    if (!rec.address.isEmpty())
        settings.setValue("address", rec.address);
    settings.endGroup();
}

void IssueTokenDialog::addTokenToTable(const IssuedTokenRecord &rec, int row)
{
    // Col 0 – Type with icon (non-editable)
    {
        static const QMap<QString, QString> typeIcons = {
            {"REISSUABLE",     ":/icons/token_reissuable"},
            {"NON_REISSUABLE", ":/icons/token_nonreissuable"},
            {"NFT",            ":/icons/token_nft"},
        };
        auto *it = new QTableWidgetItem(QIcon(typeIcons.value(rec.tokenType)), rec.tokenType);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        ui->tokenTable->setItem(row, ColType, it);
    }
    // Col 1 – Full colorId (non-editable, monospace font)
    {
        auto *it = new QTableWidgetItem(rec.colorId);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        QFont mono = it->font();
        mono.setFamily("Monospace");
        mono.setStyleHint(QFont::TypeWriter);
        it->setFont(mono);
        it->setToolTip(rec.colorId);
        ui->tokenTable->setItem(row, ColColor, it);
    }
    // Col 2 – Label (editable)
    {
        auto *it = new QTableWidgetItem(rec.label);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        ui->tokenTable->setItem(row, ColLabel, it);
    }
    // Col 3 – Balance (non-editable, numeric sort)
    {
        auto *it = new NumericItem(QString::number(rec.balance));
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui->tokenTable->setItem(row, ColBalance, it);
    }
}


void IssueTokenDialog::highlightTokenRow(const QString &colorId)
{
    for (int row = 0; row < ui->tokenTable->rowCount(); ++row) {
        QTableWidgetItem *it = ui->tokenTable->item(row, ColColor);
        if (it && it->text() == colorId) {
            ui->tokenTable->clearSelection();
            ui->tokenTable->selectRow(row);
            ui->tokenTable->scrollToItem(it);
            ui->tokenTable->setFocus();
            return;
        }
    }
}

int IssueTokenDialog::currentTokenType() const
{
    if (ui->radioNonReissuable->isChecked()) return 2;
    if (ui->radioNft->isChecked())           return 3;
    return 1; // REISSUABLE (default)
}

QString IssueTokenDialog::tokenTypeName(int type) const
{
    switch (type) {
    case 2: return "NON_REISSUABLE";
    case 3: return "NFT";
    default: return "REISSUABLE";
    }
}


