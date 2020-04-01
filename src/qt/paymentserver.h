// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PAYMENTSERVER_H
#define BITCOIN_QT_PAYMENTSERVER_H

// This class handles payment requests from clicking on
// tapyrus: URIs
//
// This is somewhat tricky, because we have to deal with
// the situation where the user clicks on a link during
// startup/initialization, when the splash-screen is up
// but the main window (and the Send Coins tab) is not.
//
// So, the strategy is:
//
// Create the server, and register the event handler,
// when the application is created. Save any URIs
// received at or during startup in a list.
//
// When startup is finished and the main window is
// shown, a signal is sent to slot uiReady(), which
// emits a receivedURI() signal for any payment
// requests that happened during startup.
//
// After startup, receivedURI() happens as usual.
//
// This class has one more feature: a static
// method that finds URIs passed in the command line
// and, if a server is running in another process,
// sends them to the server.
//

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/walletmodel.h>

#include <QObject>
#include <QString>

class OptionsModel;

QT_BEGIN_NAMESPACE
class QApplication;
class QByteArray;
class QLocalServer;
class QNetworkAccessManager;
class QNetworkReply;
class QSslError;
class QUrl;
QT_END_NAMESPACE

class PaymentServer : public QObject
{
    Q_OBJECT

public:
    // parent should be QApplication object
    explicit PaymentServer(QObject* parent, bool startLocalServer = true);
    ~PaymentServer();

    // OptionsModel is used for getting proxy settings and display unit
    void setOptionsModel(OptionsModel *optionsModel);

Q_SIGNALS:
    // Fired when a valid payment request is received
    void receivedPaymentRequest(SendCoinsRecipient);

    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

public Q_SLOTS:
    // Signal this when the main window's UI is ready
    // to display payment requests to the user
    void uiReady();

    // Handle an incoming URI, URI with local file scheme or file
    void handleURIOrFile(const QString& s);

protected:
    // Constructor registers this on the parent QApplication to
    // receive QEvent::FileOpen and QEvent:Drop events
    bool eventFilter(QObject *object, QEvent *event);

private:
    bool saveURIs;                      // true during startup
    OptionsModel *optionsModel;

};

#endif // BITCOIN_QT_PAYMENTSERVER_H
