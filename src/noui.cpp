// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <noui.h>

#include <ui_interface.h>
#include <util.h>

#include <boost/signals2/connection.hpp>

#include <cstdio>
#include <stdint.h>
#include <string>

static bool noui_ThreadSafeMessageBox(const std::string& message, const std::string& caption, unsigned int style)
{
    bool fSecure = style & CClientUIInterface::SECURE;
    style &= ~CClientUIInterface::SECURE;

    std::string strCaption;
    // Check for usage of predefined caption
    switch (style) {
    case CClientUIInterface::MSG_ERROR:
        strCaption += _("Error");
        break;
    case CClientUIInterface::MSG_WARNING:
        strCaption += _("Warning");
        break;
    case CClientUIInterface::MSG_INFORMATION:
        strCaption += _("Information");
        break;
    default:
        strCaption += caption; // Use supplied caption (can be empty)
    }

    if (!fSecure)
        LogPrintf("%s: %s\n", strCaption, message);
    fprintf(stderr, "%s: %s\n", strCaption.c_str(), message.c_str());
    return false;
}

static bool noui_ThreadSafeQuestion(const std::string& /* ignored interactive message */, const std::string& message, const std::string& caption, unsigned int style)
{
    return noui_ThreadSafeMessageBox(message, caption, style);
}

static void noui_InitMessage(const std::string& message)
{
    LogPrintf("init message: %s\n", message);
}

static boost::signals2::connection noui_ThreadSafeMessageBoxConn;
static boost::signals2::connection noui_ThreadSafeQuestionConn;
static boost::signals2::connection noui_InitMessageConn;

static void noui_disconnect()
{
    noui_ThreadSafeMessageBoxConn.disconnect();
    noui_ThreadSafeQuestionConn.disconnect();
    noui_InitMessageConn.disconnect();
}

void noui_connect()
{
    // Each test fixture connects again; drop the earlier handlers so messages are not repeated.
    noui_disconnect();
    noui_ThreadSafeMessageBoxConn = uiInterface.ThreadSafeMessageBox.connect(noui_ThreadSafeMessageBox);
    noui_ThreadSafeQuestionConn = uiInterface.ThreadSafeQuestion.connect(noui_ThreadSafeQuestion);
    noui_InitMessageConn = uiInterface.InitMessage.connect(noui_InitMessage);
}

static bool noui_ThreadSafeMessageBoxRedirect(const std::string& message, const std::string& caption, unsigned int style)
{
    LogPrintf("%s: %s\n", caption, message);
    return false;
}

static bool noui_ThreadSafeQuestionRedirect(const std::string& /* ignored interactive message */, const std::string& message, const std::string& caption, unsigned int style)
{
    LogPrintf("%s: %s\n", caption, message);
    return false;
}

static void noui_InitMessageRedirect(const std::string& message)
{
    LogPrintf("init message: %s\n", message);
}

void noui_test_redirect()
{
    noui_disconnect();
    noui_ThreadSafeMessageBoxConn = uiInterface.ThreadSafeMessageBox.connect(noui_ThreadSafeMessageBoxRedirect);
    noui_ThreadSafeQuestionConn = uiInterface.ThreadSafeQuestion.connect(noui_ThreadSafeQuestionRedirect);
    noui_InitMessageConn = uiInterface.InitMessage.connect(noui_InitMessageRedirect);
}

void noui_reconnect()
{
    noui_connect();
}
