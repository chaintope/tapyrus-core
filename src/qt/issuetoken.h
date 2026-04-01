// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_QT_ISSUETOKEN_H
#define TAPYRUS_QT_ISSUETOKEN_H

#include <qt/walletmodel.h>

#include <QWidget>

class PlatformStyle;

namespace Ui {
    class IssueTokenDialog;
}

class QPoint;
class QTableWidgetItem;

/**
 * Page for issuing and burning colored coin tokens.
 *
 * Layout:
 *   QTabWidget
 *     Tab 0 – Issue Token
 *     Tab 1 – Burn Token
 *   QGroupBox "Tokens"
 *     QTableWidget  columns: Type | Color ID | Label | Balance
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
    void refreshTokenTable(const QList<WalletModel::IssuedTokenRecord>& tokens);

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

    // In-memory cache rebuilt from the wallet address book on each refresh.
    QList<WalletModel::IssuedTokenRecord> m_tokens;

    void addTokenToTable(const WalletModel::IssuedTokenRecord &rec, int row);
    void refreshBurnCombo();
    void refreshReissueCombo();
    void highlightTokenRow(const QString &colorId);

    int     currentTokenType() const;   // 1/2/3
    QString tokenTypeName(int type) const;
};

#endif // TAPYRUS_QT_ISSUETOKEN_H
