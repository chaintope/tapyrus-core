// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_QT_ISSUETOKEN_H
#define TAPYRUS_QT_ISSUETOKEN_H

#include <amount.h>

#include <QString>
#include <QWidget>

class PlatformStyle;
class WalletModel;

namespace Ui {
    class IssueTokenDialog;
}

class QPoint;
class QTableWidgetItem;

/** Persistent record for a token issued by this wallet. */
struct IssuedTokenRecord {
    QString colorId;       // hex ColorIdentifier (primary key)
    QString label;         // user-editable label
    QString tokenType;     // "REISSUABLE" / "NON_REISSUABLE" / "NFT"
    CAmount balance = 0;   // refreshed from wallet; may be 0 after transfer
    QString scriptPubKey;  // REISSUABLE only: original P2PKH script (for reissuing)
    QString address;       // REISSUABLE only: address for setlabel RPC
};

/**
 * Page for issuing and burning colored coin tokens.
 *
 * Layout:
 *   QTabWidget
 *     Tab 0 – Issue Token  (fully implemented)
 *     Tab 1 – Burn Token   (UI scaffold; wire up burnToken() to implement)
 *   QGroupBox "Issued Tokens"
 *     QTableWidget  columns: Color+Copy | Label | Type | Balance
 */
class IssueTokenDialog : public QWidget
{
    Q_OBJECT

public:
    explicit IssueTokenDialog(const PlatformStyle *platformStyle,
                              QWidget *parent = nullptr);
    ~IssueTokenDialog();

    void setModel(WalletModel *model);

    // Table column indices
    enum Column { ColType = 0, ColColor, ColLabel, ColBalance };

public Q_SLOTS:
    void clear();
    void refreshTokenTable();   // re-read balances from wallet

Q_SIGNALS:
    void message(const QString &title, const QString &message, unsigned int style);

private Q_SLOTS:
    void on_issueButton_clicked();
    void on_clearButton_clicked();
    void on_burnButton_clicked();
    void onTokenTypeChanged();
    void onIssueModeChanged();
    void onLabelChanged(QTableWidgetItem *item);
    void onBurnColorChanged(int index);
    void onTokenTableContextMenu(const QPoint &pos);

private:
    Ui::IssueTokenDialog *ui;
    WalletModel *model;
    const PlatformStyle *platformStyle;

    QList<IssuedTokenRecord> m_tokens;

    void loadPersistedTokens();
    void saveToken(const IssuedTokenRecord &rec);
    void addTokenToTable(const IssuedTokenRecord &rec, int row);
    void refreshBurnCombo();
    void refreshReissueCombo();
    void highlightTokenRow(const QString &colorId);

    int  currentTokenType() const;            // 1/2/3
    QString tokenTypeName(int type) const;
};

#endif // TAPYRUS_QT_ISSUETOKEN_H
