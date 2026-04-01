// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RECEIVECOINSDIALOG_H
#define BITCOIN_QT_RECEIVECOINSDIALOG_H

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QDialog>
#include <QHeaderView>
#include <QItemSelection>
#include <QKeyEvent>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QRadioButton>
#include <QVariant>

class PlatformStyle;
class WalletModel;

namespace Ui {
    class ReceiveCoinsDialog;
}

class QModelIndex;

/** Dialog for requesting payment of tpc */
class ReceiveCoinsDialog : public QDialog
{
    Q_OBJECT

public:
    enum ColumnWidths {
        DATE_COLUMN_WIDTH = 130,
        LABEL_COLUMN_WIDTH = 120,
        TOKEN_COLUMN_WIDTH = 130,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 160,
        MINIMUM_COLUMN_WIDTH = 130
    };

    explicit ReceiveCoinsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ReceiveCoinsDialog();

    void setModel(WalletModel *model);

public Q_SLOTS:
    void clear();
    void reject() override;
    void accept() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    Ui::ReceiveCoinsDialog *ui;
    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;
    QList<WalletModel::IssuedTokenRecord> m_tokenRecords;

    QModelIndex selectedRow();
    void copyColumnToClipboard(int column);
    void resizeEvent(QResizeEvent *event) override;
    void refreshTokenCombo();

private Q_SLOTS:
    void on_receiveButton_clicked();
    void on_showRequestButton_clicked();
    void on_removeRequestButton_clicked();
    void on_recentRequestsView_doubleClicked(const QModelIndex &index);
    void recentRequestsView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void updateDisplayUnit();
    void on_radioToken_toggled(bool checked);
    void on_reqToken_currentIndexChanged(int index);
    void showMenu(const QPoint &point);
    void copyURI();
    void copyLabel();
    void copyMessage();
    void copyAmount();
    void copyColorId();
};

#endif // BITCOIN_QT_RECEIVECOINSDIALOG_H
