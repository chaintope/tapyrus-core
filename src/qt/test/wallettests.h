#ifndef BITCOIN_QT_TEST_WALLETTESTS_H
#define BITCOIN_QT_TEST_WALLETTESTS_H

#include <QObject>
#include <QTest>

#include <qt/walletmodel.h>
#include <qt/paymentserver.h>

class WalletTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void walletTests();
};

// Dummy class to receive paymentserver signals.
// If SendCoinsRecipient was a proper QObject, then
// we could use QSignalSpy... but it's not.
class RecipientCatcher : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void getRecipient(const SendCoinsRecipient& r);

public:
    SendCoinsRecipient recipient;
};
#endif // BITCOIN_QT_TEST_WALLETTESTS_H
