// Copyright (c) 2016-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_RPCNESTEDTESTS_H
#define BITCOIN_QT_TEST_RPCNESTEDTESTS_H

#include <QObject>
#include <QTest>

#include <txdb.h>
#include <txmempool.h>

struct NodeContext;

class RPCNestedTests : public QObject
{
    Q_OBJECT

public:
    /** node is the test fixture's, whose chain the RPCs read. */
    explicit RPCNestedTests(NodeContext& node) : m_node(node) {}

    private Q_SLOTS:
    void rpcNestedTests();

private:
    NodeContext& m_node;
};

#endif // BITCOIN_QT_TEST_RPCNESTEDTESTS_H
