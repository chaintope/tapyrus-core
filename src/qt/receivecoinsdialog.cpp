// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <qt/receivecoinsdialog.h>
#include <qt/forms/ui_receivecoinsdialog.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/tapyrusunits.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/receiverequestdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/walletmodel.h>

#include <coloridentifier.h>
#include <key_io.h>
#include <utilstrencodings.h>

#include <QAction>
#include <QCursor>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>

ReceiveCoinsDialog::ReceiveCoinsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ReceiveCoinsDialog),
    columnResizingFixer(0),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    if (!_platformStyle->getImagesOnButtons()) {
        ui->clearButton->setIcon(QIcon());
        ui->receiveButton->setIcon(QIcon());
        ui->showRequestButton->setIcon(QIcon());
        ui->removeRequestButton->setIcon(QIcon());
    } else {
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->receiveButton->setIcon(_platformStyle->SingleColorIcon(":/icons/receiving_addresses"));
        ui->showRequestButton->setIcon(_platformStyle->SingleColorIcon(":/icons/edit"));
        ui->removeRequestButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
    }

    // context menu actions
    QAction *copyURIAction = new QAction(tr("Copy URI"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyMessageAction = new QAction(tr("Copy message"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyURIAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyMessageAction);
    contextMenu->addAction(copyAmountAction);

    // context menu signals
    connect(ui->recentRequestsView, &QAbstractItemView::customContextMenuRequested, this, &ReceiveCoinsDialog::showMenu);
    connect(copyURIAction, &QAction::triggered, this, &ReceiveCoinsDialog::copyURI);
    connect(copyLabelAction, &QAction::triggered, this, &ReceiveCoinsDialog::copyLabel);
    connect(copyMessageAction, &QAction::triggered, this, &ReceiveCoinsDialog::copyMessage);
    connect(copyAmountAction, &QAction::triggered, this, &ReceiveCoinsDialog::copyAmount);

    connect(ui->clearButton, &QPushButton::clicked, this, &ReceiveCoinsDialog::clear);
}

void ReceiveCoinsDialog::refreshTokenCombo()
{
    if (!model) return;

    ui->reqToken->blockSignals(true);
    int prevIndex = ui->reqToken->currentIndex();
    QString prevColorId;
    if (prevIndex >= 0 && prevIndex < m_tokenRecords.size())
        prevColorId = m_tokenRecords[prevIndex].colorId;

    ui->reqToken->clear();

    static const QMap<QString, QString> typeIcons = {
        {"REISSUABLE",     ":/icons/token_reissuable"},
        {"NON_REISSUABLE", ":/icons/token_nonreissuable"},
        {"NFT",            ":/icons/token_nft"},
    };

    m_tokenRecords = model->getIssuedTokens();
    for (const WalletModel::IssuedTokenRecord& rec : m_tokenRecords) {
        QIcon icon(typeIcons.value(rec.tokenType));
        ui->reqToken->addItem(icon, rec.colorId, rec.colorId);
    }

    ui->reqToken->blockSignals(false);

    // Restore previous selection if possible
    int restoreIndex = 0;
    if (!prevColorId.isEmpty()) {
        int found = ui->reqToken->findData(prevColorId);
        if (found >= 0) restoreIndex = found;
    }
    ui->reqToken->setCurrentIndex(restoreIndex);
}

void ReceiveCoinsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        _model->getRecentRequestsTableModel()->sort(RecentRequestsTableModel::Date, Qt::DescendingOrder);
        connect(_model->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &ReceiveCoinsDialog::updateDisplayUnit);
        connect(_model, &WalletModel::tokenListChanged, this, &ReceiveCoinsDialog::refreshTokenCombo);
        connect(ui->radioToken, &QRadioButton::toggled, this, &ReceiveCoinsDialog::on_radioToken_toggled);
        connect(ui->reqToken, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ReceiveCoinsDialog::on_reqToken_currentIndexChanged);
        refreshTokenCombo();
        updateDisplayUnit();

        QTableView* tableView = ui->recentRequestsView;

        tableView->verticalHeader()->hide();
        tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tableView->setModel(_model->getRecentRequestsTableModel());
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
        tableView->setColumnWidth(RecentRequestsTableModel::Date, DATE_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Label, LABEL_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Token, TOKEN_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);

        connect(tableView->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            &ReceiveCoinsDialog::recentRequestsView_selectionChanged);
        // Last 2 columns are set by the columnResizingFixer, when the table geometry is ready.
        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, AMOUNT_MINIMUM_COLUMN_WIDTH, DATE_COLUMN_WIDTH, this);


        // eventually disable the main receive button if private key operations are disabled
        ui->receiveButton->setEnabled(!model->privateKeysDisabled());
    }
}

ReceiveCoinsDialog::~ReceiveCoinsDialog()
{
    delete ui;
}

void ReceiveCoinsDialog::clear()
{
    ui->radioTPC->setChecked(true);
    ui->reqToken->setCurrentIndex(0);
    ui->reqToken->setEnabled(false);
    ui->reqAmount->setTokenMode(false);
    ui->reqAmount->setEnabled(true);
    ui->reqAmount->clear();
    ui->reqLabel->setText("");
    ui->reqMessage->setText("");
    if (model)
        ui->receiveButton->setEnabled(!model->privateKeysDisabled());
    ui->receiveButton->setToolTip(QString());
    updateDisplayUnit();
}

void ReceiveCoinsDialog::reject()
{
    clear();
}

void ReceiveCoinsDialog::accept()
{
    clear();
}

void ReceiveCoinsDialog::on_radioToken_toggled(bool checked)
{
    ui->reqToken->setEnabled(checked);
    ui->reqAmount->setTokenMode(checked);
    if (!checked) {
        ui->reqToken->setCurrentIndex(0);
        ui->reqAmount->setEnabled(true);
        ui->receiveButton->setEnabled(!model->privateKeysDisabled());
        ui->receiveButton->setToolTip(QString());
    } else {
        on_reqToken_currentIndexChanged(ui->reqToken->currentIndex());
    }
}

void ReceiveCoinsDialog::on_reqToken_currentIndexChanged(int index)
{
    if (!ui->radioToken->isChecked() || index < 0 || index >= m_tokenRecords.size())
        return;

    const WalletModel::IssuedTokenRecord& rec = m_tokenRecords[index];
    if (rec.tokenType == "NFT") {
        // NFT: fixed amount of 1, no more requests if already holding 1
        ui->reqAmount->setValue(1);
        ui->reqAmount->setEnabled(false);
        bool alreadyOwned = (rec.balance + rec.unconfirmedBalance) >= 1;
        bool canRequest = !alreadyOwned && !model->privateKeysDisabled();
        ui->receiveButton->setEnabled(canRequest);
        ui->receiveButton->setToolTip(alreadyOwned
            ? tr("This NFT token is already in your wallet. Only one can exist in the network.")
            : QString());
    } else {
        ui->reqAmount->setEnabled(true);
        ui->receiveButton->setEnabled(!model->privateKeysDisabled());
        ui->receiveButton->setToolTip(QString());
    }
}

void ReceiveCoinsDialog::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
        ui->reqAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
}

void ReceiveCoinsDialog::on_receiveButton_clicked()
{
    if(!model || !model->getOptionsModel() || !model->getAddressTableModel() || !model->getRecentRequestsTableModel())
        return;

    QString address;
    QString label = ui->reqLabel->text();
    int tokenIndex = ui->reqToken->currentIndex();

    if (ui->radioToken->isChecked() && tokenIndex >= 0 && tokenIndex < m_tokenRecords.size()) {
        // Generate a colored receiving address
        const WalletModel::IssuedTokenRecord& rec = m_tokenRecords[tokenIndex];
        const std::vector<unsigned char> vColorId(ParseHex(rec.colorId.toStdString()));
        ColorIdentifier colorId(vColorId.data(), vColorId.data() + vColorId.size());

        CPubKey newKey;
        if (!model->wallet().getKeyFromPool(false /* internal */, newKey)) {
            WalletModel::UnlockContext ctx(model->requestUnlock());
            if (!ctx.isValid()) return;
            if (!model->wallet().getKeyFromPool(false, newKey)) return;
        }
        CColorKeyID colorKeyId(newKey.GetID(), colorId);
        address = QString::fromStdString(EncodeDestination(colorKeyId));
        model->wallet().setAddressBook(colorKeyId, label.toStdString(), "receive");
    } else {
        // Generate a standard TPC receiving address
        OutputType address_type = model->wallet().getDefaultAddressType();
        address = model->getAddressTableModel()->addRow(AddressTableModel::Receive, label, "", address_type);
    }

    SendCoinsRecipient info(address, label,
        ui->reqAmount->value(), ui->reqMessage->text());
    if (ui->radioToken->isChecked() && tokenIndex >= 0 && tokenIndex < m_tokenRecords.size()) {
        const std::vector<unsigned char> vColorId(ParseHex(m_tokenRecords[tokenIndex].colorId.toStdString()));
        info.colorid = ColorIdentifier(vColorId.data(), vColorId.data() + vColorId.size());
    }
    ReceiveRequestDialog *dialog = new ReceiveRequestDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModel(model);
    dialog->setInfo(info);
    dialog->show();
    clear();

    /* Store request for later reference */
    model->getRecentRequestsTableModel()->addNewRequest(info);
}

void ReceiveCoinsDialog::on_recentRequestsView_doubleClicked(const QModelIndex &index)
{
    const RecentRequestsTableModel *submodel = model->getRecentRequestsTableModel();
    ReceiveRequestDialog *dialog = new ReceiveRequestDialog(this);
    dialog->setModel(model);
    dialog->setInfo(submodel->entry(index.row()).recipient);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void ReceiveCoinsDialog::recentRequestsView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    // Enable Show/Remove buttons only if anything is selected.
    bool enable = !ui->recentRequestsView->selectionModel()->selectedRows().isEmpty();
    ui->showRequestButton->setEnabled(enable);
    ui->removeRequestButton->setEnabled(enable);
}

void ReceiveCoinsDialog::on_showRequestButton_clicked()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();

    for (const QModelIndex& index : selection) {
        on_recentRequestsView_doubleClicked(index);
    }
}

void ReceiveCoinsDialog::on_removeRequestButton_clicked()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if(selection.empty())
        return;
    // correct for selection mode ContiguousSelection
    QModelIndex firstIndex = selection.at(0);
    model->getRecentRequestsTableModel()->removeRows(firstIndex.row(), selection.length(), firstIndex.parent());
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void ReceiveCoinsDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(RecentRequestsTableModel::Message);
}

void ReceiveCoinsDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return)
    {
        // press return -> submit form
        if (ui->reqLabel->hasFocus() || ui->reqAmount->hasFocus() || ui->reqMessage->hasFocus())
        {
            event->ignore();
            on_receiveButton_clicked();
            return;
        }
    }

    this->QDialog::keyPressEvent(event);
}

QModelIndex ReceiveCoinsDialog::selectedRow()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return QModelIndex();
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if(selection.empty())
        return QModelIndex();
    // correct for selection mode ContiguousSelection
    QModelIndex firstIndex = selection.at(0);
    return firstIndex;
}

// copy column of selected row to clipboard
void ReceiveCoinsDialog::copyColumnToClipboard(int column)
{
    QModelIndex firstIndex = selectedRow();
    if (!firstIndex.isValid()) {
        return;
    }
    GUIUtil::setClipboard(model->getRecentRequestsTableModel()->index(firstIndex.row(), column, firstIndex).data(Qt::EditRole).toString());
}

// context menu
void ReceiveCoinsDialog::showMenu(const QPoint &point)
{
    if (!selectedRow().isValid()) {
        return;
    }
    contextMenu->exec(QCursor::pos());
}

// context menu action: copy URI
void ReceiveCoinsDialog::copyURI()
{
    QModelIndex sel = selectedRow();
    if (!sel.isValid()) {
        return;
    }

    const RecentRequestsTableModel * const submodel = model->getRecentRequestsTableModel();
    const QString uri = GUIUtil::formatTapyrusURI(submodel->entry(sel.row()).recipient);
    GUIUtil::setClipboard(uri);
}

// context menu action: copy label
void ReceiveCoinsDialog::copyLabel()
{
    copyColumnToClipboard(RecentRequestsTableModel::Label);
}

// context menu action: copy message
void ReceiveCoinsDialog::copyMessage()
{
    copyColumnToClipboard(RecentRequestsTableModel::Message);
}

// context menu action: copy amount
void ReceiveCoinsDialog::copyAmount()
{
    copyColumnToClipboard(RecentRequestsTableModel::Amount);
}
